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

static void ReconcileStoredMeshAfterMeshEdit(
    LayerMesh& storedMesh,
    const LayerMesh& editedMesh,
    const std::vector<int>& oldToNewVertex
) {
    LayerMesh reconciled;
    reconciled.vertices = editedMesh.vertices;
    reconciled.indices = editedMesh.indices;
    reconciled.edges = editedMesh.edges;

    for (std::size_t oldIndex = 0; oldIndex < oldToNewVertex.size(); ++oldIndex) {
        const int newIndex = oldToNewVertex[oldIndex];
        if (
            newIndex < 0 ||
            newIndex >= static_cast<int>(reconciled.vertices.size()) ||
            oldIndex >= storedMesh.vertices.size()
        ) {
            continue;
        }

        reconciled.vertices[static_cast<std::size_t>(newIndex)].position =
            storedMesh.vertices[oldIndex].position;
    }

    storedMesh = std::move(reconciled);
}

static void ReconcileParameterMeshesAfterMeshEdit(
    EditorState& editor,
    int layerIndex,
    const std::vector<int>& oldToNewVertex
) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    const LayerMesh& editedMesh = editor.document.layers[layerIndex].mesh;
    for (DeformParameter& parameter : editor.parameters) {
        DeformParameterLayerState* state = FindParameterLayerState(parameter, layerIndex);
        if (!state) {
            continue;
        }

        ReconcileStoredMeshAfterMeshEdit(state->meshAt0, editedMesh, oldToNewVertex);
        ReconcileStoredMeshAfterMeshEdit(state->meshAt1, editedMesh, oldToNewVertex);
        for (DeformParameterLayerSetpoint& setpoint : state->setpoints) {
            ReconcileStoredMeshAfterMeshEdit(setpoint.mesh, editedMesh, oldToNewVertex);
        }
        for (DeformParameterMeshCorner& corner : state->meshCorners) {
            ReconcileStoredMeshAfterMeshEdit(corner.mesh, editedMesh, oldToNewVertex);
        }
    }
}

static void ReconcileStoredMeshAfterMeshVertexMove(
    LayerMesh& storedMesh,
    const LayerMesh& beforeMesh,
    const LayerMesh& editedMesh
) {
    if (!MeshTopologyMatches(storedMesh, beforeMesh) || !MeshTopologyMatches(storedMesh, editedMesh)) {
        std::vector<int> identity(beforeMesh.vertices.size(), -1);
        for (std::size_t i = 0; i < identity.size(); ++i) {
            identity[i] = static_cast<int>(i);
        }
        ReconcileStoredMeshAfterMeshEdit(storedMesh, editedMesh, identity);
        return;
    }

    storedMesh.indices = editedMesh.indices;
    storedMesh.edges = editedMesh.edges;
    for (std::size_t i = 0; i < storedMesh.vertices.size(); ++i) {
        storedMesh.vertices[i].uv = editedMesh.vertices[i].uv;
        storedMesh.vertices[i].position.x +=
            editedMesh.vertices[i].position.x - beforeMesh.vertices[i].position.x;
        storedMesh.vertices[i].position.y +=
            editedMesh.vertices[i].position.y - beforeMesh.vertices[i].position.y;
    }
}

static void ReconcileParameterMeshesAfterMeshVertexMove(
    EditorState& editor,
    int layerIndex,
    const LayerMesh& beforeMesh
) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    const LayerMesh& editedMesh = editor.document.layers[layerIndex].mesh;
    for (DeformParameter& parameter : editor.parameters) {
        DeformParameterLayerState* state = FindParameterLayerState(parameter, layerIndex);
        if (!state) {
            continue;
        }

        ReconcileStoredMeshAfterMeshVertexMove(state->meshAt0, beforeMesh, editedMesh);
        ReconcileStoredMeshAfterMeshVertexMove(state->meshAt1, beforeMesh, editedMesh);
        for (DeformParameterLayerSetpoint& setpoint : state->setpoints) {
            ReconcileStoredMeshAfterMeshVertexMove(setpoint.mesh, beforeMesh, editedMesh);
        }
        for (DeformParameterMeshCorner& corner : state->meshCorners) {
            ReconcileStoredMeshAfterMeshVertexMove(corner.mesh, beforeMesh, editedMesh);
        }
    }
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

static bool IsStateParameter(const DeformParameter& parameter) {
    return parameter.type == DeformParameterType::State;
}

static void EnsureStateParameterDefaults(DeformParameter& parameter) {
    if (!IsStateParameter(parameter)) {
        return;
    }

    if (parameter.stateNames.empty()) {
        parameter.stateNames.push_back("Default");
    }
    parameter.selectedState = std::clamp(
        parameter.selectedState,
        0,
        static_cast<int>(parameter.stateNames.size()) - 1
    );
    parameter.value = static_cast<float>(parameter.selectedState);
}

static DeformParameterLayerSetpoint MakeStateSetpointFromLayer(
    const EditorLayer& layer,
    int stateIndex
) {
    DeformParameterLayerSetpoint setpoint;
    setpoint.value = static_cast<float>(stateIndex);
    setpoint.mesh = layer.mesh;
    setpoint.textureIndex = layer.textureIndex;
    setpoint.opacity = layer.opacity;
    setpoint.hueShift = layer.hueShift;
    setpoint.saturationScale = layer.saturationScale;
    setpoint.lightnessShift = layer.lightnessShift;
    setpoint.renderOrderOverride = layer.renderOrderOverride;
    setpoint.maskLayerIndices = layer.maskLayerIndices;
    return setpoint;
}

static void EnsureStateParameterLayerSetpoints(
    const DeformParameter& parameter,
    DeformParameterLayerState& state
) {
    if (!IsStateParameter(parameter)) {
        EnsureParameterLayerSetpoints(state);
        return;
    }

    const int stateCount = std::max(1, static_cast<int>(parameter.stateNames.size()));
    if (state.setpoints.empty()) {
        DeformParameterLayerSetpoint base = MakeSetpointFromState(state, 0.0f);
        base.value = 0.0f;
        state.setpoints.push_back(std::move(base));
    }

    for (int stateIndex = 0; stateIndex < stateCount; ++stateIndex) {
        const float stateValue = static_cast<float>(stateIndex);
        const bool exists = std::any_of(
            state.setpoints.begin(),
            state.setpoints.end(),
            [&](const DeformParameterLayerSetpoint& setpoint) {
                return NearlyEqualSetpointValue(setpoint.value, stateValue);
            }
        );
        if (!exists) {
            DeformParameterLayerSetpoint copy = state.setpoints.front();
            copy.value = stateValue;
            state.setpoints.push_back(std::move(copy));
        }
    }

    state.setpoints.erase(
        std::remove_if(
            state.setpoints.begin(),
            state.setpoints.end(),
            [&](const DeformParameterLayerSetpoint& setpoint) {
                return setpoint.value < -0.001f || setpoint.value > static_cast<float>(stateCount - 1) + 0.001f;
            }
        ),
        state.setpoints.end()
    );
    SortParameterSetpoints(state);
}

static float ParameterEvaluationValue(const DeformParameter& parameter) {
    return IsStateParameter(parameter)
        ? static_cast<float>(std::clamp(
            parameter.selectedState,
            0,
            std::max(0, static_cast<int>(parameter.stateNames.size()) - 1)
        ))
        : std::clamp(parameter.value, 0.0f, 1.0f);
}

static std::vector<float> CollectParameterSetpointValues(const DeformParameter& parameter) {
    std::vector<float> values;
    if (IsStateParameter(parameter)) {
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

static bool IsEndpointSetpointValue(float value) {
    return NearlyEqualSetpointValue(value, 0.0f) || NearlyEqualSetpointValue(value, 1.0f);
}

static bool FindNearbyParameterSetpointValue(
    const DeformParameter& parameter,
    float value,
    float radius,
    float& nearestValue
) {
    bool found = false;
    float bestDistance = radius;
    for (const float setpoint : CollectParameterSetpointValues(parameter)) {
        const float distance = std::abs(setpoint - value);
        if (distance <= bestDistance) {
            bestDistance = distance;
            nearestValue = setpoint;
            found = true;
        }
    }

    return found;
}

static bool ParameterHasSetpointValue(const DeformParameter& parameter, float value) {
    const std::vector<float> setpoints = CollectParameterSetpointValues(parameter);
    return std::any_of(setpoints.begin(), setpoints.end(), [&](float setpoint) {
        return NearlyEqualSetpointValue(setpoint, value);
    });
}

static void ApplyAllDeformParameters(EditorState& editor);

static bool ActiveParameterNeedsSetpointSnapBeforeEdit(EditorState& editor) {
    if (
        editor.selectedParameter < 0 ||
        editor.selectedParameter >= static_cast<int>(editor.parameters.size())
    ) {
        return false;
    }

    DeformParameter& parameter = editor.parameters[editor.selectedParameter];
    if (IsStateParameter(parameter)) {
        return false;
    }
    constexpr float snapRadius = 0.05f;
    float nearestValue = 0.0f;
    if (!FindNearbyParameterSetpointValue(parameter, parameter.value, snapRadius, nearestValue)) {
        return false;
    }

    if (NearlyEqualSetpointValue(parameter.value, nearestValue)) {
        parameter.value = nearestValue;
        return false;
    }

    editor.parameterEditSnapActive = true;
    editor.parameterEditSnapTarget = nearestValue;
    editor.statusText = "Snapping to nearby parameter setpoint before editing.";
    return true;
}

static bool ContinueParameterEditSetpointSnap(EditorState& editor) {
    if (
        !editor.parameterEditSnapActive ||
        editor.selectedParameter < 0 ||
        editor.selectedParameter >= static_cast<int>(editor.parameters.size())
    ) {
        editor.parameterEditSnapActive = false;
        return false;
    }

    DeformParameter& parameter = editor.parameters[editor.selectedParameter];
    const float target = editor.parameterEditSnapTarget;
    parameter.value += (target - parameter.value) * 0.28f;
    if (std::abs(parameter.value - target) <= 0.001f) {
        parameter.value = target;
        editor.parameterEditSnapActive = false;
    }

    ApplyAllDeformParameters(editor);
    return true;
}

static float ResolveDraggedSetpointValue(
    const DeformParameter& parameter,
    float oldValue,
    float requestedValue
) {
    constexpr float minSpacing = 0.05f;
    float resolved = std::clamp(requestedValue, 0.001f, 0.999f);
    const std::vector<float> setpoints = CollectParameterSetpointValues(parameter);

    for (int pass = 0; pass < 4; ++pass) {
        bool adjusted = false;
        for (const float setpoint : setpoints) {
            if (NearlyEqualSetpointValue(setpoint, oldValue)) {
                continue;
            }

            if (std::abs(resolved - setpoint) < minSpacing) {
                resolved = requestedValue < setpoint
                    ? setpoint - minSpacing
                    : setpoint + minSpacing;
                resolved = std::clamp(resolved, 0.001f, 0.999f);
                adjusted = true;
            }
        }

        if (!adjusted) {
            break;
        }
    }

    return resolved;
}

static void MoveParameterSetpointValue(
    DeformParameter& parameter,
    int parameterIndex,
    float oldValue,
    float newValue
) {
    if (NearlyEqualSetpointValue(oldValue, newValue)) {
        return;
    }

    for (DeformParameterLayerState& state : parameter.layers) {
        for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
            if (NearlyEqualSetpointValue(setpoint.value, oldValue)) {
                setpoint.value = newValue;
            }
        }
        SortParameterSetpoints(state);

        for (DeformParameterMeshCorner& corner : state.meshCorners) {
            for (std::size_t i = 0; i < corner.parameterIndices.size() && i < corner.parameterValues.size(); ++i) {
                if (
                    corner.parameterIndices[i] == parameterIndex &&
                    NearlyEqualSetpointValue(corner.parameterValues[i], oldValue)
                ) {
                    corner.parameterValues[i] = newValue;
                }
            }
        }
    }
}

static void DeleteParameterSetpointValue(
    DeformParameter& parameter,
    int parameterIndex,
    float value
) {
    if (IsEndpointSetpointValue(value)) {
        return;
    }

    for (DeformParameterLayerState& state : parameter.layers) {
        state.setpoints.erase(
            std::remove_if(
                state.setpoints.begin(),
                state.setpoints.end(),
                [&](const DeformParameterLayerSetpoint& setpoint) {
                    return NearlyEqualSetpointValue(setpoint.value, value);
                }
            ),
            state.setpoints.end()
        );
        EnsureParameterLayerSetpoints(state);

        state.meshCorners.erase(
            std::remove_if(
                state.meshCorners.begin(),
                state.meshCorners.end(),
                [&](const DeformParameterMeshCorner& corner) {
                    for (std::size_t i = 0; i < corner.parameterIndices.size() && i < corner.parameterValues.size(); ++i) {
                        if (
                            corner.parameterIndices[i] == parameterIndex &&
                            NearlyEqualSetpointValue(corner.parameterValues[i], value)
                        ) {
                            return true;
                        }
                    }
                    return false;
                }
            ),
            state.meshCorners.end()
        );
    }
}

static DeformParameterLayerSetpoint EvaluateLayerSetpoint(
    const DeformParameterLayerState& state,
    float value
);
static void UpsertLayerSetpoint(
    DeformParameterLayerState& state,
    const DeformParameterLayerSetpoint& setpoint
);
static void ApplyAllDeformParameters(EditorState& editor);

struct AlignMeshVertexBinding {
    std::uint32_t targetVertex = 0;
    std::uint32_t sourceA = 0;
    std::uint32_t sourceB = 0;
    std::uint32_t sourceC = 0;
    float wa = 0.0f;
    float wb = 0.0f;
    float wc = 0.0f;
};

static bool ComputeBarycentric(
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
    return true;
}

static bool BarycentricInsideTriangle(float wa, float wb, float wc) {
    constexpr float epsilon = 0.0005f;
    return wa >= -epsilon && wb >= -epsilon && wc >= -epsilon;
}

static bool FindSourceTriangleBinding(
    const LayerMesh& sourceMesh,
    Vec2 targetPoint,
    AlignMeshVertexBinding& binding
) {
    for (std::size_t i = 0; i + 2u < sourceMesh.indices.size(); i += 3u) {
        const std::uint32_t ia = sourceMesh.indices[i + 0u];
        const std::uint32_t ib = sourceMesh.indices[i + 1u];
        const std::uint32_t ic = sourceMesh.indices[i + 2u];
        if (
            ia >= sourceMesh.vertices.size() ||
            ib >= sourceMesh.vertices.size() ||
            ic >= sourceMesh.vertices.size()
        ) {
            continue;
        }

        float wa = 0.0f;
        float wb = 0.0f;
        float wc = 0.0f;
        if (!ComputeBarycentric(
            targetPoint,
            sourceMesh.vertices[ia].position,
            sourceMesh.vertices[ib].position,
            sourceMesh.vertices[ic].position,
            wa,
            wb,
            wc
        )) {
            continue;
        }

        if (!BarycentricInsideTriangle(wa, wb, wc)) {
            continue;
        }

        binding.sourceA = ia;
        binding.sourceB = ib;
        binding.sourceC = ic;
        binding.wa = wa;
        binding.wb = wb;
        binding.wc = wc;
        return true;
    }

    return false;
}

static Vec2 EvaluateBoundSourcePoint(
    const LayerMesh& sourceMesh,
    const AlignMeshVertexBinding& binding
) {
    if (
        binding.sourceA >= sourceMesh.vertices.size() ||
        binding.sourceB >= sourceMesh.vertices.size() ||
        binding.sourceC >= sourceMesh.vertices.size()
    ) {
        return Vec2{};
    }

    const Vec2 a = sourceMesh.vertices[binding.sourceA].position;
    const Vec2 b = sourceMesh.vertices[binding.sourceB].position;
    const Vec2 c = sourceMesh.vertices[binding.sourceC].position;
    return Vec2{
        a.x * binding.wa + b.x * binding.wb + c.x * binding.wc,
        a.y * binding.wa + b.y * binding.wb + c.y * binding.wc
    };
}

static void InitializeAlignMeshesDefaults(EditorState& editor) {
    if (
        !IsValidLayerIndex(editor, editor.alignMeshesTargetLayer) &&
        IsValidLayerIndex(editor, editor.selectedLayer)
    ) {
        editor.alignMeshesTargetLayer = editor.selectedLayer;
    }
    if (!IsValidLayerIndex(editor, editor.alignMeshesTargetLayer) && !editor.document.layers.empty()) {
        editor.alignMeshesTargetLayer = 0;
    }

    if (!IsValidLayerIndex(editor, editor.alignMeshesSourceLayer)) {
        editor.alignMeshesSourceLayer = editor.document.layers.size() > 1u ? 1 : 0;
    }
    if (editor.alignMeshesSourceLayer == editor.alignMeshesTargetLayer && editor.document.layers.size() > 1u) {
        editor.alignMeshesSourceLayer = editor.alignMeshesTargetLayer == 0 ? 1 : 0;
    }

    if (
        editor.alignMeshesParameter < 0 ||
        editor.alignMeshesParameter >= static_cast<int>(editor.parameters.size())
    ) {
        editor.alignMeshesParameter = editor.selectedParameter >= 0
            ? editor.selectedParameter
            : (editor.parameters.empty() ? -1 : 0);
    }
}

static bool AlignMeshesForParameter(EditorState& editor) {
    const int targetLayer = editor.alignMeshesTargetLayer;
    const int sourceLayer = editor.alignMeshesSourceLayer;
    const int parameterIndex = editor.alignMeshesParameter;
    if (
        !IsValidLayerIndex(editor, targetLayer) ||
        !IsValidLayerIndex(editor, sourceLayer) ||
        targetLayer == sourceLayer ||
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size())
    ) {
        editor.statusText = "Align meshes needs two layers and a parameter.";
        return false;
    }

    DeformParameter& parameter = editor.parameters[parameterIndex];
    if (!parameter.affectsMesh) {
        editor.statusText = "Align meshes needs a parameter that can edit meshes.";
        return false;
    }

    DeformParameterLayerState* targetState = FindParameterLayerState(parameter, targetLayer);
    DeformParameterLayerState* sourceState = FindParameterLayerState(parameter, sourceLayer);
    if (!targetState || !sourceState) {
        editor.statusText = "Both layers must be assigned to the selected parameter.";
        return false;
    }

    EnsureParameterLayerSetpoints(*targetState);
    EnsureParameterLayerSetpoints(*sourceState);

    const float startValue = std::clamp(editor.alignMeshesStartSetpoint, 0.0f, 1.0f);
    const LayerMesh targetStart = EvaluateLayerSetpoint(*targetState, startValue).mesh;
    const LayerMesh sourceStart = EvaluateLayerSetpoint(*sourceState, startValue).mesh;
    if (targetStart.vertices.empty() || sourceStart.vertices.empty() || sourceStart.indices.empty()) {
        editor.statusText = "Align meshes needs valid target vertices and source triangles.";
        return false;
    }

    std::vector<AlignMeshVertexBinding> bindings;
    bindings.reserve(targetStart.vertices.size());
    for (std::size_t i = 0; i < targetStart.vertices.size(); ++i) {
        AlignMeshVertexBinding binding;
        binding.targetVertex = static_cast<std::uint32_t>(i);
        if (FindSourceTriangleBinding(sourceStart, targetStart.vertices[i].position, binding)) {
            bindings.push_back(binding);
        }
    }

    if (bindings.empty()) {
        editor.statusText = "No target vertices were inside the source mesh.";
        return false;
    }

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedBefore = editor.selectedParameter;
    const std::vector<float> setpointValues = CollectParameterSetpointValues(parameter);

    for (const float setpointValue : setpointValues) {
        LayerMesh targetMesh = EvaluateLayerSetpoint(*targetState, setpointValue).mesh;
        const LayerMesh sourceMesh = EvaluateLayerSetpoint(*sourceState, setpointValue).mesh;
        if (!MeshTopologyMatches(sourceStart, sourceMesh)) {
            continue;
        }

        for (const AlignMeshVertexBinding& binding : bindings) {
            if (binding.targetVertex >= targetMesh.vertices.size()) {
                continue;
            }

            targetMesh.vertices[binding.targetVertex].position =
                EvaluateBoundSourcePoint(sourceMesh, binding);
        }

        DeformParameterLayerSetpoint setpoint = EvaluateLayerSetpoint(*targetState, setpointValue);
        setpoint.value = setpointValue;
        setpoint.mesh = std::move(targetMesh);
        UpsertLayerSetpoint(*targetState, setpoint);

        if (NearlyEqualSetpointValue(setpointValue, 0.0f)) {
            targetState->meshAt0 = setpoint.mesh;
        } else if (NearlyEqualSetpointValue(setpointValue, 1.0f)) {
            targetState->meshAt1 = setpoint.mesh;
        }
    }

    editor.selectedParameter = parameterIndex;
    PushParameterOperation(
        editor,
        parametersBefore,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Align meshes"
    );
    ApplyAllDeformParameters(editor);
    editor.statusText =
        "Aligned " + std::to_string(bindings.size()) + " target vertex" +
        (bindings.size() == 1u ? "." : "es.");
    return true;
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
    const float tValue = value;
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
    result.hueShift = lower->hueShift + (upper->hueShift - lower->hueShift) * localT;
    result.saturationScale = lower->saturationScale + (upper->saturationScale - lower->saturationScale) * localT;
    result.lightnessShift = lower->lightnessShift + (upper->lightnessShift - lower->lightnessShift) * localT;

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

static LayerMesh BuildSeedMeshForMultilinearCornerValues(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues
);

static bool BuildFallbackMultilinearCornerMesh(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& parameterValues,
    LayerMesh& mesh
) {
    mesh = BuildSeedMeshForMultilinearCornerValues(
        editor,
        layerIndex,
        parameterIndices,
        parameterValues
    );
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
        const float value = ParameterEvaluationValue(editor.parameters[parameterIndex]);
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
        const float value = ParameterEvaluationValue(editor.parameters[parameterIndex]);
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

static bool CornerMatchesRequestedValues(
    const DeformParameterMeshCorner& corner,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues
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
        if (
            targetIndex >= cornerValues.size() ||
            !NearlyEqualSetpointValue(corner.parameterValues[i], cornerValues[targetIndex])
        ) {
            return false;
        }
    }

    return true;
}

static std::vector<std::uint8_t> FindRepresentedParametersForCorner(
    const DeformParameterMeshCorner* corner,
    const std::vector<int>& parameterIndices
) {
    std::vector<std::uint8_t> represented(parameterIndices.size(), 0u);
    if (!corner) {
        return represented;
    }

    for (const int cornerParameterIndex : corner->parameterIndices) {
        const auto it = std::find(parameterIndices.begin(), parameterIndices.end(), cornerParameterIndex);
        if (it != parameterIndices.end()) {
            represented[static_cast<std::size_t>(std::distance(parameterIndices.begin(), it))] = 1u;
        }
    }

    return represented;
}

static LayerMesh BuildSeedMeshForMultilinearCornerValues(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues
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
                CornerMatchesRequestedValues(corner, parameterIndices, cornerValues) &&
                MeshTopologyMatches(fallbackMesh, corner.mesh)
            ) {
                bestExistingCorner = &corner;
                bestExistingCornerParameterCount = corner.parameterIndices.size();
            }
        }
    }

    std::vector<std::uint8_t> represented =
        FindRepresentedParametersForCorner(bestExistingCorner, parameterIndices);

    LayerMesh seedMesh;
    if (bestExistingCorner) {
        seedMesh = bestExistingCorner->mesh;
    } else if (!baseState || !MeshTopologyMatches(fallbackMesh, baseState->meshAt0)) {
        return fallbackMesh;
    } else {
        seedMesh = EvaluateLayerSetpoint(*baseState, 0.0f).mesh;
    }

    for (std::size_t parameterOffset = 0; parameterOffset < parameterIndices.size(); ++parameterOffset) {
        if (represented[parameterOffset]) {
            continue;
        }

        const int parameterIndex = parameterIndices[parameterOffset];
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (!state) {
            continue;
        }

        const DeformParameterLayerSetpoint baseSetpoint = EvaluateLayerSetpoint(*state, 0.0f);
        const DeformParameterLayerSetpoint evaluatedSetpoint =
            EvaluateLayerSetpoint(*state, cornerValues[parameterOffset]);
        if (
            !MeshTopologyMatches(seedMesh, baseSetpoint.mesh) ||
            !MeshTopologyMatches(seedMesh, evaluatedSetpoint.mesh)
        ) {
            continue;
        }

        for (std::size_t vertexIndex = 0; vertexIndex < seedMesh.vertices.size(); ++vertexIndex) {
            seedMesh.vertices[vertexIndex].position.x +=
                evaluatedSetpoint.mesh.vertices[vertexIndex].position.x -
                baseSetpoint.mesh.vertices[vertexIndex].position.x;
            seedMesh.vertices[vertexIndex].position.y +=
                evaluatedSetpoint.mesh.vertices[vertexIndex].position.y -
                baseSetpoint.mesh.vertices[vertexIndex].position.y;
        }
    }

    return seedMesh;
}

static LayerMesh BuildSeedMeshForMultilinearCorner(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    std::uint32_t cornerBits
) {
    std::vector<float> cornerValues;
    cornerValues.reserve(parameterIndices.size());
    for (std::size_t i = 0; i < parameterIndices.size(); ++i) {
        cornerValues.push_back(((cornerBits >> i) & 1u) ? 1.0f : 0.0f);
    }

    return BuildSeedMeshForMultilinearCornerValues(
        editor,
        layerIndex,
        parameterIndices,
        cornerValues
    );
}

static void CollectMultilinearCornerValueCombinations(
    const EditorState& editor,
    const std::vector<int>& parameterIndices,
    std::vector<std::vector<float>>& combinations
) {
    combinations.clear();
    if (parameterIndices.empty()) {
        return;
    }

    std::vector<std::vector<float>> valuesByParameter;
    valuesByParameter.reserve(parameterIndices.size());
    std::size_t totalCombinationCount = 1u;
    constexpr std::size_t kMaxSeededCornerCombinations = 4096u;

    for (const int parameterIndex : parameterIndices) {
        if (
            parameterIndex < 0 ||
            parameterIndex >= static_cast<int>(editor.parameters.size())
        ) {
            return;
        }

        std::vector<float> values = CollectParameterSetpointValues(editor.parameters[parameterIndex]);
        if (values.empty()) {
            values.push_back(0.0f);
            values.push_back(1.0f);
        }

        totalCombinationCount *= values.size();
        if (totalCombinationCount > kMaxSeededCornerCombinations) {
            combinations.clear();
            return;
        }

        valuesByParameter.push_back(std::move(values));
    }

    std::vector<float> current;
    current.reserve(parameterIndices.size());
    const std::function<void(std::size_t)> appendCombination = [&](std::size_t parameterOffset) {
        if (parameterOffset >= valuesByParameter.size()) {
            combinations.push_back(current);
            return;
        }

        for (const float value : valuesByParameter[parameterOffset]) {
            current.push_back(value);
            appendCombination(parameterOffset + 1u);
            current.pop_back();
        }
    };

    appendCombination(0u);
}

static void EnsureMultilinearMeshCornersForLayer(EditorState& editor, int layerIndex) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    const std::vector<int> parameterIndices = CollectMeshParametersForLayer(editor, layerIndex);
    if (parameterIndices.size() < 2u || parameterIndices.size() > 12u) {
        return;
    }

    std::vector<std::vector<float>> cornerValueCombinations;
    CollectMultilinearCornerValueCombinations(editor, parameterIndices, cornerValueCombinations);
    if (cornerValueCombinations.empty()) {
        return;
    }

    for (const std::vector<float>& cornerValues : cornerValueCombinations) {
        const LayerMesh seedMesh = BuildSeedMeshForMultilinearCornerValues(
            editor,
            layerIndex,
            parameterIndices,
            cornerValues
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

static LayerMesh BuildAdditiveSeedMeshForMultilinearCornerValues(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues
) {
    LayerMesh seedMesh = editor.document.layers[layerIndex].mesh;
    const DeformParameterLayerState* baseState = nullptr;
    for (const int parameterIndex : parameterIndices) {
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (state) {
            baseState = state;
            break;
        }
    }

    if (!baseState || !MeshTopologyMatches(seedMesh, baseState->meshAt0)) {
        return seedMesh;
    }

    seedMesh = EvaluateLayerSetpoint(*baseState, 0.0f).mesh;
    for (std::size_t parameterOffset = 0; parameterOffset < parameterIndices.size(); ++parameterOffset) {
        const int parameterIndex = parameterIndices[parameterOffset];
        const DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (!state) {
            continue;
        }

        const DeformParameterLayerSetpoint baseSetpoint = EvaluateLayerSetpoint(*state, 0.0f);
        const DeformParameterLayerSetpoint evaluatedSetpoint =
            EvaluateLayerSetpoint(*state, cornerValues[parameterOffset]);
        if (
            !MeshTopologyMatches(seedMesh, baseSetpoint.mesh) ||
            !MeshTopologyMatches(seedMesh, evaluatedSetpoint.mesh)
        ) {
            continue;
        }

        for (std::size_t vertexIndex = 0; vertexIndex < seedMesh.vertices.size(); ++vertexIndex) {
            seedMesh.vertices[vertexIndex].position.x +=
                evaluatedSetpoint.mesh.vertices[vertexIndex].position.x -
                baseSetpoint.mesh.vertices[vertexIndex].position.x;
            seedMesh.vertices[vertexIndex].position.y +=
                evaluatedSetpoint.mesh.vertices[vertexIndex].position.y -
                baseSetpoint.mesh.vertices[vertexIndex].position.y;
        }
    }

    return seedMesh;
}

static bool FindNeighborSetpointValuesExcludingValue(
    const DeformParameter& parameter,
    float value,
    float& lower,
    float& upper
) {
    std::vector<float> values = CollectParameterSetpointValues(parameter);
    values.erase(
        std::remove_if(
            values.begin(),
            values.end(),
            [&](float setpoint) {
                return NearlyEqualSetpointValue(setpoint, value);
            }
        ),
        values.end()
    );

    if (values.empty()) {
        return false;
    }

    std::sort(values.begin(), values.end());
    lower = values.front();
    upper = values.back();
    for (const float setpoint : values) {
        if (setpoint <= value) {
            lower = setpoint;
        }
        if (setpoint >= value) {
            upper = setpoint;
            break;
        }
    }

    return true;
}

static bool TryBuildInsertedParameterSliceCornerMesh(
    const EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues,
    int changedParameterIndex,
    float changedParameterValue,
    LayerMesh& mesh
) {
    const auto changedIt = std::find(
        parameterIndices.begin(),
        parameterIndices.end(),
        changedParameterIndex
    );
    if (changedIt == parameterIndices.end()) {
        return false;
    }

    const std::size_t changedOffset =
        static_cast<std::size_t>(std::distance(parameterIndices.begin(), changedIt));
    if (
        changedOffset >= cornerValues.size() ||
        changedParameterIndex < 0 ||
        changedParameterIndex >= static_cast<int>(editor.parameters.size()) ||
        !NearlyEqualSetpointValue(cornerValues[changedOffset], changedParameterValue)
    ) {
        return false;
    }

    float lowerValue = 0.0f;
    float upperValue = 1.0f;
    if (!FindNeighborSetpointValuesExcludingValue(
        editor.parameters[changedParameterIndex],
        changedParameterValue,
        lowerValue,
        upperValue
    )) {
        return false;
    }

    std::vector<float> lowerCornerValues = cornerValues;
    std::vector<float> upperCornerValues = cornerValues;
    lowerCornerValues[changedOffset] = lowerValue;
    upperCornerValues[changedOffset] = upperValue;

    LayerMesh lowerMesh = BuildSeedMeshForMultilinearCornerValues(
        editor,
        layerIndex,
        parameterIndices,
        lowerCornerValues
    );
    LayerMesh upperMesh = BuildSeedMeshForMultilinearCornerValues(
        editor,
        layerIndex,
        parameterIndices,
        upperCornerValues
    );

    if (!MeshTopologyMatches(lowerMesh, upperMesh)) {
        return false;
    }

    const float t = NearlyEqualSetpointValue(lowerValue, upperValue)
        ? 0.0f
        : std::clamp(
            (changedParameterValue - lowerValue) / (upperValue - lowerValue),
            0.0f,
            1.0f
        );

    mesh = lowerMesh;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        mesh.vertices[i].position.x =
            lowerMesh.vertices[i].position.x +
            (upperMesh.vertices[i].position.x - lowerMesh.vertices[i].position.x) * t;
        mesh.vertices[i].position.y =
            lowerMesh.vertices[i].position.y +
            (upperMesh.vertices[i].position.y - lowerMesh.vertices[i].position.y) * t;
    }

    return true;
}

static void RebuildMultilinearMeshCornersForParameterValue(
    EditorState& editor,
    int layerIndex,
    int changedParameterIndex,
    float changedParameterValue
) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    const std::vector<int> parameterIndices = CollectMeshParametersForLayer(editor, layerIndex);
    if (parameterIndices.size() < 2u || parameterIndices.size() > 12u) {
        return;
    }

    const auto changedIt = std::find(
        parameterIndices.begin(),
        parameterIndices.end(),
        changedParameterIndex
    );
    if (changedIt == parameterIndices.end()) {
        return;
    }

    const std::size_t changedOffset =
        static_cast<std::size_t>(std::distance(parameterIndices.begin(), changedIt));
    std::vector<std::vector<float>> combinations;
    CollectMultilinearCornerValueCombinations(editor, parameterIndices, combinations);
    if (combinations.empty()) {
        return;
    }

    for (std::vector<float>& cornerValues : combinations) {
        if (
            changedOffset >= cornerValues.size() ||
            !NearlyEqualSetpointValue(cornerValues[changedOffset], changedParameterValue)
        ) {
            continue;
        }

        LayerMesh seedMesh;
        if (!TryBuildInsertedParameterSliceCornerMesh(
            editor,
            layerIndex,
            parameterIndices,
            cornerValues,
            changedParameterIndex,
            changedParameterValue,
            seedMesh
        )) {
            seedMesh = BuildAdditiveSeedMeshForMultilinearCornerValues(
                editor,
                layerIndex,
                parameterIndices,
                cornerValues
            );
        }

        for (const int parameterIndex : parameterIndices) {
            DeformParameterLayerState* state =
                FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
            if (state) {
                StoreMeshCornerForState(*state, parameterIndices, cornerValues, seedMesh);
            }
        }
    }
}

static void EnsureCurrentMeshSetpointsForSharedCorner(
    EditorState& editor,
    int layerIndex,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues
) {
    if (parameterIndices.size() != cornerValues.size()) {
        return;
    }

    for (std::size_t i = 0; i < parameterIndices.size(); ++i) {
        const int parameterIndex = parameterIndices[i];
        if (
            parameterIndex < 0 ||
            parameterIndex >= static_cast<int>(editor.parameters.size())
        ) {
            continue;
        }

        DeformParameter& parameter = editor.parameters[parameterIndex];
        DeformParameterLayerState* state = FindParameterLayerState(parameter, layerIndex);
        if (!state) {
            continue;
        }

        EnsureStateParameterLayerSetpoints(parameter, *state);
        if (FindLayerSetpointAtValue(*state, cornerValues[i])) {
            continue;
        }

        DeformParameterLayerSetpoint setpoint = EvaluateLayerSetpoint(*state, cornerValues[i]);
        setpoint.value = cornerValues[i];
        UpsertLayerSetpoint(*state, setpoint);
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

    EnsureCurrentMeshSetpointsForSharedCorner(
        editor,
        layerIndex,
        parameterIndices,
        cornerValues
    );

    for (const int parameterIndex : parameterIndices) {
        DeformParameterLayerState* state =
            FindParameterLayerState(editor.parameters[parameterIndex], layerIndex);
        if (state) {
            StoreMeshCornerForState(*state, parameterIndices, cornerValues, mesh);
        }
    }

    EnsureMultilinearMeshCornersForLayer(editor, layerIndex);
    return true;
}

static void ApplyAllDeformParameters(EditorState& editor) {
    for (DeformParameter& parameter : editor.parameters) {
        EnsureStateParameterDefaults(parameter);
        for (DeformParameterLayerState& state : parameter.layers) {
            EnsureStateParameterLayerSetpoints(parameter, state);
        }
    }

    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        const DeformParameterLayerState* meshBaseState = nullptr;
        const DeformParameterLayerState* opacityBaseState = nullptr;
        const DeformParameterLayerState* renderOrderBaseState = nullptr;
        const DeformParameterLayerState* maskingBaseState = nullptr;
        const DeformParameterLayerState* textureBaseState = nullptr;
        const DeformParameterLayerState* colorBaseState = nullptr;
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
        LayerMesh resultMesh = meshBaseState ? EvaluateLayerSetpoint(*meshBaseState, 0.0f).mesh : layer.mesh;
        int resultTextureIndex = textureBaseState ? EvaluateLayerSetpoint(*textureBaseState, 0.0f).textureIndex : layer.textureIndex;
        float resultOpacity = opacityBaseState ? EvaluateLayerSetpoint(*opacityBaseState, 0.0f).opacity : layer.opacity;
        float resultHueShift = colorBaseState ? EvaluateLayerSetpoint(*colorBaseState, 0.0f).hueShift : layer.hueShift;
        float resultSaturationScale = colorBaseState ? EvaluateLayerSetpoint(*colorBaseState, 0.0f).saturationScale : layer.saturationScale;
        float resultLightnessShift = colorBaseState ? EvaluateLayerSetpoint(*colorBaseState, 0.0f).lightnessShift : layer.lightnessShift;
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

            const float value = ParameterEvaluationValue(parameter);
            if (!usingMultilinearMesh && parameter.affectsMesh && CanAccumulateParameterState(resultMesh, *state)) {
                AccumulateParameterMeshSetpointDelta(resultMesh, *state, value);
            }
            if (parameter.affectsOpacity) {
                resultOpacity +=
                    EvaluateLayerSetpoint(*state, value).opacity -
                    EvaluateLayerSetpoint(*state, 0.0f).opacity;
            }
            if (parameter.affectsTexture) {
                resultTextureIndex = EvaluateLayerSetpoint(*state, value).textureIndex;
            }
            if (parameter.affectsColor) {
                const DeformParameterLayerSetpoint evaluated = EvaluateLayerSetpoint(*state, value);
                const DeformParameterLayerSetpoint base = EvaluateLayerSetpoint(*state, 0.0f);
                resultHueShift += evaluated.hueShift - base.hueShift;
                resultSaturationScale += evaluated.saturationScale - base.saturationScale;
                resultLightnessShift += evaluated.lightnessShift - base.lightnessShift;
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

static void ApplyBaseParameterStateForMeshEditing(EditorState& editor) {
    for (DeformParameter& parameter : editor.parameters) {
        EnsureStateParameterDefaults(parameter);
        for (DeformParameterLayerState& state : parameter.layers) {
            EnsureStateParameterLayerSetpoints(parameter, state);
        }
    }

    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        const DeformParameterLayerState* meshBaseState = nullptr;
        const DeformParameterLayerState* opacityBaseState = nullptr;
        const DeformParameterLayerState* renderOrderBaseState = nullptr;
        const DeformParameterLayerState* maskingBaseState = nullptr;
        const DeformParameterLayerState* textureBaseState = nullptr;
        const DeformParameterLayerState* colorBaseState = nullptr;

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
        if (meshBaseState) {
            layer.mesh = EvaluateLayerSetpoint(*meshBaseState, 0.0f).mesh;
            UpdateLayerBoundsFromMesh(layer);
        }
        if (textureBaseState) {
            ApplyTextureToLayer(
                editor.document,
                layer,
                EvaluateLayerSetpoint(*textureBaseState, 0.0f).textureIndex
            );
        }
        if (opacityBaseState) {
            layer.opacity = std::clamp(EvaluateLayerSetpoint(*opacityBaseState, 0.0f).opacity, 0.0f, 1.0f);
        }
        if (colorBaseState) {
            const DeformParameterLayerSetpoint color = EvaluateLayerSetpoint(*colorBaseState, 0.0f);
            layer.hueShift = std::clamp(color.hueShift, -100.0f, 100.0f);
            layer.saturationScale = std::clamp(color.saturationScale, 0.0f, 200.0f);
            layer.lightnessShift = std::clamp(color.lightnessShift, -100.0f, 100.0f);
        }
        if (renderOrderBaseState) {
            layer.renderOrderOverride = EvaluateLayerSetpoint(*renderOrderBaseState, 0.0f).renderOrderOverride;
        }
        if (maskingBaseState) {
            layer.maskLayerIndices = EvaluateLayerSetpoint(*maskingBaseState, 0.0f).maskLayerIndices;
        }
    }
}

static void CaptureMeshModeGuideMeshes(EditorState& editor) {
    editor.meshModeGuideMeshes.clear();
    editor.meshModeGuideMeshes.reserve(editor.document.layers.size());
    for (const EditorLayer& layer : editor.document.layers) {
        editor.meshModeGuideMeshes.push_back(layer.mesh);
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
        if (!otherParameter.affectsMesh || !otherState) {
            continue;
        }

        const DeformParameterLayerSetpoint baseSetpoint = EvaluateLayerSetpoint(*otherState, 0.0f);
        const DeformParameterLayerSetpoint evaluatedSetpoint =
            EvaluateLayerSetpoint(*otherState, ParameterEvaluationValue(otherParameter));
        if (
            !MeshTopologyMatches(endpointMesh, baseSetpoint.mesh) ||
            !MeshTopologyMatches(endpointMesh, evaluatedSetpoint.mesh)
        ) {
            continue;
        }

        for (std::size_t i = 0; i < endpointMesh.vertices.size(); ++i) {
            endpointMesh.vertices[i].position.x -=
                evaluatedSetpoint.mesh.vertices[i].position.x - baseSetpoint.mesh.vertices[i].position.x;
            endpointMesh.vertices[i].position.y -=
                evaluatedSetpoint.mesh.vertices[i].position.y - baseSetpoint.mesh.vertices[i].position.y;
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
    state.textureIndexAt0 = layer.textureIndex;
    state.textureIndexAt1 = layer.textureIndex;
    state.opacityAt0 = layer.opacity;
    state.opacityAt1 = layer.opacity;
    state.hueShiftAt0 = layer.hueShift;
    state.hueShiftAt1 = layer.hueShift;
    state.saturationScaleAt0 = layer.saturationScale;
    state.saturationScaleAt1 = layer.saturationScale;
    state.lightnessShiftAt0 = layer.lightnessShift;
    state.lightnessShiftAt1 = layer.lightnessShift;
    state.renderOrderOverrideAt0 = layer.renderOrderOverride;
    state.renderOrderOverrideAt1 = layer.renderOrderOverride;
    state.maskLayerIndicesAt0 = layer.maskLayerIndices;
    state.maskLayerIndicesAt1 = layer.maskLayerIndices;
    EnsureParameterLayerSetpoints(state);
    return state;
}

static void AddDeformParameter(EditorState& editor, DeformParameterType type = DeformParameterType::Slider) {
    if (editor.selectedLayers.empty()) {
        return;
    }

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedParameterBefore = editor.selectedParameter;

    DeformParameter parameter;
    parameter.name = "Parameter " + std::to_string(editor.parameters.size() + 1u);
    parameter.type = type;
    parameter.value = 0.0f;
    parameter.affectsMesh = true;
    parameter.affectsRenderOrder = false;
    parameter.affectsMasking = false;
    parameter.affectsOpacity = false;
    parameter.affectsTexture = false;
    parameter.affectsColor = false;
    if (type == DeformParameterType::State) {
        parameter.stateNames.push_back("Default");
        parameter.selectedState = 0;
    }

    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        DeformParameterLayerState state = CreateParameterLayerStateForLayer(
            editor.document.layers[layerIndex],
            layerIndex
        );
        if (type == DeformParameterType::State) {
            state.setpoints.clear();
            state.setpoints.push_back(MakeStateSetpointFromLayer(editor.document.layers[layerIndex], 0));
        }
        parameter.layers.push_back(std::move(state));
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

static void AddStateToParameter(EditorState& editor, int parameterIndex, const std::string& stateName) {
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size()) ||
        !IsStateParameter(editor.parameters[parameterIndex])
    ) {
        return;
    }

    const std::vector<DeformParameter> before = editor.parameters;
    const int selectedBefore = editor.selectedParameter;
    DeformParameter& parameter = editor.parameters[parameterIndex];
    const int stateIndex = static_cast<int>(parameter.stateNames.size());
    parameter.stateNames.push_back(stateName.empty() ? "State " + std::to_string(stateIndex + 1) : stateName);

    for (DeformParameterLayerState& state : parameter.layers) {
        if (!IsValidLayerIndex(editor, state.layerIndex)) {
            continue;
        }
        state.setpoints.push_back(MakeStateSetpointFromLayer(
            editor.document.layers[state.layerIndex],
            stateIndex
        ));
        SortParameterSetpoints(state);
    }

    parameter.selectedState = stateIndex;
    parameter.value = static_cast<float>(stateIndex);
    editor.selectedParameter = parameterIndex;
    PushParameterOperation(
        editor,
        before,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Add parameter state"
    );
    ApplyAllDeformParameters(editor);
}

static void RenameParameterState(
    EditorState& editor,
    int parameterIndex,
    int stateIndex,
    const std::string& name
) {
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size()) ||
        !IsStateParameter(editor.parameters[parameterIndex])
    ) {
        return;
    }

    DeformParameter& parameter = editor.parameters[parameterIndex];
    if (stateIndex < 0 || stateIndex >= static_cast<int>(parameter.stateNames.size()) || parameter.stateNames[stateIndex] == name) {
        return;
    }

    const std::vector<DeformParameter> before = editor.parameters;
    const int selectedBefore = editor.selectedParameter;
    parameter.stateNames[stateIndex] = name.empty() ? "State " + std::to_string(stateIndex + 1) : name;
    PushParameterOperation(
        editor,
        before,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Rename parameter state"
    );
}

static void DeleteParameterState(EditorState& editor, int parameterIndex, int stateIndex) {
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size()) ||
        !IsStateParameter(editor.parameters[parameterIndex])
    ) {
        return;
    }

    DeformParameter& parameter = editor.parameters[parameterIndex];
    if (parameter.stateNames.size() <= 1u || stateIndex < 0 || stateIndex >= static_cast<int>(parameter.stateNames.size())) {
        return;
    }

    const std::vector<DeformParameter> before = editor.parameters;
    const int selectedBefore = editor.selectedParameter;
    parameter.stateNames.erase(parameter.stateNames.begin() + stateIndex);
    for (DeformParameterLayerState& state : parameter.layers) {
        state.setpoints.erase(
            std::remove_if(
                state.setpoints.begin(),
                state.setpoints.end(),
                [&](const DeformParameterLayerSetpoint& setpoint) {
                    return NearlyEqualSetpointValue(setpoint.value, static_cast<float>(stateIndex));
                }
            ),
            state.setpoints.end()
        );
        for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
            if (setpoint.value > static_cast<float>(stateIndex)) {
                setpoint.value -= 1.0f;
            }
        }
        EnsureStateParameterLayerSetpoints(parameter, state);
    }
    parameter.selectedState = std::clamp(
        parameter.selectedState >= stateIndex ? parameter.selectedState - 1 : parameter.selectedState,
        0,
        static_cast<int>(parameter.stateNames.size()) - 1
    );
    parameter.value = static_cast<float>(parameter.selectedState);
    editor.selectedParameter = parameterIndex;
    PushParameterOperation(
        editor,
        before,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Delete parameter state"
    );
    ApplyAllDeformParameters(editor);
}

static void MoveParameterState(EditorState& editor, int parameterIndex, int stateIndex, int direction) {
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size()) ||
        !IsStateParameter(editor.parameters[parameterIndex])
    ) {
        return;
    }

    DeformParameter& parameter = editor.parameters[parameterIndex];
    const int targetIndex = stateIndex + direction;
    if (
        stateIndex < 0 ||
        targetIndex < 0 ||
        stateIndex >= static_cast<int>(parameter.stateNames.size()) ||
        targetIndex >= static_cast<int>(parameter.stateNames.size())
    ) {
        return;
    }

    const std::vector<DeformParameter> before = editor.parameters;
    const int selectedBefore = editor.selectedParameter;

    std::swap(parameter.stateNames[stateIndex], parameter.stateNames[targetIndex]);
    for (DeformParameterLayerState& state : parameter.layers) {
        for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
            if (NearlyEqualSetpointValue(setpoint.value, static_cast<float>(stateIndex))) {
                setpoint.value = static_cast<float>(targetIndex);
            } else if (NearlyEqualSetpointValue(setpoint.value, static_cast<float>(targetIndex))) {
                setpoint.value = static_cast<float>(stateIndex);
            }
        }
        SortParameterSetpoints(state);
    }

    if (parameter.selectedState == stateIndex) {
        parameter.selectedState = targetIndex;
    } else if (parameter.selectedState == targetIndex) {
        parameter.selectedState = stateIndex;
    }
    parameter.value = static_cast<float>(parameter.selectedState);
    editor.selectedParameter = parameterIndex;

    PushParameterOperation(
        editor,
        before,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Reorder parameter states"
    );
    ApplyAllDeformParameters(editor);
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
        DeformParameterLayerState state = CreateParameterLayerStateForLayer(
            editor.document.layers[layerIndex],
            layerIndex
        );
        if (IsStateParameter(parameter)) {
            state.setpoints.clear();
            const int stateCount = std::max(1, static_cast<int>(parameter.stateNames.size()));
            for (int stateIndex = 0; stateIndex < stateCount; ++stateIndex) {
                state.setpoints.push_back(MakeStateSetpointFromLayer(
                    editor.document.layers[layerIndex],
                    stateIndex
                ));
            }
        }
        parameter.layers.push_back(std::move(state));
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
    std::unordered_set<int> editableLayerSet(layerIndices.begin(), layerIndices.end());
    const bool restrictToLayerSet = !editableLayerSet.empty();

    const float setpointValue = ParameterEvaluationValue(parameter);
    constexpr float existingSetpointSnapRadius = 0.05f;
    float effectiveSetpointValue = setpointValue;
    if (!IsStateParameter(parameter) &&
        FindNearbyParameterSetpointValue(
            parameter,
            setpointValue,
            existingSetpointSnapRadius,
            effectiveSetpointValue
        ) &&
        !NearlyEqualSetpointValue(effectiveSetpointValue, setpointValue)
    ) {
        parameter.value = parameter.value + (effectiveSetpointValue - parameter.value) * 0.35f;
        if (std::abs(parameter.value - effectiveSetpointValue) <= 0.002f) {
            parameter.value = effectiveSetpointValue;
        }
        editor.statusText = "Editing nearby parameter setpoint " + std::to_string(effectiveSetpointValue) + ".";
    }

    const bool updateZero = !IsStateParameter(parameter) && effectiveSetpointValue <= 0.001f;
    const bool updateOne = !IsStateParameter(parameter) && effectiveSetpointValue >= 0.999f;
    bool seedMissingCustomSetpoints =
        !IsStateParameter(parameter) &&
        !updateZero &&
        !updateOne;
    if (seedMissingCustomSetpoints) {
        seedMissingCustomSetpoints = std::any_of(
            parameter.layers.begin(),
            parameter.layers.end(),
            [&](const DeformParameterLayerState& state) {
                return !FindLayerSetpointAtValue(state, effectiveSetpointValue);
            }
        );
    }

    if (seedMissingCustomSetpoints) {
        for (DeformParameterLayerState& state : parameter.layers) {
            if (!IsValidLayerIndex(editor, state.layerIndex)) {
                continue;
            }

            EnsureStateParameterLayerSetpoints(parameter, state);
            if (FindLayerSetpointAtValue(state, effectiveSetpointValue)) {
                continue;
            }

            DeformParameterLayerSetpoint seed = EvaluateLayerSetpoint(state, effectiveSetpointValue);
            seed.value = effectiveSetpointValue;
            if (parameter.affectsMesh) {
                seed.mesh = MeshWithoutOtherParameterDeltas(
                    editor,
                    editor.selectedParameter,
                    state.layerIndex,
                    editor.document.layers[state.layerIndex].mesh
                );
            }
            if (parameter.affectsOpacity) {
                seed.opacity = OpacityWithoutOtherParameterDeltas(
                    editor,
                    editor.selectedParameter,
                    state.layerIndex,
                    editor.document.layers[state.layerIndex].opacity
                );
            }
            if (parameter.affectsTexture) {
                seed.textureIndex = editor.document.layers[state.layerIndex].textureIndex;
            }
            if (parameter.affectsColor) {
                seed.hueShift = editor.document.layers[state.layerIndex].hueShift;
                seed.saturationScale = editor.document.layers[state.layerIndex].saturationScale;
                seed.lightnessShift = editor.document.layers[state.layerIndex].lightnessShift;
            }
            if (parameter.affectsRenderOrder) {
                seed.renderOrderOverride = editor.document.layers[state.layerIndex].renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                seed.maskLayerIndices = editor.document.layers[state.layerIndex].maskLayerIndices;
            }
            UpsertLayerSetpoint(state, seed);
        }

        if (parameter.affectsMesh) {
            for (const DeformParameterLayerState& state : parameter.layers) {
                RebuildMultilinearMeshCornersForParameterValue(
                    editor,
                    state.layerIndex,
                    editor.selectedParameter,
                    effectiveSetpointValue
                );
                StoreCurrentMeshCornerForLayer(
                    editor,
                    state.layerIndex,
                    editor.document.layers[state.layerIndex].mesh
                );
                EnsureMultilinearMeshCornersForLayer(editor, state.layerIndex);
            }
        }
    }

    for (DeformParameterLayerState& parameterLayerState : parameter.layers) {
        const int layerIndex = parameterLayerState.layerIndex;
        if (restrictToLayerSet && !editableLayerSet.contains(layerIndex)) {
            continue;
        }
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        DeformParameterLayerState* state = &parameterLayerState;
        EnsureStateParameterLayerSetpoints(parameter, *state);

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

        DeformParameterLayerSetpoint setpoint = EvaluateLayerSetpoint(*state, effectiveSetpointValue);
        setpoint.value = effectiveSetpointValue;
        if (parameter.affectsMesh && !storedMultilinearMeshCorner) {
            setpoint.mesh = endpointMesh;
        }
        if (parameter.affectsTexture) {
            setpoint.textureIndex = layer.textureIndex;
        }
        if (parameter.affectsOpacity) {
            setpoint.opacity = endpointOpacity;
        }
        if (parameter.affectsColor) {
            setpoint.hueShift = layer.hueShift;
            setpoint.saturationScale = layer.saturationScale;
            setpoint.lightnessShift = layer.lightnessShift;
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
            if (parameter.affectsTexture) {
                state->textureIndexAt0 = layer.textureIndex;
            }
            if (parameter.affectsOpacity) {
                state->opacityAt0 = endpointOpacity;
            }
            if (parameter.affectsColor) {
                state->hueShiftAt0 = layer.hueShift;
                state->saturationScaleAt0 = layer.saturationScale;
                state->lightnessShiftAt0 = layer.lightnessShift;
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
            if (parameter.affectsTexture) {
                state->textureIndexAt1 = layer.textureIndex;
            }
            if (parameter.affectsOpacity) {
                state->opacityAt1 = endpointOpacity;
            }
            if (parameter.affectsColor) {
                state->hueShiftAt1 = layer.hueShift;
                state->saturationScaleAt1 = layer.saturationScale;
                state->lightnessShiftAt1 = layer.lightnessShift;
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
    EditorState& editor,
    int parameterIndex,
    const std::vector<float>& setpoints,
    float& value,
    bool allowSetpointEditing = true,
    bool allowSetpointSnap = true
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
        const bool selected =
            editor.selectedParameterSetpointParameter == parameterIndex &&
            NearlyEqualSetpointValue(editor.selectedParameterSetpointValue, setpoint);
        const float x = sliderMin.x + sliderWidth * setpoint;
        drawList->AddLine(
            ImVec2(x, sliderMin.y + 2.0f),
            ImVec2(x, sliderMax.y - 2.0f),
            markerShadow,
            selected ? 6.0f : 3.0f
        );
        drawList->AddLine(
            ImVec2(x, sliderMin.y + 2.0f),
            ImVec2(x, sliderMax.y - 2.0f),
            markerColor,
            selected ? 3.5f : 1.5f
        );
    }

    const ImGuiIO& io = ImGui::GetIO();
    const float snapRadius = 10.0f;

    auto mouseIsNearSlider = [&]() {
        return
            io.MousePos.y >= sliderMin.y - snapRadius &&
            io.MousePos.y <= sliderMax.y + snapRadius;
    };

    if (
        allowSetpointEditing &&
        editor.selectedParameterSetpointParameter == parameterIndex &&
        editor.pendingParameterSetpointDrag &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right)
    ) {
        const float dragDistance = std::abs(io.MousePos.x - editor.parameterSetpointDragStartMouseX);
        const double dragAge = ImGui::GetTime() - editor.parameterSetpointDragStartTime;
        constexpr float dragDistanceThresholdPx = 4.0f;
        constexpr double dragDelaySeconds = 0.12;
        if (dragDistance >= dragDistanceThresholdPx && dragAge >= dragDelaySeconds) {
            editor.pendingParameterSetpointDrag = false;
            editor.draggingParameterSetpoint = true;
        }
    }

    if (
        allowSetpointEditing &&
        editor.selectedParameterSetpointParameter == parameterIndex &&
        editor.draggingParameterSetpoint &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right)
    ) {
        DeformParameter& parameter = editor.parameters[parameterIndex];
        const float oldValue = editor.selectedParameterSetpointValue;
        if (!IsEndpointSetpointValue(oldValue)) {
            const float requested = std::clamp(
                editor.parameterSetpointDragStartValue +
                    (io.MousePos.x - editor.parameterSetpointDragStartMouseX) / sliderWidth,
                0.0f,
                1.0f
            );
            const float moved = ResolveDraggedSetpointValue(parameter, oldValue, requested);
            MoveParameterSetpointValue(parameter, parameterIndex, oldValue, moved);
            editor.selectedParameterSetpointValue = moved;
            value = moved;
            return true;
        }
    }

    if (
        allowSetpointEditing &&
        editor.selectedParameterSetpointParameter == parameterIndex &&
        (editor.draggingParameterSetpoint || editor.pendingParameterSetpointDrag) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)
    ) {
        if (editor.draggingParameterSetpoint) {
            PushParameterOperation(
                editor,
                editor.parameterSetpointDragBefore,
                editor.parameterSetpointDragSelectedBefore,
                editor.parameters,
                editor.selectedParameter,
                "Move parameter setpoint"
            );
        }
        editor.draggingParameterSetpoint = false;
        editor.pendingParameterSetpointDrag = false;
        editor.parameterSetpointDragStartMouseX = 0.0f;
        editor.parameterSetpointDragStartValue = 0.0f;
        editor.parameterSetpointDragStartTime = 0.0;
        editor.parameterSetpointDragBefore.clear();
        editor.parameterSetpointDragSelectedBefore = -1;
    }

    if (
        allowSetpointSnap &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        mouseIsNearSlider()
    ) {
        bool foundSetpoint = false;
        float nearestSetpoint = 0.0f;
        float nearestDistance = snapRadius;
        for (const float setpoint : setpoints) {
            const float x = sliderMin.x + sliderWidth * setpoint;
            const float distance = std::abs(io.MousePos.x - x);
            if (distance <= nearestDistance) {
                nearestDistance = distance;
                nearestSetpoint = setpoint;
                foundSetpoint = true;
            }
        }

        if (foundSetpoint) {
            editor.selectedParameter = parameterIndex;
            if (allowSetpointEditing) {
                ClearAllVertexSelection(editor);
                editor.selectedLayers.clear();
                editor.selectedLayer = -1;
                editor.selectedParameterSetpointParameter = parameterIndex;
                editor.selectedParameterSetpointValue = nearestSetpoint;
                editor.draggingParameterSetpoint = false;
                editor.pendingParameterSetpointDrag = !IsEndpointSetpointValue(nearestSetpoint);
                if (editor.pendingParameterSetpointDrag) {
                    editor.parameterSetpointDragStartMouseX = io.MousePos.x;
                    editor.parameterSetpointDragStartValue = nearestSetpoint;
                    editor.parameterSetpointDragStartTime = ImGui::GetTime();
                    editor.parameterSetpointDragBefore = editor.parameters;
                    editor.parameterSetpointDragSelectedBefore = editor.selectedParameter;
                }
            } else {
                ClearParameterSetpointSelection(editor);
            }
            value = nearestSetpoint;
            return true;
        }
    }

    return false;
}

static bool DeleteSelectedParameterSetpoint(EditorState& editor) {
    const int parameterIndex = editor.selectedParameterSetpointParameter;
    if (
        parameterIndex < 0 ||
        parameterIndex >= static_cast<int>(editor.parameters.size()) ||
        IsEndpointSetpointValue(editor.selectedParameterSetpointValue)
    ) {
        return false;
    }

    DeformParameter& parameter = editor.parameters[parameterIndex];
    if (!ParameterHasSetpointValue(parameter, editor.selectedParameterSetpointValue)) {
        ClearParameterSetpointSelection(editor);
        return false;
    }

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedBefore = editor.selectedParameter;
    DeleteParameterSetpointValue(parameter, parameterIndex, editor.selectedParameterSetpointValue);
    ClearParameterSetpointSelection(editor);
    PushParameterOperation(
        editor,
        parametersBefore,
        selectedBefore,
        editor.parameters,
        editor.selectedParameter,
        "Delete parameter setpoint"
    );
    ApplyAllDeformParameters(editor);
    editor.statusText = "Deleted parameter setpoint.";
    return true;
}

static void DrawAlignMeshesPopup(EditorState& editor) {
    if (!ImGui::BeginPopupModal("Align Meshes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    InitializeAlignMeshesDefaults(editor);

    auto drawLayerCombo = [&](const char* label, int& layerIndex) {
        const char* preview = IsValidLayerIndex(editor, layerIndex)
            ? editor.document.layers[layerIndex].name.c_str()
            : "None";
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
                const bool selectedLayer = layerIndex == i;
                if (ImGui::Selectable(editor.document.layers[i].name.c_str(), selectedLayer)) {
                    layerIndex = i;
                }
                if (selectedLayer) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    };

    drawLayerCombo("Mesh To Transform", editor.alignMeshesTargetLayer);
    drawLayerCombo("Align To Mesh", editor.alignMeshesSourceLayer);

    const char* parameterPreview =
        editor.alignMeshesParameter >= 0 &&
        editor.alignMeshesParameter < static_cast<int>(editor.parameters.size())
            ? editor.parameters[editor.alignMeshesParameter].name.c_str()
            : "None";
    if (ImGui::BeginCombo("Parameter", parameterPreview)) {
        for (int i = 0; i < static_cast<int>(editor.parameters.size()); ++i) {
            const bool selectedParameter = editor.alignMeshesParameter == i;
            if (ImGui::Selectable(editor.parameters[i].name.c_str(), selectedParameter)) {
                editor.alignMeshesParameter = i;
                editor.alignMeshesStartSetpoint = std::clamp(
                    editor.alignMeshesStartSetpoint,
                    0.0f,
                    1.0f
                );
            }
            if (selectedParameter) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (
        editor.alignMeshesParameter >= 0 &&
        editor.alignMeshesParameter < static_cast<int>(editor.parameters.size())
    ) {
        const std::vector<float> setpoints =
            CollectParameterSetpointValues(editor.parameters[editor.alignMeshesParameter]);
        std::string setpointPreview = std::to_string(editor.alignMeshesStartSetpoint);
        if (ImGui::BeginCombo("Starting Setpoint", setpointPreview.c_str())) {
            for (const float setpoint : setpoints) {
                char label[32] = {};
                std::snprintf(label, sizeof(label), "%.2f", setpoint);
                const bool selectedSetpoint =
                    NearlyEqualSetpointValue(editor.alignMeshesStartSetpoint, setpoint);
                if (ImGui::Selectable(label, selectedSetpoint)) {
                    editor.alignMeshesStartSetpoint = setpoint;
                }
                if (selectedSetpoint) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    const bool validAlign =
        IsValidLayerIndex(editor, editor.alignMeshesTargetLayer) &&
        IsValidLayerIndex(editor, editor.alignMeshesSourceLayer) &&
        editor.alignMeshesTargetLayer != editor.alignMeshesSourceLayer &&
        editor.alignMeshesParameter >= 0 &&
        editor.alignMeshesParameter < static_cast<int>(editor.parameters.size()) &&
        editor.parameters[editor.alignMeshesParameter].affectsMesh &&
        FindParameterLayerState(
            editor.parameters[editor.alignMeshesParameter],
            editor.alignMeshesTargetLayer
        ) &&
        FindParameterLayerState(
            editor.parameters[editor.alignMeshesParameter],
            editor.alignMeshesSourceLayer
        );
    if (!validAlign) {
        ImGui::TextDisabled("Both layers must be different and assigned to a mesh-editing parameter.");
    }

    if (!validAlign) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("OK")) {
        if (AlignMeshesForParameter(editor)) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (!validAlign) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

static void DrawDeformParametersPanel(EditorState& editor) {
    ImGui::Begin("Parameters");

    const bool parametersFrozen = editor.mode == EditorMode::Mesh;
    const bool parameterEditingLocked =
        editor.mode == EditorMode::View ||
        editor.mode == EditorMode::Mesh;
    const bool parameterSelectionLocked = parameterEditingLocked;

    if (editor.selectedLayers.empty() || parameterEditingLocked) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Add")) {
        ImGui::OpenPopup("Add Parameter Type");
    }
    if (editor.selectedLayers.empty() || parameterEditingLocked) {
        ImGui::EndDisabled();
    }

    if (!parameterEditingLocked && ImGui::BeginPopupModal("Add Parameter Type", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Button("Slider", ImVec2(160.0f, 0.0f))) {
            AddDeformParameter(editor, DeformParameterType::Slider);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("State", ImVec2(160.0f, 0.0f))) {
            AddDeformParameter(editor, DeformParameterType::State);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Cancel", ImVec2(160.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    const bool canDelete =
        !parameterEditingLocked &&
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
        const bool displaySelected = selected && !parameterSelectionLocked;
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const ImGuiSelectableFlags parameterRowFlags =
            parameterSelectionLocked ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None;
        if (
            ImGui::Selectable(
                "##parameter_row",
                displaySelected,
                parameterRowFlags,
                ImVec2(rowWidth, rowHeight)
            )
        ) {
            if (editor.selectedParameterSetpointParameter != i || parameterEditingLocked) {
                ClearParameterSetpointSelection(editor);
            }
            editor.selectedParameter = i;
        }

        ImDrawList* rowDrawList = ImGui::GetWindowDrawList();
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        const std::string layerCount =
            std::to_string(parameter.layers.size()) +
            (parameter.layers.size() == 1u ? " layer" : " layers");
        const ImVec2 countSize = ImGui::CalcTextSize(layerCount.c_str());
        rowDrawList->AddText(
            ImVec2(rowStart.x, rowStart.y + ImGui::GetStyle().FramePadding.y),
            textColor,
            parameter.name.c_str()
        );
        rowDrawList->AddText(
            ImVec2(
                rowStart.x + std::max(0.0f, rowWidth - countSize.x),
                rowStart.y + ImGui::GetStyle().FramePadding.y
            ),
            textColor,
            layerCount.c_str()
        );

        if (!parameterEditingLocked && ImGui::BeginPopupContextItem("parameter_options")) {
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
            ImGui::Text("Type: %s", IsStateParameter(parameter) ? "State" : "Slider");
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

            bool affectsTexture = parameter.affectsTexture;
            if (ImGui::Checkbox("Textures", &affectsTexture)) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.affectsTexture = affectsTexture;
                for (DeformParameterLayerState& state : parameter.layers) {
                    if (!IsValidLayerIndex(editor, state.layerIndex)) {
                        continue;
                    }

                    const int neutralTexture = editor.document.layers[state.layerIndex].textureIndex;
                    state.textureIndexAt0 = neutralTexture;
                    state.textureIndexAt1 = neutralTexture;
                    for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                        setpoint.textureIndex = neutralTexture;
                    }
                }
                PushParameterChannelOperation(editor, before, selectedBefore, "Change parameter channels");
            }

            bool affectsColor = parameter.affectsColor;
            if (ImGui::Checkbox("Color", &affectsColor)) {
                const std::vector<DeformParameter> before = editor.parameters;
                const int selectedBefore = editor.selectedParameter;
                parameter.affectsColor = affectsColor;
                for (DeformParameterLayerState& state : parameter.layers) {
                    if (!IsValidLayerIndex(editor, state.layerIndex)) {
                        continue;
                    }

                    const EditorLayer& layer = editor.document.layers[state.layerIndex];
                    state.hueShiftAt0 = layer.hueShift;
                    state.hueShiftAt1 = layer.hueShift;
                    state.saturationScaleAt0 = layer.saturationScale;
                    state.saturationScaleAt1 = layer.saturationScale;
                    state.lightnessShiftAt0 = layer.lightnessShift;
                    state.lightnessShiftAt1 = layer.lightnessShift;
                    for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                        setpoint.hueShift = layer.hueShift;
                        setpoint.saturationScale = layer.saturationScale;
                        setpoint.lightnessShift = layer.lightnessShift;
                    }
                }
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

        if (parametersFrozen) {
            ImGui::BeginDisabled();
        }

        if (IsStateParameter(parameter)) {
            EnsureStateParameterDefaults(parameter);
            const char* preview = parameter.stateNames[static_cast<std::size_t>(parameter.selectedState)].c_str();
            bool requestAddParameterState = false;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##state", preview)) {
                for (int stateIndex = 0; stateIndex < static_cast<int>(parameter.stateNames.size()); ++stateIndex) {
                    ImGui::PushID(stateIndex);
                    const bool stateSelected = stateIndex == parameter.selectedState;
                    const bool canMoveUp = !parameterEditingLocked && stateIndex > 0;
                    const bool canMoveDown =
                        !parameterEditingLocked &&
                        stateIndex + 1 < static_cast<int>(parameter.stateNames.size());
                    if (!canMoveUp) {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::SmallButton("^")) {
                        MoveParameterState(editor, i, stateIndex, -1);
                        ImGui::PopID();
                        break;
                    }
                    if (!canMoveUp) {
                        ImGui::EndDisabled();
                    }
                    ImGui::SameLine();
                    if (!canMoveDown) {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::SmallButton("v")) {
                        MoveParameterState(editor, i, stateIndex, 1);
                        ImGui::PopID();
                        break;
                    }
                    if (!canMoveDown) {
                        ImGui::EndDisabled();
                    }
                    ImGui::SameLine();
                    if (ImGui::Selectable(parameter.stateNames[stateIndex].c_str(), stateSelected)) {
                        parameter.selectedState = stateIndex;
                        parameter.value = static_cast<float>(stateIndex);
                        editor.selectedParameter = i;
                        ClearParameterSetpointSelection(editor);
                        ApplyAllDeformParameters(editor);
                    }
                    if (!parameterEditingLocked && ImGui::BeginPopupContextItem("state_options")) {
                        char stateNameBuffer[128] = {};
                        std::strncpy(stateNameBuffer, parameter.stateNames[stateIndex].c_str(), sizeof(stateNameBuffer) - 1u);
                        ImGui::SetNextItemWidth(220.0f);
                        ImGui::InputText("Name", stateNameBuffer, sizeof(stateNameBuffer));
                        if (ImGui::IsItemDeactivatedAfterEdit() && parameter.stateNames[stateIndex] != stateNameBuffer) {
                            RenameParameterState(editor, i, stateIndex, stateNameBuffer);
                        }
                        if (parameter.stateNames.size() <= 1u) {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::Button("Delete State")) {
                            DeleteParameterState(editor, i, stateIndex);
                            ImGui::CloseCurrentPopup();
                        }
                        if (parameter.stateNames.size() <= 1u) {
                            ImGui::EndDisabled();
                        }
                        ImGui::EndPopup();
                    }
                    if (stateSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }

                if (!parameterEditingLocked && ImGui::Selectable("Add Item +", false)) {
                    editor.addParameterStateTarget = i;
                    std::snprintf(
                        editor.addParameterStateName,
                        sizeof(editor.addParameterStateName),
                        "State %d",
                        static_cast<int>(parameter.stateNames.size()) + 1
                    );
                    requestAddParameterState = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndCombo();
            }
            if (requestAddParameterState) {
                ImGui::OpenPopup("Add Parameter State");
            }
            if (!parameterEditingLocked && ImGui::BeginPopupModal("Add Parameter State", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::InputText("Name", editor.addParameterStateName, sizeof(editor.addParameterStateName));
                if (ImGui::Button("OK")) {
                    AddStateToParameter(editor, editor.addParameterStateTarget, editor.addParameterStateName);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::SetNextItemWidth(-1.0f);
            float value = parameter.value;
            const bool sliderChanged = ImGui::SliderFloat("##value", &value, 0.0f, 1.0f, "%.2f");
            if (
                ImGui::IsItemActive() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                editor.selectedParameterSetpointParameter >= 0
            ) {
                ClearParameterSetpointSelection(editor);
            }
            const bool snappedToSetpoint = DrawParameterSetpointMarkersAndHandleSnap(
                editor,
                i,
                CollectParameterSetpointValues(parameter),
                value,
                !parameterEditingLocked,
                editor.mode != EditorMode::Mesh
            );
            const bool setpointSelectedForThisParameter =
                editor.selectedParameterSetpointParameter == i &&
                ParameterHasSetpointValue(parameter, editor.selectedParameterSetpointValue);
            if (setpointSelectedForThisParameter) {
                value = editor.selectedParameterSetpointValue;
            } else if (editor.selectedParameterSetpointParameter == i) {
                ClearParameterSetpointSelection(editor);
            }

            if (sliderChanged || snappedToSetpoint || setpointSelectedForThisParameter) {
                parameter.value = value;
                editor.selectedParameter = i;
                ApplyAllDeformParameters(editor);
            }
        }

        if (parametersFrozen) {
            ImGui::EndDisabled();
        }

        ImGui::PopID();
    }

    ImGui::End();
}
