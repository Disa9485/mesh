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
    state.hueShift = layer.hueShift;
    state.saturationScale = layer.saturationScale;
    state.lightnessShift = layer.lightnessShift;
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
    layer.hueShift = state.hueShift;
    layer.saturationScale = state.saturationScale;
    layer.lightnessShift = state.lightnessShift;
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
            av.uv.y != bv.uv.y ||
            av.physicsEnabled != bv.physicsEnabled
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
            a[i].hueShift != b[i].hueShift ||
            a[i].saturationScale != b[i].saturationScale ||
            a[i].lightnessShift != b[i].lightnessShift ||
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
        a.hueShiftAt0 == b.hueShiftAt0 &&
        a.hueShiftAt1 == b.hueShiftAt1 &&
        a.saturationScaleAt0 == b.saturationScaleAt0 &&
        a.saturationScaleAt1 == b.saturationScaleAt1 &&
        a.lightnessShiftAt0 == b.lightnessShiftAt0 &&
        a.lightnessShiftAt1 == b.lightnessShiftAt1 &&
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
            a[i].affectsColor != b[i].affectsColor ||
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
        a.hueShift == b.hueShift &&
        a.saturationScale == b.saturationScale &&
        a.lightnessShift == b.lightnessShift &&
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

static bool IsStateParameterForHistory(const DeformParameter& parameter) {
    return parameter.type == DeformParameterType::State;
}

static float ParameterEvaluationValueForHistory(const DeformParameter& parameter) {
    return IsStateParameterForHistory(parameter)
        ? static_cast<float>(std::clamp(
            parameter.selectedState,
            0,
            std::max(0, static_cast<int>(parameter.stateNames.size()) - 1)
        ))
        : std::clamp(parameter.value, 0.0f, 1.0f);
}

static bool NearlyEqualSetpointValueForHistory(float a, float b) {
    return std::abs(a - b) <= 0.001f;
}

static DeformParameterLayerSetpoint MakeSetpointFromStateForHistory(
    const DeformParameterLayerState& state,
    float value
) {
    DeformParameterLayerSetpoint setpoint;
    setpoint.value = std::clamp(value, 0.0f, 1.0f);
    if (setpoint.value <= 0.001f) {
        setpoint.mesh = state.meshAt0;
        setpoint.textureIndex = state.textureIndexAt0;
        setpoint.opacity = state.opacityAt0;
        setpoint.hueShift = state.hueShiftAt0;
        setpoint.saturationScale = state.saturationScaleAt0;
        setpoint.lightnessShift = state.lightnessShiftAt0;
        setpoint.renderOrderOverride = state.renderOrderOverrideAt0;
        setpoint.maskLayerIndices = state.maskLayerIndicesAt0;
    } else {
        setpoint.mesh = state.meshAt1;
        setpoint.textureIndex = state.textureIndexAt1;
        setpoint.opacity = state.opacityAt1;
        setpoint.hueShift = state.hueShiftAt1;
        setpoint.saturationScale = state.saturationScaleAt1;
        setpoint.lightnessShift = state.lightnessShiftAt1;
        setpoint.renderOrderOverride = state.renderOrderOverrideAt1;
        setpoint.maskLayerIndices = state.maskLayerIndicesAt1;
    }
    return setpoint;
}

static DeformParameterLayerSetpoint EvaluateLayerSetpointForHistory(
    const DeformParameterLayerState& state,
    float value
) {
    if (state.setpoints.empty()) {
        return MakeSetpointFromStateForHistory(state, value <= 0.5f ? 0.0f : 1.0f);
    }

    const DeformParameterLayerSetpoint* lower = &state.setpoints.front();
    const DeformParameterLayerSetpoint* upper = &state.setpoints.back();

    for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
        if (setpoint.value <= value) {
            lower = &setpoint;
        }
        if (setpoint.value >= value) {
            upper = &setpoint;
            break;
        }
    }

    if (NearlyEqualSetpointValueForHistory(lower->value, upper->value)) {
        return *lower;
    }

    const float localT = std::clamp((value - lower->value) / (upper->value - lower->value), 0.0f, 1.0f);
    DeformParameterLayerSetpoint result = *lower;
    result.value = value;
    result.opacity = lower->opacity + (upper->opacity - lower->opacity) * localT;
    result.hueShift = lower->hueShift + (upper->hueShift - lower->hueShift) * localT;
    result.saturationScale = lower->saturationScale + (upper->saturationScale - lower->saturationScale) * localT;
    result.lightnessShift = lower->lightnessShift + (upper->lightnessShift - lower->lightnessShift) * localT;

    if (MeshVertexCountMatches(lower->mesh, upper->mesh)) {
        result.mesh = lower->mesh;
        for (std::size_t i = 0; i < result.mesh.vertices.size(); ++i) {
            result.mesh.vertices[i].position.x =
                lower->mesh.vertices[i].position.x +
                (upper->mesh.vertices[i].position.x - lower->mesh.vertices[i].position.x) * localT;
            result.mesh.vertices[i].position.y =
                lower->mesh.vertices[i].position.y +
                (upper->mesh.vertices[i].position.y - lower->mesh.vertices[i].position.y) * localT;
        }
    }

    if (value <= 0.001f) {
        result.renderOrderOverride = lower->renderOrderOverride;
        result.maskLayerIndices = lower->maskLayerIndices;
    } else if (value >= 0.999f) {
        result.renderOrderOverride = upper->renderOrderOverride;
        result.maskLayerIndices = upper->maskLayerIndices;
    }

    return result;
}

static std::vector<float> CollectParameterSetpointValuesForHistory(const DeformParameter& parameter) {
    std::vector<float> values;
    if (IsStateParameterForHistory(parameter)) {
        const int stateCount = std::max(1, static_cast<int>(parameter.stateNames.size()));
        values.reserve(static_cast<std::size_t>(stateCount));
        for (int i = 0; i < stateCount; ++i) {
            values.push_back(static_cast<float>(i));
        }
        return values;
    }

    values.push_back(0.0f);
    values.push_back(1.0f);

    for (const DeformParameterLayerState& state : parameter.layers) {
        for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
            const float value = std::clamp(setpoint.value, 0.0f, 1.0f);
            const bool exists = std::any_of(values.begin(), values.end(), [&](float existing) {
                return NearlyEqualSetpointValueForHistory(existing, value);
            });
            if (!exists) {
                values.push_back(value);
            }
        }
    }

    std::sort(values.begin(), values.end());
    return values;
}

static std::vector<int> CollectMeshParametersForLayerForHistory(
    const EditorState& editor,
    int layerIndex
) {
    std::vector<int> parameterIndices;
    for (int parameterIndex = 0; parameterIndex < static_cast<int>(editor.parameters.size()); ++parameterIndex) {
        const DeformParameter& parameter = editor.parameters[parameterIndex];
        if (parameter.affectsMesh && FindParameterLayerStateForHistory(parameter, layerIndex)) {
            parameterIndices.push_back(parameterIndex);
        }
    }
    return parameterIndices;
}

static bool ParameterIndexListsMatchForHistory(const std::vector<int>& a, const std::vector<int>& b) {
    return a == b;
}

static bool ParameterValueListsMatchForHistory(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!NearlyEqualSetpointValueForHistory(a[i], b[i])) {
            return false;
        }
    }

    return true;
}

static const DeformParameterMeshCorner* FindMeshCornerForHistory(
    const DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& parameterValues
) {
    for (const DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            ParameterIndexListsMatchForHistory(corner.parameterIndices, parameterIndices) &&
            ParameterValueListsMatchForHistory(corner.parameterValues, parameterValues)
        ) {
            return &corner;
        }
    }

    return nullptr;
}

static bool ApplyMultilinearMeshForLayerForHistory(
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
            FindParameterLayerStateForHistory(editor.parameters[parameterIndex], layerIndex);
        if (state && !state->meshCorners.empty()) {
            cornerSource = state;
            break;
        }
    }

    if (!cornerSource) {
        return false;
    }

    std::vector<float> lowerValues;
    std::vector<float> upperValues;
    std::vector<float> localValues;
    lowerValues.reserve(parameterIndices.size());
    upperValues.reserve(parameterIndices.size());
    localValues.reserve(parameterIndices.size());

    for (const int parameterIndex : parameterIndices) {
        const DeformParameter& parameter = editor.parameters[parameterIndex];
        const float value = ParameterEvaluationValueForHistory(parameter);
        const std::vector<float> setpointValues = CollectParameterSetpointValuesForHistory(parameter);

        float lower = setpointValues.front();
        float upper = setpointValues.back();
        for (const float setpointValue : setpointValues) {
            if (setpointValue <= value) {
                lower = setpointValue;
            }
            if (setpointValue >= value) {
                upper = setpointValue;
                break;
            }
        }

        lowerValues.push_back(lower);
        upperValues.push_back(upper);
        localValues.push_back(
            NearlyEqualSetpointValueForHistory(lower, upper)
                ? 0.0f
                : std::clamp((value - lower) / (upper - lower), 0.0f, 1.0f)
        );
    }

    const std::uint32_t cornerCount = 1u << static_cast<std::uint32_t>(parameterIndices.size());
    std::vector<LayerMesh> cornerMeshes(cornerCount);
    for (std::uint32_t cornerBits = 0; cornerBits < cornerCount; ++cornerBits) {
        std::vector<float> cornerValues;
        cornerValues.reserve(parameterIndices.size());
        for (std::size_t i = 0; i < parameterIndices.size(); ++i) {
            cornerValues.push_back(((cornerBits >> i) & 1u) ? upperValues[i] : lowerValues[i]);
        }

        const DeformParameterMeshCorner* storedCorner =
            FindMeshCornerForHistory(*cornerSource, parameterIndices, cornerValues);
        if (!storedCorner || !MeshVertexCountMatches(resultMesh, storedCorner->mesh)) {
            return false;
        }
        cornerMeshes[cornerBits] = storedCorner->mesh;
    }

    resultMesh = cornerMeshes[0];
    for (MeshVertex& vertex : resultMesh.vertices) {
        vertex.position = Vec2{0.0f, 0.0f};
    }

    for (std::uint32_t cornerBits = 0; cornerBits < cornerCount; ++cornerBits) {
        float weight = 1.0f;
        for (std::size_t i = 0; i < localValues.size(); ++i) {
            weight *= ((cornerBits >> i) & 1u) ? localValues[i] : (1.0f - localValues[i]);
        }

        const LayerMesh& cornerMesh = cornerMeshes[cornerBits];
        for (std::size_t i = 0; i < resultMesh.vertices.size(); ++i) {
            resultMesh.vertices[i].position.x += cornerMesh.vertices[i].position.x * weight;
            resultMesh.vertices[i].position.y += cornerMesh.vertices[i].position.y * weight;
        }
    }

    return true;
}

static void ApplyParameterSnapshotsToLayers(EditorState& editor) {
    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        const DeformParameterLayerState* meshBaseState = nullptr;
        const DeformParameterLayerState* opacityBaseState = nullptr;
        const DeformParameterLayerState* renderOrderBaseState = nullptr;
        const DeformParameterLayerState* maskingBaseState = nullptr;
        const DeformParameterLayerState* textureBaseState = nullptr;
        const DeformParameterLayerState* colorBaseState = nullptr;
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
            if (parameter.affectsColor && !colorBaseState) {
                colorBaseState = state;
            }
        }

        if (!meshBaseState && !opacityBaseState && !renderOrderBaseState && !maskingBaseState && !textureBaseState && !colorBaseState) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        LayerMesh resultMesh = meshBaseState ? EvaluateLayerSetpointForHistory(*meshBaseState, 0.0f).mesh : layer.mesh;
        int resultTextureIndex = textureBaseState ? EvaluateLayerSetpointForHistory(*textureBaseState, 0.0f).textureIndex : layer.textureIndex;
        float resultOpacity = opacityBaseState ? EvaluateLayerSetpointForHistory(*opacityBaseState, 0.0f).opacity : layer.opacity;
        float resultHueShift = colorBaseState ? EvaluateLayerSetpointForHistory(*colorBaseState, 0.0f).hueShift : layer.hueShift;
        float resultSaturationScale = colorBaseState ? EvaluateLayerSetpointForHistory(*colorBaseState, 0.0f).saturationScale : layer.saturationScale;
        float resultLightnessShift = colorBaseState ? EvaluateLayerSetpointForHistory(*colorBaseState, 0.0f).lightnessShift : layer.lightnessShift;
        std::string resultRenderOrderOverride = renderOrderBaseState
            ? renderOrderBaseState->renderOrderOverrideAt0
            : layer.renderOrderOverride;
        std::vector<int> resultMaskLayerIndices = maskingBaseState
            ? maskingBaseState->maskLayerIndicesAt0
            : layer.maskLayerIndices;
        const std::vector<int> meshParameterIndices =
            CollectMeshParametersForLayerForHistory(editor, layerIndex);
        const bool usingMultilinearMesh =
            ApplyMultilinearMeshForLayerForHistory(editor, layerIndex, meshParameterIndices, resultMesh);

        for (const DeformParameter& parameter : editor.parameters) {
            const DeformParameterLayerState* state = FindParameterLayerStateForHistory(parameter, layerIndex);
            if (!state) {
                continue;
            }

            const float value = ParameterEvaluationValueForHistory(parameter);
            if (!usingMultilinearMesh && parameter.affectsMesh) {
                const DeformParameterLayerSetpoint base = EvaluateLayerSetpointForHistory(*state, 0.0f);
                const DeformParameterLayerSetpoint evaluated = EvaluateLayerSetpointForHistory(*state, value);
                if (!MeshVertexCountMatches(resultMesh, base.mesh) || !MeshVertexCountMatches(resultMesh, evaluated.mesh)) {
                    continue;
                }
                for (std::size_t i = 0; i < resultMesh.vertices.size(); ++i) {
                    resultMesh.vertices[i].position.x +=
                        evaluated.mesh.vertices[i].position.x - base.mesh.vertices[i].position.x;
                    resultMesh.vertices[i].position.y +=
                        evaluated.mesh.vertices[i].position.y - base.mesh.vertices[i].position.y;
                }
            }

            if (parameter.affectsOpacity) {
                resultOpacity +=
                    EvaluateLayerSetpointForHistory(*state, value).opacity -
                    EvaluateLayerSetpointForHistory(*state, 0.0f).opacity;
            }
            if (parameter.affectsTexture) {
                resultTextureIndex = EvaluateLayerSetpointForHistory(*state, value).textureIndex;
            }
            if (parameter.affectsColor) {
                const DeformParameterLayerSetpoint evaluated = EvaluateLayerSetpointForHistory(*state, value);
                const DeformParameterLayerSetpoint base = EvaluateLayerSetpointForHistory(*state, 0.0f);
                resultHueShift += evaluated.hueShift - base.hueShift;
                resultSaturationScale += evaluated.saturationScale - base.saturationScale;
                resultLightnessShift += evaluated.lightnessShift - base.lightnessShift;
            }
            if (parameter.affectsRenderOrder && value <= 0.001f) {
                resultRenderOrderOverride = EvaluateLayerSetpointForHistory(*state, value).renderOrderOverride;
            } else if (parameter.affectsRenderOrder && value >= 0.999f) {
                resultRenderOrderOverride = EvaluateLayerSetpointForHistory(*state, value).renderOrderOverride;
            } else if (parameter.affectsRenderOrder) {
                resultRenderOrderOverride = EvaluateLayerSetpointForHistory(*state, value).renderOrderOverride;
            }
            if (parameter.affectsMasking && value <= 0.001f) {
                resultMaskLayerIndices = EvaluateLayerSetpointForHistory(*state, value).maskLayerIndices;
            } else if (parameter.affectsMasking && value >= 0.999f) {
                resultMaskLayerIndices = EvaluateLayerSetpointForHistory(*state, value).maskLayerIndices;
            } else if (parameter.affectsMasking) {
                resultMaskLayerIndices = EvaluateLayerSetpointForHistory(*state, value).maskLayerIndices;
            }
        }

        layer.mesh = std::move(resultMesh);
        if (resultTextureIndex != layer.textureIndex) {
            ApplyTextureToLayer(editor.document, layer, resultTextureIndex);
        }
        layer.opacity = std::clamp(resultOpacity, 0.0f, 1.0f);
        layer.hueShift = std::clamp(resultHueShift, -100.0f, 100.0f);
        layer.saturationScale = std::clamp(resultSaturationScale, 0.0f, 200.0f);
        layer.lightnessShift = std::clamp(resultLightnessShift, -100.0f, 100.0f);
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
            editor.parameters[i].selectedState = current->selectedState;
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
