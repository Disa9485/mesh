static void SetPremultipliedAlphaBlendCallback(
    const ImDrawList*,
    const ImDrawCmd*
) {
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void RestoreStraightAlphaBlendCallback(
    const ImDrawList*,
    const ImDrawCmd*
) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static std::string DefaultAssetProjectPath() {
    return (std::filesystem::path("assets") / "project.mesh.bin").string();
}

static std::string DefaultAssetPsdPath() {
    return (std::filesystem::path("assets") / "project.psd").string();
}

static void DrawCheckerboard(
    ImDrawList* drawList,
    ImVec2 origin,
    ImVec2 size,
    float cellSize
) {
    const ImU32 a = IM_COL32(48, 48, 48, 255);
    const ImU32 b = IM_COL32(64, 64, 64, 255);

    const int cols = static_cast<int>(size.x / cellSize) + 1;
    const int rows = static_cast<int>(size.y / cellSize) + 1;

    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < cols; ++x) {
            ImVec2 p0(
                origin.x + static_cast<float>(x) * cellSize,
                origin.y + static_cast<float>(y) * cellSize
            );

            ImVec2 p1(
                std::min(p0.x + cellSize, origin.x + size.x),
                std::min(p0.y + cellSize, origin.y + size.y)
            );

            drawList->AddRectFilled(p0, p1, ((x + y) & 1) ? a : b);
        }
    }
}

static void DrawTopBar(EditorState& editor, GLFWwindow* window) {
    ImGui::Begin("Project");

    if (ImGui::Button("Load Project")) {
        editor.pendingProjectPath = editor.document.projectPath.empty()
            ? (editor.pendingProjectPath.empty() ? DefaultAssetProjectPath() : editor.pendingProjectPath)
            : editor.document.projectPath;
        editor.showLoadProjectDialog = true;
        ImGui::OpenPopup("Load Project");
    }
    ImGui::SameLine();

    if (ImGui::Button("Save Project")) {
        std::string error;
        const std::string savePath = !editor.document.projectPath.empty()
            ? editor.document.projectPath
            : editor.pendingProjectPath;
        if (savePath.empty()) {
            editor.showSaveProjectAsDialog = true;
            ImGui::OpenPopup("Save Project As");
        } else if (SaveProjectForEditor(editor, savePath, error)) {
            editor.document.projectPath = savePath;
            editor.document.path = savePath;
            editor.pendingProjectPath = savePath;
            SaveLastProjectPath(savePath);
            editor.statusText = "Saved project.";
            editor.errorText.clear();
        } else {
            editor.errorText = error;
            editor.statusText = "Project save failed.";
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Save As Project")) {
        editor.pendingProjectPath = editor.document.projectPath.empty()
            ? (editor.pendingProjectPath.empty() ? DefaultAssetProjectPath() : editor.pendingProjectPath)
            : editor.document.projectPath;
        editor.showSaveProjectAsDialog = true;
        ImGui::OpenPopup("Save Project As");
    }
    ImGui::SameLine();

    if (ImGui::Button("Load PSD")) {
        editor.pendingPsdPath = editor.document.psdPath.empty()
            ? (editor.pendingPath.empty() ? DefaultAssetPsdPath() : editor.pendingPath)
            : editor.document.psdPath;
        editor.showAttachPsdDialog = true;
        ImGui::OpenPopup("Load PSD");
    }

    auto drawPathDialog = [&](const char* title, std::string& path, const char* actionLabel, auto&& onAccept) {
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        char buffer[1024] = {};
        std::strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
        ImGui::SetNextItemWidth(520.0f);
        if (ImGui::InputText("Path", buffer, sizeof(buffer))) {
            path = buffer;
        }

        if (ImGui::Button(actionLabel)) {
            onAccept();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    };

    if (editor.showLoadProjectDialog) {
        ImGui::OpenPopup("Load Project");
        editor.showLoadProjectDialog = false;
    }
    drawPathDialog("Load Project", editor.pendingProjectPath, "Load", [&]() {
        std::string error;
        if (!LoadProjectForEditor(editor.pendingProjectPath, editor, window, error)) {
            editor.errorText = error;
            editor.statusText = "Project load failed.";
        }
    });

    if (editor.showSaveProjectAsDialog) {
        ImGui::OpenPopup("Save Project As");
        editor.showSaveProjectAsDialog = false;
    }
    drawPathDialog("Save Project As", editor.pendingProjectPath, "Save", [&]() {
        std::string error;
        if (SaveProjectForEditor(editor, editor.pendingProjectPath, error)) {
            editor.document.projectPath = editor.pendingProjectPath;
            editor.document.path = editor.pendingProjectPath;
            SaveLastProjectPath(editor.pendingProjectPath);
            editor.statusText = "Saved project.";
            editor.errorText.clear();
        } else {
            editor.errorText = error;
            editor.statusText = "Project save failed.";
        }
    });

    if (editor.showAttachPsdDialog) {
        ImGui::OpenPopup("Load PSD");
        editor.showAttachPsdDialog = false;
    }
    drawPathDialog("Load PSD", editor.pendingPsdPath, "Load", [&]() {
        std::string error;
        if (!AttachPsdToProject(editor.pendingPsdPath, editor, window, error)) {
            editor.errorText = error;
            editor.statusText = "PSD load failed.";
        } else {
            editor.pendingPath = editor.pendingPsdPath;
        }
    });

    ImGui::Separator();

    ImGui::Text("Status: %s", editor.statusText.c_str());

    if (!editor.errorText.empty()) {
        ImGui::TextWrapped("Error: %s", editor.errorText.c_str());
    }

    if (editor.document.canvasWidth > 0 && editor.document.canvasHeight > 0) {
        ImGui::Text("Canvas: %d x %d",
            editor.document.canvasWidth,
            editor.document.canvasHeight
        );
    }
    if (!editor.document.projectPath.empty()) {
        ImGui::Text("Project: %s", editor.document.projectPath.c_str());
    }
    if (!editor.document.psdPath.empty()) {
        ImGui::Text("PSD: %s", editor.document.psdPath.c_str());
    } else {
        ImGui::TextDisabled("PSD: none attached");
    }

    ImGui::End();
}

static std::vector<int> GetGenerateMeshTargetLayers(const EditorState& editor) {
    if (editor.mode != EditorMode::Layer || editor.selectedLayers.empty()) {
        return IsValidLayerIndex(editor, editor.selectedLayer)
            ? std::vector<int>{editor.selectedLayer}
            : std::vector<int>{};
    }

    std::vector<int> layers;
    layers.reserve(editor.selectedLayers.size());

    for (const int layerIndex : editor.selectedLayers) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            layers.push_back(layerIndex);
        }
    }

    return layers;
}

static void GenerateMeshForLayers(EditorState& editor, const std::vector<int>& layerIndices) {
    const int originalSelectedLayer = editor.selectedLayer;

    for (const int layerIndex : layerIndices) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        editor.selectedLayer = layerIndex;
        GenerateMeshForSelectedLayer(editor);
    }

    if (IsValidLayerIndex(editor, originalSelectedLayer)) {
        editor.selectedLayer = originalSelectedLayer;
    } else if (!layerIndices.empty()) {
        editor.selectedLayer = layerIndices.front();
    }
}

static void ResetMeshForLayers(EditorState& editor, const std::vector<int>& layerIndices) {
    const int originalSelectedLayer = editor.selectedLayer;
    bool resetAnyLayer = false;
    bool failed = false;

    for (const int layerIndex : layerIndices) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        const LayerHistoryState before = CaptureLayerHistoryState(layer);
        const std::vector<DeformParameter> parametersBefore = editor.parameters;

        try {
            layer.mesh = GenerateLayerMesh(layer, editor.meshSettings);
            UpdateLayerBoundsFromMesh(layer);

            editor.selectedLayer = layerIndex;
            editor.selectedVertices.clear();
            editor.selectedEdges.clear();
            UpdateActiveParameterEndpointForLayers(editor, std::vector<int>{layerIndex});
            RebuildLayersAffectedByLayerGeometry(editor, layerIndex);

            PushLayerOperationWithParameters(
                editor,
                layerIndex,
                before,
                CaptureLayerHistoryState(layer),
                parametersBefore,
                editor.parameters,
                "Reset mesh"
            );
            resetAnyLayer = true;
        } catch (const std::exception& ex) {
            ApplyLayerHistoryState(layer, before);
            editor.errorText = ex.what();
            editor.statusText = "Mesh reset failed.";
            failed = true;
            break;
        }
    }

    if (IsValidLayerIndex(editor, originalSelectedLayer)) {
        editor.selectedLayer = originalSelectedLayer;
    } else if (!layerIndices.empty()) {
        editor.selectedLayer = layerIndices.front();
    }

    if (!failed) {
        editor.statusText = resetAnyLayer ? "Reset mesh." : "No mesh was reset.";
        editor.errorText.clear();
    }
}

static void SaveMeshesFromUi(EditorState& editor) {
    std::string error;
    if (SaveMeshesForEditor(editor, error)) {
        editor.statusText = "Saved project.";
        editor.errorText.clear();
    } else {
        editor.statusText = "Project save failed.";
        editor.errorText = error;
    }
}

static void ClearHistoryFromUi(EditorState& editor) {
    editor.history.undoStack.clear();
    editor.history.redoStack.clear();
    editor.statusText = "Cleared edit history. Save to persist.";
    editor.errorText.clear();
}

static void RequestGenerateMesh(EditorState& editor) {
    std::vector<int> targetLayers = GetGenerateMeshTargetLayers(editor);
    if (targetLayers.empty()) {
        return;
    }

    const bool needsConfirmation = std::any_of(
        targetLayers.begin(),
        targetLayers.end(),
        [&](int layerIndex) {
            return
                IsValidLayerIndex(editor, layerIndex) &&
                editor.document.layers[layerIndex].mesh.vertices.size() > 10u;
        }
    );

    if (!needsConfirmation) {
        GenerateMeshForLayers(editor, targetLayers);
        return;
    }

    editor.pendingGenerateMeshLayers = std::move(targetLayers);
    editor.pendingGenerateMeshConfirmation = true;
    ImGui::OpenPopup("Generate Mesh?");
}

static void DrawGenerateMeshConfirmationPopup(EditorState& editor) {
    if (editor.pendingGenerateMeshConfirmation) {
        ImGui::OpenPopup("Generate Mesh?");
        editor.pendingGenerateMeshConfirmation = false;
    }

    if (!ImGui::BeginPopupModal("Generate Mesh?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Are you sure?");

    if (!editor.pendingGenerateMeshLayers.empty()) {
        std::size_t replaceCount = 0;
        for (const int layerIndex : editor.pendingGenerateMeshLayers) {
            if (
                IsValidLayerIndex(editor, layerIndex) &&
                editor.document.layers[layerIndex].mesh.vertices.size() > 10u
            ) {
                ++replaceCount;
            }
        }

        ImGui::Text(
            "This will replace existing meshes on %zu layer%s.",
            replaceCount,
            replaceCount == 1u ? "" : "s"
        );
    }

    if (ImGui::Button("Generate")) {
        GenerateMeshForLayers(editor, editor.pendingGenerateMeshLayers);

        editor.pendingGenerateMeshLayers.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        editor.pendingGenerateMeshLayers.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
