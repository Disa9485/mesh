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

static void UpdateActiveParameterEndpointForLayers(EditorState& editor, const std::vector<int>& layerIndices);

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
