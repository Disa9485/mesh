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
            if (parameter.affectsRenderOrder) {
                state.renderOrderOverrideAt0 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state.maskLayerIndicesAt0 = layer.maskLayerIndices;
            }
        } else if (updateOne) {
            if (parameter.affectsOpacity) {
                state.opacityAt1 = endpointOpacity;
            }
            if (parameter.affectsRenderOrder) {
                state.renderOrderOverrideAt1 = layer.renderOrderOverride;
            }
            if (parameter.affectsMasking) {
                state.maskLayerIndicesAt1 = layer.maskLayerIndices;
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
        }
    }

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        RebuildLayerRenderedTexture(editor, i);
    }
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
