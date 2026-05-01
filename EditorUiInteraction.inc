static void HandleMeshEditing(
    EditorState& editor,
    ImVec2 imagePos,
    bool hovered,
    float zoom
) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const int pickedVertex = PickVertexAtCanvasPoint(layer, canvasPoint, zoom);

        if (pickedVertex >= 0) {
            const std::uint32_t vertex = static_cast<std::uint32_t>(pickedVertex);

            if (io.KeyShift) {
                ToggleIndex(editor.selectedVertices, vertex);
            } else if (!ContainsIndex(editor.selectedVertices, vertex)) {
                ClearMeshSelection(editor);
                editor.selectedVertices.push_back(vertex);
            }

            editor.draggingMeshSelection = ContainsIndex(editor.selectedVertices, vertex);
            editor.meshSelectionMoved = false;
            editor.lastMeshDragCanvasPos = canvasPoint;
            editor.meshEditStartLayerState = CaptureLayerHistoryState(layer);
            return;
        }

        const int pickedEdge = PickEdgeAtCanvasPoint(layer, canvasPoint, zoom);
        if (pickedEdge >= 0) {
            const std::uint32_t edge = static_cast<std::uint32_t>(pickedEdge);

            if (io.KeyShift) {
                ToggleIndex(editor.selectedEdges, edge);
            } else {
                ClearMeshSelection(editor);
                editor.selectedEdges.push_back(edge);
            }
            return;
        }

        if (!CanvasPointInMeshBounds(layer.mesh, canvasPoint)) {
            const int pickedLayer = PickTopmostLayerAtCanvasPoint(
                editor,
                canvasPoint.x,
                canvasPoint.y
            );

            if (
                IsValidLayerIndex(editor, pickedLayer) &&
                pickedLayer != editor.selectedLayer
            ) {
                SelectSingleLayer(editor, pickedLayer);
                return;
            }
        }

        editor.boxSelectingMesh = true;
        editor.boxSelectAdditive = io.KeyShift;
        editor.boxSelectionMoved = false;
        editor.boxSelectStartCanvasPos = canvasPoint;
        editor.boxSelectCurrentCanvasPos = canvasPoint;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const int pickedVertex = PickVertexAtCanvasPoint(layer, canvasPoint, zoom);

        if (pickedVertex >= 0) {
            const LayerHistoryState before = CaptureLayerHistoryState(layer);
            if (HasSelectedVertex(editor)) {
                AddMeshEdgeIfMissing(
                    layer.mesh,
                    ActiveSelectedVertex(editor),
                    static_cast<std::uint32_t>(pickedVertex)
                );
            }

            ClearMeshSelection(editor);
            editor.selectedVertices.push_back(static_cast<std::uint32_t>(pickedVertex));
            UpdateLayerBoundsFromMesh(layer);
            PushMeshEditOperation(editor, before, "Add mesh edge");
            return;
        }

        AddVertexAtCanvasPoint(editor, canvasPoint, io.KeyShift);
    }

    if (
        editor.draggingMeshSelection &&
        !editor.selectedVertices.empty() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)
    ) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const float dx = canvasPoint.x - editor.lastMeshDragCanvasPos.x;
        const float dy = canvasPoint.y - editor.lastMeshDragCanvasPos.y;

        if (dx != 0.0f || dy != 0.0f) {
            for (const std::uint32_t vertex : editor.selectedVertices) {
                if (vertex >= layer.mesh.vertices.size()) {
                    continue;
                }

                layer.mesh.vertices[vertex].position.x += dx;
                layer.mesh.vertices[vertex].position.y += dy;
                RefreshMeshVertexUv(layer, layer.mesh.vertices[vertex]);
            }

            editor.lastMeshDragCanvasPos = canvasPoint;
            editor.meshSelectionMoved = true;
            UpdateLayerBoundsFromMesh(layer);
            RebuildLayersAffectedByLayerGeometry(editor, editor.selectedLayer);
        }
    }

    if (editor.draggingMeshSelection && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.meshSelectionMoved) {
            PushMeshEditOperation(editor, editor.meshEditStartLayerState, "Move mesh vertices");
        }

        editor.draggingMeshSelection = false;
        editor.meshSelectionMoved = false;
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        editor.boxSelectCurrentCanvasPos = canvasPoint;

        const Vec2 delta = editor.boxSelectCurrentCanvasPos - editor.boxSelectStartCanvasPos;
        if (Dot(delta, delta) > 4.0f) {
            editor.boxSelectionMoved = true;
        }
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.boxSelectionMoved) {
            ApplyMeshBoxSelection(editor, layer);
        } else if (!editor.boxSelectAdditive) {
            ClearMeshSelection(editor);
        }

        editor.boxSelectingMesh = false;
        editor.boxSelectAdditive = false;
        editor.boxSelectionMoved = false;
    }
}

static bool HandleDeformVertexEditing(
    EditorState& editor,
    ImVec2 imagePos,
    bool hovered,
    float zoom
) {
    int activeLayerIndex = editor.selectedLayer;
    if (!IsValidLayerIndex(editor, activeLayerIndex)) {
        for (const int layerIndex : editor.selectedLayers) {
            if (IsValidLayerIndex(editor, layerIndex)) {
                activeLayerIndex = layerIndex;
                break;
            }
        }
    }

    if (!IsValidLayerIndex(editor, activeLayerIndex)) {
        return false;
    }

    editor.selectedLayer = activeLayerIndex;
    EditorLayer& layer = editor.document.layers[activeLayerIndex];
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const bool insideSelectedLayerBox = CanvasPointInSelectedLayerTransformBox(editor, canvasPoint, zoom);

        LayerTransformMode transformMode = LayerTransformMode::None;
        int scaleAxisX = 0;
        int scaleAxisY = 0;
        if (
            !editor.selectedDeformVertices.empty() &&
            PickDeformVertexTransformHandle(
                editor,
                canvasPoint,
                zoom,
                transformMode,
                scaleAxisX,
                scaleAxisY
            )
        ) {
            if (transformMode == LayerTransformMode::Move) {
                float sx0 = 0.0f;
                float sy0 = 0.0f;
                float sx1 = 0.0f;
                float sy1 = 0.0f;
                if (GetSelectedLayerBounds(editor, sx0, sy0, sx1, sy1)) {
                    PadBounds(sx0, sy0, sx1, sy1, zoom);
                    const Vec2 layerCenter{(sx0 + sx1) * 0.5f, (sy0 + sy1) * 0.5f};
                    const float centerRadius = std::max(18.0f / std::max(zoom, 0.0001f), 8.0f);
                    const Vec2 delta = canvasPoint - layerCenter;
                    if (Dot(delta, delta) <= centerRadius * centerRadius) {
                        return false;
                    }
                }
            }

            BeginDeformVertexTransform(
                editor,
                layer,
                canvasPoint,
                transformMode,
                scaleAxisX,
                scaleAxisY
            );
            return true;
        }

        if (
            PickSelectedLayersTransformHandle(
                editor,
                canvasPoint,
                zoom,
                transformMode,
                scaleAxisX,
                scaleAxisY
            )
        ) {
            return false;
        }

        SelectedVertexRef pickedVertex;
        if (PickVertexInSelectedLayers(editor, canvasPoint, zoom, pickedVertex)) {

            if (io.KeyShift) {
                ToggleVertexRef(editor.selectedDeformVertices, pickedVertex);
            } else if (!ContainsVertexRef(editor.selectedDeformVertices, pickedVertex)) {
                ClearDeformVertexSelection(editor);
                editor.selectedDeformVertices.push_back(pickedVertex);
            }

            editor.selectedEdges.clear();
            if (ContainsVertexRef(editor.selectedDeformVertices, pickedVertex)) {
                BeginDeformVertexTransform(
                    editor,
                    editor.document.layers[pickedVertex.layerIndex],
                    canvasPoint,
                    LayerTransformMode::Move,
                    0,
                    0
                );
            }
            return true;
        }

        if (
            (
                insideSelectedLayerBox ||
                CanvasPointInMeshBounds(layer.mesh, canvasPoint)
            ) &&
            (
                insideSelectedLayerBox ||
                io.KeyShift ||
                !editor.selectedDeformVertices.empty() ||
                !LayerAlphaHitTest(layer, canvasPoint.x, canvasPoint.y)
            )
        ) {
            editor.boxSelectingMesh = true;
            editor.boxSelectAdditive = io.KeyShift;
            editor.boxSelectionMoved = false;
            editor.boxSelectStartCanvasPos = canvasPoint;
            editor.boxSelectCurrentCanvasPos = canvasPoint;
            return true;
        }
    }

    if (
        editor.deformingVertices &&
        !editor.selectedDeformVertices.empty() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)
    ) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        const Vec2 totalDelta = canvasPoint - editor.deformTransformStartMouse;
        constexpr float kDeformDragThresholdPx = 4.0f;

        if (!editor.deformVerticesMoved) {
            if (Dot(totalDelta, totalDelta) < kDeformDragThresholdPx * kDeformDragThresholdPx) {
                return true;
            }
        }

        if (editor.deformTransformMode == LayerTransformMode::Move) {
            const float dx = canvasPoint.x - editor.lastDeformDragCanvasPos.x;
            const float dy = canvasPoint.y - editor.lastDeformDragCanvasPos.y;

            if (dx != 0.0f || dy != 0.0f) {
                for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
                    if (!IsValidLayerIndex(editor, ref.layerIndex)) {
                        continue;
                    }

                    EditorLayer& targetLayer = editor.document.layers[ref.layerIndex];
                    if (ref.vertexIndex >= targetLayer.mesh.vertices.size()) {
                        continue;
                    }

                    targetLayer.mesh.vertices[ref.vertexIndex].position.x += dx;
                    targetLayer.mesh.vertices[ref.vertexIndex].position.y += dy;
                }
                editor.lastDeformDragCanvasPos = canvasPoint;
                editor.deformVerticesMoved = true;
                for (const int layerIndex : editor.deformLayerIndices) {
                    if (IsValidLayerIndex(editor, layerIndex)) {
                        UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
                    }
                }
                RebuildLayersAffectedByLayerGeometry(editor, editor.deformLayerIndices);
            }
        } else if (editor.deformTransformMode == LayerTransformMode::Scale) {
            ApplyDeformVertexScaleTransform(editor, layer, canvasPoint, io.KeyShift);
            editor.lastDeformDragCanvasPos = canvasPoint;
            editor.deformVerticesMoved = true;
            RebuildLayersAffectedByLayerGeometry(editor, editor.deformLayerIndices);
        } else if (editor.deformTransformMode == LayerTransformMode::Rotate) {
            ApplyDeformVertexRotateTransform(editor, layer, canvasPoint);
            editor.lastDeformDragCanvasPos = canvasPoint;
            editor.deformVerticesMoved = true;
            RebuildLayersAffectedByLayerGeometry(editor, editor.deformLayerIndices);
        }

        return true;
    }

    if (editor.deformingVertices && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.deformVerticesMoved) {
            const std::vector<DeformParameter> parametersBefore = editor.parameters;
            UpdateActiveParameterEndpointForLayers(editor, editor.deformLayerIndices);
            const std::vector<DeformParameter> parametersAfter = editor.parameters;
            for (std::size_t i = 0; i < editor.deformLayerIndices.size(); ++i) {
                const int layerIndex = editor.deformLayerIndices[i];
                if (
                    !IsValidLayerIndex(editor, layerIndex) ||
                    i >= editor.deformStartLayerStates.size()
                ) {
                    continue;
                }

                PushLayerOperationWithParameters(
                    editor,
                    layerIndex,
                    editor.deformStartLayerStates[i],
                    CaptureLayerHistoryState(editor.document.layers[layerIndex]),
                    parametersBefore,
                    parametersAfter,
                    "Deform vertices"
                );
            }
        }

        editor.deformingVertices = false;
        editor.deformVerticesMoved = false;
        editor.deformTransformMode = LayerTransformMode::None;
        editor.deformStartMesh = {};
        editor.deformLayerIndices.clear();
        editor.deformStartLayerStates.clear();
        return true;
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Vec2 canvasPoint = ScreenToCanvasPoint(io.MousePos, imagePos, zoom);
        editor.boxSelectCurrentCanvasPos = canvasPoint;

        const Vec2 delta = editor.boxSelectCurrentCanvasPos - editor.boxSelectStartCanvasPos;
        if (Dot(delta, delta) > 4.0f) {
            editor.boxSelectionMoved = true;
        }
        return true;
    }

    if (editor.boxSelectingMesh && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (editor.boxSelectionMoved) {
            ApplyVertexBoxSelection(editor);
        } else if (!editor.boxSelectAdditive) {
            ClearDeformVertexSelection(editor);
        }

        editor.boxSelectingMesh = false;
        editor.boxSelectAdditive = false;
        editor.boxSelectionMoved = false;
        return true;
    }

    return false;
}

