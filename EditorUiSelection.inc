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

static void ClearParameterSetpointSelection(EditorState& editor) {
    editor.selectedParameterSetpointParameter = -1;
    editor.selectedParameterSetpointValue = -1.0f;
    editor.draggingParameterSetpoint = false;
    editor.pendingParameterSetpointDrag = false;
    editor.parameterSetpointDragStartMouseX = 0.0f;
    editor.parameterSetpointDragStartValue = 0.0f;
    editor.parameterSetpointDragStartTime = 0.0;
    editor.parameterEditSnapActive = false;
    editor.parameterEditSnapTarget = 0.0f;
    editor.parameterSetpointDragBefore.clear();
    editor.parameterSetpointDragSelectedBefore = -1;
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
    ClearParameterSetpointSelection(editor);
    editor.selectedLayer = layerIndex;
    editor.selectedLayers.clear();
    if (layerIndex >= 0) {
        editor.selectedLayers.push_back(layerIndex);
    }
    ClearAllVertexSelection(editor);
}

static void SelectAllLayers(EditorState& editor) {
    ClearParameterSetpointSelection(editor);
    editor.selectedLayers.clear();
    editor.selectedLayers.reserve(editor.document.layers.size());
    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
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
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    ClearParameterSetpointSelection(editor);
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
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    ClearParameterSetpointSelection(editor);
    const int anchor = IsValidLayerIndex(editor, editor.selectedLayer)
        ? editor.selectedLayer
        : layerIndex;
    const int first = std::min(anchor, layerIndex);
    const int last = std::max(anchor, layerIndex);

    editor.selectedLayers.clear();
    for (int i = first; i <= last; ++i) {
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

static void SelectAllVisibleDeformVertices(EditorState& editor) {
    editor.selectedDeformVertices.clear();
    editor.selectedVertices.clear();
    editor.selectedEdges.clear();

    std::size_t vertexCount = 0u;
    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        vertexCount += layer.mesh.vertices.size();
    }

    editor.selectedDeformVertices.reserve(vertexCount);
    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        for (std::uint32_t vertexIndex = 0; vertexIndex < layer.mesh.vertices.size(); ++vertexIndex) {
            editor.selectedDeformVertices.push_back(SelectedVertexRef{layerIndex, vertexIndex});
        }
    }

    editor.statusText = editor.selectedDeformVertices.empty()
        ? "No visible selected-layer vertices to select."
        : "Selected " + std::to_string(editor.selectedDeformVertices.size()) + " visible vertex" +
            (editor.selectedDeformVertices.size() == 1u ? "." : "es.");
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

static bool ComputeBarycentricForUi(
    Vec2 p,
    Vec2 a,
    Vec2 b,
    Vec2 c,
    float& wa,
    float& wb,
    float& wc
) {
    const Vec2 v0{b.x - a.x, b.y - a.y};
    const Vec2 v1{c.x - a.x, c.y - a.y};
    const Vec2 v2{p.x - a.x, p.y - a.y};
    const float d00 = Dot(v0, v0);
    const float d01 = Dot(v0, v1);
    const float d11 = Dot(v1, v1);
    const float d20 = Dot(v2, v0);
    const float d21 = Dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) <= 0.000001f) {
        return false;
    }

    wb = (d11 * d20 - d01 * d21) / denom;
    wc = (d00 * d21 - d01 * d20) / denom;
    wa = 1.0f - wb - wc;

    constexpr float kEpsilon = -0.0005f;
    return wa >= kEpsilon && wb >= kEpsilon && wc >= kEpsilon;
}

static bool TryComputeMeshUvAtCanvasPoint(const LayerMesh& mesh, Vec2 point, Vec2& uv) {
    for (std::size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
        const std::uint32_t ia = mesh.indices[i + 0u];
        const std::uint32_t ib = mesh.indices[i + 1u];
        const std::uint32_t ic = mesh.indices[i + 2u];
        if (
            ia >= mesh.vertices.size() ||
            ib >= mesh.vertices.size() ||
            ic >= mesh.vertices.size()
        ) {
            continue;
        }

        const MeshVertex& a = mesh.vertices[ia];
        const MeshVertex& b = mesh.vertices[ib];
        const MeshVertex& c = mesh.vertices[ic];

        float wa = 0.0f;
        float wb = 0.0f;
        float wc = 0.0f;
        if (!ComputeBarycentricForUi(point, a.position, b.position, c.position, wa, wb, wc)) {
            continue;
        }

        uv.x = std::clamp(a.uv.x * wa + b.uv.x * wb + c.uv.x * wc, 0.0f, 1.0f);
        uv.y = std::clamp(a.uv.y * wa + b.uv.y * wb + c.uv.y * wc, 0.0f, 1.0f);
        return true;
    }

    return false;
}

static void RefreshMeshVertexUvForMeshEdit(EditorState& editor, int layerIndex, MeshVertex& vertex) {
    if (
        layerIndex >= 0 &&
        layerIndex < static_cast<int>(editor.meshModeGuideMeshes.size())
    ) {
        Vec2 uv;
        if (TryComputeMeshUvAtCanvasPoint(
            editor.meshModeGuideMeshes[static_cast<std::size_t>(layerIndex)],
            vertex.position,
            uv
        )) {
            vertex.uv = uv;
            return;
        }
    }

    if (IsValidLayerIndex(editor, layerIndex)) {
        RefreshMeshVertexUv(editor.document.layers[layerIndex], vertex);
    }
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

static MeshVertex CreateMeshVertexAtPoint(EditorState& editor, int layerIndex, Vec2 point) {
    MeshVertex vertex{point, Vec2{}};
    RefreshMeshVertexUvForMeshEdit(editor, layerIndex, vertex);
    return vertex;
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

static float ScreenPixelsToCanvas(float pixels, float zoom) {
    return pixels / std::max(zoom, 0.0001f);
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

    const float edgeHandleHalfSize = ScreenPixelsToCanvas(7.0f, zoom);
    const float cornerScaleRadius = ScreenPixelsToCanvas(13.0f, zoom);
    const float rotateInnerRadius = ScreenPixelsToCanvas(14.0f, zoom);
    const float rotateOuterRadius = ScreenPixelsToCanvas(24.0f, zoom);
    const float centerRadius = ScreenPixelsToCanvas(10.0f, zoom);
    const float edgeHandleHalfSizeSq = edgeHandleHalfSize * edgeHandleHalfSize;
    const float cornerScaleRadiusSq = cornerScaleRadius * cornerScaleRadius;
    const float rotateInnerRadiusSq = rotateInnerRadius * rotateInnerRadius;
    const float rotateOuterRadiusSq = rotateOuterRadius * rotateOuterRadius;
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
            distanceSq >= rotateInnerRadiusSq &&
            distanceSq <= rotateOuterRadiusSq
        ) {
            mode = LayerTransformMode::Rotate;
            return true;
        }
    }

    struct EdgeHandle {
        Vec2 point;
        int axisX;
        int axisY;
    };

    const EdgeHandle edgeHandles[] = {
        {Vec2{(x0 + x1) * 0.5f, y0}, 0, -1},
        {Vec2{x1, (y0 + y1) * 0.5f}, 1, 0},
        {Vec2{(x0 + x1) * 0.5f, y1}, 0, 1},
        {Vec2{x0, (y0 + y1) * 0.5f}, -1, 0},
    };

    for (const EdgeHandle& edgeHandle : edgeHandles) {
        const Vec2 delta = point - edgeHandle.point;
        if (
            std::abs(delta.x) <= edgeHandleHalfSize &&
            std::abs(delta.y) <= edgeHandleHalfSize &&
            Dot(delta, delta) <= edgeHandleHalfSizeSq * 2.0f
        ) {
            mode = LayerTransformMode::Scale;
            axisX = edgeHandle.axisX;
            axisY = edgeHandle.axisY;
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

static bool CanvasPointInSelectionDragShape(
    const EditorState& editor,
    Vec2 point
) {
    if (editor.selectionDragShape == SelectionDragShape::Rectangle) {
        return CanvasPointInBox(
            point,
            editor.boxSelectStartCanvasPos,
            editor.boxSelectCurrentCanvasPos
        );
    }

    const Vec2 delta = editor.boxSelectCurrentCanvasPos - editor.boxSelectStartCanvasPos;
    const float radiusSq = Dot(delta, delta);
    const Vec2 pointDelta = point - editor.boxSelectStartCanvasPos;
    return Dot(pointDelta, pointDelta) <= radiusSq;
}

static bool SegmentIntersectsSelectionCircle(Vec2 a, Vec2 b, Vec2 center, Vec2 radiusPoint) {
    const Vec2 ab = b - a;
    const float abLenSq = Dot(ab, ab);
    const Vec2 radiusDelta = radiusPoint - center;
    const float radiusSq = Dot(radiusDelta, radiusDelta);

    if (radiusSq <= 0.0001f) {
        return false;
    }

    if (abLenSq <= 0.0001f) {
        const Vec2 delta = a - center;
        return Dot(delta, delta) <= radiusSq;
    }

    const float t = std::clamp(Dot(center - a, ab) / abLenSq, 0.0f, 1.0f);
    const Vec2 closest{
        a.x + ab.x * t,
        a.y + ab.y * t
    };
    const Vec2 delta = closest - center;
    return Dot(delta, delta) <= radiusSq;
}

static bool SegmentIntersectsSelectionDragShape(
    const EditorState& editor,
    Vec2 a,
    Vec2 b
) {
    if (editor.selectionDragShape == SelectionDragShape::Rectangle) {
        return SegmentIntersectsBox(
            a,
            b,
            editor.boxSelectStartCanvasPos,
            editor.boxSelectCurrentCanvasPos
        );
    }

    return
        CanvasPointInSelectionDragShape(editor, a) ||
        CanvasPointInSelectionDragShape(editor, b) ||
        SegmentIntersectsSelectionCircle(
            a,
            b,
            editor.boxSelectStartCanvasPos,
            editor.boxSelectCurrentCanvasPos
        );
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
            CanvasPointInSelectionDragShape(editor, layer.mesh.vertices[i].position)
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

        if (SegmentIntersectsSelectionDragShape(
            editor,
            layer.mesh.vertices[edge.a].position,
            layer.mesh.vertices[edge.b].position
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
                CanvasPointInSelectionDragShape(editor, layer.mesh.vertices[i].position)
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

static void PushMeshEditOperationWithParameters(
    EditorState& editor,
    const LayerHistoryState& before,
    const std::vector<DeformParameter>& parametersBefore,
    const std::string& description
) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    PushLayerOperationWithParameters(
        editor,
        editor.selectedLayer,
        before,
        CaptureLayerHistoryState(editor.document.layers[editor.selectedLayer]),
        parametersBefore,
        editor.parameters,
        description
    );
}

static std::vector<int> MakeIdentityVertexRemap(std::size_t vertexCount) {
    std::vector<int> oldToNew(vertexCount, -1);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        oldToNew[i] = static_cast<int>(i);
    }
    return oldToNew;
}

static void AddVertexAtCanvasPoint(EditorState& editor, Vec2 canvasPoint, bool connectToSelection) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const LayerHistoryState before = CaptureLayerHistoryState(layer);
    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const std::vector<int> oldToNewVertex = MakeIdentityVertexRemap(layer.mesh.vertices.size());
    const int previousVertex = HasSelectedVertex(editor)
        ? static_cast<int>(ActiveSelectedVertex(editor))
        : -1;

    layer.mesh.vertices.push_back(CreateMeshVertexAtPoint(editor, editor.selectedLayer, canvasPoint));
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
    ReconcileParameterMeshesAfterMeshEdit(editor, editor.selectedLayer, oldToNewVertex);
    ClearMeshSelection(editor);
    editor.selectedVertices.push_back(newVertex);
    PushMeshEditOperationWithParameters(editor, before, parametersBefore, "Add mesh vertex");
}

static void DeleteSelectedMeshItems(EditorState& editor) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const LayerHistoryState before = CaptureLayerHistoryState(layer);
    const std::vector<DeformParameter> parametersBefore = editor.parameters;

    if (!editor.selectedVertices.empty()) {
        std::vector<std::uint8_t> removeVertex(layer.mesh.vertices.size(), 0u);
        for (const std::uint32_t vertex : editor.selectedVertices) {
            if (vertex < removeVertex.size()) {
                removeVertex[vertex] = 1u;
            }
        }

        std::vector<std::uint32_t> remap(layer.mesh.vertices.size(), 0u);
        std::vector<int> oldToNewVertex(layer.mesh.vertices.size(), -1);
        std::vector<MeshVertex> keptVertices;
        keptVertices.reserve(layer.mesh.vertices.size());

        for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
            if (removeVertex[i]) {
                continue;
            }

            remap[i] = static_cast<std::uint32_t>(keptVertices.size());
            oldToNewVertex[i] = static_cast<int>(keptVertices.size());
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
        ReconcileParameterMeshesAfterMeshEdit(editor, editor.selectedLayer, oldToNewVertex);
    } else if (!editor.selectedEdges.empty()) {
        const std::vector<int> oldToNewVertex = MakeIdentityVertexRemap(layer.mesh.vertices.size());
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
        ReconcileParameterMeshesAfterMeshEdit(editor, editor.selectedLayer, oldToNewVertex);
    }

    ClearMeshSelection(editor);
    UpdateLayerBoundsFromMesh(layer);
    PushMeshEditOperationWithParameters(editor, before, parametersBefore, "Delete mesh selection");
}
