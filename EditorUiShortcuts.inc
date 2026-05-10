static void CopySelectedDeformVertexPositions(EditorState& editor) {
    editor.meshPositionClipboard.clear();
    editor.meshPositionClipboard.reserve(editor.selectedDeformVertices.size());

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= layer.mesh.vertices.size()) {
            continue;
        }

        CopiedMeshVertexPosition copied;
        copied.ref = ref;
        copied.position = layer.mesh.vertices[ref.vertexIndex].position;
        editor.meshPositionClipboard.push_back(copied);
    }

    editor.statusText = editor.meshPositionClipboard.empty()
        ? "No deform vertices selected to copy."
        : "Copied " + std::to_string(editor.meshPositionClipboard.size()) + " vertex position" +
            (editor.meshPositionClipboard.size() == 1u ? "." : "s.");
}

static void PasteCopiedDeformVertexPositions(EditorState& editor) {
    if (editor.meshPositionClipboard.empty()) {
        editor.statusText = "No copied vertex positions to paste.";
        return;
    }

    if (editor.parameterEditSnapActive) {
        editor.pasteAfterParameterEditSnap = true;
        return;
    }

    if (ActiveParameterNeedsSetpointSnapBeforeEdit(editor)) {
        editor.pasteAfterParameterEditSnap = true;
        return;
    }

    std::vector<int> changedLayerIndices;
    std::vector<LayerHistoryState> beforeStates;

    for (const CopiedMeshVertexPosition& copied : editor.meshPositionClipboard) {
        if (!IsValidLayerIndex(editor, copied.ref.layerIndex)) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[copied.ref.layerIndex];
        if (copied.ref.vertexIndex >= layer.mesh.vertices.size()) {
            continue;
        }

        MeshVertex& vertex = layer.mesh.vertices[copied.ref.vertexIndex];
        if (
            vertex.position.x == copied.position.x &&
            vertex.position.y == copied.position.y
        ) {
            continue;
        }

        if (!ContainsLayerIndex(changedLayerIndices, copied.ref.layerIndex)) {
            changedLayerIndices.push_back(copied.ref.layerIndex);
            beforeStates.push_back(CaptureLayerHistoryState(layer));
        }

        vertex.position = copied.position;
    }

    if (changedLayerIndices.empty()) {
        editor.statusText = "Pasted vertex positions; nothing changed.";
        return;
    }

    for (const int layerIndex : changedLayerIndices) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
        }
    }
    RebuildLayersAffectedByLayerGeometry(editor, changedLayerIndices);

    const std::vector<DeformParameter> parametersBefore = editor.parameters;
    UpdateActiveParameterEndpointForLayers(editor, changedLayerIndices);
    const std::vector<DeformParameter> parametersAfter = editor.parameters;

    for (std::size_t i = 0; i < changedLayerIndices.size(); ++i) {
        const int layerIndex = changedLayerIndices[i];
        if (!IsValidLayerIndex(editor, layerIndex) || i >= beforeStates.size()) {
            continue;
        }

        PushLayerOperationWithParameters(
            editor,
            layerIndex,
            beforeStates[i],
            CaptureLayerHistoryState(editor.document.layers[layerIndex]),
            parametersBefore,
            parametersAfter,
            "Paste vertex positions"
        );
    }

    editor.statusText = "Pasted copied vertex positions.";
}

static void HandleEditorShortcuts(EditorState& editor) {
    const ImGuiIO& io = ImGui::GetIO();

    if (editor.pasteAfterParameterEditSnap && !editor.parameterEditSnapActive) {
        editor.pasteAfterParameterEditSnap = false;
        PasteCopiedDeformVertexPositions(editor);
        return;
    }

    if (
        editor.draggingLayer ||
        editor.draggingMeshSelection ||
        editor.boxSelectingMesh ||
        io.WantTextInput
    ) {
        return;
    }

    if (
        editor.mode == EditorMode::Mesh &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    ) {
        SelectAllMeshItems(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    ) {
        SelectAllVisibleDeformVertices(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A, false)
    ) {
        SelectAllLayers(editor);
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        SaveMeshesFromUi(editor);
        return;
    }

    if (editor.mode == EditorMode::View) {
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        editor.selectedParameterSetpointParameter >= 0 &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)
    ) {
        DeleteSelectedParameterSetpoint(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false)
    ) {
        CopySelectedDeformVertexPositions(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Layer &&
        io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_V, false)
    ) {
        PasteCopiedDeformVertexPositions(editor);
        return;
    }

    if (
        editor.mode == EditorMode::Mesh &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        (!editor.selectedVertices.empty() || !editor.selectedEdges.empty())
    ) {
        DeleteSelectedMeshItems(editor);
        return;
    }

    if (!io.KeyCtrl) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        UndoLastOperation(editor);
        ClearMeshSelection(editor);
    } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        RedoLastOperation(editor);
        ClearMeshSelection(editor);
    }
}

void DrawEditor(EditorState& editor, GLFWwindow* window) {
    if (editor.loadingScreenActive) {
        DrawLoadingScreen(editor, window);
        return;
    }

    HandleEditorShortcuts(editor);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;

    ImGui::SetNextWindowPos(workPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workSize.x, 110.0f), ImGuiCond_Always);
    DrawTopBar(editor, window);

    ImGui::SetNextWindowPos(ImVec2(workPos.x, workPos.y + 110.0f), ImGuiCond_Always);
    const float sideWidth = 300.0f;
    const float sideTop = workPos.y + 110.0f;
    const float sideHeight = workSize.y - 110.0f;
    const float parameterHeight = std::clamp(sideHeight * 0.32f, 170.0f, 280.0f);
    const float layerHeight = std::max(180.0f, sideHeight - parameterHeight);

    ImGui::SetNextWindowSize(ImVec2(sideWidth, layerHeight), ImGuiCond_Always);
    DrawLayerPanel(editor);

    ImGui::SetNextWindowPos(ImVec2(workPos.x, sideTop + layerHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sideWidth, parameterHeight), ImGuiCond_Always);
    DrawDeformParametersPanel(editor);

    DrawMeshGeneratorSettingsPanel(editor);
    DrawPhysicsSettingsPanels(editor);
    DrawHistoryPanel(editor);

    int physicsWindowWidth = 0;
    int physicsWindowHeight = 0;
    glfwGetWindowSize(window, &physicsWindowWidth, &physicsWindowHeight);
    const bool windowSizeChanged =
        physicsWindowWidth != editor.physicsWindowWidth ||
        physicsWindowHeight != editor.physicsWindowHeight;
    if (windowSizeChanged) {
        editor.physicsWindowWidth = physicsWindowWidth;
        editor.physicsWindowHeight = physicsWindowHeight;
        editor.physicsResizeStableFrames = 0;
        editor.physicsPausedByResize = true;
    } else if (editor.physicsPausedByResize) {
        ++editor.physicsResizeStableFrames;
    }

    const bool windowMinimized =
        glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE ||
        physicsWindowWidth <= 0 ||
        physicsWindowHeight <= 0;
    if (windowMinimized) {
        editor.physicsPausedByMinimize = true;
    } else {
        const bool resizeSettled =
            !editor.physicsPausedByResize ||
            editor.physicsResizeStableFrames >= 2;
        if (editor.physicsPausedByMinimize || (editor.physicsPausedByResize && resizeSettled)) {
            ApplyAllDeformParameters(editor);
            ResetViewPhysics(editor);
            editor.physicsPausedByMinimize = false;
            editor.physicsPausedByResize = false;
            editor.physicsResizeStableFrames = 0;
        }
        if (resizeSettled) {
            StepViewPhysics(editor, ImGui::GetIO().DeltaTime);
        }
    }

    ImGui::SetNextWindowPos(ImVec2(workPos.x + sideWidth, sideTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(workSize.x - sideWidth, sideHeight), ImGuiCond_Always);
    DrawViewport(editor);

    DrawGenerateMeshConfirmationPopup(editor);
}

void DropCallback(GLFWwindow* window, int count, const char** paths) {
    EditorState* editor = static_cast<EditorState*>(glfwGetWindowUserPointer(window));

    if (!editor || count <= 0 || !paths || !paths[0]) {
        return;
    }

    editor->pendingPath = paths[0];

    std::string error;
    const std::filesystem::path dropped(editor->pendingPath);
    const bool loadAsPsd = dropped.extension() == ".psd" || dropped.extension() == ".PSD";
    const bool loaded = loadAsPsd
        ? LoadPsdIntoEditor(editor->pendingPath, *editor, window, error)
        : LoadProjectForEditor(editor->pendingPath, *editor, window, error);
    if (!loaded) {
        editor->errorText = error;
        editor->statusText = "Load failed.";
    }
}
