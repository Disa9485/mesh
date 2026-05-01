static void DrawLayerPanel(EditorState& editor) {
    ImGui::Begin("Layers");

    const bool canUndo = !editor.history.undoStack.empty();
    const bool canRedo = !editor.history.redoStack.empty();

    if (!canUndo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Undo")) {
        UndoLastOperation(editor);
    }
    if (!canUndo) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (!canRedo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Redo")) {
        RedoLastOperation(editor);
    }
    if (!canRedo) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    if (editor.document.layers.empty()) {
        ImGui::TextDisabled("No layers loaded.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Render order: 01 is front");

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        EditorLayer& layer = editor.document.layers[i];

        ImGui::PushID(i);
        ImGui::BeginGroup();

        if (layer.renderOrderOverride.empty()) {
            ImGui::Text("%02d", i + 1);
        } else {
            ImGui::Text("%s", layer.renderOrderOverride.c_str());
        }
        ImGui::SameLine();

        if (i <= 0) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton("^")) {
            MoveLayerInList(editor, i, i - 1);
            ImGui::EndGroup();
            ImGui::PopID();
            break;
        }
        if (i <= 0) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (i + 1 >= static_cast<int>(editor.document.layers.size())) {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton("v")) {
            MoveLayerInList(editor, i, i + 1);
            ImGui::EndGroup();
            ImGui::PopID();
            break;
        }
        if (i + 1 >= static_cast<int>(editor.document.layers.size())) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        const LayerHistoryState beforeVisibility = CaptureLayerHistoryState(layer);
        if (ImGui::Checkbox("##visible", &layer.visible)) {
            if (!layer.visible) {
                RemoveLayerFromSelection(editor, i);
            }

            PushLayerOperation(
                editor,
                i,
                beforeVisibility,
                CaptureLayerHistoryState(layer),
                "Change layer visibility"
            );
        }
        ImGui::SameLine();

        const float previewSize = 20.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float selectableWidth = std::max(
            60.0f,
            ImGui::GetContentRegionAvail().x - previewSize - spacing
        );
        const bool selected = IsLayerSelected(editor, i);
        if (ImGui::Selectable(layer.name.c_str(), selected, 0, ImVec2(selectableWidth, previewSize))) {
            HandleLayerSelectionClick(editor, i);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("Layer Data");
        }
        DrawLayerDataPopup(editor, i);

        ImGui::SameLine();
        DrawLayerPreviewThumbnail(layer, previewSize);

        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::Separator();

    if (IsValidLayerIndex(editor, editor.selectedLayer)) {
        const EditorLayer& layer = editor.document.layers[editor.selectedLayer];

        ImGui::Text("Selected:");
        ImGui::TextWrapped("%s", layer.name.c_str());

        ImGui::Text("Bounds:");
        ImGui::Text("L:%d T:%d R:%d B:%d",
            layer.left,
            layer.top,
            layer.right,
            layer.bottom
        );

        ImGui::Text("Initial mesh:");
        ImGui::Text("%zu vertices", layer.mesh.vertices.size());
        ImGui::Text("%zu indices", layer.mesh.indices.size());

        float mx0 = 0.0f;
        float my0 = 0.0f;
        float mx1 = 0.0f;
        float my1 = 0.0f;

        if (GetMeshBounds(layer.mesh, mx0, my0, mx1, my1)) {
            ImGui::Text("Mesh bounds:");
            ImGui::Text("X: %.0f -> %.0f", mx0, mx1);
            ImGui::Text("Y: %.0f -> %.0f", my0, my1);
        }
    }

    ImGui::End();
}

static void DrawMeshGeneratorSettingsPanel(EditorState& editor) {
    if (!editor.showMeshGeneratorSettings) {
        return;
    }

    ImGui::Begin("Mesh Generator Settings", &editor.showMeshGeneratorSettings);

    ImGui::SliderInt("Alpha Threshold", &editor.meshSettings.alphaThreshold, 0, 255);
    ImGui::SliderInt("Mesh Detail", &editor.meshSettings.meshDetail, 1, 5);
    ImGui::SliderInt(
        "Perimeter Spacing",
        &editor.meshSettings.perimeterSpacing,
        1,
        2000,
        "%d px"
    );
    ImGui::SliderInt(
        "Perimeter Buffer",
        &editor.meshSettings.perimeterBuffer,
        0,
        500,
        "%d px"
    );
    ImGui::SliderInt(
        "Interior Depth Spacing",
        &editor.meshSettings.interiorDepthSpacing,
        1,
        2000,
        "%d px"
    );
    ImGui::SliderInt(
        "Interior Point Spacing",
        &editor.meshSettings.interiorPointSpacing,
        1,
        2000,
        "%d px"
    );
    ImGui::SliderInt(
        "Max Perimeter Points",
        &editor.meshSettings.maxPerimeterPoints,
        16,
        10000
    );
    ImGui::SliderInt(
        "Max Interior Points",
        &editor.meshSettings.maxInteriorPoints,
        0,
        20000
    );

    ImGui::End();
}

static void DrawViewportControlPanel(EditorState& editor) {
    ImGui::TextUnformatted("Edit Mode");
    ImGui::SameLine();

    int mode = editor.mode == EditorMode::Mesh ? 1 : 0;

    if (ImGui::RadioButton("Deform", mode == 0)) {
        editor.mode = EditorMode::Layer;
        ClearMeshSelection(editor);
        editor.boxSelectingMesh = false;
        editor.draggingMeshSelection = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Mesh", mode == 1)) {
        editor.mode = EditorMode::Mesh;
        editor.draggingLayer = false;
        editor.draggedLayer = -1;
        editor.layerTransformMode = LayerTransformMode::None;
    }

    ImGui::SameLine();

    const char* selectionType =
        editor.selectionDragShape == SelectionDragShape::Rectangle
            ? "Selection Type: Rectangle"
            : "Selection Type: Circle";
    if (ImGui::Button(selectionType)) {
        editor.selectionDragShape =
            editor.selectionDragShape == SelectionDragShape::Rectangle
                ? SelectionDragShape::Circle
                : SelectionDragShape::Rectangle;
    }

    ImGui::SameLine();

    if (ImGui::Button(editor.showHistoryPanel ? "Hide History" : "Show History")) {
        editor.showHistoryPanel = !editor.showHistoryPanel;
    }

    ImGui::SameLine();

    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Generate Mesh")) {
        RequestGenerateMesh(editor);
    }

    ImGui::SameLine();

    if (ImGui::Button("Mesh Generator Settings")) {
        editor.showMeshGeneratorSettings = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Save Mesh")) {
        SaveMeshesFromUi(editor);
    }

    if (editor.mode == EditorMode::Mesh) {
        ImGui::SameLine();
        ImGui::Text(
            "V:%zu E:%zu",
            editor.selectedVertices.size(),
            editor.selectedEdges.size()
        );
    }

    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        ImGui::EndDisabled();
    }
}

static void SeekHistoryTo(EditorState& editor, std::size_t appliedCount) {
    while (editor.history.undoStack.size() > appliedCount) {
        if (!UndoLastOperation(editor)) {
            break;
        }
    }

    while (editor.history.undoStack.size() < appliedCount && !editor.history.redoStack.empty()) {
        if (!RedoLastOperation(editor)) {
            break;
        }
    }
}

static void DrawHistoryPanel(EditorState& editor) {
    if (!editor.showHistoryPanel) {
        return;
    }

    ImGui::Begin("History", &editor.showHistoryPanel);

    const bool hasHistory =
        !editor.history.undoStack.empty() ||
        !editor.history.redoStack.empty();
    if (!hasHistory) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Clear History")) {
        ClearHistoryFromUi(editor);
    }
    if (!hasHistory) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    const std::size_t appliedCount = editor.history.undoStack.size();
    const std::size_t totalCount = editor.history.undoStack.size() + editor.history.redoStack.size();

    if (ImGui::Selectable("Initial state", appliedCount == 0u)) {
        SeekHistoryTo(editor, 0u);
        ImGui::End();
        return;
    }

    for (std::size_t i = 0; i < totalCount; ++i) {
        const bool isApplied = i < editor.history.undoStack.size();
        const LayerOperation& operation = isApplied
            ? editor.history.undoStack[i]
            : editor.history.redoStack[editor.history.redoStack.size() - 1u - (i - editor.history.undoStack.size())];

        std::string label = std::to_string(i + 1u) + ". ";
        label += operation.description.empty() ? "Edit" : operation.description;
        if (!isApplied) {
            label += " (redo)";
        }

        if (ImGui::Selectable(label.c_str(), appliedCount == i + 1u)) {
            SeekHistoryTo(editor, i + 1u);
            ImGui::End();
            return;
        }
    }

    if (totalCount == 0u) {
        ImGui::TextDisabled("No history yet.");
    }

    ImGui::End();
}
