#include "EditorUi.hpp"

#include "EditorDocument.hpp"
#include "EditorHistory.hpp"
#include "EditorSettings.hpp"
#include "MeshGenerator.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>

static void UpdateActiveParameterEndpointForLayers(EditorState& editor, const std::vector<int>& layerIndices);
static void ReconcileParameterMeshesAfterMeshEdit(
    EditorState& editor,
    int layerIndex,
    const std::vector<int>& oldToNewVertex
);
static void ReconcileParameterMeshesAfterMeshVertexMove(
    EditorState& editor,
    int layerIndex,
    const LayerMesh& beforeMesh
);
static bool ActiveParameterNeedsSetpointSnapBeforeEdit(EditorState& editor);
static bool ContinueParameterEditSetpointSnap(EditorState& editor);

#include "EditorUiProject.inc"
#include "EditorUiSelection.inc"
#include "EditorUiLayers.inc"
#include "EditorUiParameters.inc"
#include "EditorUiPanels.inc"
#include "EditorUiViewportPicking.inc"
#include "EditorUiTransforms.inc"
#include "EditorUiViewportMath.inc"
#include "EditorUiRendering.inc"
#include "EditorUiInteraction.inc"
#include "EditorUiViewportWindow.inc"
#include "EditorUiShortcuts.inc"
