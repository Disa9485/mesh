#pragma once

#include "EditorTypes.hpp"
#include "PsdLoader.hpp"

struct GLFWwindow;

void DestroyDocument(EditorDocument& doc);
bool GetMeshBounds(const LayerMesh& mesh, float& x0, float& y0, float& x1, float& y1);
void RebuildMeshTrianglesFromEdges(LayerMesh& mesh);
LayerMesh CreateInitialQuadMeshForLayer(
    const LayerImageRGBA& layer,
    std::uint8_t alphaThreshold = kOpaquePixelAlphaThreshold
);
bool LoadPsdIntoEditor(
    const std::string& path,
    EditorState& editor,
    GLFWwindow* window,
    std::string& error
);
bool AttachPsdToProject(
    const std::string& path,
    EditorState& editor,
    GLFWwindow* window,
    std::string& error
);
bool LoadProjectForEditor(
    const std::string& path,
    EditorState& editor,
    GLFWwindow* window,
    std::string& error
);
bool SaveProjectForEditor(const EditorState& editor, const std::string& path, std::string& error);
bool SaveMeshesForEditor(const EditorState& editor, std::string& error);
void TranslateLayerMesh(EditorLayer& layer, float dx, float dy);
void RebuildLayerRenderedTexture(EditorState& editor, int layerIndex);
void ApplyTextureToLayer(EditorDocument& document, EditorLayer& layer, int textureIndex);
