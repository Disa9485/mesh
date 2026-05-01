#pragma once

#include "EditorTypes.hpp"

std::uint8_t LayerAlphaAt(const EditorLayer& layer, int x, int y);
void UpdateLayerBoundsFromMesh(EditorLayer& layer);
LayerMesh GenerateLayerMesh(
    const EditorLayer& layer,
    const MeshGeneratorSettings& settings
);
bool GenerateMeshForSelectedLayer(EditorState& editor);
