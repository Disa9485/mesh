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

static LayerMesh CreateInitialQuadMeshForTexture(const EditorTexture& texture) {
    LayerMesh mesh;
    if (texture.width <= 0 || texture.height <= 0 || texture.alpha.empty()) {
        return mesh;
    }

    int minX = texture.width;
    int minY = texture.height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                static_cast<std::size_t>(x);
            if (index < texture.alpha.size() && texture.alpha[index] > kOpaquePixelAlphaThreshold) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return mesh;
    }

    const float x0 = static_cast<float>(minX);
    const float y0 = static_cast<float>(minY);
    const float x1 = static_cast<float>(maxX + 1);
    const float y1 = static_cast<float>(maxY + 1);
    const float w = static_cast<float>(std::max(1, texture.width));
    const float h = static_cast<float>(std::max(1, texture.height));

    mesh.vertices = {
        {{x0, y0}, {x0 / w, y0 / h}},
        {{x1, y0}, {x1 / w, y0 / h}},
        {{x1, y1}, {x1 / w, y1 / h}},
        {{x0, y1}, {x0 / w, y1 / h}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.edges = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}};
    return mesh;
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
        if (!otherParameter.affectsOpacity) {
            continue;
        }

        const float value = std::clamp(otherParameter.value, 0.0f, 1.0f);
        for (const DeformParameterLayerState& otherState : otherParameter.layers) {
            if (otherState.layerIndex == layerIndex) {
                endpointOpacity -= (otherState.opacityAt1 - otherState.opacityAt0) * value;
                break;
            }
        }
    }
    endpointOpacity = std::clamp(endpointOpacity, 0.0f, 1.0f);

    if (parameter.type == DeformParameterType::State) {
        const float stateValue = static_cast<float>(std::max(0, parameter.selectedState));
        for (DeformParameterLayerState& state : parameter.layers) {
            if (state.layerIndex != layerIndex) {
                continue;
            }

            DeformParameterLayerSetpoint* targetSetpoint = nullptr;
            for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                if (std::abs(setpoint.value - stateValue) <= 0.001f) {
                    targetSetpoint = &setpoint;
                    break;
                }
            }
            if (!targetSetpoint) {
                DeformParameterLayerSetpoint setpoint;
                setpoint.value = stateValue;
                setpoint.mesh = editor.document.layers[layerIndex].mesh;
                setpoint.textureIndex = editor.document.layers[layerIndex].textureIndex;
                setpoint.opacity = endpointOpacity;
                setpoint.renderOrderOverride = editor.document.layers[layerIndex].renderOrderOverride;
                setpoint.maskLayerIndices = editor.document.layers[layerIndex].maskLayerIndices;
                state.setpoints.push_back(std::move(setpoint));
                targetSetpoint = &state.setpoints.back();
            }

            const EditorLayer& layer = editor.document.layers[layerIndex];
            if (parameter.affectsOpacity) {
                targetSetpoint->opacity = endpointOpacity;
            }
            if (parameter.affectsColor) {
                targetSetpoint->hueShift = layer.hueShift;
                targetSetpoint->saturationScale = layer.saturationScale;
                targetSetpoint->lightnessShift = layer.lightnessShift;
            }
            if (parameter.affectsRenderOrder) {
                targetSetpoint->renderOrderOverride = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                targetSetpoint->maskLayerIndices = layer.maskLayerIndices;
            }
            if (parameter.affectsTexture) {
                targetSetpoint->textureIndex = layer.textureIndex;
            }
        }
        return;
    }

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
            if (parameter.affectsColor) {
                state.hueShiftAt0 = layer.hueShift;
                state.saturationScaleAt0 = layer.saturationScale;
                state.lightnessShiftAt0 = layer.lightnessShift;
            }
            if (parameter.affectsRenderOrder) {
                state.renderOrderOverrideAt0 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state.maskLayerIndicesAt0 = layer.maskLayerIndices;
            }
            if (parameter.affectsTexture) {
                state.textureIndexAt0 = layer.textureIndex;
            }
        } else if (updateOne) {
            if (parameter.affectsOpacity) {
                state.opacityAt1 = endpointOpacity;
            }
            if (parameter.affectsColor) {
                state.hueShiftAt1 = layer.hueShift;
                state.saturationScaleAt1 = layer.saturationScale;
                state.lightnessShiftAt1 = layer.lightnessShift;
            }
            if (parameter.affectsRenderOrder) {
                state.renderOrderOverrideAt1 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state.maskLayerIndicesAt1 = layer.maskLayerIndices;
            }
            if (parameter.affectsTexture) {
                state.textureIndexAt1 = layer.textureIndex;
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
            for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                for (int& maskLayerIndex : setpoint.maskLayerIndices) {
                    maskLayerIndex = RemapLayerIndexAfterMove(maskLayerIndex, from, to);
                }
            }
        }
    }

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        RebuildLayerRenderedTexture(editor, i);
    }
}

static void ApplyLayerIndexRemap(EditorState& editor, const std::vector<int>& oldToNew) {
    auto remap = [&](int index) -> int {
        if (index < 0 || index >= static_cast<int>(oldToNew.size())) {
            return index;
        }
        return oldToNew[index];
    };

    editor.selectedLayer = remap(editor.selectedLayer);
    for (int& layerIndex : editor.selectedLayers) {
        layerIndex = remap(layerIndex);
    }
    std::sort(editor.selectedLayers.begin(), editor.selectedLayers.end());

    for (EditorLayer& layer : editor.document.layers) {
        for (int& maskLayerIndex : layer.maskLayerIndices) {
            maskLayerIndex = remap(maskLayerIndex);
        }
    }

    for (DeformParameter& parameter : editor.parameters) {
        for (DeformParameterLayerState& state : parameter.layers) {
            state.layerIndex = remap(state.layerIndex);
            for (int& maskLayerIndex : state.maskLayerIndicesAt0) {
                maskLayerIndex = remap(maskLayerIndex);
            }
            for (int& maskLayerIndex : state.maskLayerIndicesAt1) {
                maskLayerIndex = remap(maskLayerIndex);
            }
            for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                for (int& maskLayerIndex : setpoint.maskLayerIndices) {
                    maskLayerIndex = remap(maskLayerIndex);
                }
            }
        }
    }
}

static std::vector<LayerListHistoryState> CaptureLayerListHistoryState(const EditorState& editor) {
    std::vector<LayerListHistoryState> states;
    states.reserve(editor.document.layers.size());
    for (const EditorLayer& layer : editor.document.layers) {
        LayerListHistoryState state;
        state.name = layer.name;
        state.state = CaptureLayerHistoryState(layer);
        states.push_back(std::move(state));
    }
    return states;
}

static bool LayerNameExists(
    const std::vector<std::string>& names,
    const std::string& name
) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

static std::string MakeDuplicateLayerName(
    std::vector<std::string>& reservedNames,
    const std::string& sourceName
) {
    const std::string baseName = sourceName.empty() ? "Layer" : sourceName;
    const std::string copyBase = baseName + " copy";
    std::string candidate = copyBase;
    int suffix = 2;
    while (LayerNameExists(reservedNames, candidate)) {
        candidate = copyBase + " " + std::to_string(suffix++);
    }

    reservedNames.push_back(candidate);
    return candidate;
}

static void DuplicateSelectedLayers(EditorState& editor, bool copyParameters) {
    std::vector<int> sourceIndices = editor.selectedLayers;
    if (sourceIndices.empty() && IsValidLayerIndex(editor, editor.selectedLayer)) {
        sourceIndices.push_back(editor.selectedLayer);
    }

    std::sort(sourceIndices.begin(), sourceIndices.end());
    sourceIndices.erase(std::unique(sourceIndices.begin(), sourceIndices.end()), sourceIndices.end());
    sourceIndices.erase(
        std::remove_if(
            sourceIndices.begin(),
            sourceIndices.end(),
            [&](int layerIndex) {
                return !IsValidLayerIndex(editor, layerIndex);
            }
        ),
        sourceIndices.end()
    );
    if (sourceIndices.empty()) {
        return;
    }

    const int oldLayerCount = static_cast<int>(editor.document.layers.size());
    const int duplicateCount = static_cast<int>(sourceIndices.size());
    const int insertIndex = sourceIndices.back() + 1;

    std::vector<LayerListHistoryState> layersBefore = CaptureLayerListHistoryState(editor);
    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedParameterBefore = editor.selectedParameter;

    std::vector<std::string> reservedNames;
    reservedNames.reserve(editor.document.layers.size() + sourceIndices.size());
    for (const EditorLayer& layer : editor.document.layers) {
        reservedNames.push_back(layer.name);
    }

    std::vector<EditorLayer> duplicates;
    duplicates.reserve(sourceIndices.size());
    for (const int sourceIndex : sourceIndices) {
        EditorLayer duplicate = editor.document.layers[sourceIndex];
        duplicate.name = MakeDuplicateLayerName(reservedNames, duplicate.name);
        ApplyTextureToLayer(editor.document, duplicate, duplicate.textureIndex);
        duplicates.push_back(std::move(duplicate));
    }

    editor.document.layers.insert(
        editor.document.layers.begin() + insertIndex,
        duplicates.begin(),
        duplicates.end()
    );

    std::vector<int> oldToNew(oldLayerCount, -1);
    for (int oldIndex = 0; oldIndex < oldLayerCount; ++oldIndex) {
        oldToNew[oldIndex] = oldIndex < insertIndex ? oldIndex : oldIndex + duplicateCount;
    }
    ApplyLayerIndexRemap(editor, oldToNew);

    if (copyParameters) {
        for (int i = 0; i < duplicateCount; ++i) {
            const int sourceIndex = sourceIndices[static_cast<std::size_t>(i)];
            const int duplicateIndex = insertIndex + i;

            for (DeformParameter& parameter : editor.parameters) {
                auto sourceStateIt = std::find_if(
                    parameter.layers.begin(),
                    parameter.layers.end(),
                    [&](const DeformParameterLayerState& state) {
                        return state.layerIndex == sourceIndex;
                    }
                );
                if (sourceStateIt == parameter.layers.end()) {
                    continue;
                }

                DeformParameterLayerState duplicateState = *sourceStateIt;
                duplicateState.layerIndex = duplicateIndex;
                parameter.layers.push_back(std::move(duplicateState));
            }
        }
    }

    editor.selectedLayers.clear();
    editor.selectedLayers.reserve(sourceIndices.size());
    for (int i = 0; i < duplicateCount; ++i) {
        editor.selectedLayers.push_back(insertIndex + i);
    }
    editor.selectedLayer = editor.selectedLayers.empty() ? -1 : editor.selectedLayers.front();
    ClearAllVertexSelection(editor);

    PushLayerListOperationWithParameters(
        editor,
        std::move(layersBefore),
        CaptureLayerListHistoryState(editor),
        parametersBefore,
        selectedParameterBefore,
        editor.parameters,
        editor.selectedParameter,
        duplicateCount == 1 ? "Duplicate layer" : "Duplicate layers"
    );
    if (copyParameters) {
        editor.statusText = duplicateCount == 1
            ? "Duplicated layer and parameter roles."
            : "Duplicated layers and parameter roles.";
    } else {
        editor.statusText = duplicateCount == 1 ? "Duplicated layer." : "Duplicated layers.";
    }
}

static void MoveLayerGroupInList(EditorState& editor, std::vector<int> movingIndices, int insertBeforeOriginalIndex) {
    const int layerCount = static_cast<int>(editor.document.layers.size());
    if (movingIndices.empty() || insertBeforeOriginalIndex < 0 || insertBeforeOriginalIndex > layerCount) {
        return;
    }

    std::sort(movingIndices.begin(), movingIndices.end());
    movingIndices.erase(std::unique(movingIndices.begin(), movingIndices.end()), movingIndices.end());
    movingIndices.erase(
        std::remove_if(
            movingIndices.begin(),
            movingIndices.end(),
            [&](int layerIndex) {
                return !IsValidLayerIndex(editor, layerIndex);
            }
        ),
        movingIndices.end()
    );
    if (movingIndices.empty()) {
        return;
    }

    const bool droppingInsideGroup = std::any_of(
        movingIndices.begin(),
        movingIndices.end(),
        [&](int layerIndex) {
            return insertBeforeOriginalIndex == layerIndex || insertBeforeOriginalIndex == layerIndex + 1;
        }
    );
    if (droppingInsideGroup) {
        return;
    }

    std::vector<LayerListHistoryState> layersBefore = CaptureLayerListHistoryState(editor);
    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    const int selectedParameterBefore = editor.selectedParameter;

    std::vector<std::uint8_t> moving(layerCount, 0u);
    for (const int layerIndex : movingIndices) {
        moving[layerIndex] = 1u;
    }

    int adjustedInsertIndex = insertBeforeOriginalIndex;
    for (const int layerIndex : movingIndices) {
        if (layerIndex < insertBeforeOriginalIndex) {
            --adjustedInsertIndex;
        }
    }
    adjustedInsertIndex = std::clamp(adjustedInsertIndex, 0, layerCount - static_cast<int>(movingIndices.size()));

    std::vector<EditorLayer> movingLayers;
    std::vector<std::pair<int, EditorLayer>> remainingLayers;
    movingLayers.reserve(movingIndices.size());
    remainingLayers.reserve(layerCount - movingIndices.size());

    for (int i = 0; i < layerCount; ++i) {
        if (moving[i]) {
            movingLayers.push_back(std::move(editor.document.layers[i]));
        } else {
            remainingLayers.push_back({i, std::move(editor.document.layers[i])});
        }
    }

    std::vector<EditorLayer> reordered;
    reordered.reserve(layerCount);
    std::vector<int> oldToNew(layerCount, -1);

    int newIndex = 0;
    int remainingIndex = 0;
    auto appendMoving = [&]() {
        for (std::size_t i = 0; i < movingLayers.size(); ++i) {
            const int oldIndex = movingIndices[i];
            oldToNew[oldIndex] = newIndex++;
            reordered.push_back(std::move(movingLayers[i]));
        }
    };

    for (; remainingIndex < static_cast<int>(remainingLayers.size()); ++remainingIndex) {
        if (remainingIndex == adjustedInsertIndex) {
            appendMoving();
        }
        oldToNew[remainingLayers[remainingIndex].first] = newIndex++;
        reordered.push_back(std::move(remainingLayers[remainingIndex].second));
    }

    if (remainingIndex == adjustedInsertIndex) {
        appendMoving();
    }

    editor.document.layers = std::move(reordered);
    ApplyLayerIndexRemap(editor, oldToNew);
    PushLayerListOperationWithParameters(
        editor,
        std::move(layersBefore),
        CaptureLayerListHistoryState(editor),
        parametersBefore,
        selectedParameterBefore,
        editor.parameters,
        editor.selectedParameter,
        "Reorder layers"
    );
    editor.statusText = "Reordered layers.";
}

static void AddLayerFromTexture(EditorState& editor, int textureIndex) {
    if (textureIndex < 0 || textureIndex >= static_cast<int>(editor.document.textures.size())) {
        return;
    }

    EditorLayer layer;
    const EditorTexture& texture = editor.document.textures[textureIndex];
    layer.name = texture.name;
    layer.left = texture.left;
    layer.top = texture.top;
    layer.right = texture.right;
    layer.bottom = texture.bottom;
    layer.visible = true;
    layer.mesh = CreateInitialQuadMeshForTexture(texture);
    ApplyTextureToLayer(editor.document, layer, textureIndex);
    editor.document.layers.push_back(std::move(layer));
    SelectSingleLayer(editor, static_cast<int>(editor.document.layers.size()) - 1);
    editor.history = EditHistory{};
    editor.statusText = "Added layer. History cleared because layer count changed.";
}

static int RemapLayerIndexAfterDelete(int index, int deletedIndex) {
    if (index == deletedIndex) {
        return -1;
    }
    return index > deletedIndex ? index - 1 : index;
}

static void DeleteLayerFromList(EditorState& editor, int layerIndex) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    editor.document.layers.erase(editor.document.layers.begin() + layerIndex);
    editor.selectedLayer = RemapLayerIndexAfterDelete(editor.selectedLayer, layerIndex);
    for (int& selectedLayer : editor.selectedLayers) {
        selectedLayer = RemapLayerIndexAfterDelete(selectedLayer, layerIndex);
    }
    editor.selectedLayers.erase(
        std::remove(editor.selectedLayers.begin(), editor.selectedLayers.end(), -1),
        editor.selectedLayers.end()
    );
    if (!IsValidLayerIndex(editor, editor.selectedLayer) && !editor.selectedLayers.empty()) {
        editor.selectedLayer = editor.selectedLayers.front();
    }

    for (EditorLayer& layer : editor.document.layers) {
        for (int& maskLayerIndex : layer.maskLayerIndices) {
            maskLayerIndex = RemapLayerIndexAfterDelete(maskLayerIndex, layerIndex);
        }
        layer.maskLayerIndices.erase(
            std::remove(layer.maskLayerIndices.begin(), layer.maskLayerIndices.end(), -1),
            layer.maskLayerIndices.end()
        );
    }

    for (DeformParameter& parameter : editor.parameters) {
        for (DeformParameterLayerState& state : parameter.layers) {
            state.layerIndex = RemapLayerIndexAfterDelete(state.layerIndex, layerIndex);
            for (int& maskLayerIndex : state.maskLayerIndicesAt0) {
                maskLayerIndex = RemapLayerIndexAfterDelete(maskLayerIndex, layerIndex);
            }
            state.maskLayerIndicesAt0.erase(
                std::remove(state.maskLayerIndicesAt0.begin(), state.maskLayerIndicesAt0.end(), -1),
                state.maskLayerIndicesAt0.end()
            );
            for (int& maskLayerIndex : state.maskLayerIndicesAt1) {
                maskLayerIndex = RemapLayerIndexAfterDelete(maskLayerIndex, layerIndex);
            }
            state.maskLayerIndicesAt1.erase(
                std::remove(state.maskLayerIndicesAt1.begin(), state.maskLayerIndicesAt1.end(), -1),
                state.maskLayerIndicesAt1.end()
            );
            for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                for (int& maskLayerIndex : setpoint.maskLayerIndices) {
                    maskLayerIndex = RemapLayerIndexAfterDelete(maskLayerIndex, layerIndex);
                }
                setpoint.maskLayerIndices.erase(
                    std::remove(setpoint.maskLayerIndices.begin(), setpoint.maskLayerIndices.end(), -1),
                    setpoint.maskLayerIndices.end()
                );
            }
        }
        parameter.layers.erase(
            std::remove_if(
                parameter.layers.begin(),
                parameter.layers.end(),
                [](const DeformParameterLayerState& state) { return state.layerIndex < 0; }
            ),
            parameter.layers.end()
        );
    }

    ClearAllVertexSelection(editor);
    editor.history = EditHistory{};
    editor.statusText = "Deleted layer. History cleared because layer count changed.";
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

    char nameBuffer[128] = {};
    std::strncpy(nameBuffer, layer.name.c_str(), sizeof(nameBuffer) - 1u);
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string newName = nameBuffer;
        if (newName.empty()) {
            newName = "Layer";
        }

        if (layer.name != newName) {
            std::vector<LayerListHistoryState> layersBefore = CaptureLayerListHistoryState(editor);
            layer.name = std::move(newName);
            PushLayerListOperation(
                editor,
                std::move(layersBefore),
                CaptureLayerListHistoryState(editor),
                "Rename layer"
            );
        }
    }

    ImGui::SetNextItemWidth(180.0f);
    float opacity = layer.opacity;
    if (ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f")) {
        const std::vector<DeformParameter> parametersBefore = editor.parameters;
        const LayerHistoryState before = CaptureLayerHistoryState(layer);
        layer.opacity = std::clamp(opacity, 0.0f, 1.0f);
        UpdateActiveParameterEndpointForLayers(editor, std::vector<int>{layerIndex});
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

    if (ImGui::Button("Color")) {
        editor.showLayerColorDialog = true;
        editor.colorDialogLayer = layerIndex;
        editor.colorDialogHue = layer.hueShift;
        editor.colorDialogSaturation = layer.saturationScale;
        editor.colorDialogLightness = layer.lightnessShift;
        editor.colorDialogOriginalHue = layer.hueShift;
        editor.colorDialogOriginalSaturation = layer.saturationScale;
        editor.colorDialogOriginalLightness = layer.lightnessShift;
        ImGui::OpenPopup("Layer Color");
    }

    if (editor.showLayerColorDialog) {
        ImGui::OpenPopup("Layer Color");
        editor.showLayerColorDialog = false;
    }

    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::BeginPopupModal("Layer Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!IsValidLayerIndex(editor, editor.colorDialogLayer)) {
            ImGui::CloseCurrentPopup();
        } else {
            EditorLayer& colorLayer = editor.document.layers[editor.colorDialogLayer];
            ImGui::TextWrapped("%s", colorLayer.name.c_str());
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::SliderFloat("Hue", &editor.colorDialogHue, -100.0f, 100.0f, "%.0f")) {
                colorLayer.hueShift = std::clamp(editor.colorDialogHue, -100.0f, 100.0f);
            }
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::SliderFloat("Saturation", &editor.colorDialogSaturation, 0.0f, 200.0f, "%.0f")) {
                colorLayer.saturationScale = std::clamp(editor.colorDialogSaturation, 0.0f, 200.0f);
            }
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::SliderFloat("Lightness", &editor.colorDialogLightness, -100.0f, 100.0f, "%.0f")) {
                colorLayer.lightnessShift = std::clamp(editor.colorDialogLightness, -100.0f, 100.0f);
            }

            if (ImGui::Button("Copy")) {
                editor.hasLayerColorClipboard = true;
                editor.colorClipboardHue = editor.colorDialogHue;
                editor.colorClipboardSaturation = editor.colorDialogSaturation;
                editor.colorClipboardLightness = editor.colorDialogLightness;
            }
            ImGui::SameLine();
            if (!editor.hasLayerColorClipboard) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Paste")) {
                editor.colorDialogHue = editor.colorClipboardHue;
                editor.colorDialogSaturation = editor.colorClipboardSaturation;
                editor.colorDialogLightness = editor.colorClipboardLightness;
                colorLayer.hueShift = std::clamp(editor.colorDialogHue, -100.0f, 100.0f);
                colorLayer.saturationScale = std::clamp(editor.colorDialogSaturation, 0.0f, 200.0f);
                colorLayer.lightnessShift = std::clamp(editor.colorDialogLightness, -100.0f, 100.0f);
            }
            if (!editor.hasLayerColorClipboard) {
                ImGui::EndDisabled();
            }

            ImGui::Separator();
            if (ImGui::Button("Confirm")) {
                const std::vector<DeformParameter> parametersBefore = editor.parameters;
                LayerHistoryState before = CaptureLayerHistoryState(colorLayer);
                before.hueShift = editor.colorDialogOriginalHue;
                before.saturationScale = editor.colorDialogOriginalSaturation;
                before.lightnessShift = editor.colorDialogOriginalLightness;
                colorLayer.hueShift = std::clamp(editor.colorDialogHue, -100.0f, 100.0f);
                colorLayer.saturationScale = std::clamp(editor.colorDialogSaturation, 0.0f, 200.0f);
                colorLayer.lightnessShift = std::clamp(editor.colorDialogLightness, -100.0f, 100.0f);
                UpdateActiveParameterEndpointForLayers(editor, std::vector<int>{editor.colorDialogLayer});
                PushLayerOperationWithParameters(
                    editor,
                    editor.colorDialogLayer,
                    before,
                    CaptureLayerHistoryState(colorLayer),
                    parametersBefore,
                    editor.parameters,
                    "Change layer color"
                );
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                colorLayer.hueShift = editor.colorDialogOriginalHue;
                colorLayer.saturationScale = editor.colorDialogOriginalSaturation;
                colorLayer.lightnessShift = editor.colorDialogOriginalLightness;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();

    char renderOrderBuffer[64] = {};
    std::strncpy(renderOrderBuffer, layer.renderOrderOverride.c_str(), sizeof(renderOrderBuffer) - 1u);
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("Render Override", renderOrderBuffer, sizeof(renderOrderBuffer))) {
        const std::vector<DeformParameter> parametersBefore = editor.parameters;
        const LayerHistoryState before = CaptureLayerHistoryState(layer);
        layer.renderOrderOverride = renderOrderBuffer;
        UpdateActiveParameterEndpointForLayers(editor, std::vector<int>{layerIndex});
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

    if (ImGui::BeginMenu("Texture")) {
        ImGui::BeginChild("texture_layers", ImVec2(300.0f, 320.0f), ImGuiChildFlags_Borders);
        for (int textureIndex = 0; textureIndex < static_cast<int>(editor.document.textures.size()); ++textureIndex) {
            const EditorTexture& texture = editor.document.textures[textureIndex];
            const bool selectedTexture = layer.textureIndex == textureIndex;
            if (ImGui::RadioButton(texture.name.c_str(), selectedTexture)) {
                const std::vector<DeformParameter> parametersBefore = editor.parameters;
                const LayerHistoryState before = CaptureLayerHistoryState(layer);
                ApplyTextureToLayer(editor.document, layer, textureIndex);
                UpdateActiveParameterEndpointForLayers(editor, std::vector<int>{layerIndex});
                PushLayerOperationWithParameters(
                    editor,
                    layerIndex,
                    before,
                    CaptureLayerHistoryState(layer),
                    parametersBefore,
                    editor.parameters,
                    "Change layer texture"
                );
            }
        }
        ImGui::EndChild();
        ImGui::EndMenu();
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
                UpdateActiveParameterEndpointForLayers(editor, std::vector<int>{layerIndex});
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
