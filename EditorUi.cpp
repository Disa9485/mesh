#include "EditorUi.hpp"

#include "EditorDocument.hpp"
#include "EditorHistory.hpp"
#include "MeshGenerator.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>

static void SetPremultipliedAlphaBlendCallback(
    const ImDrawList*,
    const ImDrawCmd*
) {
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void RestoreStraightAlphaBlendCallback(
    const ImDrawList*,
    const ImDrawCmd*
) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void DrawCheckerboard(
    ImDrawList* drawList,
    ImVec2 origin,
    ImVec2 size,
    float cellSize
) {
    const ImU32 a = IM_COL32(48, 48, 48, 255);
    const ImU32 b = IM_COL32(64, 64, 64, 255);

    const int cols = static_cast<int>(size.x / cellSize) + 1;
    const int rows = static_cast<int>(size.y / cellSize) + 1;

    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            ImVec2 p0(
                origin.x + static_cast<float>(x) * cellSize,
                origin.y + static_cast<float>(y) * cellSize
            );

            ImVec2 p1(
                std::min(p0.x + cellSize, origin.x + size.x),
                std::min(p0.y + cellSize, origin.y + size.y)
            );

            drawList->AddRectFilled(p0, p1, ((x + y) & 1) ? a : b);
        }
    }
}

static void DrawTopBar(EditorState& editor, GLFWwindow* window) {
    ImGui::Begin("Project");

    ImGui::TextUnformatted("PSD Path");

    char buffer[1024];
    std::memset(buffer, 0, sizeof(buffer));
    std::strncpy(buffer, editor.pendingPath.c_str(), sizeof(buffer) - 1);

    if (ImGui::InputText("##psd_path", buffer, sizeof(buffer))) {
        editor.pendingPath = buffer;
    }

    ImGui::SameLine();

    if (ImGui::Button("Load PSD")) {
        std::string error;
        if (!LoadPsdIntoEditor(editor.pendingPath, editor, window, error)) {
            editor.errorText = error;
            editor.statusText = "Load failed.";
        }
    }

    ImGui::Separator();

    ImGui::Text("Status: %s", editor.statusText.c_str());

    if (!editor.errorText.empty()) {
        ImGui::TextWrapped("Error: %s", editor.errorText.c_str());
    }

    if (!editor.document.path.empty()) {
        ImGui::Text("Canvas: %d x %d",
            editor.document.canvasWidth,
            editor.document.canvasHeight
        );
        ImGui::Text("File: %s", editor.document.path.c_str());
    }

    ImGui::End();
}

static std::vector<int> GetGenerateMeshTargetLayers(const EditorState& editor) {
    if (editor.mode != EditorMode::Layer || editor.selectedLayers.empty()) {
        return IsValidLayerIndex(editor, editor.selectedLayer)
            ? std::vector<int>{editor.selectedLayer}
            : std::vector<int>{};
    }

    std::vector<int> layers;
    layers.reserve(editor.selectedLayers.size());

    for (const int layerIndex : editor.selectedLayers) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            layers.push_back(layerIndex);
        }
    }

    return layers;
}

static void GenerateMeshForLayers(EditorState& editor, const std::vector<int>& layerIndices) {
    const int originalSelectedLayer = editor.selectedLayer;

    for (const int layerIndex : layerIndices) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        editor.selectedLayer = layerIndex;
        GenerateMeshForSelectedLayer(editor);
    }

    if (IsValidLayerIndex(editor, originalSelectedLayer)) {
        editor.selectedLayer = originalSelectedLayer;
    } else if (!layerIndices.empty()) {
        editor.selectedLayer = layerIndices.front();
    }
}

static void SaveMeshesFromUi(EditorState& editor) {
    std::string error;
    if (SaveMeshesForEditor(editor, error)) {
        editor.statusText = "Saved mesh file.";
        editor.errorText.clear();
    } else {
        editor.statusText = "Mesh save failed.";
        editor.errorText = error;
    }
}

static void ClearHistoryFromUi(EditorState& editor) {
    editor.history.undoStack.clear();
    editor.history.redoStack.clear();
    editor.statusText = "Cleared edit history. Save to persist.";
    editor.errorText.clear();
}

static void RequestGenerateMesh(EditorState& editor) {
    std::vector<int> targetLayers = GetGenerateMeshTargetLayers(editor);
    if (targetLayers.empty()) {
        return;
    }

    const bool needsConfirmation = std::any_of(
        targetLayers.begin(),
        targetLayers.end(),
        [&](int layerIndex) {
            return
                IsValidLayerIndex(editor, layerIndex) &&
                editor.document.layers[layerIndex].mesh.vertices.size() > 10u;
        }
    );

    if (!needsConfirmation) {
        GenerateMeshForLayers(editor, targetLayers);
        return;
    }

    editor.pendingGenerateMeshLayers = std::move(targetLayers);
    editor.pendingGenerateMeshConfirmation = true;
    ImGui::OpenPopup("Generate Mesh?");
}

static void DrawGenerateMeshConfirmationPopup(EditorState& editor) {
    if (editor.pendingGenerateMeshConfirmation) {
        ImGui::OpenPopup("Generate Mesh?");
        editor.pendingGenerateMeshConfirmation = false;
    }

    if (!ImGui::BeginPopupModal("Generate Mesh?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Are you sure?");

    if (!editor.pendingGenerateMeshLayers.empty()) {
        std::size_t replaceCount = 0;
        for (const int layerIndex : editor.pendingGenerateMeshLayers) {
            if (
                IsValidLayerIndex(editor, layerIndex) &&
                editor.document.layers[layerIndex].mesh.vertices.size() > 10u
            ) {
                ++replaceCount;
            }
        }

        ImGui::Text(
            "This will replace existing meshes on %zu layer%s.",
            replaceCount,
            replaceCount == 1u ? "" : "s"
        );
    }

    if (ImGui::Button("Generate")) {
        GenerateMeshForLayers(editor, editor.pendingGenerateMeshLayers);

        editor.pendingGenerateMeshLayers.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        editor.pendingGenerateMeshLayers.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static bool ContainsIndex(const std::vector<std::uint32_t>& values, std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

static void ToggleIndex(std::vector<std::uint32_t>& values, std::uint32_t value) {
    auto it = std::find(values.begin(), values.end(), value);
    if (it == values.end()) {
        values.push_back(value);
    } else {
        values.erase(it);
    }
}

static bool ContainsVertexRef(
    const std::vector<SelectedVertexRef>& values,
    SelectedVertexRef value
) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

static void AddVertexRefIfMissing(
    std::vector<SelectedVertexRef>& values,
    SelectedVertexRef value
) {
    if (!ContainsVertexRef(values, value)) {
        values.push_back(value);
    }
}

static void ToggleVertexRef(
    std::vector<SelectedVertexRef>& values,
    SelectedVertexRef value
) {
    auto it = std::find(values.begin(), values.end(), value);
    if (it == values.end()) {
        values.push_back(value);
    } else {
        values.erase(it);
    }
}

static void ClearMeshSelection(EditorState& editor) {
    editor.selectedVertices.clear();
    editor.selectedEdges.clear();
}

static void ClearDeformVertexSelection(EditorState& editor) {
    editor.selectedDeformVertices.clear();
}

static void ClearAllVertexSelection(EditorState& editor) {
    ClearMeshSelection(editor);
    ClearDeformVertexSelection(editor);
}

static bool HasSelectedVertex(const EditorState& editor) {
    return !editor.selectedVertices.empty();
}

static std::uint32_t ActiveSelectedVertex(const EditorState& editor) {
    return editor.selectedVertices.empty() ? 0u : editor.selectedVertices.back();
}

static bool IsEdgeSelected(const EditorState& editor, std::uint32_t edgeIndex) {
    return ContainsIndex(editor.selectedEdges, edgeIndex);
}

static bool IsLayerSelected(const EditorState& editor, int layerIndex) {
    return std::find(
        editor.selectedLayers.begin(),
        editor.selectedLayers.end(),
        layerIndex
    ) != editor.selectedLayers.end();
}

static void RemoveLayerFromSelection(EditorState& editor, int layerIndex) {
    editor.selectedLayers.erase(
        std::remove(editor.selectedLayers.begin(), editor.selectedLayers.end(), layerIndex),
        editor.selectedLayers.end()
    );

    if (editor.selectedLayer == layerIndex) {
        editor.selectedLayer = editor.selectedLayers.empty() ? -1 : editor.selectedLayers.back();
    }
}

static void SelectSingleLayer(EditorState& editor, int layerIndex) {
    if (IsValidLayerIndex(editor, layerIndex) && !editor.document.layers[layerIndex].visible) {
        layerIndex = -1;
    }

    editor.selectedLayer = layerIndex;
    editor.selectedLayers.clear();
    if (layerIndex >= 0) {
        editor.selectedLayers.push_back(layerIndex);
    }
    ClearAllVertexSelection(editor);
}

static void SelectAllLayers(EditorState& editor) {
    editor.selectedLayers.clear();
    editor.selectedLayers.reserve(editor.document.layers.size());
    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        if (!editor.document.layers[i].visible) {
            continue;
        }
        editor.selectedLayers.push_back(i);
    }
    editor.selectedLayer = editor.document.layers.empty() ? -1 : 0;
    if (!editor.selectedLayers.empty()) {
        editor.selectedLayer = editor.selectedLayers.front();
    } else {
        editor.selectedLayer = -1;
    }
    ClearAllVertexSelection(editor);
}

static void ToggleLayerSelection(EditorState& editor, int layerIndex) {
    if (!IsValidLayerIndex(editor, layerIndex) || !editor.document.layers[layerIndex].visible) {
        return;
    }

    auto it = std::find(editor.selectedLayers.begin(), editor.selectedLayers.end(), layerIndex);
    if (it == editor.selectedLayers.end()) {
        editor.selectedLayers.push_back(layerIndex);
        editor.selectedLayer = layerIndex;
    } else {
        editor.selectedLayers.erase(it);
        if (editor.selectedLayer == layerIndex) {
            editor.selectedLayer = editor.selectedLayers.empty() ? -1 : editor.selectedLayers.back();
        }
    }

    ClearAllVertexSelection(editor);
}

static void SelectLayerRange(EditorState& editor, int layerIndex) {
    if (!IsValidLayerIndex(editor, layerIndex) || !editor.document.layers[layerIndex].visible) {
        return;
    }

    const int anchor = IsValidLayerIndex(editor, editor.selectedLayer)
        ? editor.selectedLayer
        : layerIndex;
    const int first = std::min(anchor, layerIndex);
    const int last = std::max(anchor, layerIndex);

    editor.selectedLayers.clear();
    for (int i = first; i <= last; ++i) {
        if (!editor.document.layers[i].visible) {
            continue;
        }
        editor.selectedLayers.push_back(i);
    }

    editor.selectedLayer = editor.selectedLayers.empty() ? -1 : layerIndex;
    ClearAllVertexSelection(editor);
}

static void HandleLayerSelectionClick(EditorState& editor, int layerIndex) {
    const ImGuiIO& io = ImGui::GetIO();

    if (io.KeyCtrl) {
        ToggleLayerSelection(editor, layerIndex);
    } else if (io.KeyShift) {
        SelectLayerRange(editor, layerIndex);
    } else {
        SelectSingleLayer(editor, layerIndex);
    }
}

static void SelectAllMeshItems(EditorState& editor) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    const EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    editor.selectedVertices.clear();
    editor.selectedEdges.clear();

    editor.selectedVertices.reserve(layer.mesh.vertices.size());
    for (std::uint32_t i = 0; i < layer.mesh.vertices.size(); ++i) {
        editor.selectedVertices.push_back(i);
    }

    editor.selectedEdges.reserve(layer.mesh.edges.size());
    for (std::uint32_t i = 0; i < layer.mesh.edges.size(); ++i) {
        editor.selectedEdges.push_back(i);
    }
}

static ImVec2 CanvasToScreenPoint(Vec2 canvasPoint, ImVec2 imagePos, float zoom) {
    return ImVec2(
        imagePos.x + canvasPoint.x * zoom,
        imagePos.y + canvasPoint.y * zoom
    );
}

static void RefreshMeshVertexUv(const EditorLayer& layer, MeshVertex& vertex) {
    const float invW = 1.0f / static_cast<float>(std::max(1, layer.width));
    const float invH = 1.0f / static_cast<float>(std::max(1, layer.height));

    vertex.uv.x = std::clamp(vertex.position.x * invW, 0.0f, 1.0f);
    vertex.uv.y = std::clamp(vertex.position.y * invH, 0.0f, 1.0f);
}

static Vec2 LayerMeshBoundsCenter(const LayerMesh& mesh) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(mesh, x0, y0, x1, y1)) {
        return {};
    }

    return Vec2{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
}

static float DistancePointToSegmentSquared(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float abLenSq = Dot(ab, ab);
    if (abLenSq <= 0.0001f) {
        const Vec2 delta = p - a;
        return Dot(delta, delta);
    }

    const float t = std::clamp(Dot(p - a, ab) / abLenSq, 0.0f, 1.0f);
    const Vec2 closest{
        a.x + ab.x * t,
        a.y + ab.y * t
    };
    const Vec2 delta = p - closest;
    return Dot(delta, delta);
}

static int PickVertexAtCanvasPoint(
    const EditorLayer& layer,
    Vec2 canvasPoint,
    float zoom
) {
    const float radius = std::max(6.0f / std::max(zoom, 0.0001f), 2.0f);
    const float radiusSq = radius * radius;

    for (int i = static_cast<int>(layer.mesh.vertices.size()) - 1; i >= 0; --i) {
        const Vec2 delta = canvasPoint - layer.mesh.vertices[static_cast<std::size_t>(i)].position;
        if (Dot(delta, delta) <= radiusSq) {
            return i;
        }
    }

    return -1;
}

static int PickEdgeAtCanvasPoint(
    const EditorLayer& layer,
    Vec2 canvasPoint,
    float zoom
) {
    const float radius = std::max(6.0f / std::max(zoom, 0.0001f), 2.0f);
    const float radiusSq = radius * radius;

    for (int i = static_cast<int>(layer.mesh.edges.size()) - 1; i >= 0; --i) {
        const MeshEdge& edge = layer.mesh.edges[static_cast<std::size_t>(i)];
        if (
            edge.a >= layer.mesh.vertices.size() ||
            edge.b >= layer.mesh.vertices.size()
        ) {
            continue;
        }

        const Vec2 a = layer.mesh.vertices[edge.a].position;
        const Vec2 b = layer.mesh.vertices[edge.b].position;
        if (DistancePointToSegmentSquared(canvasPoint, a, b) <= radiusSq) {
            return i;
        }
    }

    return -1;
}

static MeshVertex CreateMeshVertexAtPoint(const EditorLayer& layer, Vec2 point) {
    const float invW = 1.0f / static_cast<float>(std::max(1, layer.width));
    const float invH = 1.0f / static_cast<float>(std::max(1, layer.height));

    return MeshVertex{
        point,
        Vec2{
            std::clamp(point.x * invW, 0.0f, 1.0f),
            std::clamp(point.y * invH, 0.0f, 1.0f)
        }
    };
}

static bool MeshHasEdge(const LayerMesh& mesh, std::uint32_t a, std::uint32_t b) {
    if (a == b) {
        return true;
    }

    return std::any_of(mesh.edges.begin(), mesh.edges.end(), [&](const MeshEdge& edge) {
        return
            (edge.a == a && edge.b == b) ||
            (edge.a == b && edge.b == a);
    });
}

static bool MeshTriangleEdgesValid(
    const LayerMesh& mesh,
    std::uint32_t a,
    std::uint32_t b,
    std::uint32_t c
) {
    if (mesh.edges.empty()) {
        return true;
    }

    return
        MeshHasEdge(mesh, a, b) &&
        MeshHasEdge(mesh, b, c) &&
        MeshHasEdge(mesh, c, a);
}

static void AddMeshEdgeIfMissing(LayerMesh& mesh, std::uint32_t a, std::uint32_t b) {
    if (a == b || a >= mesh.vertices.size() || b >= mesh.vertices.size() || MeshHasEdge(mesh, a, b)) {
        return;
    }

    mesh.edges.push_back(MeshEdge{a, b});
    RebuildMeshTrianglesFromEdges(mesh);
}

static bool CanvasPointInBox(Vec2 point, Vec2 a, Vec2 b) {
    const float minX = std::min(a.x, b.x);
    const float maxX = std::max(a.x, b.x);
    const float minY = std::min(a.y, b.y);
    const float maxY = std::max(a.y, b.y);

    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

static float TransformBoxPadding(float zoom) {
    return std::max(14.0f / std::max(zoom, 0.0001f), 6.0f);
}

static void PadBounds(float& x0, float& y0, float& x1, float& y1, float zoom) {
    const float padding = TransformBoxPadding(zoom);
    x0 -= padding;
    y0 -= padding;
    x1 += padding;
    y1 += padding;
}

static bool GetSelectedVertexBounds(
    const EditorLayer& layer,
    const std::vector<std::uint32_t>& selectedVertices,
    float& x0,
    float& y0,
    float& x1,
    float& y1
) {
    bool found = false;

    for (const std::uint32_t vertexIndex : selectedVertices) {
        if (vertexIndex >= layer.mesh.vertices.size()) {
            continue;
        }

        const Vec2 position = layer.mesh.vertices[vertexIndex].position;
        if (!found) {
            x0 = position.x;
            y0 = position.y;
            x1 = position.x;
            y1 = position.y;
            found = true;
        } else {
            x0 = std::min(x0, position.x);
            y0 = std::min(y0, position.y);
            x1 = std::max(x1, position.x);
            y1 = std::max(y1, position.y);
        }
    }

    return found;
}

static bool ExpandBounds(float& x0, float& y0, float& x1, float& y1, float bx0, float by0, float bx1, float by1, bool& found) {
    if (!found) {
        x0 = bx0;
        y0 = by0;
        x1 = bx1;
        y1 = by1;
        found = true;
    } else {
        x0 = std::min(x0, bx0);
        y0 = std::min(y0, by0);
        x1 = std::max(x1, bx1);
        y1 = std::max(y1, by1);
    }

    return found;
}

static bool GetSelectedLayerBounds(
    const EditorState& editor,
    float& x0,
    float& y0,
    float& x1,
    float& y1
) {
    bool found = false;

    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        float lx0 = 0.0f;
        float ly0 = 0.0f;
        float lx1 = 0.0f;
        float ly1 = 0.0f;
        if (GetMeshBounds(layer.mesh, lx0, ly0, lx1, ly1)) {
            ExpandBounds(x0, y0, x1, y1, lx0, ly0, lx1, ly1, found);
        }
    }

    return found;
}

static bool CanvasPointInSelectedLayerTransformBox(
    const EditorState& editor,
    Vec2 point,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetSelectedLayerBounds(editor, x0, y0, x1, y1)) {
        return false;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    return CanvasPointInBox(point, Vec2{x0, y0}, Vec2{x1, y1});
}

static bool GetDeformVertexSelectionBounds(
    const EditorState& editor,
    float& x0,
    float& y0,
    float& x1,
    float& y1
) {
    bool found = false;

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= layer.mesh.vertices.size()) {
            continue;
        }

        const Vec2 position = layer.mesh.vertices[ref.vertexIndex].position;
        ExpandBounds(x0, y0, x1, y1, position.x, position.y, position.x, position.y, found);
    }

    return found;
}

static bool CanvasPointInMeshBounds(const LayerMesh& mesh, Vec2 point) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(mesh, x0, y0, x1, y1)) {
        return false;
    }

    return CanvasPointInBox(point, Vec2{x0, y0}, Vec2{x1, y1});
}

static bool PickBoundsTransformHandle(
    float x0,
    float y0,
    float x1,
    float y1,
    Vec2 point,
    float zoom,
    LayerTransformMode& mode,
    int& axisX,
    int& axisY
) {
    mode = LayerTransformMode::None;
    axisX = 0;
    axisY = 0;

    const float edgeRadius = std::max(8.0f / std::max(zoom, 0.0001f), 3.0f);
    const float cornerScaleRadius = std::max(10.0f / std::max(zoom, 0.0001f), 4.0f);
    const float rotateRadius = std::max(22.0f / std::max(zoom, 0.0001f), 8.0f);
    const float centerRadius = std::max(18.0f / std::max(zoom, 0.0001f), 8.0f);
    const float cornerScaleRadiusSq = cornerScaleRadius * cornerScaleRadius;
    const float rotateRadiusSq = rotateRadius * rotateRadius;
    const float centerRadiusSq = centerRadius * centerRadius;
    const bool insideBounds = CanvasPointInBox(point, Vec2{x0, y0}, Vec2{x1, y1});

    const Vec2 center{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    const Vec2 centerDelta = point - center;
    if (Dot(centerDelta, centerDelta) <= centerRadiusSq) {
        mode = LayerTransformMode::Move;
        return true;
    }

    struct CornerHandle {
        Vec2 point;
        int axisX;
        int axisY;
    };

    const CornerHandle corners[] = {
        {Vec2{x0, y0}, -1, -1},
        {Vec2{x1, y0}, 1, -1},
        {Vec2{x1, y1}, 1, 1},
        {Vec2{x0, y1}, -1, 1},
    };

    for (const CornerHandle& corner : corners) {
        const Vec2 delta = point - corner.point;
        const float distanceSq = Dot(delta, delta);

        if (distanceSq <= cornerScaleRadiusSq) {
            mode = LayerTransformMode::Scale;
            axisX = corner.axisX;
            axisY = corner.axisY;
            return true;
        }

        if (
            !insideBounds &&
            distanceSq > cornerScaleRadiusSq &&
            distanceSq <= rotateRadiusSq
        ) {
            mode = LayerTransformMode::Rotate;
            return true;
        }
    }

    if (point.y >= y0 - edgeRadius && point.y <= y1 + edgeRadius) {
        if (std::abs(point.x - x0) <= edgeRadius) {
            mode = LayerTransformMode::Scale;
            axisX = -1;
            return true;
        }

        if (std::abs(point.x - x1) <= edgeRadius) {
            mode = LayerTransformMode::Scale;
            axisX = 1;
            return true;
        }
    }

    if (point.x >= x0 - edgeRadius && point.x <= x1 + edgeRadius) {
        if (std::abs(point.y - y0) <= edgeRadius) {
            mode = LayerTransformMode::Scale;
            axisY = -1;
            return true;
        }

        if (std::abs(point.y - y1) <= edgeRadius) {
            mode = LayerTransformMode::Scale;
            axisY = 1;
            return true;
        }
    }

    return false;
}

static bool PickLayerTransformHandle(
    const EditorLayer& layer,
    Vec2 point,
    float zoom,
    LayerTransformMode& mode,
    int& axisX,
    int& axisY
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(layer.mesh, x0, y0, x1, y1)) {
        mode = LayerTransformMode::None;
        axisX = 0;
        axisY = 0;
        return false;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    return PickBoundsTransformHandle(x0, y0, x1, y1, point, zoom, mode, axisX, axisY);
}

static bool PickSelectedLayersTransformHandle(
    const EditorState& editor,
    Vec2 point,
    float zoom,
    LayerTransformMode& mode,
    int& axisX,
    int& axisY
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetSelectedLayerBounds(editor, x0, y0, x1, y1)) {
        mode = LayerTransformMode::None;
        axisX = 0;
        axisY = 0;
        return false;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    return PickBoundsTransformHandle(x0, y0, x1, y1, point, zoom, mode, axisX, axisY);
}

static bool PickSelectedVertexTransformHandle(
    const EditorLayer& layer,
    const std::vector<std::uint32_t>& selectedVertices,
    Vec2 point,
    float zoom,
    LayerTransformMode& mode,
    int& axisX,
    int& axisY
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetSelectedVertexBounds(layer, selectedVertices, x0, y0, x1, y1)) {
        mode = LayerTransformMode::None;
        axisX = 0;
        axisY = 0;
        return false;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    return PickBoundsTransformHandle(x0, y0, x1, y1, point, zoom, mode, axisX, axisY);
}

static bool PickDeformVertexTransformHandle(
    const EditorState& editor,
    Vec2 point,
    float zoom,
    LayerTransformMode& mode,
    int& axisX,
    int& axisY
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        mode = LayerTransformMode::None;
        axisX = 0;
        axisY = 0;
        return false;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    return PickBoundsTransformHandle(x0, y0, x1, y1, point, zoom, mode, axisX, axisY);
}

static bool SegmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    auto orient = [](Vec2 p, Vec2 q, Vec2 r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };

    const float o1 = orient(a, b, c);
    const float o2 = orient(a, b, d);
    const float o3 = orient(c, d, a);
    const float o4 = orient(c, d, b);

    return (o1 * o2 <= 0.0f) && (o3 * o4 <= 0.0f);
}

static bool SegmentIntersectsBox(Vec2 a, Vec2 b, Vec2 boxA, Vec2 boxB) {
    const float minX = std::min(boxA.x, boxB.x);
    const float maxX = std::max(boxA.x, boxB.x);
    const float minY = std::min(boxA.y, boxB.y);
    const float maxY = std::max(boxA.y, boxB.y);

    if (
        CanvasPointInBox(a, boxA, boxB) ||
        CanvasPointInBox(b, boxA, boxB)
    ) {
        return true;
    }

    const Vec2 tl{minX, minY};
    const Vec2 tr{maxX, minY};
    const Vec2 br{maxX, maxY};
    const Vec2 bl{minX, maxY};

    return
        SegmentsIntersect(a, b, tl, tr) ||
        SegmentsIntersect(a, b, tr, br) ||
        SegmentsIntersect(a, b, br, bl) ||
        SegmentsIntersect(a, b, bl, tl);
}

static void AddIndexIfMissing(std::vector<std::uint32_t>& values, std::uint32_t value) {
    if (!ContainsIndex(values, value)) {
        values.push_back(value);
    }
}

static void ApplyMeshBoxSelection(EditorState& editor, const EditorLayer& layer) {
    if (!editor.boxSelectAdditive) {
        ClearMeshSelection(editor);
    }

    for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
        if (
            CanvasPointInBox(
                layer.mesh.vertices[i].position,
                editor.boxSelectStartCanvasPos,
                editor.boxSelectCurrentCanvasPos
            )
        ) {
            AddIndexIfMissing(editor.selectedVertices, static_cast<std::uint32_t>(i));
        }
    }

    for (std::size_t i = 0; i < layer.mesh.edges.size(); ++i) {
        const MeshEdge& edge = layer.mesh.edges[i];
        if (
            edge.a >= layer.mesh.vertices.size() ||
            edge.b >= layer.mesh.vertices.size()
        ) {
            continue;
        }

        if (SegmentIntersectsBox(
            layer.mesh.vertices[edge.a].position,
            layer.mesh.vertices[edge.b].position,
            editor.boxSelectStartCanvasPos,
            editor.boxSelectCurrentCanvasPos
        )) {
            AddIndexIfMissing(editor.selectedEdges, static_cast<std::uint32_t>(i));
        }
    }
}

static void ApplyVertexBoxSelection(EditorState& editor) {
    if (!editor.boxSelectAdditive) {
        ClearDeformVertexSelection(editor);
    }

    editor.selectedEdges.clear();

    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
            if (
                CanvasPointInBox(
                    layer.mesh.vertices[i].position,
                    editor.boxSelectStartCanvasPos,
                    editor.boxSelectCurrentCanvasPos
                )
            ) {
                AddVertexRefIfMissing(
                    editor.selectedDeformVertices,
                    SelectedVertexRef{layerIndex, static_cast<std::uint32_t>(i)}
                );
            }
        }
    }
}

static void PushMeshEditOperation(
    EditorState& editor,
    const LayerHistoryState& before,
    const std::string& description
) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    PushLayerOperation(
        editor,
        editor.selectedLayer,
        before,
        CaptureLayerHistoryState(editor.document.layers[editor.selectedLayer]),
        description
    );
}

static void AddVertexAtCanvasPoint(EditorState& editor, Vec2 canvasPoint, bool connectToSelection) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const LayerHistoryState before = CaptureLayerHistoryState(layer);
    const int previousVertex = HasSelectedVertex(editor)
        ? static_cast<int>(ActiveSelectedVertex(editor))
        : -1;

    layer.mesh.vertices.push_back(CreateMeshVertexAtPoint(layer, canvasPoint));
    const std::uint32_t newVertex =
        static_cast<std::uint32_t>(layer.mesh.vertices.size() - 1u);

    if (
        connectToSelection &&
        previousVertex >= 0 &&
        static_cast<std::uint32_t>(previousVertex) < layer.mesh.vertices.size()
    ) {
        AddMeshEdgeIfMissing(layer.mesh, static_cast<std::uint32_t>(previousVertex), newVertex);
    }

    UpdateLayerBoundsFromMesh(layer);
    ClearMeshSelection(editor);
    editor.selectedVertices.push_back(newVertex);
    PushMeshEditOperation(editor, before, "Add mesh vertex");
}

static void DeleteSelectedMeshItems(EditorState& editor) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const LayerHistoryState before = CaptureLayerHistoryState(layer);

    if (!editor.selectedVertices.empty()) {
        std::vector<std::uint8_t> removeVertex(layer.mesh.vertices.size(), 0u);
        for (const std::uint32_t vertex : editor.selectedVertices) {
            if (vertex < removeVertex.size()) {
                removeVertex[vertex] = 1u;
            }
        }

        std::vector<std::uint32_t> remap(layer.mesh.vertices.size(), 0u);
        std::vector<MeshVertex> keptVertices;
        keptVertices.reserve(layer.mesh.vertices.size());

        for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
            if (removeVertex[i]) {
                continue;
            }

            remap[i] = static_cast<std::uint32_t>(keptVertices.size());
            keptVertices.push_back(layer.mesh.vertices[i]);
        }

        std::vector<MeshEdge> keptEdges;
        keptEdges.reserve(layer.mesh.edges.size());
        for (const MeshEdge& edge : layer.mesh.edges) {
            if (
                edge.a >= removeVertex.size() ||
                edge.b >= removeVertex.size() ||
                removeVertex[edge.a] ||
                removeVertex[edge.b]
            ) {
                continue;
            }

            keptEdges.push_back(MeshEdge{remap[edge.a], remap[edge.b]});
        }

        layer.mesh.vertices = std::move(keptVertices);
        layer.mesh.edges = std::move(keptEdges);
        RebuildMeshTrianglesFromEdges(layer.mesh);
    } else if (!editor.selectedEdges.empty()) {
        std::vector<std::uint8_t> removeEdge(layer.mesh.edges.size(), 0u);
        for (const std::uint32_t edge : editor.selectedEdges) {
            if (edge < removeEdge.size()) {
                removeEdge[edge] = 1u;
            }
        }

        std::vector<MeshEdge> keptEdges;
        keptEdges.reserve(layer.mesh.edges.size());
        for (std::size_t i = 0; i < layer.mesh.edges.size(); ++i) {
            if (!removeEdge[i]) {
                keptEdges.push_back(layer.mesh.edges[i]);
            }
        }

        layer.mesh.edges = std::move(keptEdges);
        RebuildMeshTrianglesFromEdges(layer.mesh);
    }

    ClearMeshSelection(editor);
    UpdateLayerBoundsFromMesh(layer);
    PushMeshEditOperation(editor, before, "Delete mesh selection");
}

static void DrawLayerPreviewThumbnail(const EditorLayer& layer, float size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 p1(pos.x + size, pos.y + size);

    drawList->AddRectFilled(pos, p1, IM_COL32(38, 40, 46, 255), 2.0f);

    if (layer.texture) {
        drawList->AddImage(
            ImTextureRef(static_cast<ImTextureID>(static_cast<intptr_t>(layer.texture))),
            pos,
            p1,
            ImVec2(layer.previewU0, layer.previewV0),
            ImVec2(layer.previewU1, layer.previewV1),
            layer.visible ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90)
        );
    }

    drawList->AddRect(pos, p1, IM_COL32(95, 100, 112, 255), 2.0f);
    ImGui::Dummy(ImVec2(size, size));
}

static bool ContainsLayerIndex(const std::vector<int>& values, int layerIndex) {
    return std::find(values.begin(), values.end(), layerIndex) != values.end();
}

static void ToggleLayerMask(EditorState& editor, int layerIndex, int maskLayerIndex) {
    if (
        !IsValidLayerIndex(editor, layerIndex) ||
        !IsValidLayerIndex(editor, maskLayerIndex) ||
        layerIndex == maskLayerIndex
    ) {
        return;
    }

    std::vector<int>& masks = editor.document.layers[layerIndex].maskLayerIndices;
    auto it = std::find(masks.begin(), masks.end(), maskLayerIndex);
    if (it == masks.end()) {
        masks.push_back(maskLayerIndex);
    } else {
        masks.erase(it);
    }

}

static bool LayerUsesAnyMask(
    const EditorLayer& layer,
    const std::vector<int>& maskLayerIndices
) {
    for (const int maskLayerIndex : maskLayerIndices) {
        if (ContainsLayerIndex(layer.maskLayerIndices, maskLayerIndex)) {
            return true;
        }
    }

    return false;
}

static void RebuildLayersAffectedByLayerGeometry(
    EditorState& editor,
    const std::vector<int>& changedLayerIndices
) {
    (void)editor;
    (void)changedLayerIndices;
}

static void RebuildLayersAffectedByLayerGeometry(EditorState& editor, int changedLayerIndex) {
    RebuildLayersAffectedByLayerGeometry(editor, std::vector<int>{changedLayerIndex});
}

static void UpdateActiveParameterLayerVisualEndpoint(EditorState& editor, int layerIndex) {
    if (
        editor.selectedParameter < 0 ||
        editor.selectedParameter >= static_cast<int>(editor.parameters.size()) ||
        !IsValidLayerIndex(editor, layerIndex)
    ) {
        return;
    }

    DeformParameter& parameter = editor.parameters[editor.selectedParameter];
    float endpointOpacity = editor.document.layers[layerIndex].opacity;
    for (int parameterIndex = 0; parameterIndex < static_cast<int>(editor.parameters.size()); ++parameterIndex) {
        if (parameterIndex == editor.selectedParameter) {
            continue;
        }

        const DeformParameter& otherParameter = editor.parameters[parameterIndex];
        const float value = std::clamp(otherParameter.value, 0.0f, 1.0f);
        for (const DeformParameterLayerState& otherState : otherParameter.layers) {
            if (otherState.layerIndex == layerIndex) {
                endpointOpacity -= (otherState.opacityAt1 - otherState.opacityAt0) * value;
                break;
            }
        }
    }
    endpointOpacity = std::clamp(endpointOpacity, 0.0f, 1.0f);

    const bool updateZero = parameter.value <= 0.001f;
    const bool updateOne = parameter.value >= 0.999f;
    if (!updateZero && !updateOne) {
        return;
    }

    for (DeformParameterLayerState& state : parameter.layers) {
        if (state.layerIndex != layerIndex) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (updateZero) {
            if (parameter.affectsOpacity) {
                state.opacityAt0 = endpointOpacity;
            }
            if (parameter.affectsRenderOrder) {
                state.renderOrderOverrideAt0 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state.maskLayerIndicesAt0 = layer.maskLayerIndices;
            }
        } else if (updateOne) {
            if (parameter.affectsOpacity) {
                state.opacityAt1 = endpointOpacity;
            }
            if (parameter.affectsRenderOrder) {
                state.renderOrderOverrideAt1 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state.maskLayerIndicesAt1 = layer.maskLayerIndices;
            }
        }
    }
}

static int RemapLayerIndexAfterMove(int index, int from, int to) {
    if (index == from) {
        return to;
    }

    if (from < to && index > from && index <= to) {
        return index - 1;
    }

    if (to < from && index >= to && index < from) {
        return index + 1;
    }

    return index;
}

static void MoveLayerInList(EditorState& editor, int from, int to) {
    if (
        !IsValidLayerIndex(editor, from) ||
        !IsValidLayerIndex(editor, to) ||
        from == to
    ) {
        return;
    }

    EditorLayer moved = std::move(editor.document.layers[from]);
    editor.document.layers.erase(editor.document.layers.begin() + from);
    editor.document.layers.insert(editor.document.layers.begin() + to, std::move(moved));

    editor.selectedLayer = RemapLayerIndexAfterMove(editor.selectedLayer, from, to);
    for (int& layerIndex : editor.selectedLayers) {
        layerIndex = RemapLayerIndexAfterMove(layerIndex, from, to);
    }

    for (EditorLayer& layer : editor.document.layers) {
        for (int& maskLayerIndex : layer.maskLayerIndices) {
            maskLayerIndex = RemapLayerIndexAfterMove(maskLayerIndex, from, to);
        }
    }

    for (DeformParameter& parameter : editor.parameters) {
        for (DeformParameterLayerState& state : parameter.layers) {
            state.layerIndex = RemapLayerIndexAfterMove(state.layerIndex, from, to);
            for (int& maskLayerIndex : state.maskLayerIndicesAt0) {
                maskLayerIndex = RemapLayerIndexAfterMove(maskLayerIndex, from, to);
            }
            for (int& maskLayerIndex : state.maskLayerIndicesAt1) {
                maskLayerIndex = RemapLayerIndexAfterMove(maskLayerIndex, from, to);
            }
        }
    }

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        RebuildLayerRenderedTexture(editor, i);
    }
}

static void DrawLayerDataPopup(EditorState& editor, int layerIndex) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[layerIndex];

    if (!ImGui::BeginPopup("Layer Data")) {
        return;
    }

    ImGui::TextWrapped("%s", layer.name.c_str());
    ImGui::Separator();

    ImGui::SetNextItemWidth(180.0f);
    float opacity = layer.opacity;
    if (ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f")) {
        const std::vector<DeformParameter> parametersBefore = editor.parameters;
        const LayerHistoryState before = CaptureLayerHistoryState(layer);
        layer.opacity = std::clamp(opacity, 0.0f, 1.0f);
        UpdateActiveParameterLayerVisualEndpoint(editor, layerIndex);
        PushLayerOperationWithParameters(
            editor,
            layerIndex,
            before,
            CaptureLayerHistoryState(layer),
            parametersBefore,
            editor.parameters,
            "Change layer opacity"
        );
    }

    char renderOrderBuffer[64] = {};
    std::strncpy(renderOrderBuffer, layer.renderOrderOverride.c_str(), sizeof(renderOrderBuffer) - 1u);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("Render Override", renderOrderBuffer, sizeof(renderOrderBuffer))) {
        const std::vector<DeformParameter> parametersBefore = editor.parameters;
        const LayerHistoryState before = CaptureLayerHistoryState(layer);
        layer.renderOrderOverride = renderOrderBuffer;
        UpdateActiveParameterLayerVisualEndpoint(editor, layerIndex);
        PushLayerOperationWithParameters(
            editor,
            layerIndex,
            before,
            CaptureLayerHistoryState(layer),
            parametersBefore,
            editor.parameters,
            "Change layer render order override"
        );
    }

    if (ImGui::BeginMenu("Masks")) {
        ImGui::BeginChild("mask_layers", ImVec2(260.0f, 320.0f), ImGuiChildFlags_Borders);
        for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
            if (i == layerIndex) {
                continue;
            }

            bool masked = ContainsLayerIndex(layer.maskLayerIndices, i);
            if (ImGui::Checkbox(editor.document.layers[i].name.c_str(), &masked)) {
                const std::vector<DeformParameter> parametersBefore = editor.parameters;
                const LayerHistoryState before = CaptureLayerHistoryState(layer);
                ToggleLayerMask(editor, layerIndex, i);
                UpdateActiveParameterLayerVisualEndpoint(editor, layerIndex);
                PushLayerOperationWithParameters(
                    editor,
                    layerIndex,
                    before,
                    CaptureLayerHistoryState(editor.document.layers[layerIndex]),
                    parametersBefore,
                    editor.parameters,
                    "Change layer masks"
                );
            }
        }
        ImGui::EndChild();
        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}

static DeformParameterLayerState* FindParameterLayerState(
    DeformParameter& parameter,
    int layerIndex
) {
    for (DeformParameterLayerState& state : parameter.layers) {
        if (state.layerIndex == layerIndex) {
            return &state;
        }
    }

    return nullptr;
}

static const DeformParameterLayerState* FindParameterLayerState(
    const DeformParameter& parameter,
    int layerIndex
) {
    for (const DeformParameterLayerState& state : parameter.layers) {
        if (state.layerIndex == layerIndex) {
            return &state;
        }
    }

    return nullptr;
}

static bool MeshTopologyMatches(const LayerMesh& a, const LayerMesh& b) {
    return a.vertices.size() == b.vertices.size();
}

static bool CanAccumulateParameterState(
    const LayerMesh& baseMesh,
    const DeformParameterLayerState& state
) {
    return
        MeshTopologyMatches(baseMesh, state.meshAt0) &&
        MeshTopologyMatches(baseMesh, state.meshAt1);
}

static void AccumulateParameterMeshDelta(
    LayerMesh& result,
    const DeformParameterLayerState& state,
    float value
) {
    const float t = std::clamp(value, 0.0f, 1.0f);

    for (std::size_t i = 0; i < result.vertices.size(); ++i) {
        result.vertices[i].position.x +=
            (state.meshAt1.vertices[i].position.x - state.meshAt0.vertices[i].position.x) * t;
        result.vertices[i].position.y +=
            (state.meshAt1.vertices[i].position.y - state.meshAt0.vertices[i].position.y) * t;
    }
}

static std::vector<int> CollectMeshParametersForLayer(const EditorState& editor, int layerIndex) {
    std::vector<int> parameterIndices;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(editor.parameters.size()); ++parameterIndex) {
        const DeformParameter& parameter = editor.parameters[parameterIndex];
        if (!parameter.affectsMesh || !FindParameterLayerState(parameter, layerIndex)) {
            continue;
        }

        parameterIndices.push_back(parameterIndex);
    }

    return parameterIndices;
}

static bool ParameterIndexListsMatch(const std::vector<int>& a, const std::vector<int>& b) {
    return a == b;
}

static const DeformParameterMeshCorner* FindMeshCorner(
    const DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    std::uint32_t cornerBits
) {
    for (const DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            !ParameterIndexListsMatch(corner.parameterIndices, parameterIndices) ||
            corner.parameterValues.size() != parameterIndices.size()
        ) {
            continue;
        }

        bool valuesMatch = true;
        for (std::size_t i = 0; i < parameterIndices.size(); ++i) {
            const std::uint8_t bit = static_cast<std::uint8_t>((cornerBits >> i) & 1u);
            if (corner.parameterValues[i] != bit) {
                valuesMatch = false;
                break;
            }
        }

        if (valuesMatch) {
            return &corner;
        }
    }

    return nullptr;
}

static bool ApplyMultilinearMeshForLayer(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    LayerMesh& resultMesh
) {
    if (parameterIndices.size() < 2u || parameterIndices.size() > 12u) {
        return false;
    }

    const DeformParameterLayerState* cornerSource = nullptr;
    for (const int parameterIndex : parameterIndices) {
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (state && !state->meshCorners.empty()) {
            cornerSource = state;
            break;
        }
    }

    if (!cornerSource) {
        return false;
    }

    const std::uint32_t cornerCount = 1u << static_cast<std::uint32_t>(parameterIndices.size());
    std::vector<const DeformParameterMeshCorner*> corners(cornerCount, nullptr);
    for (std::uint32_t cornerBits = 0; cornerBits < cornerCount; ++cornerBits) {
        corners[cornerBits] = FindMeshCorner(*cornerSource, parameterIndices, cornerBits);
        if (!corners[cornerBits] || !MeshTopologyMatches(resultMesh, corners[cornerBits]->mesh)) {
            return false;
        }
    }

    std::vector<float> values;
    values.reserve(parameterIndices.size());
    for (const int parameterIndex : parameterIndices) {
        values.push_back(std::clamp(editor.parameters[parameterIndex].value, 0.0f, 1.0f));
    }

    resultMesh = corners[0]->mesh;
    for (MeshVertex& vertex : resultMesh.vertices) {
        vertex.position = Vec2{0.0f, 0.0f};
    }

    for (std::uint32_t cornerBits = 0; cornerBits < cornerCount; ++cornerBits) {
        float weight = 1.0f;
        for (std::size_t i = 0; i < values.size(); ++i) {
            weight *= ((cornerBits >> i) & 1u) ? values[i] : (1.0f - values[i]);
        }

        const LayerMesh& cornerMesh = corners[cornerBits]->mesh;
        for (std::size_t i = 0; i < resultMesh.vertices.size(); ++i) {
            resultMesh.vertices[i].position.x += cornerMesh.vertices[i].position.x * weight;
            resultMesh.vertices[i].position.y += cornerMesh.vertices[i].position.y * weight;
        }
    }

    return true;
}

static bool CurrentValuesAreMeshCorner(
    const EditorState& editor,
    const std::vector<int>& parameterIndices,
    std::vector<std::uint8_t>& cornerValues
) {
    cornerValues.clear();
    cornerValues.reserve(parameterIndices.size());

    for (const int parameterIndex : parameterIndices) {
        const float value = std::clamp(editor.parameters[parameterIndex].value, 0.0f, 1.0f);
        if (value <= 0.001f) {
            cornerValues.push_back(0u);
        } else if (value >= 0.999f) {
            cornerValues.push_back(1u);
        } else {
            cornerValues.clear();
            return false;
        }
    }

    return true;
}

static void StoreMeshCornerForState(
    DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    const std::vector<std::uint8_t>& cornerValues,
    const LayerMesh& mesh
) {
    for (DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            ParameterIndexListsMatch(corner.parameterIndices, parameterIndices) &&
            corner.parameterValues == cornerValues
        ) {
            corner.mesh = mesh;
            return;
        }
    }

    DeformParameterMeshCorner corner;
    corner.parameterIndices = parameterIndices;
    corner.parameterValues = cornerValues;
    corner.mesh = mesh;
    state.meshCorners.push_back(std::move(corner));
}

static bool HasMeshCorner(
    const DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    const std::vector<std::uint8_t>& cornerValues
) {
    for (const DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            ParameterIndexListsMatch(corner.parameterIndices, parameterIndices) &&
            corner.parameterValues == cornerValues
        ) {
            return true;
        }
    }

    return false;
}

static bool CornerMatchesRequestedBits(
    const DeformParameterMeshCorner& corner,
    const std::vector<int>& parameterIndices,
    std::uint32_t cornerBits
) {
    if (corner.parameterIndices.size() != corner.parameterValues.size()) {
        return false;
    }

    for (std::size_t i = 0; i < corner.parameterIndices.size(); ++i) {
        const auto it = std::find(
            parameterIndices.begin(),
            parameterIndices.end(),
            corner.parameterIndices[i]
        );
        if (it == parameterIndices.end()) {
            return false;
        }

        const std::size_t targetIndex = static_cast<std::size_t>(std::distance(parameterIndices.begin(), it));
        const std::uint8_t requestedValue = static_cast<std::uint8_t>((cornerBits >> targetIndex) & 1u);
        if (corner.parameterValues[i] != requestedValue) {
            return false;
        }
    }

    return true;
}

static LayerMesh BuildSeedMeshForMultilinearCorner(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    std::uint32_t cornerBits
) {
    LayerMesh fallbackMesh = editor.document.layers[layerIndex].mesh;
    const DeformParameterLayerState* baseState = nullptr;

    const DeformParameterMeshCorner* bestExistingCorner = nullptr;
    std::size_t bestExistingCornerParameterCount = 0u;
    for (const int parameterIndex : parameterIndices) {
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (!state) {
            continue;
        }

        if (!baseState) {
            baseState = state;
        }

        for (const DeformParameterMeshCorner& corner : state->meshCorners) {
            if (
                corner.parameterIndices.size() > bestExistingCornerParameterCount &&
                CornerMatchesRequestedBits(corner, parameterIndices, cornerBits) &&
                MeshTopologyMatches(fallbackMesh, corner.mesh)
            ) {
                bestExistingCorner = &corner;
                bestExistingCornerParameterCount = corner.parameterIndices.size();
            }
        }
    }

    if (bestExistingCorner) {
        return bestExistingCorner->mesh;
    }

    if (!baseState || !MeshTopologyMatches(fallbackMesh, baseState->meshAt0)) {
        return fallbackMesh;
    }

    LayerMesh seedMesh = baseState->meshAt0;
    for (std::size_t parameterOffset = 0; parameterOffset < parameterIndices.size(); ++parameterOffset) {
        if (((cornerBits >> parameterOffset) & 1u) == 0u) {
            continue;
        }

        const int parameterIndex = parameterIndices[parameterOffset];
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (!state || !CanAccumulateParameterState(seedMesh, *state)) {
            continue;
        }

        AccumulateParameterMeshDelta(seedMesh, *state, 1.0f);
    }

    return seedMesh;
}

static void EnsureMultilinearMeshCornersForLayer(EditorState& editor, int layerIndex) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    const std::vector<int> parameterIndices = CollectMeshParametersForLayer(editor, layerIndex);
    if (parameterIndices.size() < 2u || parameterIndices.size() > 12u) {
        return;
    }

    const std::uint32_t cornerCount = 1u << static_cast<std::uint32_t>(parameterIndices.size());
    for (std::uint32_t cornerBits = 0; cornerBits < cornerCount; ++cornerBits) {
        std::vector<std::uint8_t> cornerValues;
        cornerValues.reserve(parameterIndices.size());
        for (std::size_t i = 0; i < parameterIndices.size(); ++i) {
            cornerValues.push_back(static_cast<std::uint8_t>((cornerBits >> i) & 1u));
        }

        const LayerMesh seedMesh = BuildSeedMeshForMultilinearCorner(
            editor,
            layerIndex,
            parameterIndices,
            cornerBits
        );

        for (const int parameterIndex : parameterIndices) {
            DeformParameterLayerState* state =
                FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
            if (state && !HasMeshCorner(*state, parameterIndices, cornerValues)) {
                StoreMeshCornerForState(*state, parameterIndices, cornerValues, seedMesh);
            }
        }
    }
}

static void EnsureMultilinearMeshCornersForAllLayers(EditorState& editor) {
    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        EnsureMultilinearMeshCornersForLayer(editor, layerIndex);
    }
}

static bool StoreCurrentMeshCornerForLayer(EditorState& editor, int layerIndex, const LayerMesh& mesh) {
    const std::vector<int> parameterIndices = CollectMeshParametersForLayer(editor, layerIndex);
    if (parameterIndices.size() < 2u) {
        return false;
    }

    std::vector<std::uint8_t> cornerValues;
    if (!CurrentValuesAreMeshCorner(editor, parameterIndices, cornerValues)) {
        return false;
    }

    for (const int parameterIndex : parameterIndices) {
        DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (state) {
            StoreMeshCornerForState(*state, parameterIndices, cornerValues, mesh);
        }
    }

    return true;
}

static void ApplyAllDeformParameters(EditorState& editor) {
    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        const DeformParameterLayerState* meshBaseState = nullptr;
        const DeformParameterLayerState* opacityBaseState = nullptr;
        const DeformParameterLayerState* renderOrderBaseState = nullptr;
        const DeformParameterLayerState* maskingBaseState = nullptr;
        for (const DeformParameter& parameter : editor.parameters) {
            const DeformParameterLayerState* state = FindParameterLayerState(parameter, layerIndex);
            if (!state) {
                continue;
            }

            if (parameter.affectsMesh && !meshBaseState) {
                meshBaseState = state;
            }
            if (parameter.affectsOpacity && !opacityBaseState) {
                opacityBaseState = state;
            }
            if (parameter.affectsRenderOrder && !renderOrderBaseState) {
                renderOrderBaseState = state;
            }
            if (parameter.affectsMasking && !maskingBaseState) {
                maskingBaseState = state;
            }
        }

        if (!meshBaseState && !opacityBaseState && !renderOrderBaseState && !maskingBaseState) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        LayerMesh resultMesh = meshBaseState ? meshBaseState->meshAt0 : layer.mesh;
        float resultOpacity = opacityBaseState ? opacityBaseState->opacityAt0 : layer.opacity;
        std::string resultRenderOrderOverride = renderOrderBaseState
            ? renderOrderBaseState->renderOrderOverrideAt0
            : layer.renderOrderOverride;
        std::vector<int> resultMaskLayerIndices = maskingBaseState
            ? maskingBaseState->maskLayerIndicesAt0
            : layer.maskLayerIndices;
        const std::vector<int> meshParameterIndices = CollectMeshParametersForLayer(editor, layerIndex);
        const bool usingMultilinearMesh =
            ApplyMultilinearMeshForLayer(editor, layerIndex, meshParameterIndices, resultMesh);

        for (const DeformParameter& parameter : editor.parameters) {
            const DeformParameterLayerState* state = FindParameterLayerState(parameter, layerIndex);
            if (!state) {
                continue;
            }

            const float value = std::clamp(parameter.value, 0.0f, 1.0f);
            if (!usingMultilinearMesh && parameter.affectsMesh && CanAccumulateParameterState(resultMesh, *state)) {
                AccumulateParameterMeshDelta(resultMesh, *state, value);
            }
            if (parameter.affectsOpacity) {
                resultOpacity += (state->opacityAt1 - state->opacityAt0) * value;
            }

            if (parameter.affectsRenderOrder && value <= 0.001f) {
                resultRenderOrderOverride = state->renderOrderOverrideAt0;
            } else if (parameter.affectsRenderOrder && value >= 0.999f) {
                resultRenderOrderOverride = state->renderOrderOverrideAt1;
            }

            if (parameter.affectsMasking && value <= 0.001f) {
                resultMaskLayerIndices = state->maskLayerIndicesAt0;
            } else if (parameter.affectsMasking && value >= 0.999f) {
                resultMaskLayerIndices = state->maskLayerIndicesAt1;
            }
        }

        layer.mesh = std::move(resultMesh);
        layer.opacity = std::clamp(resultOpacity, 0.0f, 1.0f);
        layer.renderOrderOverride = std::move(resultRenderOrderOverride);
        layer.maskLayerIndices = std::move(resultMaskLayerIndices);
        UpdateLayerBoundsFromMesh(layer);
    }
}

static LayerMesh MeshWithoutOtherParameterDeltas(
    const EditorState& editor,
    int selectedParameterIndex,
    int layerIndex,
    const LayerMesh& currentMesh
) {
    LayerMesh endpointMesh = currentMesh;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(editor.parameters.size()); ++parameterIndex) {
        if (parameterIndex == selectedParameterIndex) {
            continue;
        }

        const DeformParameter& otherParameter = editor.parameters[parameterIndex];
        const DeformParameterLayerState* otherState = FindParameterLayerState(otherParameter, layerIndex);
        if (!otherParameter.affectsMesh || !otherState || !CanAccumulateParameterState(endpointMesh, *otherState)) {
            continue;
        }

        const float value = std::clamp(otherParameter.value, 0.0f, 1.0f);
        for (std::size_t i = 0; i < endpointMesh.vertices.size(); ++i) {
            endpointMesh.vertices[i].position.x -=
                (otherState->meshAt1.vertices[i].position.x - otherState->meshAt0.vertices[i].position.x) * value;
            endpointMesh.vertices[i].position.y -=
                (otherState->meshAt1.vertices[i].position.y - otherState->meshAt0.vertices[i].position.y) * value;
        }
    }

    return endpointMesh;
}

static float OpacityWithoutOtherParameterDeltas(
    const EditorState& editor,
    int selectedParameterIndex,
    int layerIndex,
    float currentOpacity
) {
    float endpointOpacity = currentOpacity;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(editor.parameters.size()); ++parameterIndex) {
        if (parameterIndex == selectedParameterIndex) {
            continue;
        }

        const DeformParameter& otherParameter = editor.parameters[parameterIndex];
        const DeformParameterLayerState* otherState = FindParameterLayerState(otherParameter, layerIndex);
        if (!otherParameter.affectsOpacity || !otherState) {
            continue;
        }

        const float value = std::clamp(otherParameter.value, 0.0f, 1.0f);
        endpointOpacity -= (otherState->opacityAt1 - otherState->opacityAt0) * value;
    }

    return std::clamp(endpointOpacity, 0.0f, 1.0f);
}

static DeformParameterLayerState CreateParameterLayerStateForLayer(
    const EditorLayer& layer,
    int layerIndex
) {
    DeformParameterLayerState state;
    state.layerIndex = layerIndex;
    state.meshAt0 = layer.mesh;
    state.meshAt1 = layer.mesh;
    state.opacityAt0 = layer.opacity;
    state.opacityAt1 = layer.opacity;
    state.renderOrderOverrideAt0 = layer.renderOrderOverride;
    state.renderOrderOverrideAt1 = layer.renderOrderOverride;
    state.maskLayerIndicesAt0 = layer.maskLayerIndices;
    state.maskLayerIndicesAt1 = layer.maskLayerIndices;
    return state;
}

static void AddDeformParameter(EditorState& editor) {
    if (editor.selectedLayers.empty()) {
        return;
    }

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedParameterBefore = editor.selectedParameter;

    DeformParameter parameter;
    parameter.name = "Parameter " + std::to_string(editor.parameters.size() + 1u);
    parameter.value = 0.0f;

    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        parameter.layers.push_back(CreateParameterLayerStateForLayer(
            editor.document.layers[layerIndex],
            layerIndex
        ));
    }

    if (parameter.layers.empty()) {
        return;
    }

    editor.parameters.push_back(std::move(parameter));
    editor.selectedParameter = static_cast<int>(editor.parameters.size() - 1u);
    for (const int layerIndex : editor.selectedLayers) {
        EnsureMultilinearMeshCornersForLayer(editor, layerIndex);
    }
    PushParameterOperation(
        editor,
        parametersBefore,
        selectedParameterBefore,
        editor.parameters,
        editor.selectedParameter,
        "Add parameter"
    );
}

static bool ParameterHasLayer(const DeformParameter& parameter, int layerIndex) {
    return std::any_of(
        parameter.layers.begin(),
        parameter.layers.end(),
        [&](const DeformParameterLayerState& state) {
            return state.layerIndex == layerIndex;
        }
    );
}

static void ToggleParameterLayerMembership(
    EditorState& editor,
    int parameterIndex,
    int layerIndex
) {
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size()) ||
        !IsValidLayerIndex(editor, layerIndex)
    ) {
        return;
    }

    const std::vector<DeformParameter> before = editor.parameters;
    const int selectedBefore = editor.selectedParameter;
    DeformParameter& parameter = editor.parameters[parameterIndex];

    auto it = std::find_if(
        parameter.layers.begin(),
        parameter.layers.end(),
        [&](const DeformParameterLayerState& state) {
            return state.layerIndex == layerIndex;
        }
    );

    if (it == parameter.layers.end()) {
        parameter.layers.push_back(CreateParameterLayerStateForLayer(
            editor.document.layers[layerIndex],
            layerIndex
        ));
        EnsureMultilinearMeshCornersForLayer(editor, layerIndex);
    } else {
        parameter.layers.erase(it);
    }

    PushParameterOperation(
        editor,
        before,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Change parameter layers"
    );
}

static void DeleteSelectedParameter(EditorState& editor) {
    if (
        editor.selectedParameter < 0 ||
        editor.selectedParameter >= static_cast<int>(editor.parameters.size())
    ) {
        return;
    }

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedParameterBefore = editor.selectedParameter;

    editor.parameters.erase(editor.parameters.begin() + editor.selectedParameter);
    if (editor.parameters.empty()) {
        editor.selectedParameter = -1;
    } else {
        editor.selectedParameter = std::min(
            editor.selectedParameter,
            static_cast<int>(editor.parameters.size()) - 1
        );
    }
    ApplyAllDeformParameters(editor);
    PushParameterOperation(
        editor,
        parametersBefore,
        selectedParameterBefore,
        editor.parameters,
        editor.selectedParameter,
        "Delete parameter"
    );
}

static void PushParameterChannelOperation(
    EditorState& editor,
    const std::vector<DeformParameter>& parametersBefore,
    int selectedParameterBefore,
    const char* description
) {
    EnsureMultilinearMeshCornersForAllLayers(editor);
    ApplyAllDeformParameters(editor);
    PushParameterOperation(
        editor,
        parametersBefore,
        selectedParameterBefore,
        editor.parameters,
        editor.selectedParameter,
        description
    );
}

static void UpdateActiveParameterEndpointForLayers(
    EditorState& editor,
    const std::vector<int>& layerIndices
) {
    if (
        editor.selectedParameter < 0 ||
        editor.selectedParameter >= static_cast<int>(editor.parameters.size())
    ) {
        return;
    }

    DeformParameter& parameter = editor.parameters[editor.selectedParameter];
    const bool updateZero = parameter.value <= 0.001f;
    const bool updateOne = parameter.value >= 0.999f;
    if (!updateZero && !updateOne) {
        return;
    }

    for (const int layerIndex : layerIndices) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        DeformParameterLayerState* state = FindParameterLayerState(parameter, layerIndex);
        if (!state) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        const LayerMesh endpointMesh = MeshWithoutOtherParameterDeltas(
            editor,
            editor.selectedParameter,
            layerIndex,
            layer.mesh
        );
        const float endpointOpacity = OpacityWithoutOtherParameterDeltas(
            editor,
            editor.selectedParameter,
            layerIndex,
            layer.opacity
        );
        const bool storedMultilinearMeshCorner =
            parameter.affectsMesh && StoreCurrentMeshCornerForLayer(editor, layerIndex, layer.mesh);

        if (updateZero) {
            if (parameter.affectsMesh && !storedMultilinearMeshCorner) {
                state->meshAt0 = endpointMesh;
            }
            if (parameter.affectsOpacity) {
                state->opacityAt0 = endpointOpacity;
            }
            if (parameter.affectsRenderOrder) {
                state->renderOrderOverrideAt0 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state->maskLayerIndicesAt0 = layer.maskLayerIndices;
            }
        } else if (updateOne) {
            if (parameter.affectsMesh && !storedMultilinearMeshCorner) {
                state->meshAt1 = endpointMesh;
            }
            if (parameter.affectsOpacity) {
                state->opacityAt1 = endpointOpacity;
            }
            if (parameter.affectsRenderOrder) {
                state->renderOrderOverrideAt1 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state->maskLayerIndicesAt1 = layer.maskLayerIndices;
            }
        }
    }
}

static void DrawDeformParametersPanel(EditorState& editor) {
    ImGui::Begin("Parameters");

    if (editor.selectedLayers.empty()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Add Parameter")) {
        AddDeformParameter(editor);
    }
    if (editor.selectedLayers.empty()) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    const bool canDelete =
        editor.selectedParameter >= 0 &&
        editor.selectedParameter < static_cast<int>(editor.parameters.size());
    if (!canDelete) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete")) {
        DeleteSelectedParameter(editor);
    }
    if (!canDelete) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (editor.parameters.empty()) {
        ImGui::TextDisabled("No parameters.");
        ImGui::End();
        return;
    }

    for (int i = 0; i < static_cast<int>(editor.parameters.size()); ++i) {
        DeformParameter& parameter = editor.parameters[i];
        ImGui::PushID(i);

        const bool selected = editor.selectedParameter == i;
        if (ImGui::Selectable(parameter.name.c_str(), selected)) {
            editor.selectedParameter = i;
        }
        if (ImGui::BeginPopupContextItem("parameter_options")) {
            editor.selectedParameter = i;
            char nameBuffer[128] = {};
            std::strncpy(nameBuffer, parameter.name.c_str(), sizeof(nameBuffer) - 1u);
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
            if (ImGui::IsItemDeactivatedAfterEdit() && parameter.name != nameBuffer) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.name = nameBuffer;
                PushParameterOperation(
                    editor,
                    before,
                    selectedBefore,
                    editor.parameters,
                    editor.selectedParameter,
                    "Rename parameter"
                );
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Can Edit");
            ImGui::Separator();

            bool affectsMesh = parameter.affectsMesh;
            if (ImGui::Checkbox("Meshes", &affectsMesh)) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.affectsMesh = affectsMesh;
                PushParameterChannelOperation(editor, before, selectedBefore, "Change parameter channels");
            }

            bool affectsRenderOrder = parameter.affectsRenderOrder;
            if (ImGui::Checkbox("Render Order", &affectsRenderOrder)) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.affectsRenderOrder = affectsRenderOrder;
                PushParameterChannelOperation(editor, before, selectedBefore, "Change parameter channels");
            }

            bool affectsMasking = parameter.affectsMasking;
            if (ImGui::Checkbox("Masking", &affectsMasking)) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.affectsMasking = affectsMasking;
                PushParameterChannelOperation(editor, before, selectedBefore, "Change parameter channels");
            }

            bool affectsOpacity = parameter.affectsOpacity;
            if (ImGui::Checkbox("Opacity", &affectsOpacity)) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.affectsOpacity = affectsOpacity;
                PushParameterChannelOperation(editor, before, selectedBefore, "Change parameter channels");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Layers");
            ImGui::BeginChild("parameter_layers", ImVec2(280.0f, 260.0f), ImGuiChildFlags_Borders);
            for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
                bool assigned = ParameterHasLayer(parameter, layerIndex);
                if (ImGui::Checkbox(editor.document.layers[layerIndex].name.c_str(), &assigned)) {
                    ToggleParameterLayerMembership(editor, i, layerIndex);
                }
            }
            ImGui::EndChild();

            ImGui::EndPopup();
        }

        ImGui::SetNextItemWidth(-1.0f);
        float value = parameter.value;
        if (ImGui::SliderFloat("##value", &value, 0.0f, 1.0f, "%.2f")) {
            parameter.value = value;
            editor.selectedParameter = i;
            ApplyAllDeformParameters(editor);
        }

        if (selected) {
            ImGui::TextDisabled("%zu layer%s",
                parameter.layers.size(),
                parameter.layers.size() == 1u ? "" : "s"
            );
        }

        ImGui::PopID();
    }

    ImGui::End();
}

static void DrawLayerPanel(EditorState& editor) {
    ImGui::Begin("Layers");

    const bool canUndo = !editor.history.undoStack.empty();
    const bool canRedo = !editor.history.redoStack.empty();

    if (!canUndo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Undo")) {
        UndoLastOperation(editor);
    }
    if (!canUndo) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (!canRedo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Redo")) {
        RedoLastOperation(editor);
    }
    if (!canRedo) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (editor.document.layers.empty()) {
        ImGui::TextDisabled("No layers loaded.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Render order: 01 is front");

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        EditorLayer& layer = editor.document.layers[i];

        ImGui::PushID(i);
        ImGui::BeginGroup();

        if (layer.renderOrderOverride.empty()) {
            ImGui::Text("%02d", i + 1);
        } else {
            ImGui::Text("%s", layer.renderOrderOverride.c_str());
        }
        ImGui::SameLine();

        if (i <= 0) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton("^")) {
            MoveLayerInList(editor, i, i - 1);
            ImGui::EndGroup();
            ImGui::PopID();
            break;
        }
        if (i <= 0) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (i + 1 >= static_cast<int>(editor.document.layers.size())) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton("v")) {
            MoveLayerInList(editor, i, i + 1);
            ImGui::EndGroup();
            ImGui::PopID();
            break;
        }
        if (i + 1 >= static_cast<int>(editor.document.layers.size())) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        const LayerHistoryState beforeVisibility = CaptureLayerHistoryState(layer);
        if (ImGui::Checkbox("##visible", &layer.visible)) {
            if (!layer.visible) {
                RemoveLayerFromSelection(editor, i);
            }

            PushLayerOperation(
                editor,
                i,
                beforeVisibility,
                CaptureLayerHistoryState(layer),
                "Change layer visibility"
            );
        }
        ImGui::SameLine();

        const float previewSize = 20.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float selectableWidth = std::max(
            60.0f,
            ImGui::GetContentRegionAvail().x - previewSize - spacing
        );
        const bool selected = IsLayerSelected(editor, i);
        if (ImGui::Selectable(layer.name.c_str(), selected, 0, ImVec2(selectableWidth, previewSize))) {
            HandleLayerSelectionClick(editor, i);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("Layer Data");
        }
        DrawLayerDataPopup(editor, i);

        ImGui::SameLine();
        DrawLayerPreviewThumbnail(layer, previewSize);

        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::Separator();

    if (IsValidLayerIndex(editor, editor.selectedLayer)) {
        const EditorLayer& layer = editor.document.layers[editor.selectedLayer];

        ImGui::Text("Selected:");
        ImGui::TextWrapped("%s", layer.name.c_str());

        ImGui::Text("Bounds:");
        ImGui::Text("L:%d T:%d R:%d B:%d",
            layer.left,
            layer.top,
            layer.right,
            layer.bottom
        );

        ImGui::Text("Initial mesh:");
        ImGui::Text("%zu vertices", layer.mesh.vertices.size());
        ImGui::Text("%zu indices", layer.mesh.indices.size());

        float mx0 = 0.0f;
        float my0 = 0.0f;
        float mx1 = 0.0f;
        float my1 = 0.0f;

        if (GetMeshBounds(layer.mesh, mx0, my0, mx1, my1)) {
            ImGui::Text("Mesh bounds:");
            ImGui::Text("X: %.0f -> %.0f", mx0, mx1);
            ImGui::Text("Y: %.0f -> %.0f", my0, my1);
        }
    }

    ImGui::End();
}

static void DrawMeshGeneratorSettingsPanel(EditorState& editor) {
    if (!editor.showMeshGeneratorSettings) {
        return;
    }

    ImGui::Begin("Mesh Generator Settings", &editor.showMeshGeneratorSettings);

    ImGui::SliderInt("Alpha Threshold", &editor.meshSettings.alphaThreshold, 0, 255);
    ImGui::SliderInt("Mesh Detail", &editor.meshSettings.meshDetail, 1, 5);
    ImGui::SliderInt(
        "Perimeter Spacing",
        &editor.meshSettings.perimeterSpacing,
        1,
        2000,
        "%d px"
    );
    ImGui::SliderInt(
        "Perimeter Buffer",
        &editor.meshSettings.perimeterBuffer,
        0,
        500,
        "%d px"
    );
    ImGui::SliderInt(
        "Interior Depth Spacing",
        &editor.meshSettings.interiorDepthSpacing,
        1,
        2000,
        "%d px"
    );
    ImGui::SliderInt(
        "Interior Point Spacing",
        &editor.meshSettings.interiorPointSpacing,
        1,
        2000,
        "%d px"
    );
    ImGui::SliderInt(
        "Max Perimeter Points",
        &editor.meshSettings.maxPerimeterPoints,
        16,
        10000
    );
    ImGui::SliderInt(
        "Max Interior Points",
        &editor.meshSettings.maxInteriorPoints,
        0,
        20000
    );

    ImGui::End();
}

static void DrawViewportControlPanel(EditorState& editor) {
    ImGui::TextUnformatted("Edit Mode");
    ImGui::SameLine();

    int mode = editor.mode == EditorMode::Mesh ? 1 : 0;

    if (ImGui::RadioButton("Deform", mode == 0)) {
        editor.mode = EditorMode::Layer;
        ClearMeshSelection(editor);
        editor.boxSelectingMesh = false;
        editor.draggingMeshSelection = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Mesh", mode == 1)) {
        editor.mode = EditorMode::Mesh;
        editor.draggingLayer = false;
        editor.draggedLayer = -1;
        editor.layerTransformMode = LayerTransformMode::None;
    }

    ImGui::SameLine();

    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Generate Mesh")) {
        RequestGenerateMesh(editor);
    }

    ImGui::SameLine();

    if (ImGui::Button("Mesh Generator Settings")) {
        editor.showMeshGeneratorSettings = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Save Mesh")) {
        SaveMeshesFromUi(editor);
    }

    ImGui::SameLine();

    const bool hasHistory =
        !editor.history.undoStack.empty() ||
        !editor.history.redoStack.empty();
    if (!hasHistory) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Clear History")) {
        ClearHistoryFromUi(editor);
    }
    if (!hasHistory) {
        ImGui::EndDisabled();
    }

    if (editor.mode == EditorMode::Mesh) {
        ImGui::SameLine();
        ImGui::Text(
            "V:%zu E:%zu",
            editor.selectedVertices.size(),
            editor.selectedEdges.size()
        );
    }

    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        ImGui::EndDisabled();
    }
}

static void FitDocumentToViewport(EditorState& editor, ImVec2 viewportSize) {
    if (
        editor.document.canvasWidth <= 0 ||
        editor.document.canvasHeight <= 0 ||
        viewportSize.x <= 1.0f ||
        viewportSize.y <= 1.0f
    ) {
        return;
    }

    const float padding = 40.0f;

    const float availableW = std::max(1.0f, viewportSize.x - padding * 2.0f);
    const float availableH = std::max(1.0f, viewportSize.y - padding * 2.0f);

    const float scaleX = availableW / static_cast<float>(editor.document.canvasWidth);
    const float scaleY = availableH / static_cast<float>(editor.document.canvasHeight);

    editor.zoom = std::clamp(std::min(scaleX, scaleY), 0.05f, 8.0f);

    const float imageW = static_cast<float>(editor.document.canvasWidth) * editor.zoom;
    const float imageH = static_cast<float>(editor.document.canvasHeight) * editor.zoom;

    editor.pan.x = (viewportSize.x - imageW) * 0.5f;
    editor.pan.y = (viewportSize.y - imageH) * 0.5f;
}

static bool GetQuadMeshBoundsAndUv(
    const EditorLayer& layer,
    float& x0,
    float& y0,
    float& x1,
    float& y1,
    float& u0,
    float& v0,
    float& u1,
    float& v1
) {
    if (layer.mesh.vertices.size() < 4) {
        return false;
    }

    x0 = layer.mesh.vertices[0].position.x;
    y0 = layer.mesh.vertices[0].position.y;
    x1 = layer.mesh.vertices[0].position.x;
    y1 = layer.mesh.vertices[0].position.y;

    u0 = layer.mesh.vertices[0].uv.x;
    v0 = layer.mesh.vertices[0].uv.y;
    u1 = layer.mesh.vertices[0].uv.x;
    v1 = layer.mesh.vertices[0].uv.y;

    for (const MeshVertex& vertex : layer.mesh.vertices) {
        x0 = std::min(x0, vertex.position.x);
        y0 = std::min(y0, vertex.position.y);
        x1 = std::max(x1, vertex.position.x);
        y1 = std::max(y1, vertex.position.y);

        u0 = std::min(u0, vertex.uv.x);
        v0 = std::min(v0, vertex.uv.y);
        u1 = std::max(u1, vertex.uv.x);
        v1 = std::max(v1, vertex.uv.y);
    }

    return x1 > x0 && y1 > y0 && u1 > u0 && v1 > v0;
}

static bool LayerAlphaHitTest(
    const EditorLayer& layer,
    float canvasX,
    float canvasY,
    std::uint8_t alphaThreshold = kOpaquePixelAlphaThreshold
) {
    if (
        !layer.visible ||
        layer.alpha.empty() ||
        layer.width <= 0 ||
        layer.height <= 0
    ) {
        return false;
    }

    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;

    if (!GetQuadMeshBoundsAndUv(layer, x0, y0, x1, y1, u0, v0, u1, v1)) {
        return false;
    }

    if (canvasX < x0 || canvasX >= x1 || canvasY < y0 || canvasY >= y1) {
        return false;
    }

    const float tx = (canvasX - x0) / (x1 - x0);
    const float ty = (canvasY - y0) / (y1 - y0);

    const float u = u0 + tx * (u1 - u0);
    const float v = v0 + ty * (v1 - v0);

    int px = static_cast<int>(u * static_cast<float>(layer.width));
    int py = static_cast<int>(v * static_cast<float>(layer.height));

    px = std::clamp(px, 0, layer.width - 1);
    py = std::clamp(py, 0, layer.height - 1);

    return LayerAlphaAt(layer, px, py) >= alphaThreshold;
}

static int PickTopmostLayerAtCanvasPoint(
    const EditorState& editor,
    float canvasX,
    float canvasY
) {
    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        const EditorLayer& layer = editor.document.layers[i];

        if (LayerAlphaHitTest(layer, canvasX, canvasY)) {
            return i;
        }
    }

    return -1;
}

static int PickSelectedLayerAtCanvasPoint(
    const EditorState& editor,
    float canvasX,
    float canvasY
) {
    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (LayerAlphaHitTest(layer, canvasX, canvasY)) {
            return layerIndex;
        }
    }

    return -1;
}

static int PickLayerForLayerModeDrag(
    const EditorState& editor,
    float canvasX,
    float canvasY
) {
    const int selectedLayerHit = PickSelectedLayerAtCanvasPoint(editor, canvasX, canvasY);
    if (selectedLayerHit >= 0) {
        return selectedLayerHit;
    }

    return PickTopmostLayerAtCanvasPoint(editor, canvasX, canvasY);
}

static bool PickVertexInSelectedLayers(
    const EditorState& editor,
    Vec2 canvasPoint,
    float zoom,
    SelectedVertexRef& picked
) {
    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        const int vertexIndex = PickVertexAtCanvasPoint(layer, canvasPoint, zoom);
        if (vertexIndex >= 0) {
            picked = SelectedVertexRef{layerIndex, static_cast<std::uint32_t>(vertexIndex)};
            return true;
        }
    }

    return false;
}

static void BeginLayerTransform(
    EditorState& editor,
    int layerIndex,
    Vec2 canvasPoint,
    LayerTransformMode mode,
    int axisX,
    int axisY
) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[layerIndex];

    editor.draggingLayer = true;
    editor.draggedLayer = layerIndex;
    editor.layerTransformMode = mode;
    editor.layerDragStartCanvasPos = canvasPoint;
    editor.lastDragCanvasPos = canvasPoint;
    editor.dragLayerMoved = false;
    editor.layerDragThresholdPassed = false;
    editor.dragStartLayerState = CaptureLayerHistoryState(layer);
    editor.layerTransformLayerIndices.clear();
    editor.layerTransformStartStates.clear();

    if (IsLayerSelected(editor, layerIndex)) {
        for (const int selectedLayerIndex : editor.selectedLayers) {
            if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
                continue;
            }

            editor.layerTransformLayerIndices.push_back(selectedLayerIndex);
            editor.layerTransformStartStates.push_back(
                CaptureLayerHistoryState(editor.document.layers[selectedLayerIndex])
            );
        }
    } else {
        editor.layerTransformLayerIndices.push_back(layerIndex);
        editor.layerTransformStartStates.push_back(CaptureLayerHistoryState(layer));
    }

    editor.layerTransformStartMesh = layer.mesh;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    if (GetSelectedLayerBounds(editor, x0, y0, x1, y1)) {
        editor.layerTransformCenter = Vec2{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    } else {
        editor.layerTransformCenter = LayerMeshBoundsCenter(layer.mesh);
    }
    editor.layerTransformStartMouse = canvasPoint;
    editor.layerScaleAxisX = axisX;
    editor.layerScaleAxisY = axisY;
}

static void ApplyLayerScaleTransform(EditorState& editor, EditorLayer& layer, Vec2 canvasPoint, bool centered) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(editor.layerTransformStartMesh, x0, y0, x1, y1)) {
        return;
    }

    const Vec2 center = editor.layerTransformCenter;
    const Vec2 startMouse = editor.layerTransformStartMouse;
    const bool uniformScale =
        centered &&
        editor.layerScaleAxisX != 0 &&
        editor.layerScaleAxisY != 0;

    float originX = center.x;
    float originY = center.y;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (editor.layerScaleAxisX != 0) {
        originX = centered ? center.x : (editor.layerScaleAxisX < 0 ? x1 : x0);
        const float startDistance = startMouse.x - originX;
        const float currentDistance = canvasPoint.x - originX;
        if (std::abs(startDistance) > 0.0001f) {
            scaleX = currentDistance / startDistance;
        }
    }

    if (editor.layerScaleAxisY != 0) {
        originY = centered ? center.y : (editor.layerScaleAxisY < 0 ? y1 : y0);
        const float startDistance = startMouse.y - originY;
        const float currentDistance = canvasPoint.y - originY;
        if (std::abs(startDistance) > 0.0001f) {
            scaleY = currentDistance / startDistance;
        }
    }

    if (uniformScale) {
        const float startDx = startMouse.x - originX;
        const float startDy = startMouse.y - originY;
        const float currentDx = canvasPoint.x - originX;
        const float currentDy = canvasPoint.y - originY;
        const float startLenSq = startDx * startDx + startDy * startDy;

        if (startLenSq > 0.0001f) {
            const float projectedScale =
                (currentDx * startDx + currentDy * startDy) / startLenSq;
            scaleX = projectedScale;
            scaleY = projectedScale;
        }
    }

    if (std::abs(scaleX) < 0.01f) {
        scaleX = scaleX < 0.0f ? -0.01f : 0.01f;
    }

    if (std::abs(scaleY) < 0.01f) {
        scaleY = scaleY < 0.0f ? -0.01f : 0.01f;
    }

    layer.mesh = editor.layerTransformStartMesh;

    for (MeshVertex& vertex : layer.mesh.vertices) {
        vertex.position.x = originX + (vertex.position.x - originX) * scaleX;
        vertex.position.y = originY + (vertex.position.y - originY) * scaleY;
    }

    UpdateLayerBoundsFromMesh(layer);
}

static void ApplyLayerRotateTransform(EditorState& editor, EditorLayer& layer, Vec2 canvasPoint) {
    const Vec2 center = editor.layerTransformCenter;
    const float startAngle = std::atan2(
        editor.layerTransformStartMouse.y - center.y,
        editor.layerTransformStartMouse.x - center.x
    );
    const float currentAngle = std::atan2(
        canvasPoint.y - center.y,
        canvasPoint.x - center.x
    );
    const float angle = currentAngle - startAngle;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    layer.mesh = editor.layerTransformStartMesh;

    for (MeshVertex& vertex : layer.mesh.vertices) {
        const Vec2 delta = vertex.position - center;
        vertex.position.x = center.x + delta.x * c - delta.y * s;
        vertex.position.y = center.y + delta.x * s + delta.y * c;
    }

    UpdateLayerBoundsFromMesh(layer);
}

static bool GetLayerTransformStartBounds(
    const EditorState& editor,
    float& x0,
    float& y0,
    float& x1,
    float& y1
) {
    bool found = false;
    for (const LayerHistoryState& state : editor.layerTransformStartStates) {
        float lx0 = 0.0f;
        float ly0 = 0.0f;
        float lx1 = 0.0f;
        float ly1 = 0.0f;
        if (GetMeshBounds(state.mesh, lx0, ly0, lx1, ly1)) {
            ExpandBounds(x0, y0, x1, y1, lx0, ly0, lx1, ly1, found);
        }
    }

    return found;
}

static void ApplyLayerScaleTransformToSelection(EditorState& editor, Vec2 canvasPoint, bool centered) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetLayerTransformStartBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    const Vec2 center = editor.layerTransformCenter;
    const Vec2 startMouse = editor.layerTransformStartMouse;
    const bool uniformScale =
        centered &&
        editor.layerScaleAxisX != 0 &&
        editor.layerScaleAxisY != 0;

    float originX = center.x;
    float originY = center.y;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (editor.layerScaleAxisX != 0) {
        originX = centered ? center.x : (editor.layerScaleAxisX < 0 ? x1 : x0);
        const float startDistance = startMouse.x - originX;
        const float currentDistance = canvasPoint.x - originX;
        if (std::abs(startDistance) > 0.0001f) {
            scaleX = currentDistance / startDistance;
        }
    }

    if (editor.layerScaleAxisY != 0) {
        originY = centered ? center.y : (editor.layerScaleAxisY < 0 ? y1 : y0);
        const float startDistance = startMouse.y - originY;
        const float currentDistance = canvasPoint.y - originY;
        if (std::abs(startDistance) > 0.0001f) {
            scaleY = currentDistance / startDistance;
        }
    }

    if (uniformScale) {
        const float startDx = startMouse.x - originX;
        const float startDy = startMouse.y - originY;
        const float currentDx = canvasPoint.x - originX;
        const float currentDy = canvasPoint.y - originY;
        const float startLenSq = startDx * startDx + startDy * startDy;

        if (startLenSq > 0.0001f) {
            const float projectedScale =
                (currentDx * startDx + currentDy * startDy) / startLenSq;
            scaleX = projectedScale;
            scaleY = projectedScale;
        }
    }

    if (std::abs(scaleX) < 0.01f) {
        scaleX = scaleX < 0.0f ? -0.01f : 0.01f;
    }

    if (std::abs(scaleY) < 0.01f) {
        scaleY = scaleY < 0.0f ? -0.01f : 0.01f;
    }

    for (std::size_t i = 0; i < editor.layerTransformLayerIndices.size(); ++i) {
        const int layerIndex = editor.layerTransformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.layerTransformStartStates.size()
        ) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        layer.mesh = editor.layerTransformStartStates[i].mesh;

        for (MeshVertex& vertex : layer.mesh.vertices) {
            vertex.position.x = originX + (vertex.position.x - originX) * scaleX;
            vertex.position.y = originY + (vertex.position.y - originY) * scaleY;
        }

        UpdateLayerBoundsFromMesh(layer);
    }
}

static void ApplyLayerRotateTransformToSelection(EditorState& editor, Vec2 canvasPoint) {
    const Vec2 center = editor.layerTransformCenter;
    const float startAngle = std::atan2(
        editor.layerTransformStartMouse.y - center.y,
        editor.layerTransformStartMouse.x - center.x
    );
    const float currentAngle = std::atan2(
        canvasPoint.y - center.y,
        canvasPoint.x - center.x
    );
    const float angle = currentAngle - startAngle;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    for (std::size_t i = 0; i < editor.layerTransformLayerIndices.size(); ++i) {
        const int layerIndex = editor.layerTransformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.layerTransformStartStates.size()
        ) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        layer.mesh = editor.layerTransformStartStates[i].mesh;

        for (MeshVertex& vertex : layer.mesh.vertices) {
            const Vec2 delta = vertex.position - center;
            vertex.position.x = center.x + delta.x * c - delta.y * s;
            vertex.position.y = center.y + delta.x * s + delta.y * c;
        }

        UpdateLayerBoundsFromMesh(layer);
    }
}

static void BeginDeformVertexTransform(
    EditorState& editor,
    EditorLayer& layer,
    Vec2 canvasPoint,
    LayerTransformMode mode,
    int axisX,
    int axisY
) {
    editor.deformingVertices = true;
    editor.deformVerticesMoved = false;
    editor.deformTransformMode = mode;
    editor.deformStartLayerState = CaptureLayerHistoryState(layer);
    editor.deformLayerIndices.clear();
    editor.deformStartLayerStates.clear();
    for (const int selectedLayerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
            continue;
        }

        editor.deformLayerIndices.push_back(selectedLayerIndex);
        editor.deformStartLayerStates.push_back(
            CaptureLayerHistoryState(editor.document.layers[selectedLayerIndex])
        );
    }
    editor.deformStartMesh = layer.mesh;
    editor.deformTransformStartMouse = canvasPoint;
    editor.lastDeformDragCanvasPos = canvasPoint;
    editor.deformScaleAxisX = axisX;
    editor.deformScaleAxisY = axisY;

    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    if (GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        editor.deformTransformCenter = Vec2{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    } else {
        editor.deformTransformCenter = canvasPoint;
    }
}

static void ApplyDeformVertexScaleTransform(
    EditorState& editor,
    EditorLayer& layer,
    Vec2 canvasPoint,
    bool centered
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    const Vec2 center = editor.deformTransformCenter;
    const Vec2 startMouse = editor.deformTransformStartMouse;
    const bool uniformScale =
        centered &&
        editor.deformScaleAxisX != 0 &&
        editor.deformScaleAxisY != 0;

    float originX = center.x;
    float originY = center.y;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (editor.deformScaleAxisX != 0) {
        originX = centered ? center.x : (editor.deformScaleAxisX < 0 ? x1 : x0);
        const float startDistance = startMouse.x - originX;
        const float currentDistance = canvasPoint.x - originX;
        if (std::abs(startDistance) > 0.0001f) {
            scaleX = currentDistance / startDistance;
        }
    }

    if (editor.deformScaleAxisY != 0) {
        originY = centered ? center.y : (editor.deformScaleAxisY < 0 ? y1 : y0);
        const float startDistance = startMouse.y - originY;
        const float currentDistance = canvasPoint.y - originY;
        if (std::abs(startDistance) > 0.0001f) {
            scaleY = currentDistance / startDistance;
        }
    }

    if (uniformScale) {
        const float startDx = startMouse.x - originX;
        const float startDy = startMouse.y - originY;
        const float currentDx = canvasPoint.x - originX;
        const float currentDy = canvasPoint.y - originY;
        const float startLenSq = startDx * startDx + startDy * startDy;

        if (startLenSq > 0.0001f) {
            const float projectedScale =
                (currentDx * startDx + currentDy * startDy) / startLenSq;
            scaleX = projectedScale;
            scaleY = projectedScale;
        }
    }

    if (std::abs(scaleX) < 0.01f) {
        scaleX = scaleX < 0.0f ? -0.01f : 0.01f;
    }

    if (std::abs(scaleY) < 0.01f) {
        scaleY = scaleY < 0.0f ? -0.01f : 0.01f;
    }

    for (std::size_t i = 0; i < editor.deformLayerIndices.size(); ++i) {
        const int layerIndex = editor.deformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.deformStartLayerStates.size()
        ) {
            continue;
        }
        editor.document.layers[layerIndex].mesh = editor.deformStartLayerStates[i].mesh;
    }

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        EditorLayer& targetLayer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= targetLayer.mesh.vertices.size()) {
            continue;
        }

        MeshVertex& vertex = targetLayer.mesh.vertices[ref.vertexIndex];
        vertex.position.x = originX + (vertex.position.x - originX) * scaleX;
        vertex.position.y = originY + (vertex.position.y - originY) * scaleY;
    }

    for (const int layerIndex : editor.deformLayerIndices) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
        }
    }
}

static void ApplyDeformVertexRotateTransform(
    EditorState& editor,
    EditorLayer& layer,
    Vec2 canvasPoint
) {
    const Vec2 center = editor.deformTransformCenter;
    const float startAngle = std::atan2(
        editor.deformTransformStartMouse.y - center.y,
        editor.deformTransformStartMouse.x - center.x
    );
    const float currentAngle = std::atan2(
        canvasPoint.y - center.y,
        canvasPoint.x - center.x
    );
    const float angle = currentAngle - startAngle;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    for (std::size_t i = 0; i < editor.deformLayerIndices.size(); ++i) {
        const int layerIndex = editor.deformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.deformStartLayerStates.size()
        ) {
            continue;
        }
        editor.document.layers[layerIndex].mesh = editor.deformStartLayerStates[i].mesh;
    }

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        EditorLayer& targetLayer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= targetLayer.mesh.vertices.size()) {
            continue;
        }

        MeshVertex& vertex = targetLayer.mesh.vertices[ref.vertexIndex];
        const Vec2 delta = vertex.position - center;
        vertex.position.x = center.x + delta.x * c - delta.y * s;
        vertex.position.y = center.y + delta.x * s + delta.y * c;
    }

    for (const int layerIndex : editor.deformLayerIndices) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
        }
    }
}

static Vec2 ScreenToCanvasPoint(
    ImVec2 mouseScreen,
    ImVec2 imagePos,
    float zoom
) {
    if (zoom <= 0.0001f) {
        return {};
    }

    return Vec2{
        (mouseScreen.x - imagePos.x) / zoom,
        (mouseScreen.y - imagePos.y) / zoom
    };
}

static float LayerEffectiveRenderOrder(const EditorLayer& layer, int layerIndex) {
    if (!layer.renderOrderOverride.empty()) {
        char* end = nullptr;
        const float value = std::strtof(layer.renderOrderOverride.c_str(), &end);
        if (end && end != layer.renderOrderOverride.c_str()) {
            return value;
        }
    }

    return static_cast<float>(layerIndex);
}

struct MaskRenderGlState {
    GLint currentProgram = 0;
    GLint activeTexture = 0;
    GLint textureBinding = 0;
    GLint arrayBuffer = 0;
    GLint elementArrayBuffer = 0;
    GLint vertexArray = 0;
    GLint blendSrcRgb = 0;
    GLint blendDstRgb = 0;
    GLint blendSrcAlpha = 0;
    GLint blendDstAlpha = 0;
    GLint blendEquationRgb = 0;
    GLint blendEquationAlpha = 0;
    GLboolean blend = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint stencilFunc = GL_ALWAYS;
    GLint stencilRef = 0;
    GLint stencilValueMask = 0xFF;
    GLint stencilWriteMask = 0xFF;
    GLint stencilFail = GL_KEEP;
    GLint stencilPassDepthFail = GL_KEEP;
    GLint stencilPassDepthPass = GL_KEEP;
};

struct MaskRenderResources {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLint displaySizeLocation = -1;
};

static EditorState* gMaskRenderEditor = nullptr;
static ImVec2 gMaskRenderImagePos;
static float gMaskRenderZoom = 1.0f;
static MaskRenderGlState gMaskRenderSavedState;

static GLuint CompileMaskShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static MaskRenderResources& GetMaskRenderResources() {
    static MaskRenderResources resources;
    if (resources.program) {
        return resources;
    }

    constexpr const char* vertexSource = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        uniform vec2 uDisplaySize;
        out vec2 vUv;
        void main() {
            vec2 ndc = vec2(
                (aPos.x / uDisplaySize.x) * 2.0 - 1.0,
                1.0 - (aPos.y / uDisplaySize.y) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
        }
    )GLSL";

    constexpr const char* fragmentSource = R"GLSL(
        #version 330 core
        uniform sampler2D uTexture;
        in vec2 vUv;
        out vec4 FragColor;
        void main() {
            float alpha = texture(uTexture, vUv).a;
            if (alpha <= 0.0039) {
                discard;
            }
            FragColor = vec4(1.0);
        }
    )GLSL";

    const GLuint vertexShader = CompileMaskShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = CompileMaskShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertexShader || !fragmentShader) {
        if (vertexShader) {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader) {
            glDeleteShader(fragmentShader);
        }
        return resources;
    }

    resources.program = glCreateProgram();
    glAttachShader(resources.program, vertexShader);
    glAttachShader(resources.program, fragmentShader);
    glLinkProgram(resources.program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = 0;
    glGetProgramiv(resources.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(resources.program);
        resources.program = 0;
        return resources;
    }

    resources.displaySizeLocation = glGetUniformLocation(resources.program, "uDisplaySize");
    glUseProgram(resources.program);
    glUniform1i(glGetUniformLocation(resources.program, "uTexture"), 0);

    glGenVertexArrays(1, &resources.vao);
    glGenBuffers(1, &resources.vbo);
    glGenBuffers(1, &resources.ebo);

    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glBindVertexArray(0);

    return resources;
}

static void SaveMaskRenderGlState(MaskRenderGlState& state) {
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.currentProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textureBinding);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state.elementArrayBuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertexArray);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
    state.blend = glIsEnabled(GL_BLEND);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask);
    glGetIntegerv(GL_STENCIL_FUNC, &state.stencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &state.stencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &state.stencilValueMask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &state.stencilWriteMask);
    glGetIntegerv(GL_STENCIL_FAIL, &state.stencilFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state.stencilPassDepthFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state.stencilPassDepthPass);
    glActiveTexture(state.activeTexture);
}

static void RestoreMaskRenderGlState(const MaskRenderGlState& state) {
    glUseProgram(static_cast<GLuint>(state.currentProgram));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textureBinding));
    glBindVertexArray(static_cast<GLuint>(state.vertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.arrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(state.elementArrayBuffer));
    glBlendEquationSeparate(state.blendEquationRgb, state.blendEquationAlpha);
    glBlendFuncSeparate(state.blendSrcRgb, state.blendDstRgb, state.blendSrcAlpha, state.blendDstAlpha);
    state.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    state.cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    state.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    state.scissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    state.stencilTest ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
    glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
    glStencilFunc(state.stencilFunc, state.stencilRef, state.stencilValueMask);
    glStencilMask(state.stencilWriteMask);
    glStencilOp(state.stencilFail, state.stencilPassDepthFail, state.stencilPassDepthPass);
    glActiveTexture(state.activeTexture);
}

static void DrawMaskLayerToStencil(const EditorLayer& layer) {
    if (
        !layer.visible ||
        !layer.texture ||
        layer.mesh.vertices.empty() ||
        layer.mesh.indices.empty()
    ) {
        return;
    }

    MaskRenderResources& resources = GetMaskRenderResources();
    if (!resources.program) {
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(layer.mesh.vertices.size() * 4u);
    for (const MeshVertex& vertex : layer.mesh.vertices) {
        const ImVec2 screen = CanvasToScreenPoint(vertex.position, gMaskRenderImagePos, gMaskRenderZoom);
        vertices.push_back(screen.x);
        vertices.push_back(screen.y);
        vertices.push_back(vertex.uv.x);
        vertices.push_back(vertex.uv.y);
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(layer.mesh.indices.size());
    for (std::size_t i = 0; i + 2u < layer.mesh.indices.size(); i += 3u) {
        const std::uint32_t ia = layer.mesh.indices[i + 0u];
        const std::uint32_t ib = layer.mesh.indices[i + 1u];
        const std::uint32_t ic = layer.mesh.indices[i + 2u];
        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size() ||
            !MeshTriangleEdgesValid(layer.mesh, ia, ib, ic)
        ) {
            continue;
        }

        indices.push_back(ia);
        indices.push_back(ib);
        indices.push_back(ic);
    }

    if (indices.empty()) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;

    glUseProgram(resources.program);
    glUniform2f(resources.displaySizeLocation, displaySize.x, displaySize.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layer.texture);
    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
}

static void BeginMaskedLayerStencilCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    if (!gMaskRenderEditor) {
        return;
    }

    const int layerIndex = static_cast<int>(reinterpret_cast<intptr_t>(cmd->UserCallbackData));
    if (!IsValidLayerIndex(*gMaskRenderEditor, layerIndex)) {
        return;
    }

    const EditorLayer& layer = gMaskRenderEditor->document.layers[layerIndex];
    SaveMaskRenderGlState(gMaskRenderSavedState);

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    for (const int maskLayerIndex : layer.maskLayerIndices) {
        if (
            !IsValidLayerIndex(*gMaskRenderEditor, maskLayerIndex) ||
            maskLayerIndex == layerIndex
        ) {
            continue;
        }

        DrawMaskLayerToStencil(gMaskRenderEditor->document.layers[maskLayerIndex]);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    RestoreMaskRenderGlState(gMaskRenderSavedState);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

static void EndMaskedLayerStencilCallback(const ImDrawList*, const ImDrawCmd*) {
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
}

static void DrawLayerTexture(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    ImVec2 imageSize,
    ImU32 tint
) {
    if (!layer.texture) {
        return;
    }

    drawList->AddImage(
        static_cast<ImTextureID>(static_cast<intptr_t>(layer.texture)),
        imagePos,
        ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        tint
    );
}

static void DrawTexturedLayerMesh(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    float zoom,
    ImU32 tint
) {
    if (
        !layer.texture ||
        layer.mesh.vertices.empty() ||
        layer.mesh.indices.empty()
    ) {
        return;
    }

    const ImTextureID textureId = static_cast<ImTextureID>(
        static_cast<intptr_t>(layer.texture)
    );

    constexpr int kMaxBatchVertices = 60000;
    constexpr int kMaxBatchIndices = kMaxBatchVertices;

    std::vector<std::uint32_t> batch;
    batch.reserve(kMaxBatchIndices);

    auto flushBatch = [&]() {
        if (batch.empty()) {
            return;
        }

        drawList->PrimReserve(
            static_cast<int>(batch.size()),
            static_cast<int>(batch.size())
        );

        const ImDrawIdx vertexOffset = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
        for (int i = 0; i < static_cast<int>(batch.size()); ++i) {
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(vertexOffset + i));
        }

        for (const std::uint32_t index : batch) {
            const MeshVertex& vertex = layer.mesh.vertices[index];
            drawList->PrimWriteVtx(
                CanvasToScreenPoint(vertex.position, imagePos, zoom),
                ImVec2(vertex.uv.x, vertex.uv.y),
                tint
            );
        }

        batch.clear();
    };

    drawList->PushTexture(ImTextureRef(textureId));

    for (std::size_t i = 0; i + 2 < layer.mesh.indices.size(); i += 3) {
        const std::uint32_t ia = layer.mesh.indices[i + 0u];
        const std::uint32_t ib = layer.mesh.indices[i + 1u];
        const std::uint32_t ic = layer.mesh.indices[i + 2u];

        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size() ||
            !MeshTriangleEdgesValid(layer.mesh, ia, ib, ic)
        ) {
            continue;
        }

        if (batch.size() + 3u > static_cast<std::size_t>(kMaxBatchIndices)) {
            flushBatch();
        }

        batch.push_back(ia);
        batch.push_back(ib);
        batch.push_back(ic);
    }

    flushBatch();
    drawList->PopTexture();
}

static void DrawLayerWithGpuMasks(
    ImDrawList* drawList,
    const EditorLayer& layer,
    int layerIndex,
    ImVec2 imagePos,
    ImVec2 imageSize,
    float zoom,
    bool deformMode
) {
    const bool hasMasks = !layer.maskLayerIndices.empty();
    const int opacityByte = static_cast<int>(std::round(std::clamp(layer.opacity, 0.0f, 1.0f) * 255.0f));
    const ImU32 tint = IM_COL32(opacityByte, opacityByte, opacityByte, opacityByte);
    if (hasMasks) {
        drawList->AddCallback(
            BeginMaskedLayerStencilCallback,
            reinterpret_cast<void*>(static_cast<intptr_t>(layerIndex))
        );
    }

    if (deformMode) {
        DrawTexturedLayerMesh(drawList, layer, imagePos, zoom, tint);
    } else {
        DrawLayerTexture(drawList, layer, imagePos, imageSize, tint);
    }

    if (hasMasks) {
        drawList->AddCallback(EndMaskedLayerStencilCallback, nullptr);
    }
}

static void DrawLayerMeshWireframe(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    float zoom,
    ImU32 color,
    float thickness
) {
    if (!layer.mesh.edges.empty()) {
        for (const MeshEdge& edge : layer.mesh.edges) {
            if (
                edge.a >= layer.mesh.vertices.size() ||
                edge.b >= layer.mesh.vertices.size()
            ) {
                continue;
            }

            const MeshVertex& a = layer.mesh.vertices[edge.a];
            const MeshVertex& b = layer.mesh.vertices[edge.b];
            const ImVec2 pa(imagePos.x + a.position.x * zoom, imagePos.y + a.position.y * zoom);
            const ImVec2 pb(imagePos.x + b.position.x * zoom, imagePos.y + b.position.y * zoom);
            drawList->AddLine(pa, pb, color, thickness);
        }
        return;
    }

    for (std::size_t i = 0; i + 2 < layer.mesh.indices.size(); i += 3) {
        const std::uint32_t ia = layer.mesh.indices[i + 0];
        const std::uint32_t ib = layer.mesh.indices[i + 1];
        const std::uint32_t ic = layer.mesh.indices[i + 2];

        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size()
        ) {
            continue;
        }

        const MeshVertex& a = layer.mesh.vertices[ia];
        const MeshVertex& b = layer.mesh.vertices[ib];
        const MeshVertex& c = layer.mesh.vertices[ic];

        const ImVec2 pa(imagePos.x + a.position.x * zoom, imagePos.y + a.position.y * zoom);
        const ImVec2 pb(imagePos.x + b.position.x * zoom, imagePos.y + b.position.y * zoom);
        const ImVec2 pc(imagePos.x + c.position.x * zoom, imagePos.y + c.position.y * zoom);

        drawList->AddLine(pa, pb, color, thickness);
        drawList->AddLine(pb, pc, color, thickness);
        drawList->AddLine(pc, pa, color, thickness);
    }
}

static void DrawLayerTransformBox(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(layer.mesh, x0, y0, x1, y1)) {
        return;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    const ImVec2 p0 = CanvasToScreenPoint(Vec2{x0, y0}, imagePos, zoom);
    const ImVec2 p1 = CanvasToScreenPoint(Vec2{x1, y1}, imagePos, zoom);
    const ImU32 boxColor = IM_COL32(255, 220, 90, 230);
    const ImU32 handleFill = IM_COL32(25, 26, 30, 230);
    const ImU32 moveFill = IM_COL32(8, 9, 12, 255);
    const float handle = 5.0f;

    drawList->AddRect(p0, p1, boxColor, 0.0f, 0, 1.5f);

    const ImVec2 edgeHandles[] = {
        ImVec2((p0.x + p1.x) * 0.5f, p0.y),
        ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
        ImVec2((p0.x + p1.x) * 0.5f, p1.y),
        ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
    };

    for (const ImVec2 h : edgeHandles) {
        drawList->AddRectFilled(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            handleFill
        );
        drawList->AddRect(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            boxColor
        );
    }

    const ImVec2 corners[] = {
        p0,
        ImVec2(p1.x, p0.y),
        p1,
        ImVec2(p0.x, p1.y),
    };

    for (const ImVec2 corner : corners) {
        drawList->AddCircle(corner, 12.0f, IM_COL32(255, 220, 90, 130), 16, 1.0f);
    }

    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawList->AddCircleFilled(center, 8.0f, moveFill, 16);
    drawList->AddCircle(center, 8.0f, boxColor, 16, 1.5f);
}

static void DrawSelectedLayersTransformBox(
    ImDrawList* drawList,
    const EditorState& editor,
    ImVec2 imagePos,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetSelectedLayerBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    const ImVec2 p0 = CanvasToScreenPoint(Vec2{x0, y0}, imagePos, zoom);
    const ImVec2 p1 = CanvasToScreenPoint(Vec2{x1, y1}, imagePos, zoom);
    const ImU32 boxColor = IM_COL32(255, 220, 90, 230);
    const ImU32 handleFill = IM_COL32(25, 26, 30, 230);
    const ImU32 moveFill = IM_COL32(8, 9, 12, 255);
    const float handle = 5.0f;

    drawList->AddRect(p0, p1, boxColor, 0.0f, 0, 1.5f);

    const ImVec2 edgeHandles[] = {
        ImVec2((p0.x + p1.x) * 0.5f, p0.y),
        ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
        ImVec2((p0.x + p1.x) * 0.5f, p1.y),
        ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
    };

    for (const ImVec2 h : edgeHandles) {
        drawList->AddRectFilled(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            handleFill
        );
        drawList->AddRect(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            boxColor
        );
    }

    const ImVec2 corners[] = {
        p0,
        ImVec2(p1.x, p0.y),
        p1,
        ImVec2(p0.x, p1.y),
    };

    for (const ImVec2 corner : corners) {
        drawList->AddCircle(corner, 12.0f, IM_COL32(255, 220, 90, 130), 16, 1.0f);
    }

    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawList->AddCircleFilled(center, 8.0f, moveFill, 16);
    drawList->AddCircle(center, 8.0f, boxColor, 16, 1.5f);
}

static void DrawSelectedVertexTransformBox(
    ImDrawList* drawList,
    const EditorState& editor,
    ImVec2 imagePos,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    const ImVec2 p0 = CanvasToScreenPoint(Vec2{x0, y0}, imagePos, zoom);
    const ImVec2 p1 = CanvasToScreenPoint(Vec2{x1, y1}, imagePos, zoom);
    const ImU32 boxColor = IM_COL32(90, 210, 255, 240);
    const ImU32 handleFill = IM_COL32(25, 26, 30, 230);
    const ImU32 moveFill = IM_COL32(8, 9, 12, 255);
    const float handle = 5.0f;

    drawList->AddRect(p0, p1, boxColor, 0.0f, 0, 1.5f);

    const ImVec2 edgeHandles[] = {
        ImVec2((p0.x + p1.x) * 0.5f, p0.y),
        ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
        ImVec2((p0.x + p1.x) * 0.5f, p1.y),
        ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
    };

    for (const ImVec2 h : edgeHandles) {
        drawList->AddRectFilled(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            handleFill
        );
        drawList->AddRect(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            boxColor
        );
    }

    const ImVec2 corners[] = {
        p0,
        ImVec2(p1.x, p0.y),
        p1,
        ImVec2(p0.x, p1.y),
    };

    for (const ImVec2 corner : corners) {
        drawList->AddCircle(corner, 12.0f, IM_COL32(90, 210, 255, 140), 16, 1.0f);
    }

    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawList->AddCircleFilled(center, 8.0f, moveFill, 16);
    drawList->AddCircle(center, 8.0f, boxColor, 16, 1.5f);
}

static void HandleMeshEditing(
    EditorState& editor,
    ImVec2 imagePos,
    bool hovered,
    float zoom
) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const int pickedVertex = PickVertexAtCanvasPoint(layer, canvasPoint, zoom);

        if (pickedVertex >= 0) {
            const std::uint32_t vertex = static_cast<std::uint32_t>(pickedVertex);

            if (io.KeyShift) {
                ToggleIndex(editor.selectedVertices, vertex);
            } else if (!ContainsIndex(editor.selectedVertices, vertex)) {
                ClearMeshSelection(editor);
                editor.selectedVertices.push_back(vertex);
            }

            editor.draggingMeshSelection = ContainsIndex(editor.selectedVertices, vertex);
            editor.meshSelectionMoved = false;
            editor.lastMeshDragCanvasPos = canvasPoint;
            editor.meshEditStartLayerState = CaptureLayerHistoryState(layer);
            return;
        }

        const int pickedEdge = PickEdgeAtCanvasPoint(layer, canvasPoint, zoom);
        if (pickedEdge >= 0) {
            const std::uint32_t edge = static_cast<std::uint32_t>(pickedEdge);

            if (io.KeyShift) {
                ToggleIndex(editor.selectedEdges, edge);
            } else {
                ClearMeshSelection(editor);
                editor.selectedEdges.push_back(edge);
            }
            return;
        }

        if (!CanvasPointInMeshBounds(layer.mesh, canvasPoint)) {
            const int pickedLayer = PickTopmostLayerAtCanvasPoint(
                editor,
                canvasPoint.x,
                canvasPoint.y
            );

            if (
                IsValidLayerIndex(editor, pickedLayer) &&
                pickedLayer != editor.selectedLayer
            ) {
                SelectSingleLayer(editor, pickedLayer);
                return;
            }
        }

        editor.boxSelectingMesh = true;
        editor.boxSelectAdditive = io.KeyShift;
        editor.boxSelectionMoved = false;
        editor.boxSelectStartCanvasPos = canvasPoint;
        editor.boxSelectCurrentCanvasPos = canvasPoint;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const int pickedVertex = PickVertexAtCanvasPoint(layer, canvasPoint, zoom);

        if (pickedVertex >= 0) {
            const LayerHistoryState before = CaptureLayerHistoryState(layer);
            if (HasSelectedVertex(editor)) {
                AddMeshEdgeIfMissing(
                    layer.mesh,
                    ActiveSelectedVertex(editor),
                    static_cast<std::uint32_t>(pickedVertex)
                );
            }

            ClearMeshSelection(editor);
            editor.selectedVertices.push_back(static_cast<std::uint32_t>(pickedVertex));
            UpdateLayerBoundsFromMesh(layer);
            PushMeshEditOperation(editor, before, "Add mesh edge");
            return;
        }

        AddVertexAtCanvasPoint(editor, canvasPoint, io.KeyShift);
    }

    if (
        editor.draggingMeshSelection &&
        !editor.selectedVertices.empty() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)
    ) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const float dx = canvasPoint.x - editor.lastMeshDragCanvasPos.x;
        const float dy = canvasPoint.y - editor.lastMeshDragCanvasPos.y;

        if (dx != 0.0f || dy != 0.0f) {
            for (const std::uint32_t vertex : editor.selectedVertices) {
                if (vertex >= layer.mesh.vertices.size()) {
                    continue;
                }

                layer.mesh.vertices[vertex].position.x += dx;
                layer.mesh.vertices[vertex].position.y += dy;
                RefreshMeshVertexUv(layer, layer.mesh.vertices[vertex]);
            }

            editor.lastMeshDragCanvasPos = canvasPoint;
            editor.meshSelectionMoved = true;
            UpdateLayerBoundsFromMesh(layer);
            RebuildLayersAffectedByLayerGeometry(editor, editor.selectedLayer);
        }
    }

    if (editor.draggingMeshSelection && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.meshSelectionMoved) {
            PushMeshEditOperation(editor, editor.meshEditStartLayerState, "Move mesh vertices");
        }

        editor.draggingMeshSelection = false;
        editor.meshSelectionMoved = false;
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        editor.boxSelectCurrentCanvasPos = canvasPoint;

        const Vec2 delta = editor.boxSelectCurrentCanvasPos - editor.boxSelectStartCanvasPos;
        if (Dot(delta, delta) > 4.0f) {
            editor.boxSelectionMoved = true;
        }
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.boxSelectionMoved) {
            ApplyMeshBoxSelection(editor, layer);
        } else if (!editor.boxSelectAdditive) {
            ClearMeshSelection(editor);
        }

        editor.boxSelectingMesh = false;
        editor.boxSelectAdditive = false;
        editor.boxSelectionMoved = false;
    }
}

static bool HandleDeformVertexEditing(
    EditorState& editor,
    ImVec2 imagePos,
    bool hovered,
    float zoom
) {
    int activeLayerIndex = editor.selectedLayer;
    if (!IsValidLayerIndex(editor, activeLayerIndex)) {
        for (const int layerIndex : editor.selectedLayers) {
            if (IsValidLayerIndex(editor, layerIndex)) {
                activeLayerIndex = layerIndex;
                break;
            }
        }
    }

    if (!IsValidLayerIndex(editor, activeLayerIndex)) {
        return false;
    }

    editor.selectedLayer = activeLayerIndex;
    EditorLayer& layer = editor.document.layers[activeLayerIndex];
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const bool insideSelectedLayerBox = CanvasPointInSelectedLayerTransformBox(editor, canvasPoint, zoom);

        LayerTransformMode transformMode = LayerTransformMode::None;
        int scaleAxisX = 0;
        int scaleAxisY = 0;
        if (
            !editor.selectedDeformVertices.empty() &&
            PickDeformVertexTransformHandle(
                editor,
                canvasPoint,
                zoom,
                transformMode,
                scaleAxisX,
                scaleAxisY
            )
        ) {
            if (transformMode == LayerTransformMode::Move) {
                float sx0 = 0.0f;
                float sy0 = 0.0f;
                float sx1 = 0.0f;
                float sy1 = 0.0f;
                if (GetSelectedLayerBounds(editor, sx0, sy0, sx1, sy1)) {
                    PadBounds(sx0, sy0, sx1, sy1, zoom);
                    const Vec2 layerCenter{(sx0 + sx1) * 0.5f, (sy0 + sy1) * 0.5f};
                    const float centerRadius = std::max(18.0f / std::max(zoom, 0.0001f), 8.0f);
                    const Vec2 delta = canvasPoint - layerCenter;
                    if (Dot(delta, delta) <= centerRadius * centerRadius) {
                        return false;
                    }
                }
            }

            BeginDeformVertexTransform(
                editor,
                layer,
                canvasPoint,
                transformMode,
                scaleAxisX,
                scaleAxisY
            );
            return true;
        }

        if (
            PickSelectedLayersTransformHandle(
                editor,
                canvasPoint,
                zoom,
                transformMode,
                scaleAxisX,
                scaleAxisY
            )
        ) {
            return false;
        }

        SelectedVertexRef pickedVertex;
        if (PickVertexInSelectedLayers(editor, canvasPoint, zoom, pickedVertex)) {

            if (io.KeyShift) {
                ToggleVertexRef(editor.selectedDeformVertices, pickedVertex);
            } else if (!ContainsVertexRef(editor.selectedDeformVertices, pickedVertex)) {
                ClearDeformVertexSelection(editor);
                editor.selectedDeformVertices.push_back(pickedVertex);
            }

            editor.selectedEdges.clear();
            if (ContainsVertexRef(editor.selectedDeformVertices, pickedVertex)) {
                BeginDeformVertexTransform(
                    editor,
                    editor.document.layers[pickedVertex.layerIndex],
                    canvasPoint,
                    LayerTransformMode::Move,
                    0,
                    0
                );
            }
            return true;
        }

        if (
            (
                insideSelectedLayerBox ||
                CanvasPointInMeshBounds(layer.mesh, canvasPoint)
            ) &&
            (
                insideSelectedLayerBox ||
                io.KeyShift ||
                !editor.selectedDeformVertices.empty() ||
                !LayerAlphaHitTest(layer, canvasPoint.x, canvasPoint.y)
            )
        ) {
            editor.boxSelectingMesh = true;
            editor.boxSelectAdditive = io.KeyShift;
            editor.boxSelectionMoved = false;
            editor.boxSelectStartCanvasPos = canvasPoint;
            editor.boxSelectCurrentCanvasPos = canvasPoint;
            return true;
        }
    }

    if (
        editor.deformingVertices &&
        !editor.selectedDeformVertices.empty() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)
    ) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const Vec2 totalDelta = canvasPoint - editor.deformTransformStartMouse;
        constexpr float kDeformDragThresholdPx = 4.0f;

        if (!editor.deformVerticesMoved) {
            if (Dot(totalDelta, totalDelta) < kDeformDragThresholdPx * kDeformDragThresholdPx) {
                return true;
            }
        }

        if (editor.deformTransformMode == LayerTransformMode::Move) {
            const float dx = canvasPoint.x - editor.lastDeformDragCanvasPos.x;
            const float dy = canvasPoint.y - editor.lastDeformDragCanvasPos.y;

            if (dx != 0.0f || dy != 0.0f) {
                for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
                    if (!IsValidLayerIndex(editor, ref.layerIndex)) {
                        continue;
                    }

                    EditorLayer& targetLayer = editor.document.layers[ref.layerIndex];
                    if (ref.vertexIndex >= targetLayer.mesh.vertices.size()) {
                        continue;
                    }

                    targetLayer.mesh.vertices[ref.vertexIndex].position.x += dx;
                    targetLayer.mesh.vertices[ref.vertexIndex].position.y += dy;
                }
                editor.lastDeformDragCanvasPos = canvasPoint;
                editor.deformVerticesMoved = true;
                for (const int layerIndex : editor.deformLayerIndices) {
                    if (IsValidLayerIndex(editor, layerIndex)) {
                        UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
                    }
                }
                RebuildLayersAffectedByLayerGeometry(editor, editor.deformLayerIndices);
            }
        } else if (editor.deformTransformMode == LayerTransformMode::Scale) {
            ApplyDeformVertexScaleTransform(editor, layer, canvasPoint, io.KeyShift);
            editor.lastDeformDragCanvasPos = canvasPoint;
            editor.deformVerticesMoved = true;
            RebuildLayersAffectedByLayerGeometry(editor, editor.deformLayerIndices);
        } else if (editor.deformTransformMode == LayerTransformMode::Rotate) {
            ApplyDeformVertexRotateTransform(editor, layer, canvasPoint);
            editor.lastDeformDragCanvasPos = canvasPoint;
            editor.deformVerticesMoved = true;
            RebuildLayersAffectedByLayerGeometry(editor, editor.deformLayerIndices);
        }

        return true;
    }

    if (editor.deformingVertices && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.deformVerticesMoved) {
            const std::vector<DeformParameter> parametersBefore = editor.parameters;
            UpdateActiveParameterEndpointForLayers(editor, editor.deformLayerIndices);
            const std::vector<DeformParameter> parametersAfter = editor.parameters;
            for (std::size_t i = 0; i < editor.deformLayerIndices.size(); ++i) {
                const int layerIndex = editor.deformLayerIndices[i];
                if (
                    !IsValidLayerIndex(editor, layerIndex) ||
                    i >= editor.deformStartLayerStates.size()
                ) {
                    continue;
                }

                PushLayerOperationWithParameters(
                    editor,
                    layerIndex,
                    editor.deformStartLayerStates[i],
                    CaptureLayerHistoryState(editor.document.layers[layerIndex]),
                    parametersBefore,
                    parametersAfter,
                    "Deform vertices"
                );
            }
        }

        editor.deformingVertices = false;
        editor.deformVerticesMoved = false;
        editor.deformTransformMode = LayerTransformMode::None;
        editor.deformStartMesh = {};
        editor.deformLayerIndices.clear();
        editor.deformStartLayerStates.clear();
        return true;
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        editor.boxSelectCurrentCanvasPos = canvasPoint;

        const Vec2 delta = editor.boxSelectCurrentCanvasPos - editor.boxSelectStartCanvasPos;
        if (Dot(delta, delta) > 4.0f) {
            editor.boxSelectionMoved = true;
        }
        return true;
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.boxSelectionMoved) {
            ApplyVertexBoxSelection(editor);
        } else if (!editor.boxSelectAdditive) {
            ClearDeformVertexSelection(editor);
        }

        editor.boxSelectingMesh = false;
        editor.boxSelectAdditive = false;
        editor.boxSelectionMoved = false;
        return true;
    }

    return false;
}

static void DrawViewport(EditorState& editor) {
    ImGui::Begin("Viewport");

    DrawViewportControlPanel(editor);
    ImGui::Separator();

    ImGui::Checkbox("Checkerboard", &editor.showCheckerboard);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Zoom", &editor.zoom, 0.05f, 8.0f, "%.2fx");

    ImGui::SameLine();

    if (ImGui::Button("Fit View")) {
        editor.requestFitView = true;
    }

    ImGui::Separator();

    const ImVec2 canvasAvail = ImGui::GetContentRegionAvail();

    if (editor.requestFitView) {
        FitDocumentToViewport(editor, canvasAvail);
        editor.requestFitView = false;
    }

    ImGui::InvisibleButton(
        "viewport_canvas",
        canvasAvail,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle |
        ImGuiButtonFlags_MouseButtonRight
    );

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const float oldZoom = editor.zoom;
            editor.zoom = std::clamp(editor.zoom * (wheel > 0.0f ? 1.1f : 0.9f), 0.05f, 8.0f);

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 before(
                (mouse.x - itemMin.x - editor.pan.x) / oldZoom,
                (mouse.y - itemMin.y - editor.pan.y) / oldZoom
            );

            editor.pan.x = mouse.x - itemMin.x - before.x * editor.zoom;
            editor.pan.y = mouse.y - itemMin.y - before.y * editor.zoom;
        }
    }

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        editor.pan.x += delta.x;
        editor.pan.y += delta.y;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();

    drawList->PushClipRect(itemMin, itemMax, true);
    drawList->AddRectFilled(itemMin, itemMax, IM_COL32(25, 26, 30, 255));

    if (!editor.document.layers.empty()) {
        const ImVec2 imagePos(
            itemMin.x + editor.pan.x,
            itemMin.y + editor.pan.y
        );

        const ImVec2 imageSize(
            static_cast<float>(editor.document.canvasWidth) * editor.zoom,
            static_cast<float>(editor.document.canvasHeight) * editor.zoom
        );

        const ImGuiIO& io = ImGui::GetIO();
        bool deformVertexInputConsumed = false;
        gMaskRenderEditor = &editor;
        gMaskRenderImagePos = imagePos;
        gMaskRenderZoom = editor.zoom;

        if (editor.mode == EditorMode::Mesh) {
            HandleMeshEditing(editor, imagePos, hovered, editor.zoom);
        } else if (editor.mode == EditorMode::Layer) {
            deformVertexInputConsumed = HandleDeformVertexEditing(
                editor,
                imagePos,
                hovered,
                editor.zoom
            );
        }

        if (
            editor.mode == EditorMode::Layer &&
            !deformVertexInputConsumed &&
            hovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        ) {
            const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, editor.zoom);

            LayerTransformMode transformMode = LayerTransformMode::None;
            int scaleAxisX = 0;
            int scaleAxisY = 0;
            if (
                IsValidLayerIndex(editor, editor.selectedLayer) &&
                PickSelectedLayersTransformHandle(
                    editor,
                    canvasPoint,
                    editor.zoom,
                    transformMode,
                    scaleAxisX,
                    scaleAxisY
                )
            ) {
                BeginLayerTransform(
                    editor,
                    editor.selectedLayer,
                    canvasPoint,
                    transformMode,
                    scaleAxisX,
                    scaleAxisY
                );
            } else {
                const int pickedLayer = PickLayerForLayerModeDrag(
                    editor,
                    canvasPoint.x,
                    canvasPoint.y
                );

                if (IsValidLayerIndex(editor, pickedLayer) && IsLayerSelected(editor, pickedLayer)) {
                    editor.selectedLayer = pickedLayer;
                } else {
                    if (
                        IsValidLayerIndex(editor, pickedLayer) &&
                        (io.KeyCtrl || io.KeyShift)
                    ) {
                        HandleLayerSelectionClick(editor, pickedLayer);
                    } else {
                        SelectSingleLayer(editor, pickedLayer);
                    }
                }

                if (IsValidLayerIndex(editor, pickedLayer)) {
                    BeginLayerTransform(
                        editor,
                        pickedLayer,
                        canvasPoint,
                        LayerTransformMode::Move,
                        0,
                        0
                    );
                } else {
                    editor.draggingLayer = false;
                    editor.draggedLayer = -1;
                    editor.layerTransformMode = LayerTransformMode::None;
                }
            }
        }

        if (
            editor.mode == EditorMode::Layer &&
            editor.draggingLayer &&
            editor.draggedLayer >= 0 &&
            editor.draggedLayer < static_cast<int>(editor.document.layers.size()) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)
        ) {
            const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, editor.zoom);

            const float dx = canvasPoint.x - editor.lastDragCanvasPos.x;
            const float dy = canvasPoint.y - editor.lastDragCanvasPos.y;
            const Vec2 totalDelta = canvasPoint - editor.layerDragStartCanvasPos;
            constexpr float kLayerDragThresholdPx = 4.0f;

            if (!editor.layerDragThresholdPassed) {
                if (Dot(totalDelta, totalDelta) >= kLayerDragThresholdPx * kLayerDragThresholdPx) {
                    editor.layerDragThresholdPassed = true;
                    editor.lastDragCanvasPos = canvasPoint;
                }
            } else if (editor.layerTransformMode == LayerTransformMode::Move && (dx != 0.0f || dy != 0.0f)) {
                for (const int layerIndex : editor.layerTransformLayerIndices) {
                    if (IsValidLayerIndex(editor, layerIndex)) {
                        TranslateLayerMesh(editor.document.layers[layerIndex], dx, dy);
                    }
                }
                editor.lastDragCanvasPos = canvasPoint;
                editor.dragLayerMoved = true;
                RebuildLayersAffectedByLayerGeometry(editor, editor.layerTransformLayerIndices);
            } else if (editor.layerTransformMode == LayerTransformMode::Scale) {
                ApplyLayerScaleTransformToSelection(
                    editor,
                    canvasPoint,
                    io.KeyShift
                );
                editor.lastDragCanvasPos = canvasPoint;
                editor.dragLayerMoved = true;
                RebuildLayersAffectedByLayerGeometry(editor, editor.layerTransformLayerIndices);
            } else if (editor.layerTransformMode == LayerTransformMode::Rotate) {
                ApplyLayerRotateTransformToSelection(
                    editor,
                    canvasPoint
                );
                editor.lastDragCanvasPos = canvasPoint;
                editor.dragLayerMoved = true;
                RebuildLayersAffectedByLayerGeometry(editor, editor.layerTransformLayerIndices);
            }
        }

        if (
            editor.mode == EditorMode::Layer &&
            editor.draggingLayer &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        ) {
            if (editor.dragLayerMoved && IsValidLayerIndex(editor, editor.draggedLayer)) {
                const std::vector<DeformParameter> parametersBefore = editor.parameters;
                UpdateActiveParameterEndpointForLayers(editor, editor.layerTransformLayerIndices);
                const std::vector<DeformParameter> parametersAfter = editor.parameters;
                for (std::size_t i = 0; i < editor.layerTransformLayerIndices.size(); ++i) {
                    const int layerIndex = editor.layerTransformLayerIndices[i];
                    if (
                        !IsValidLayerIndex(editor, layerIndex) ||
                        i >= editor.layerTransformStartStates.size()
                    ) {
                        continue;
                    }

                    PushLayerOperationWithParameters(
                        editor,
                        layerIndex,
                        editor.layerTransformStartStates[i],
                        CaptureLayerHistoryState(editor.document.layers[layerIndex]),
                        parametersBefore,
                        parametersAfter,
                        "Transform layer"
                    );
                }
            }

            editor.draggingLayer = false;
            editor.draggedLayer = -1;
            editor.layerTransformMode = LayerTransformMode::None;
            editor.dragLayerMoved = false;
            editor.layerDragThresholdPassed = false;
            editor.layerTransformStartMesh = {};
            editor.layerTransformLayerIndices.clear();
            editor.layerTransformStartStates.clear();
        }

        if (editor.showCheckerboard) {
            DrawCheckerboard(drawList, imagePos, imageSize, 16.0f * editor.zoom);
        }

        drawList->AddRect(
            imagePos,
            ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
            IM_COL32(100, 120, 150, 255)
        );

        drawList->AddCallback(SetPremultipliedAlphaBlendCallback, nullptr);

        std::vector<int> renderOrder;
        renderOrder.reserve(editor.document.layers.size());
        for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
            renderOrder.push_back(i);
        }

        std::stable_sort(renderOrder.begin(), renderOrder.end(), [&](int a, int b) {
            return
                LayerEffectiveRenderOrder(editor.document.layers[a], a) >
                LayerEffectiveRenderOrder(editor.document.layers[b], b);
        });

        for (const int i : renderOrder) {
            const EditorLayer& layer = editor.document.layers[i];

            if (!layer.visible || !layer.texture) {
                continue;
            }

            DrawLayerWithGpuMasks(
                drawList,
                layer,
                i,
                imagePos,
                imageSize,
                editor.zoom,
                editor.mode == EditorMode::Layer
            );
        }

        drawList->AddCallback(RestoreStraightAlphaBlendCallback, nullptr);

        if (editor.mode == EditorMode::Layer) {
            for (const int selectedLayerIndex : editor.selectedLayers) {
                if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
                    continue;
                }

                const EditorLayer& layer = editor.document.layers[selectedLayerIndex];

                DrawLayerMeshWireframe(
                    drawList,
                    layer,
                    imagePos,
                    editor.zoom,
                    selectedLayerIndex == editor.selectedLayer
                        ? IM_COL32(255, 220, 90, 150)
                        : IM_COL32(70, 230, 130, 140),
                    selectedLayerIndex == editor.selectedLayer ? 1.5f : 1.0f
                );
            }

            DrawSelectedLayersTransformBox(drawList, editor, imagePos, editor.zoom);

            for (const int selectedLayerIndex : editor.selectedLayers) {
                if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
                    continue;
                }

                const EditorLayer& layer = editor.document.layers[selectedLayerIndex];
                for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
                    const MeshVertex& v = layer.mesh.vertices[i];
                    const ImVec2 vp(
                        imagePos.x + v.position.x * editor.zoom,
                        imagePos.y + v.position.y * editor.zoom
                    );

                    const bool selectedVertex = ContainsVertexRef(
                        editor.selectedDeformVertices,
                        SelectedVertexRef{
                            selectedLayerIndex,
                            static_cast<std::uint32_t>(i)
                        }
                    );

                    drawList->AddCircleFilled(
                        vp,
                        selectedVertex ? 6.0f : 4.0f,
                        selectedVertex
                            ? IM_COL32(255, 230, 70, 255)
                            : IM_COL32(70, 230, 130, 255)
                    );
                }
            }

            if (!editor.selectedDeformVertices.empty()) {
                DrawSelectedVertexTransformBox(drawList, editor, imagePos, editor.zoom);
            }
        }

        if (IsValidLayerIndex(editor, editor.selectedLayer)) {
            const EditorLayer& layer = editor.document.layers[editor.selectedLayer];

            float sx0 = 0.0f;
            float sy0 = 0.0f;
            float sx1 = 0.0f;
            float sy1 = 0.0f;

            if (editor.mode == EditorMode::Mesh && GetMeshBounds(layer.mesh, sx0, sy0, sx1, sy1)) {
                const ImVec2 p0(
                    imagePos.x + sx0 * editor.zoom,
                    imagePos.y + sy0 * editor.zoom
                );

                const ImVec2 p1(
                    imagePos.x + sx1 * editor.zoom,
                    imagePos.y + sy1 * editor.zoom
                );

                drawList->AddRect(p0, p1, IM_COL32(255, 220, 90, 255), 0.0f, 0, 2.0f);
            }

            if (editor.mode == EditorMode::Mesh) {
                DrawLayerMeshWireframe(
                    drawList,
                    layer,
                    imagePos,
                    editor.zoom,
                    IM_COL32(70, 230, 130, 220),
                    2.0f
                );
            }

            if (editor.mode == EditorMode::Mesh) {
                for (const std::uint32_t edgeIndex : editor.selectedEdges) {
                    if (edgeIndex >= layer.mesh.edges.size()) {
                        continue;
                    }

                    const MeshEdge& edge = layer.mesh.edges[edgeIndex];
                    if (
                        edge.a >= layer.mesh.vertices.size() ||
                        edge.b >= layer.mesh.vertices.size()
                    ) {
                        continue;
                    }

                    const Vec2 a = layer.mesh.vertices[edge.a].position;
                    const Vec2 b = layer.mesh.vertices[edge.b].position;
                    drawList->AddLine(
                        CanvasToScreenPoint(a, imagePos, editor.zoom),
                        CanvasToScreenPoint(b, imagePos, editor.zoom),
                        IM_COL32(255, 230, 70, 255),
                        3.0f
                    );
                }
            }

            if (editor.mode == EditorMode::Mesh) {
            for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
                const MeshVertex& v = layer.mesh.vertices[i];
                const ImVec2 vp(
                    imagePos.x + v.position.x * editor.zoom,
                    imagePos.y + v.position.y * editor.zoom
                );

                const bool showVertexSelection =
                    editor.mode == EditorMode::Mesh ||
                    editor.mode == EditorMode::Layer;
                const bool selectedVertex = showVertexSelection &&
                    ContainsIndex(editor.selectedVertices, static_cast<std::uint32_t>(i));
                drawList->AddCircleFilled(
                    vp,
                    selectedVertex ? 6.0f : 4.0f,
                    showVertexSelection
                        ? (selectedVertex ? IM_COL32(255, 230, 70, 255) : IM_COL32(70, 230, 130, 255))
                        : IM_COL32(255, 220, 90, 255)
                );
            }
            }

            if (
                (editor.mode == EditorMode::Mesh || editor.mode == EditorMode::Layer) &&
                editor.boxSelectingMesh
            ) {
                const ImVec2 p0 = CanvasToScreenPoint(
                    editor.boxSelectStartCanvasPos,
                    imagePos,
                    editor.zoom
                );
                const ImVec2 p1 = CanvasToScreenPoint(
                    editor.boxSelectCurrentCanvasPos,
                    imagePos,
                    editor.zoom
                );
                const ImVec2 r0(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
                const ImVec2 r1(std::max(p0.x, p1.x), std::max(p0.y, p1.y));

                drawList->AddRectFilled(r0, r1, IM_COL32(90, 210, 255, 35));
                drawList->AddRect(r0, r1, IM_COL32(255, 230, 70, 230), 0.0f, 0, 1.5f);
            }
        }
    } else {
        drawList->AddText(
            ImVec2(itemMin.x + 20.0f, itemMin.y + 20.0f),
            IM_COL32(180, 185, 195, 255),
            "Load a PSD to view its separated layers."
        );
    }

    drawList->PopClipRect();

    ImGui::End();
}

static void CopySelectedDeformVertexPositions(EditorState& editor) {
    editor.meshPositionClipboard.clear();
    editor.meshPositionClipboard.reserve(editor.selectedDeformVertices.size());

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= layer.mesh.vertices.size()) {
            continue;
        }

        CopiedMeshVertexPosition copied;
        copied.ref = ref;
        copied.position = layer.mesh.vertices[ref.vertexIndex].position;
        editor.meshPositionClipboard.push_back(copied);
    }

    editor.statusText = editor.meshPositionClipboard.empty()
        ? "No deform vertices selected to copy."
        : "Copied " + std::to_string(editor.meshPositionClipboard.size()) + " vertex position" +
            (editor.meshPositionClipboard.size() == 1u ? "." : "s.");
}

static void PasteCopiedDeformVertexPositions(EditorState& editor) {
    if (editor.meshPositionClipboard.empty()) {
        editor.statusText = "No copied vertex positions to paste.";
        return;
    }

    std::vector<int> changedLayerIndices;
    std::vector<LayerHistoryState> beforeStates;

    for (const CopiedMeshVertexPosition& copied : editor.meshPositionClipboard) {
        if (!IsValidLayerIndex(editor, copied.ref.layerIndex)) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[copied.ref.layerIndex];
        if (copied.ref.vertexIndex >= layer.mesh.vertices.size()) {
            continue;
        }

        MeshVertex& vertex = layer.mesh.vertices[copied.ref.vertexIndex];
        if (
            vertex.position.x == copied.position.x &&
            vertex.position.y == copied.position.y
        ) {
            continue;
        }

        if (!ContainsLayerIndex(changedLayerIndices, copied.ref.layerIndex)) {
            changedLayerIndices.push_back(copied.ref.layerIndex);
            beforeStates.push_back(CaptureLayerHistoryState(layer));
        }

        vertex.position = copied.position;
    }

    if (changedLayerIndices.empty()) {
        editor.statusText = "Pasted vertex positions; nothing changed.";
        return;
    }

    for (const int layerIndex : changedLayerIndices) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
        }
    }
    RebuildLayersAffectedByLayerGeometry(editor, changedLayerIndices);

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    UpdateActiveParameterEndpointForLayers(editor, changedLayerIndices);
    const std::vector<DeformParameter> parametersAfter = editor.parameters;

    for (std::size_t i = 0; i < changedLayerIndices.size(); ++i) {
        const int layerIndex = changedLayerIndices[i];
        if (!IsValidLayerIndex(editor, layerIndex) || i >= beforeStates.size()) {
            continue;
        }

        PushLayerOperationWithParameters(
            editor,
            layerIndex,
            beforeStates[i],
            CaptureLayerHistoryState(editor.document.layers[layerIndex]),
            parametersBefore,
            parametersAfter,
            "Paste vertex positions"
        );
    }

    editor.statusText = "Pasted copied vertex positions.";
}

static void HandleEditorShortcuts(EditorState& editor) {
    const ImGuiIO& io = ImGui::GetIO();

    if (
        editor.draggingLayer ||
        editor.draggingMeshSelection ||
        editor.boxSelectingMesh ||
        io.WantTextInput
    ) {
        return;
    }

    if (
        editor.mode == EditorMode::Mesh &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    ) {
        SelectAllMeshItems(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    ) {
        SelectAllLayers(editor);
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        SaveMeshesFromUi(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false)
    ) {
        CopySelectedDeformVertexPositions(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_V, false)
    ) {
        PasteCopiedDeformVertexPositions(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Mesh &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        (!editor.selectedVertices.empty() || !editor.selectedEdges.empty())
    ) {
        DeleteSelectedMeshItems(editor);
        return;
    }

    if (!io.KeyCtrl) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        UndoLastOperation(editor);
        ClearMeshSelection(editor);
    } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        RedoLastOperation(editor);
        ClearMeshSelection(editor);
    }
}

void DrawEditor(EditorState& editor, GLFWwindow* window) {
    HandleEditorShortcuts(editor);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;

    ImGui::SetNextWindowPos(workPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workSize.x, 110.0f), ImGuiCond_Always);
    DrawTopBar(editor, window);

    ImGui::SetNextWindowPos(ImVec2(workPos.x, workPos.y + 110.0f), ImGuiCond_Always);
    const float sideWidth = 300.0f;
    const float sideTop = workPos.y + 110.0f;
    const float sideHeight = workSize.y - 110.0f;
    const float parameterHeight = std::clamp(sideHeight * 0.32f, 170.0f, 280.0f);
    const float layerHeight = std::max(180.0f, sideHeight - parameterHeight);

    ImGui::SetNextWindowSize(ImVec2(sideWidth, layerHeight), ImGuiCond_Always);
    DrawLayerPanel(editor);

    ImGui::SetNextWindowPos(ImVec2(workPos.x, sideTop + layerHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sideWidth, parameterHeight), ImGuiCond_Always);
    DrawDeformParametersPanel(editor);

    DrawMeshGeneratorSettingsPanel(editor);

    ImGui::SetNextWindowPos(ImVec2(workPos.x + sideWidth, sideTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workSize.x - sideWidth, sideHeight), ImGuiCond_Always);
    DrawViewport(editor);

    DrawGenerateMeshConfirmationPopup(editor);
}

void DropCallback(GLFWwindow* window, int count, const char** paths) {
    EditorState* editor = static_cast<EditorState*>(glfwGetWindowUserPointer(window));

    if (!editor || count <= 0 || !paths || !paths[0]) {
        return;
    }

    editor->pendingPath = paths[0];

    std::string error;
    if (!LoadPsdIntoEditor(editor->pendingPath, *editor, window, error)) {
        editor->errorText = error;
        editor->statusText = "Load failed.";
    }
}
