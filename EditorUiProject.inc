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

static std::string TrimProjectName(std::string name) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) {
        name.erase(name.begin());
    }
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
        name.pop_back();
    }
    return name;
}

static std::string SanitizeProjectBaseName(std::string name) {
    name = TrimProjectName(std::move(name));
    if (name.size() > 9u && name.substr(name.size() - 9u) == ".mesh.bin") {
        name.resize(name.size() - 9u);
    } else if (name.size() > 8u && name.substr(name.size() - 8u) == ".project") {
        name.resize(name.size() - 8u);
    }

    for (char& c : name) {
        const bool invalid =
            c < ' ' ||
            c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
        if (invalid) {
            c = '_';
        }
    }

    name = TrimProjectName(std::move(name));
    return name.empty() ? "project" : name;
}

static std::string ProjectPathForBaseName(const std::string& baseName) {
    return (std::filesystem::path("assets") / (SanitizeProjectBaseName(baseName) + ".mesh.bin")).string();
}

static std::string DefaultAssetPsdPath() {
    return (std::filesystem::path("assets") / "project.psd").string();
}

static std::string PsdPathForProjectPath(const std::string& projectPath) {
    if (projectPath.empty()) {
        return DefaultAssetPsdPath();
    }

    std::filesystem::path path(projectPath);
    std::string filename = path.filename().string();
    if (filename.size() > 9u && filename.substr(filename.size() - 9u) == ".mesh.bin") {
        filename.resize(filename.size() - 9u);
        path = path.parent_path() / (filename + ".psd");
        return path.string();
    }

    path.replace_extension(".psd");
    return path.string();
}

static bool HasPsdExtension(const std::string& path) {
    const std::filesystem::path fsPath(path);
    const std::string extension = fsPath.extension().string();
    return extension == ".psd" || extension == ".PSD";
}

static std::string DisplayNameForLoadingPath(const std::string& path) {
    if (path.empty()) {
        return "new_project";
    }

    std::string filename = std::filesystem::path(path).filename().string();
    if (filename.size() > 9u && filename.substr(filename.size() - 9u) == ".mesh.bin") {
        filename.resize(filename.size() - 9u);
        return filename.empty() ? "new_project" : filename;
    }

    const std::filesystem::path fsPath(filename);
    const std::string stem = fsPath.stem().string();
    return stem.empty() ? "new_project" : stem;
}

static void ScheduleLoadingOperation(
    EditorState& editor,
    EditorLoadingOperation operation,
    const std::string& path
) {
    editor.loadingScreenActive = true;
    editor.loadingFramesBeforeExecute = 1;
    editor.loadingOperation = operation;
    editor.loadingPath = path;
    editor.loadingDisplayName = DisplayNameForLoadingPath(path);
    editor.statusText = "Loading " + editor.loadingDisplayName + ".";
    editor.errorText.clear();
}

static void ResetEditorForNewProject(EditorState& editor, const std::string& path) {
    DestroyDocument(editor.document);
    editor.document = EditorDocument{};
    editor.document.projectPath = path;
    editor.document.path = path;

    editor.pendingPath = path;
    editor.pendingProjectPath = path;
    editor.pendingPsdPath.clear();
    editor.parameters.clear();
    editor.selectedParameter = -1;
    editor.selectedParameterSetpointParameter = -1;
    editor.selectedParameterSetpointValue = -1.0f;
    editor.history = EditHistory{};
    editor.selectedLayer = -1;
    editor.selectedLayers.clear();
    editor.selectedVertices.clear();
    editor.selectedEdges.clear();
    editor.selectedDeformVertices.clear();
    editor.physicsLayers.clear();
    editor.meshModeGuideMeshes.clear();
    editor.draggingLayer = false;
    editor.draggedLayer = -1;
    editor.layerTransformMode = LayerTransformMode::None;
    editor.deformingVertices = false;
    editor.deformTransformMode = LayerTransformMode::None;
    editor.boxSelectingMesh = false;
    editor.draggingMeshSelection = false;
    editor.requestFitView = true;
}

static void ExecuteLoadingOperation(EditorState& editor, GLFWwindow* window) {
    const EditorLoadingOperation operation = editor.loadingOperation;
    const std::string path = editor.loadingPath;
    editor.loadingOperation = EditorLoadingOperation::None;
    editor.loadingPath.clear();

    std::string error;
    bool ok = false;
    if (operation == EditorLoadingOperation::LoadPath) {
        ok = HasPsdExtension(path)
            ? LoadPsdIntoEditor(path, editor, window, error)
            : LoadProjectForEditor(path, editor, window, error);
    } else if (operation == EditorLoadingOperation::NewProject) {
        ResetEditorForNewProject(editor, path);
        ok = SaveProjectForEditor(editor, path, error);
        if (ok) {
            SaveLastProjectPath(path);
            editor.statusText = "Created project: " + path;
            editor.errorText.clear();
        }
    } else if (operation == EditorLoadingOperation::AttachPsd) {
        ok = AttachPsdToProject(path, editor, window, error);
        if (ok) {
            editor.pendingPath = path;
        }
    }

    if (!ok && operation != EditorLoadingOperation::None) {
        editor.errorText = error;
        if (operation == EditorLoadingOperation::AttachPsd) {
            editor.statusText = "PSD load failed.";
        } else if (operation == EditorLoadingOperation::NewProject) {
            editor.statusText = "Project create failed.";
        } else {
            editor.statusText = "Project load failed.";
        }
    }

    editor.loadingScreenActive = false;
    editor.loadingFramesBeforeExecute = 0;
    editor.loadingDisplayName.clear();
}

static void DrawLoadingScreen(EditorState& editor, GLFWwindow* window) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::Begin(
        "Loading Screen",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus
    );

    const int dotCount = static_cast<int>(ImGui::GetTime() * 2.5) % 4;
    char loadingText[32] = {};
    std::snprintf(loadingText, sizeof(loadingText), "Loading%.*s", dotCount, "...");

    const std::string projectName = editor.loadingDisplayName.empty()
        ? "new_project"
        : editor.loadingDisplayName;
    const ImVec2 loadingSize = ImGui::CalcTextSize(loadingText);
    const ImVec2 projectSize = ImGui::CalcTextSize(projectName.c_str());
    const float blockHeight = loadingSize.y + projectSize.y + 12.0f;
    const ImVec2 center = viewport->GetCenter();

    ImGui::SetCursorScreenPos(ImVec2(center.x - loadingSize.x * 0.5f, center.y - blockHeight * 0.5f));
    ImGui::TextUnformatted(loadingText);
    ImGui::SetCursorScreenPos(ImVec2(center.x - projectSize.x * 0.5f, center.y - blockHeight * 0.5f + loadingSize.y + 12.0f));
    ImGui::TextDisabled("%s", projectName.c_str());

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (editor.loadingFramesBeforeExecute > 0) {
        --editor.loadingFramesBeforeExecute;
        return;
    }

    ExecuteLoadingOperation(editor, window);
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

    if (ImGui::Button("New Project")) {
        if (editor.newProjectName[0] == '\0') {
            std::strncpy(editor.newProjectName, "project", sizeof(editor.newProjectName) - 1);
        }
        editor.showNewProjectDialog = true;
        ImGui::OpenPopup("New Project");
    }
    ImGui::SameLine();

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
            ? (
                HasPsdExtension(editor.pendingPsdPath)
                    ? editor.pendingPsdPath
                    : PsdPathForProjectPath(
                        !editor.document.projectPath.empty()
                            ? editor.document.projectPath
                            : editor.pendingPath
                    )
            )
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

    if (editor.showNewProjectDialog) {
        ImGui::OpenPopup("New Project");
        editor.showNewProjectDialog = false;
    }
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("Name", editor.newProjectName, sizeof(editor.newProjectName));

        const std::string sanitizedName = SanitizeProjectBaseName(editor.newProjectName);
        const std::string newProjectPath = ProjectPathForBaseName(sanitizedName);
        ImGui::Text("File: %s", newProjectPath.c_str());

        if (ImGui::Button("Create")) {
            ScheduleLoadingOperation(editor, EditorLoadingOperation::NewProject, newProjectPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (editor.showLoadProjectDialog) {
        ImGui::OpenPopup("Load Project");
        editor.showLoadProjectDialog = false;
    }
    drawPathDialog("Load Project", editor.pendingProjectPath, "Load", [&]() {
        ScheduleLoadingOperation(editor, EditorLoadingOperation::LoadPath, editor.pendingProjectPath);
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
        ScheduleLoadingOperation(editor, EditorLoadingOperation::AttachPsd, editor.pendingPsdPath);
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
