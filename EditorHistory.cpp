#include "EditorHistory.hpp"

#include "EditorDocument.hpp"
#include "MeshGenerator.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

LayerHistoryState CaptureLayerHistoryState(const EditorLayer& layer) {
    LayerHistoryState state;
    state.textureIndex = layer.textureIndex;
    state.left = layer.left;
    state.top = layer.top;
    state.right = layer.right;
    state.bottom = layer.bottom;
    state.visible = layer.visible;
    state.opacity = layer.opacity;
    state.renderOrderOverride = layer.renderOrderOverride;
    state.maskLayerIndices = layer.maskLayerIndices;
    state.mesh = layer.mesh;
    return state;
}

void ApplyLayerHistoryState(EditorLayer& layer, const LayerHistoryState& state) {
    layer.textureIndex = state.textureIndex;
    layer.left = state.left;
    layer.top = state.top;
    layer.right = state.right;
    layer.bottom = state.bottom;
    layer.visible = state.visible;
    layer.opacity = state.opacity;
    layer.renderOrderOverride = state.renderOrderOverride;
    layer.maskLayerIndices = state.maskLayerIndices;
    layer.mesh = state.mesh;
}

static bool AreLayerMeshesEqual(const LayerMesh& a, const LayerMesh& b) {
    if (
        a.vertices.size() != b.vertices.size() ||
        a.indices != b.indices ||
        a.edges != b.edges
    ) {
        return false;
    }

    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        const MeshVertex& av = a.vertices[i];
        const MeshVertex& bv = b.vertices[i];

        if (
            av.position.x != bv.position.x ||
            av.position.y != bv.position.y ||
            av.uv.x != bv.uv.x ||
            av.uv.y != bv.uv.y
        ) {
            return false;
        }
    }

    return true;
}

static bool AreParameterMeshCornersEqual(
    const std::vector<DeformParameterMeshCorner>& a,
    const std::vector<DeformParameterMeshCorner>& b
) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (
            a[i].parameterIndices != b[i].parameterIndices ||
            a[i].parameterValues != b[i].parameterValues ||
            !AreLayerMeshesEqual(a[i].mesh, b[i].mesh)
        ) {
            return false;
        }
    }

    return true;
}

static bool AreParameterLayerSetpointsEqual(
    const std::vector<DeformParameterLayerSetpoint>& a,
    const std::vector<DeformParameterLayerSetpoint>& b
) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (
            a[i].value != b[i].value ||
            !AreLayerMeshesEqual(a[i].mesh, b[i].mesh) ||
            a[i].textureIndex != b[i].textureIndex ||
            a[i].opacity != b[i].opacity ||
            a[i].renderOrderOverride != b[i].renderOrderOverride ||
            a[i].maskLayerIndices != b[i].maskLayerIndices
        ) {
            return false;
        }
    }

    return true;
}

static bool AreParameterLayerStatesEqual(
    const DeformParameterLayerState& a,
    const DeformParameterLayerState& b
) {
    return
        a.layerIndex == b.layerIndex &&
        AreLayerMeshesEqual(a.meshAt0, b.meshAt0) &&
        AreLayerMeshesEqual(a.meshAt1, b.meshAt1) &&
        a.textureIndexAt0 == b.textureIndexAt0 &&
        a.textureIndexAt1 == b.textureIndexAt1 &&
        a.opacityAt0 == b.opacityAt0 &&
        a.opacityAt1 == b.opacityAt1 &&
        a.renderOrderOverrideAt0 == b.renderOrderOverrideAt0 &&
        a.renderOrderOverrideAt1 == b.renderOrderOverrideAt1 &&
        a.maskLayerIndicesAt0 == b.maskLayerIndicesAt0 &&
        a.maskLayerIndicesAt1 == b.maskLayerIndicesAt1 &&
        AreParameterMeshCornersEqual(a.meshCorners, b.meshCorners) &&
        AreParameterLayerSetpointsEqual(a.setpoints, b.setpoints);
}

static bool AreParameterListsEqual(
    const std::vector<DeformParameter>& a,
    const std::vector<DeformParameter>& b
) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (
            a[i].name != b[i].name ||
            a[i].type != b[i].type ||
            a[i].value != b[i].value ||
            a[i].selectedState != b[i].selectedState ||
            a[i].stateNames != b[i].stateNames ||
            a[i].affectsMesh != b[i].affectsMesh ||
            a[i].affectsRenderOrder != b[i].affectsRenderOrder ||
            a[i].affectsMasking != b[i].affectsMasking ||
            a[i].affectsOpacity != b[i].affectsOpacity ||
            a[i].affectsTexture != b[i].affectsTexture ||
            a[i].layers.size() != b[i].layers.size()
        ) {
            return false;
        }

        for (std::size_t j = 0; j < a[i].layers.size(); ++j) {
            if (!AreParameterLayerStatesEqual(a[i].layers[j], b[i].layers[j])) {
                return false;
            }
        }
    }

    return true;
}

static bool MeshVertexCountMatches(const LayerMesh& a, const LayerMesh& b) {
    return a.vertices.size() == b.vertices.size();
}

static bool AreLayerHistoryStatesEqual(
    const LayerHistoryState& a,
    const LayerHistoryState& b
) {
    return
        a.left == b.left &&
        a.textureIndex == b.textureIndex &&
        a.top == b.top &&
        a.right == b.right &&
        a.bottom == b.bottom &&
        a.visible == b.visible &&
        a.opacity == b.opacity &&
        a.renderOrderOverride == b.renderOrderOverride &&
        a.maskLayerIndices == b.maskLayerIndices &&
        AreLayerMeshesEqual(a.mesh, b.mesh);
}

static bool AreLayerListsEqual(
    const std::vector<LayerListHistoryState>& a,
    const std::vector<LayerListHistoryState>& b
) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (
            a[i].name != b[i].name ||
            !AreLayerHistoryStatesEqual(a[i].state, b[i].state)
        ) {
            return false;
        }
    }

    return true;
}

bool IsValidLayerIndex(const EditorState& editor, int layerIndex) {
    return
        layerIndex >= 0 &&
        layerIndex < static_cast<int>(editor.document.layers.size());
}

void PushLayerOperation(
    EditorState& editor,
    int layerIndex,
    const LayerHistoryState& before,
    const LayerHistoryState& after,
    const std::string& description
) {
    if (!IsValidLayerIndex(editor, layerIndex) || AreLayerHistoryStatesEqual(before, after)) {
        return;
    }

    LayerOperation operation;
    operation.layerIndex = layerIndex;
    operation.description = description;
    operation.before = before;
    operation.after = after;

    editor.history.undoStack.push_back(std::move(operation));
    editor.history.redoStack.clear();
}

void PushLayerOperationWithParameters(
    EditorState& editor,
    int layerIndex,
    const LayerHistoryState& before,
    const LayerHistoryState& after,
    const std::vector<DeformParameter>& parametersBefore,
    const std::vector<DeformParameter>& parametersAfter,
    const std::string& description
) {
    if (!IsValidLayerIndex(editor, layerIndex) || AreLayerHistoryStatesEqual(before, after)) {
        return;
    }

    LayerOperation operation;
    operation.layerIndex = layerIndex;
    operation.description = description;
    operation.before = before;
    operation.after = after;
    operation.hasParameterSnapshot = true;
    operation.parametersBefore = parametersBefore;
    operation.parametersAfter = parametersAfter;
    operation.selectedParameterBefore = editor.selectedParameter;
    operation.selectedParameterAfter = editor.selectedParameter;

    editor.history.undoStack.push_back(std::move(operation));
    editor.history.redoStack.clear();
}

void PushParameterOperation(
    EditorState& editor,
    const std::vector<DeformParameter>& parametersBefore,
    int selectedParameterBefore,
    const std::vector<DeformParameter>& parametersAfter,
    int selectedParameterAfter,
    const std::string& description
) {
    if (
        selectedParameterBefore == selectedParameterAfter &&
        AreParameterListsEqual(parametersBefore, parametersAfter)
    ) {
        return;
    }

    LayerOperation operation;
    operation.layerIndex = -1;
    operation.description = description;
    operation.hasParameterSnapshot = true;
    operation.parametersBefore = parametersBefore;
    operation.parametersAfter = parametersAfter;
    operation.selectedParameterBefore = selectedParameterBefore;
    operation.selectedParameterAfter = selectedParameterAfter;

    editor.history.undoStack.push_back(std::move(operation));
    editor.history.redoStack.clear();
}

void PushLayerListOperation(
    EditorState& editor,
    std::vector<LayerListHistoryState> layersBefore,
    std::vector<LayerListHistoryState> layersAfter,
    const std::string& description
) {
    if (AreLayerListsEqual(layersBefore, layersAfter)) {
        return;
    }

    LayerOperation operation;
    operation.layerIndex = -1;
    operation.description = description;
    operation.hasLayerListSnapshot = true;
    operation.layersBefore = std::move(layersBefore);
    operation.layersAfter = std::move(layersAfter);

    editor.history.undoStack.push_back(std::move(operation));
    editor.history.redoStack.clear();
}

void PushLayerListOperationWithParameters(
    EditorState& editor,
    std::vector<LayerListHistoryState> layersBefore,
    std::vector<LayerListHistoryState> layersAfter,
    const std::vector<DeformParameter>& parametersBefore,
    int selectedParameterBefore,
    const std::vector<DeformParameter>& parametersAfter,
    int selectedParameterAfter,
    const std::string& description
) {
    const bool layersChanged = !AreLayerListsEqual(layersBefore, layersAfter);
    const bool parametersChanged =
        !AreParameterListsEqual(parametersBefore, parametersAfter) ||
        selectedParameterBefore != selectedParameterAfter;
    if (!layersChanged && !parametersChanged) {
        return;
    }

    LayerOperation operation;
    operation.layerIndex = -1;
    operation.description = description;
    if (layersChanged) {
        operation.hasLayerListSnapshot = true;
        operation.layersBefore = std::move(layersBefore);
        operation.layersAfter = std::move(layersAfter);
    }
    if (parametersChanged) {
        operation.hasParameterSnapshot = true;
        operation.parametersBefore = parametersBefore;
        operation.parametersAfter = parametersAfter;
        operation.selectedParameterBefore = selectedParameterBefore;
        operation.selectedParameterAfter = selectedParameterAfter;
    }

    editor.history.undoStack.push_back(std::move(operation));
    editor.history.redoStack.clear();
}

static void ClampSelectedParameter(EditorState& editor) {
    if (editor.selectedParameter >= static_cast<int>(editor.parameters.size())) {
        editor.selectedParameter = editor.parameters.empty() ? -1 : static_cast<int>(editor.parameters.size()) - 1;
    }
    if (editor.selectedParameter < -1) {
        editor.selectedParameter = editor.parameters.empty() ? -1 : 0;
    }
}

static const DeformParameterLayerState* FindParameterLayerStateForHistory(
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

static void ApplyParameterSnapshotsToLayers(EditorState& editor) {
    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        const DeformParameterLayerState* meshBaseState = nullptr;
        const DeformParameterLayerState* opacityBaseState = nullptr;
        const DeformParameterLayerState* renderOrderBaseState = nullptr;
        const DeformParameterLayerState* maskingBaseState = nullptr;
        const DeformParameterLayerState* textureBaseState = nullptr;
        for (const DeformParameter& parameter : editor.parameters) {
            const DeformParameterLayerState* state = FindParameterLayerStateForHistory(parameter, layerIndex);
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
            if (parameter.affectsTexture && !textureBaseState) {
                textureBaseState = state;
            }
        }

        if (!meshBaseState && !opacityBaseState && !renderOrderBaseState && !maskingBaseState && !textureBaseState) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        LayerMesh resultMesh = meshBaseState ? meshBaseState->meshAt0 : layer.mesh;
        int resultTextureIndex = textureBaseState ? textureBaseState->textureIndexAt0 : layer.textureIndex;
        float resultOpacity = opacityBaseState ? opacityBaseState->opacityAt0 : layer.opacity;
        std::string resultRenderOrderOverride = renderOrderBaseState
            ? renderOrderBaseState->renderOrderOverrideAt0
            : layer.renderOrderOverride;
        std::vector<int> resultMaskLayerIndices = maskingBaseState
            ? maskingBaseState->maskLayerIndicesAt0
            : layer.maskLayerIndices;

        for (const DeformParameter& parameter : editor.parameters) {
            const DeformParameterLayerState* state = FindParameterLayerStateForHistory(parameter, layerIndex);
            if (!state) {
                continue;
            }

            const float value = std::clamp(parameter.value, 0.0f, 1.0f);
            if (
                parameter.affectsMesh &&
                MeshVertexCountMatches(resultMesh, state->meshAt0) &&
                MeshVertexCountMatches(resultMesh, state->meshAt1)
            ) {
                for (std::size_t i = 0; i < resultMesh.vertices.size(); ++i) {
                    resultMesh.vertices[i].position.x +=
                        (state->meshAt1.vertices[i].position.x - state->meshAt0.vertices[i].position.x) * value;
                    resultMesh.vertices[i].position.y +=
                        (state->meshAt1.vertices[i].position.y - state->meshAt0.vertices[i].position.y) * value;
                }
            }

            if (parameter.affectsOpacity) {
                resultOpacity += (state->opacityAt1 - state->opacityAt0) * value;
            }
            if (parameter.affectsTexture) {
                resultTextureIndex = value >= 0.999f ? state->textureIndexAt1 : state->textureIndexAt0;
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
        if (resultTextureIndex != layer.textureIndex) {
            ApplyTextureToLayer(editor.document, layer, resultTextureIndex);
        }
        layer.opacity = std::clamp(resultOpacity, 0.0f, 1.0f);
        layer.renderOrderOverride = std::move(resultRenderOrderOverride);
        layer.maskLayerIndices = std::move(resultMaskLayerIndices);
        UpdateLayerBoundsFromMesh(layer);
    }
}

static void RestoreParameterSnapshot(
    EditorState& editor,
    const std::vector<DeformParameter>& snapshot,
    int selectedParameter,
    bool restoreValuesAndSelection
) {
    if (restoreValuesAndSelection) {
        editor.parameters = snapshot;
        editor.selectedParameter = selectedParameter;
        ClampSelectedParameter(editor);
        ApplyParameterSnapshotsToLayers(editor);
        return;
    }

    const std::vector<DeformParameter> currentParameters = editor.parameters;
    const int currentSelectedParameter = editor.selectedParameter;
    editor.parameters = snapshot;

    for (std::size_t i = 0; i < editor.parameters.size(); ++i) {
        const DeformParameter* current = nullptr;
        if (i < currentParameters.size() && currentParameters[i].name == editor.parameters[i].name) {
            current = &currentParameters[i];
        } else {
            for (const DeformParameter& candidate : currentParameters) {
                if (candidate.name == editor.parameters[i].name) {
                    current = &candidate;
                    break;
                }
            }
        }

        if (current) {
            editor.parameters[i].value = current->value;
        }
    }

    editor.selectedParameter = currentSelectedParameter;
    ClampSelectedParameter(editor);
    ApplyParameterSnapshotsToLayers(editor);
}

static void RestoreLayerListSnapshot(EditorState& editor, const std::vector<LayerListHistoryState>& layers) {
    editor.document.layers.clear();
    editor.document.layers.reserve(layers.size());
    for (const LayerListHistoryState& snapshot : layers) {
        EditorLayer layer;
        layer.name = snapshot.name;
        ApplyLayerHistoryState(layer, snapshot.state);
        editor.document.layers.push_back(std::move(layer));
    }

    for (EditorLayer& layer : editor.document.layers) {
        ApplyTextureToLayer(editor.document, layer, layer.textureIndex);
    }

    if (editor.document.layers.empty()) {
        editor.selectedLayer = -1;
        editor.selectedLayers.clear();
    } else {
        editor.selectedLayer = std::clamp(editor.selectedLayer, 0, static_cast<int>(editor.document.layers.size()) - 1);
        editor.selectedLayers.erase(
            std::remove_if(
                editor.selectedLayers.begin(),
                editor.selectedLayers.end(),
                [&](int layerIndex) {
                    return !IsValidLayerIndex(editor, layerIndex);
                }
            ),
            editor.selectedLayers.end()
        );
        if (editor.selectedLayers.empty()) {
            editor.selectedLayers.push_back(editor.selectedLayer);
        }
    }

    editor.selectedVertices.clear();
    editor.selectedEdges.clear();
    editor.selectedDeformVertices.clear();
}

bool UndoLastOperation(EditorState& editor) {
    if (editor.history.undoStack.empty()) {
        return false;
    }

    LayerOperation operation = std::move(editor.history.undoStack.back());
    editor.history.undoStack.pop_back();

    if (!IsValidLayerIndex(editor, operation.layerIndex) && !operation.hasParameterSnapshot && !operation.hasLayerListSnapshot) {
        return false;
    }

    if (operation.hasLayerListSnapshot) {
        RestoreLayerListSnapshot(editor, operation.layersBefore);
    }
    if (IsValidLayerIndex(editor, operation.layerIndex)) {
        ApplyLayerHistoryState(editor.document.layers[operation.layerIndex], operation.before);
        ApplyTextureToLayer(
            editor.document,
            editor.document.layers[operation.layerIndex],
            editor.document.layers[operation.layerIndex].textureIndex
        );
        RebuildLayerRenderedTexture(editor, operation.layerIndex);
        editor.selectedLayer = operation.layerIndex;
    }
    if (operation.hasParameterSnapshot) {
        RestoreParameterSnapshot(
            editor,
            operation.parametersBefore,
            operation.selectedParameterBefore,
            !IsValidLayerIndex(editor, operation.layerIndex)
        );
    }
    editor.history.redoStack.push_back(std::move(operation));
    editor.statusText = "Undid edit.";
    return true;
}

bool RedoLastOperation(EditorState& editor) {
    if (editor.history.redoStack.empty()) {
        return false;
    }

    LayerOperation operation = std::move(editor.history.redoStack.back());
    editor.history.redoStack.pop_back();

    if (!IsValidLayerIndex(editor, operation.layerIndex) && !operation.hasParameterSnapshot && !operation.hasLayerListSnapshot) {
        return false;
    }

    if (operation.hasLayerListSnapshot) {
        RestoreLayerListSnapshot(editor, operation.layersAfter);
    }
    if (IsValidLayerIndex(editor, operation.layerIndex)) {
        ApplyLayerHistoryState(editor.document.layers[operation.layerIndex], operation.after);
        ApplyTextureToLayer(
            editor.document,
            editor.document.layers[operation.layerIndex],
            editor.document.layers[operation.layerIndex].textureIndex
        );
        RebuildLayerRenderedTexture(editor, operation.layerIndex);
        editor.selectedLayer = operation.layerIndex;
    }
    if (operation.hasParameterSnapshot) {
        RestoreParameterSnapshot(
            editor,
            operation.parametersAfter,
            operation.selectedParameterAfter,
            !IsValidLayerIndex(editor, operation.layerIndex)
        );
    }
    editor.history.undoStack.push_back(std::move(operation));
    editor.statusText = "Redid edit.";
    return true;
}
