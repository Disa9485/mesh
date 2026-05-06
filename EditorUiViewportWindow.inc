static void DrawViewport(EditorState& editor) {
    ImGui::Begin("Viewport");

    DrawViewportControlPanel(editor);
    ImGui::Separator();

    if (ImGui::Button(editor.showHistoryPanel ? "Hide History" : "Show History")) {
        editor.showHistoryPanel = !editor.showHistoryPanel;
    }
    ImGui::SameLine();

    const bool canUndo = !editor.history.undoStack.empty();
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

    const bool canRedo = !editor.history.redoStack.empty();
    if (!canRedo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Redo")) {
        RedoLastOperation(editor);
    }
    if (!canRedo) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    ImGui::Checkbox("Checkerboard", &editor.showCheckerboard);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Zoom", &editor.zoom, 0.05f, 8.0f, "%.2fx");

    ImGui::SameLine();

    if (ImGui::Button("Fit View")) {
        editor.requestFitView = true;
    }

    ImGui::Separator();

    const ImVec2 canvasAvail = ImGui::GetContentRegionAvail();

    if (editor.requestFitView) {
        FitDocumentToViewport(editor, canvasAvail);
        editor.requestFitView = false;
    }

    ImGui::InvisibleButton(
        "viewport_canvas",
        canvasAvail,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonMiddle |
        ImGuiButtonFlags_MouseButtonRight
    );

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const float oldZoom = editor.zoom;
            editor.zoom = std::clamp(editor.zoom * (wheel > 0.0f ? 1.1f : 0.9f), 0.05f, 8.0f);

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 before(
                (mouse.x - itemMin.x - editor.pan.x) / oldZoom,
                (mouse.y - itemMin.y - editor.pan.y) / oldZoom
            );

            editor.pan.x = mouse.x - itemMin.x - before.x * editor.zoom;
            editor.pan.y = mouse.y - itemMin.y - before.y * editor.zoom;
        }
    }

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        editor.pan.x += delta.x;
        editor.pan.y += delta.y;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();

    drawList->PushClipRect(itemMin, itemMax, true);
    drawList->AddRectFilled(itemMin, itemMax, IM_COL32(25, 26, 30, 255));

    if (!editor.document.layers.empty()) {
        const ImVec2 imagePos(
            itemMin.x + editor.pan.x,
            itemMin.y + editor.pan.y
        );

        const ImVec2 imageSize(
            static_cast<float>(editor.document.canvasWidth) * editor.zoom,
            static_cast<float>(editor.document.canvasHeight) * editor.zoom
        );

        const ImGuiIO& io = ImGui::GetIO();
        bool deformVertexInputConsumed = false;
        gMaskRenderEditor = &editor;
        gMaskRenderImagePos = imagePos;
        gMaskRenderImageSize = imageSize;
        gMaskRenderZoom = editor.zoom;
        gMaskRenderMeshes =
            editor.mode == EditorMode::Mesh &&
            editor.meshModeGuideMeshes.size() == editor.document.layers.size()
                ? &editor.meshModeGuideMeshes
                : nullptr;

        const bool snappingParameterForEdit = ContinueParameterEditSetpointSnap(editor);

        if (snappingParameterForEdit) {
            // Parameter is being eased onto a nearby setpoint; hold off on edits until it lands.
        } else if (editor.mode == EditorMode::Mesh) {
            HandleMeshEditing(editor, imagePos, hovered, editor.zoom);
        } else if (editor.mode == EditorMode::Layer) {
            deformVertexInputConsumed = HandleDeformVertexEditing(
                editor,
                imagePos,
                hovered,
                editor.zoom
            );
        }

        if (
            editor.mode == EditorMode::Layer &&
            !snappingParameterForEdit &&
            !deformVertexInputConsumed &&
            hovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        ) {
            if (ActiveParameterNeedsSetpointSnapBeforeEdit(editor)) {
                deformVertexInputConsumed = true;
            } else {
            const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, editor.zoom);

            LayerTransformMode transformMode = LayerTransformMode::None;
            int scaleAxisX = 0;
            int scaleAxisY = 0;
            if (
                IsValidLayerIndex(editor, editor.selectedLayer) &&
                PickSelectedLayersTransformHandle(
                    editor,
                    canvasPoint,
                    editor.zoom,
                    transformMode,
                    scaleAxisX,
                    scaleAxisY
                )
            ) {
                BeginLayerTransform(
                    editor,
                    editor.selectedLayer,
                    canvasPoint,
                    transformMode,
                    scaleAxisX,
                    scaleAxisY
                );
            } else {
                const int pickedLayer = PickLayerForLayerModeDrag(
                    editor,
                    canvasPoint.x,
                    canvasPoint.y
                );

                if (IsValidLayerIndex(editor, pickedLayer) && IsLayerSelected(editor, pickedLayer)) {
                    editor.selectedLayer = pickedLayer;
                } else {
                    if (
                        IsValidLayerIndex(editor, pickedLayer) &&
                        (io.KeyCtrl || io.KeyShift)
                    ) {
                        HandleLayerSelectionClick(editor, pickedLayer);
                    } else {
                        SelectSingleLayer(editor, pickedLayer);
                    }
                }

                if (IsValidLayerIndex(editor, pickedLayer)) {
                    BeginLayerTransform(
                        editor,
                        pickedLayer,
                        canvasPoint,
                        LayerTransformMode::Move,
                        0,
                        0
                    );
                } else {
                    editor.draggingLayer = false;
                    editor.draggedLayer = -1;
                    editor.layerTransformMode = LayerTransformMode::None;
                }
            }
            }
        }

        if (
            editor.mode == EditorMode::Layer &&
            !snappingParameterForEdit &&
            editor.draggingLayer &&
            editor.draggedLayer >= 0 &&
            editor.draggedLayer < static_cast<int>(editor.document.layers.size()) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)
        ) {
            const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, editor.zoom);

            const float dx = canvasPoint.x - editor.lastDragCanvasPos.x;
            const float dy = canvasPoint.y - editor.lastDragCanvasPos.y;
            const Vec2 totalDelta = canvasPoint - editor.layerDragStartCanvasPos;
            constexpr float kLayerDragThresholdPx = 4.0f;

            if (!editor.layerDragThresholdPassed) {
                if (Dot(totalDelta, totalDelta) >= kLayerDragThresholdPx * kLayerDragThresholdPx) {
                    editor.layerDragThresholdPassed = true;
                    editor.lastDragCanvasPos = canvasPoint;
                }
            } else if (editor.layerTransformMode == LayerTransformMode::Move && (dx != 0.0f || dy != 0.0f)) {
                for (const int layerIndex : editor.layerTransformLayerIndices) {
                    if (IsValidLayerIndex(editor, layerIndex)) {
                        TranslateLayerMesh(editor.document.layers[layerIndex], dx, dy);
                    }
                }
                editor.lastDragCanvasPos = canvasPoint;
                editor.dragLayerMoved = true;
                RebuildLayersAffectedByLayerGeometry(editor, editor.layerTransformLayerIndices);
            } else if (editor.layerTransformMode == LayerTransformMode::Scale) {
                ApplyLayerScaleTransformToSelection(
                    editor,
                    canvasPoint,
                    io.KeyShift
                );
                editor.lastDragCanvasPos = canvasPoint;
                editor.dragLayerMoved = true;
                RebuildLayersAffectedByLayerGeometry(editor, editor.layerTransformLayerIndices);
            } else if (editor.layerTransformMode == LayerTransformMode::Rotate) {
                ApplyLayerRotateTransformToSelection(
                    editor,
                    canvasPoint
                );
                editor.lastDragCanvasPos = canvasPoint;
                editor.dragLayerMoved = true;
                RebuildLayersAffectedByLayerGeometry(editor, editor.layerTransformLayerIndices);
            }
        }

        if (
            editor.mode == EditorMode::Layer &&
            editor.draggingLayer &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)
        ) {
            if (editor.dragLayerMoved && IsValidLayerIndex(editor, editor.draggedLayer)) {
                const std::vector<DeformParameter> parametersBefore = editor.parameters;
                UpdateActiveParameterEndpointForLayers(editor, editor.layerTransformLayerIndices);
                const std::vector<DeformParameter> parametersAfter = editor.parameters;
                for (std::size_t i = 0; i < editor.layerTransformLayerIndices.size(); ++i) {
                    const int layerIndex = editor.layerTransformLayerIndices[i];
                    if (
                        !IsValidLayerIndex(editor, layerIndex) ||
                        i >= editor.layerTransformStartStates.size()
                    ) {
                        continue;
                    }

                    PushLayerOperationWithParameters(
                        editor,
                        layerIndex,
                        editor.layerTransformStartStates[i],
                        CaptureLayerHistoryState(editor.document.layers[layerIndex]),
                        parametersBefore,
                        parametersAfter,
                        "Transform layer"
                    );
                }
            }

            editor.draggingLayer = false;
            editor.draggedLayer = -1;
            editor.layerTransformMode = LayerTransformMode::None;
            editor.dragLayerMoved = false;
            editor.layerDragThresholdPassed = false;
            editor.layerTransformStartMesh = {};
            editor.layerTransformLayerIndices.clear();
            editor.layerTransformStartStates.clear();
        }

        if (editor.showCheckerboard) {
            DrawCheckerboard(drawList, imagePos, imageSize, 16.0f * editor.zoom);
        }

        drawList->AddRect(
            imagePos,
            ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
            IM_COL32(100, 120, 150, 255)
        );

        drawList->AddCallback(SetPremultipliedAlphaBlendCallback, nullptr);

        std::vector<int> renderOrder;
        renderOrder.reserve(editor.document.layers.size());
        gLayerColorDrawParams.clear();
        gLayerColorDrawParams.reserve(editor.document.layers.size());
        for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
            renderOrder.push_back(i);
        }

        std::stable_sort(renderOrder.begin(), renderOrder.end(), [&](int a, int b) {
            return
                LayerEffectiveRenderOrder(editor.document.layers[a], a) >
                LayerEffectiveRenderOrder(editor.document.layers[b], b);
        });

        for (const int i : renderOrder) {
            const EditorLayer& layer = editor.document.layers[i];

            if (!layer.visible || !layer.texture) {
                continue;
            }

            DrawLayerWithGpuMasks(
                drawList,
                layer,
                i,
                imagePos,
                imageSize,
                editor.zoom,
                editor.mode == EditorMode::Layer ||
                    editor.mode == EditorMode::View ||
                    (
                        editor.mode == EditorMode::Mesh &&
                        editor.meshModeGuideMeshes.size() == editor.document.layers.size()
                    )
            );
        }

        drawList->AddCallback(RestoreStraightAlphaBlendCallback, nullptr);

        if (editor.mode == EditorMode::Layer) {
            for (const int selectedLayerIndex : editor.selectedLayers) {
                if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
                    continue;
                }

                const EditorLayer& layer = editor.document.layers[selectedLayerIndex];

                DrawLayerMeshWireframe(
                    drawList,
                    layer,
                    imagePos,
                    editor.zoom,
                    selectedLayerIndex == editor.selectedLayer
                        ? IM_COL32(255, 220, 90, 150)
                        : IM_COL32(70, 230, 130, 140),
                    selectedLayerIndex == editor.selectedLayer ? 1.5f : 1.0f
                );
            }

            DrawSelectedLayersTransformBox(drawList, editor, imagePos, editor.zoom);

            for (const int selectedLayerIndex : editor.selectedLayers) {
                if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
                    continue;
                }

                const EditorLayer& layer = editor.document.layers[selectedLayerIndex];
                for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
                    const MeshVertex& v = layer.mesh.vertices[i];
                    const ImVec2 vp(
                        imagePos.x + v.position.x * editor.zoom,
                        imagePos.y + v.position.y * editor.zoom
                    );

                    const bool selectedVertex = ContainsVertexRef(
                        editor.selectedDeformVertices,
                        SelectedVertexRef{
                            selectedLayerIndex,
                            static_cast<std::uint32_t>(i)
                        }
                    );

                    drawList->AddCircleFilled(
                        vp,
                        selectedVertex ? 6.0f : 4.0f,
                        selectedVertex
                            ? IM_COL32(255, 230, 70, 255)
                            : IM_COL32(70, 230, 130, 255)
                    );
                }
            }

            if (!editor.selectedDeformVertices.empty()) {
                DrawSelectedVertexTransformBox(drawList, editor, imagePos, editor.zoom);
            }
        }

        if (IsValidLayerIndex(editor, editor.selectedLayer)) {
            const EditorLayer& layer = editor.document.layers[editor.selectedLayer];

            float sx0 = 0.0f;
            float sy0 = 0.0f;
            float sx1 = 0.0f;
            float sy1 = 0.0f;

            if (editor.mode == EditorMode::Mesh && GetMeshBounds(layer.mesh, sx0, sy0, sx1, sy1)) {
                const ImVec2 p0(
                    imagePos.x + sx0 * editor.zoom,
                    imagePos.y + sy0 * editor.zoom
                );

                const ImVec2 p1(
                    imagePos.x + sx1 * editor.zoom,
                    imagePos.y + sy1 * editor.zoom
                );

                drawList->AddRect(p0, p1, IM_COL32(255, 220, 90, 255), 0.0f, 0, 2.0f);
            }

            if (editor.mode == EditorMode::Mesh) {
                DrawLayerMeshWireframe(
                    drawList,
                    layer,
                    imagePos,
                    editor.zoom,
                    IM_COL32(70, 230, 130, 220),
                    2.0f
                );
            }

            if (editor.mode == EditorMode::Mesh) {
                for (const std::uint32_t edgeIndex : editor.selectedEdges) {
                    if (edgeIndex >= layer.mesh.edges.size()) {
                        continue;
                    }

                    const MeshEdge& edge = layer.mesh.edges[edgeIndex];
                    if (
                        edge.a >= layer.mesh.vertices.size() ||
                        edge.b >= layer.mesh.vertices.size()
                    ) {
                        continue;
                    }

                    const Vec2 a = layer.mesh.vertices[edge.a].position;
                    const Vec2 b = layer.mesh.vertices[edge.b].position;
                    drawList->AddLine(
                        CanvasToScreenPoint(a, imagePos, editor.zoom),
                        CanvasToScreenPoint(b, imagePos, editor.zoom),
                        IM_COL32(255, 230, 70, 255),
                        3.0f
                    );
                }
            }

            if (editor.mode == EditorMode::Mesh) {
            for (std::size_t i = 0; i < layer.mesh.vertices.size(); ++i) {
                const MeshVertex& v = layer.mesh.vertices[i];
                const ImVec2 vp(
                    imagePos.x + v.position.x * editor.zoom,
                    imagePos.y + v.position.y * editor.zoom
                );

                const bool showVertexSelection =
                    editor.mode == EditorMode::Mesh ||
                    editor.mode == EditorMode::Layer;
                const bool selectedVertex = showVertexSelection &&
                    ContainsIndex(editor.selectedVertices, static_cast<std::uint32_t>(i));
                const ImU32 vertexColor = v.physicsEnabled
                    ? (selectedVertex ? IM_COL32(255, 150, 40, 255) : IM_COL32(80, 150, 255, 255))
                    : (selectedVertex ? IM_COL32(255, 230, 70, 255) : IM_COL32(70, 230, 130, 255));
                drawList->AddCircleFilled(
                    vp,
                    selectedVertex ? 6.0f : 4.0f,
                    showVertexSelection ? vertexColor : IM_COL32(255, 220, 90, 255)
                );
            }
            }

            if (
                (editor.mode == EditorMode::Mesh || editor.mode == EditorMode::Layer) &&
                editor.boxSelectingMesh
            ) {
                const ImVec2 p0 = CanvasToScreenPoint(
                    editor.boxSelectStartCanvasPos,
                    imagePos,
                    editor.zoom
                );
                const ImVec2 p1 = CanvasToScreenPoint(
                    editor.boxSelectCurrentCanvasPos,
                    imagePos,
                    editor.zoom
                );
                const ImVec2 r0(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
                const ImVec2 r1(std::max(p0.x, p1.x), std::max(p0.y, p1.y));

                if (editor.selectionDragShape == SelectionDragShape::Circle) {
                    const float radius = std::sqrt(
                        (p1.x - p0.x) * (p1.x - p0.x) +
                        (p1.y - p0.y) * (p1.y - p0.y)
                    );
                    drawList->AddCircleFilled(p0, radius, IM_COL32(90, 210, 255, 35), 48);
                    drawList->AddCircle(p0, radius, IM_COL32(255, 230, 70, 230), 48, 1.5f);
                } else {
                    drawList->AddRectFilled(r0, r1, IM_COL32(90, 210, 255, 35));
                    drawList->AddRect(r0, r1, IM_COL32(255, 230, 70, 230), 0.0f, 0, 1.5f);
                }
            }
        }
    } else {
        drawList->AddText(
            ImVec2(itemMin.x + 20.0f, itemMin.y + 20.0f),
            IM_COL32(180, 185, 195, 255),
            "Load a PSD to view its separated layers."
        );
    }

    drawList->PopClipRect();

    ImGui::End();
}
