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

static bool NearlyEqualSetpointValue(float a, float b) {
    return std::abs(a - b) <= 0.001f;
}

static DeformParameterLayerSetpoint MakeSetpointFromState(
    const DeformParameterLayerState& state,
    float value
) {
    DeformParameterLayerSetpoint setpoint;
    setpoint.value = std::clamp(value, 0.0f, 1.0f);
    if (setpoint.value <= 0.001f) {
        setpoint.mesh = state.meshAt0;
        setpoint.opacity = state.opacityAt0;
        setpoint.renderOrderOverride = state.renderOrderOverrideAt0;
        setpoint.maskLayerIndices = state.maskLayerIndicesAt0;
    } else {
        setpoint.mesh = state.meshAt1;
        setpoint.opacity = state.opacityAt1;
        setpoint.renderOrderOverride = state.renderOrderOverrideAt1;
        setpoint.maskLayerIndices = state.maskLayerIndicesAt1;
    }
    return setpoint;
}

static void SortParameterSetpoints(DeformParameterLayerState& state) {
    std::sort(
        state.setpoints.begin(),
        state.setpoints.end(),
        [](const DeformParameterLayerSetpoint& a, const DeformParameterLayerSetpoint& b) {
            return a.value < b.value;
        }
    );
}

static void EnsureParameterLayerSetpoints(DeformParameterLayerState& state) {
    if (state.setpoints.empty()) {
        state.setpoints.push_back(MakeSetpointFromState(state, 0.0f));
        state.setpoints.push_back(MakeSetpointFromState(state, 1.0f));
        return;
    }

    bool hasZero = false;
    bool hasOne = false;
    for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
        hasZero = hasZero || NearlyEqualSetpointValue(setpoint.value, 0.0f);
        hasOne = hasOne || NearlyEqualSetpointValue(setpoint.value, 1.0f);
    }

    if (!hasZero) {
        state.setpoints.push_back(MakeSetpointFromState(state, 0.0f));
    }
    if (!hasOne) {
        state.setpoints.push_back(MakeSetpointFromState(state, 1.0f));
    }
    SortParameterSetpoints(state);
}

static std::vector<float> CollectParameterSetpointValues(const DeformParameter& parameter) {
    std::vector<float> values;
    values.push_back(0.0f);
    values.push_back(1.0f);

    for (const DeformParameterLayerState& state : parameter.layers) {
        for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
            const float value = std::clamp(setpoint.value, 0.0f, 1.0f);
            const bool exists = std::any_of(values.begin(), values.end(), [&](float existing) {
                return NearlyEqualSetpointValue(existing, value);
            });
            if (!exists) {
                values.push_back(value);
            }
        }
    }

    std::sort(values.begin(), values.end());
    return values;
}

static const DeformParameterLayerSetpoint* FindLayerSetpointAtValue(
    const DeformParameterLayerState& state,
    float value
) {
    for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
        if (NearlyEqualSetpointValue(setpoint.value, value)) {
            return &setpoint;
        }
    }

    return nullptr;
}

static DeformParameterLayerSetpoint EvaluateLayerSetpoint(
    const DeformParameterLayerState& state,
    float value
) {
    const float tValue = std::clamp(value, 0.0f, 1.0f);
    if (state.setpoints.empty()) {
        return MakeSetpointFromState(state, tValue <= 0.5f ? 0.0f : 1.0f);
    }

    const DeformParameterLayerSetpoint* lower = &state.setpoints.front();
    const DeformParameterLayerSetpoint* upper = &state.setpoints.back();

    for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
        if (setpoint.value <= tValue) {
            lower = &setpoint;
        }
        if (setpoint.value >= tValue) {
            upper = &setpoint;
            break;
        }
    }

    if (NearlyEqualSetpointValue(lower->value, upper->value)) {
        return *lower;
    }

    const float localT = std::clamp((tValue - lower->value) / (upper->value - lower->value), 0.0f, 1.0f);
    DeformParameterLayerSetpoint result = *lower;
    result.value = tValue;
    result.opacity = lower->opacity + (upper->opacity - lower->opacity) * localT;

    if (MeshTopologyMatches(lower->mesh, upper->mesh)) {
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

    if (tValue <= 0.001f) {
        result.renderOrderOverride = lower->renderOrderOverride;
        result.maskLayerIndices = lower->maskLayerIndices;
    } else if (tValue >= 0.999f) {
        result.renderOrderOverride = upper->renderOrderOverride;
        result.maskLayerIndices = upper->maskLayerIndices;
    }

    return result;
}

static void UpsertLayerSetpoint(
    DeformParameterLayerState& state,
    const DeformParameterLayerSetpoint& setpoint
) {
    for (DeformParameterLayerSetpoint& existing : state.setpoints) {
        if (NearlyEqualSetpointValue(existing.value, setpoint.value)) {
            existing = setpoint;
            SortParameterSetpoints(state);
            return;
        }
    }

    state.setpoints.push_back(setpoint);
    SortParameterSetpoints(state);
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

static void AccumulateParameterMeshSetpointDelta(
    LayerMesh& result,
    const DeformParameterLayerState& state,
    float value
) {
    if (state.setpoints.empty()) {
        AccumulateParameterMeshDelta(result, state, value);
        return;
    }

    const DeformParameterLayerSetpoint base = EvaluateLayerSetpoint(state, 0.0f);
    const DeformParameterLayerSetpoint evaluated = EvaluateLayerSetpoint(state, value);
    if (!MeshTopologyMatches(result, base.mesh) || !MeshTopologyMatches(result, evaluated.mesh)) {
        return;
    }

    for (std::size_t i = 0; i < result.vertices.size(); ++i) {
        result.vertices[i].position.x +=
            evaluated.mesh.vertices[i].position.x - base.mesh.vertices[i].position.x;
        result.vertices[i].position.y +=
            evaluated.mesh.vertices[i].position.y - base.mesh.vertices[i].position.y;
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

static bool ParameterValueListsMatch(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!NearlyEqualSetpointValue(a[i], b[i])) {
            return false;
        }
    }

    return true;
}

static const DeformParameterMeshCorner* FindMeshCorner(
    const DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& parameterValues
) {
    for (const DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            !ParameterIndexListsMatch(corner.parameterIndices, parameterIndices) ||
            corner.parameterValues.size() != parameterValues.size()
        ) {
            continue;
        }

        bool valuesMatch = true;
        for (std::size_t i = 0; i < parameterValues.size(); ++i) {
            if (!NearlyEqualSetpointValue(corner.parameterValues[i], parameterValues[i])) {
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

static bool BuildFallbackMultilinearCornerMesh(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& parameterValues,
    LayerMesh& mesh
) {
    const DeformParameterLayerState* baseState = nullptr;
    for (const int parameterIndex : parameterIndices) {
        baseState = FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (baseState) {
            break;
        }
    }

    if (!baseState) {
        return false;
    }

    mesh = EvaluateLayerSetpoint(*baseState, 0.0f).mesh;

    for (std::size_t parameterOffset = 0; parameterOffset < parameterIndices.size(); ++parameterOffset) {
        const int parameterIndex = parameterIndices[parameterOffset];
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (!state) {
            continue;
        }

        const DeformParameterLayerSetpoint baseSetpoint = EvaluateLayerSetpoint(*state, 0.0f);
        const DeformParameterLayerSetpoint evaluatedSetpoint =
            EvaluateLayerSetpoint(*state, parameterValues[parameterOffset]);

        if (
            !MeshTopologyMatches(mesh, baseSetpoint.mesh) ||
            !MeshTopologyMatches(mesh, evaluatedSetpoint.mesh)
        ) {
            return false;
        }

        for (std::size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            mesh.vertices[vertexIndex].position.x +=
                evaluatedSetpoint.mesh.vertices[vertexIndex].position.x -
                baseSetpoint.mesh.vertices[vertexIndex].position.x;
            mesh.vertices[vertexIndex].position.y +=
                evaluatedSetpoint.mesh.vertices[vertexIndex].position.y -
                baseSetpoint.mesh.vertices[vertexIndex].position.y;
        }
    }

    return true;
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

    std::vector<float> lowerValues;
    std::vector<float> upperValues;
    std::vector<float> localValues;
    lowerValues.reserve(parameterIndices.size());
    upperValues.reserve(parameterIndices.size());
    localValues.reserve(parameterIndices.size());
    for (const int parameterIndex : parameterIndices) {
        const float value = std::clamp(editor.parameters[parameterIndex].value, 0.0f, 1.0f);
        const std::vector<float> setpointValues = CollectParameterSetpointValues(editor.parameters[parameterIndex]);

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
            NearlyEqualSetpointValue(lower, upper)
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
            FindMeshCorner(*cornerSource, parameterIndices, cornerValues);
        if (storedCorner) {
            cornerMeshes[cornerBits] = storedCorner->mesh;
        } else if (!BuildFallbackMultilinearCornerMesh(
            editor,
            layerIndex,
            parameterIndices,
            cornerValues,
            cornerMeshes[cornerBits]
        )) {
            return false;
        }

        if (!MeshTopologyMatches(resultMesh, cornerMeshes[cornerBits])) {
            return false;
        }
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

static bool CurrentValuesAreMeshCorner(
    const EditorState& editor,
    const std::vector<int>& parameterIndices,
    std::vector<float>& cornerValues
) {
    cornerValues.clear();
    cornerValues.reserve(parameterIndices.size());

    for (const int parameterIndex : parameterIndices) {
        const float value = std::clamp(editor.parameters[parameterIndex].value, 0.0f, 1.0f);
        cornerValues.push_back(value);
    }

    return true;
}

static void StoreMeshCornerForState(
    DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues,
    const LayerMesh& mesh
) {
    for (DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            ParameterIndexListsMatch(corner.parameterIndices, parameterIndices) &&
            ParameterValueListsMatch(corner.parameterValues, cornerValues)
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
    const std::vector<float>& cornerValues
) {
    for (const DeformParameterMeshCorner& corner : state.meshCorners) {
        if (
            ParameterIndexListsMatch(corner.parameterIndices, parameterIndices) &&
            ParameterValueListsMatch(corner.parameterValues, cornerValues)
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
        const float requestedValue = ((cornerBits >> targetIndex) & 1u) ? 1.0f : 0.0f;
        if (!NearlyEqualSetpointValue(corner.parameterValues[i], requestedValue)) {
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
        std::vector<float> cornerValues;
        cornerValues.reserve(parameterIndices.size());
        for (std::size_t i = 0; i < parameterIndices.size(); ++i) {
            cornerValues.push_back(((cornerBits >> i) & 1u) ? 1.0f : 0.0f);
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

    std::vector<float> cornerValues;
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
    for (DeformParameter& parameter : editor.parameters) {
        for (DeformParameterLayerState& state : parameter.layers) {
            EnsureParameterLayerSetpoints(state);
        }
    }

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
        LayerMesh resultMesh = meshBaseState ? EvaluateLayerSetpoint(*meshBaseState, 0.0f).mesh : layer.mesh;
        float resultOpacity = opacityBaseState ? EvaluateLayerSetpoint(*opacityBaseState, 0.0f).opacity : layer.opacity;
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
                AccumulateParameterMeshSetpointDelta(resultMesh, *state, value);
            }
            if (parameter.affectsOpacity) {
                resultOpacity +=
                    EvaluateLayerSetpoint(*state, value).opacity -
                    EvaluateLayerSetpoint(*state, 0.0f).opacity;
            }

            if (parameter.affectsRenderOrder && value <= 0.001f) {
                resultRenderOrderOverride = EvaluateLayerSetpoint(*state, value).renderOrderOverride;
            } else if (parameter.affectsRenderOrder && value >= 0.999f) {
                resultRenderOrderOverride = EvaluateLayerSetpoint(*state, value).renderOrderOverride;
            } else if (parameter.affectsRenderOrder) {
                const DeformParameterLayerSetpoint evaluated = EvaluateLayerSetpoint(*state, value);
                resultRenderOrderOverride = evaluated.renderOrderOverride;
            }

            if (parameter.affectsMasking && value <= 0.001f) {
                resultMaskLayerIndices = EvaluateLayerSetpoint(*state, value).maskLayerIndices;
            } else if (parameter.affectsMasking && value >= 0.999f) {
                resultMaskLayerIndices = EvaluateLayerSetpoint(*state, value).maskLayerIndices;
            } else if (parameter.affectsMasking) {
                const DeformParameterLayerSetpoint evaluated = EvaluateLayerSetpoint(*state, value);
                resultMaskLayerIndices = evaluated.maskLayerIndices;
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
    EnsureParameterLayerSetpoints(state);
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

static void DisableParameterOpacityChannel(
    EditorState& editor,
    int parameterIndex,
    const std::vector<DeformParameter>& parametersBefore,
    int selectedParameterBefore
) {
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size())
    ) {
        return;
    }

    editor.parameters[parameterIndex].affectsOpacity = false;
    ApplyAllDeformParameters(editor);

    DeformParameter& parameter = editor.parameters[parameterIndex];
    for (DeformParameterLayerState& state : parameter.layers) {
        if (!IsValidLayerIndex(editor, state.layerIndex)) {
            continue;
        }

        const float neutralOpacity = editor.document.layers[state.layerIndex].opacity;
        state.opacityAt0 = neutralOpacity;
        state.opacityAt1 = neutralOpacity;
    }

    PushParameterOperation(
        editor,
        parametersBefore,
        selectedParameterBefore,
        editor.parameters,
        editor.selectedParameter,
        "Change parameter channels"
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
    (void)layerIndices;

    const float setpointValue = std::clamp(parameter.value, 0.0f, 1.0f);
    const bool updateZero = setpointValue <= 0.001f;
    const bool updateOne = setpointValue >= 0.999f;

    for (DeformParameterLayerState& parameterLayerState : parameter.layers) {
        const int layerIndex = parameterLayerState.layerIndex;
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        DeformParameterLayerState* state = &parameterLayerState;
        EnsureParameterLayerSetpoints(*state);

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

        DeformParameterLayerSetpoint setpoint = EvaluateLayerSetpoint(*state, setpointValue);
        setpoint.value = setpointValue;
        if (parameter.affectsMesh) {
            setpoint.mesh = storedMultilinearMeshCorner ? layer.mesh : endpointMesh;
        }
        if (parameter.affectsOpacity) {
            setpoint.opacity = endpointOpacity;
        }
        if (parameter.affectsRenderOrder) {
            setpoint.renderOrderOverride = layer.renderOrderOverride;
        }
        if (parameter.affectsMasking) {
            setpoint.maskLayerIndices = layer.maskLayerIndices;
        }
        UpsertLayerSetpoint(*state, setpoint);

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

static bool DrawParameterSetpointMarkersAndHandleSnap(
    const std::vector<float>& setpoints,
    float& value
) {
    const ImVec2 sliderMin = ImGui::GetItemRectMin();
    const ImVec2 sliderMax = ImGui::GetItemRectMax();
    const float sliderWidth = sliderMax.x - sliderMin.x;
    if (sliderWidth <= 0.0f) {
        return false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 markerColor = IM_COL32(255, 220, 90, 230);
    const ImU32 markerShadow = IM_COL32(20, 20, 20, 180);

    for (const float setpoint : setpoints) {
        const float x = sliderMin.x + sliderWidth * setpoint;
        drawList->AddLine(
            ImVec2(x, sliderMin.y + 2.0f),
            ImVec2(x, sliderMax.y - 2.0f),
            markerShadow,
            3.0f
        );
        drawList->AddLine(
            ImVec2(x, sliderMin.y + 2.0f),
            ImVec2(x, sliderMax.y - 2.0f),
            markerColor,
            1.5f
        );
    }

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float snapRadius = 10.0f;
    if (
        io.MousePos.y < sliderMin.y - snapRadius ||
        io.MousePos.y > sliderMax.y + snapRadius
    ) {
        return false;
    }

    for (const float setpoint : setpoints) {
        const float x = sliderMin.x + sliderWidth * setpoint;
        if (std::abs(io.MousePos.x - x) <= snapRadius) {
            value = setpoint;
            return true;
        }
    }

    return false;
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
                if (affectsOpacity) {
                    parameter.affectsOpacity = true;
                    for (DeformParameterLayerState& state : parameter.layers) {
                        if (!IsValidLayerIndex(editor, state.layerIndex)) {
                            continue;
                        }

                        const float neutralOpacity = editor.document.layers[state.layerIndex].opacity;
                        state.opacityAt0 = neutralOpacity;
                        state.opacityAt1 = neutralOpacity;
                    }
                    PushParameterChannelOperation(editor, before, selectedBefore, "Change parameter channels");
                } else {
                    DisableParameterOpacityChannel(editor, i, before, selectedBefore);
                }
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
        const bool sliderChanged = ImGui::SliderFloat("##value", &value, 0.0f, 1.0f, "%.2f");
        const bool snappedToSetpoint = DrawParameterSetpointMarkersAndHandleSnap(
            CollectParameterSetpointValues(parameter),
            value
        );
        if (sliderChanged || snappedToSetpoint) {
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
