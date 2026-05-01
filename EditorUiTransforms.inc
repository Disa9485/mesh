static void BeginLayerTransform(
    EditorState& editor,
    int layerIndex,
    Vec2 canvasPoint,
    LayerTransformMode mode,
    int axisX,
    int axisY
) {
    if (!IsValidLayerIndex(editor, layerIndex)) {
        return;
    }

    EditorLayer& layer = editor.document.layers[layerIndex];

    editor.draggingLayer = true;
    editor.draggedLayer = layerIndex;
    editor.layerTransformMode = mode;
    editor.layerDragStartCanvasPos = canvasPoint;
    editor.lastDragCanvasPos = canvasPoint;
    editor.dragLayerMoved = false;
    editor.layerDragThresholdPassed = false;
    editor.dragStartLayerState = CaptureLayerHistoryState(layer);
    editor.layerTransformLayerIndices.clear();
    editor.layerTransformStartStates.clear();

    if (IsLayerSelected(editor, layerIndex)) {
        for (const int selectedLayerIndex : editor.selectedLayers) {
            if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
                continue;
            }

            editor.layerTransformLayerIndices.push_back(selectedLayerIndex);
            editor.layerTransformStartStates.push_back(
                CaptureLayerHistoryState(editor.document.layers[selectedLayerIndex])
            );
        }
    } else {
        editor.layerTransformLayerIndices.push_back(layerIndex);
        editor.layerTransformStartStates.push_back(CaptureLayerHistoryState(layer));
    }

    editor.layerTransformStartMesh = layer.mesh;
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    if (GetSelectedLayerBounds(editor, x0, y0, x1, y1)) {
        editor.layerTransformCenter = Vec2{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    } else {
        editor.layerTransformCenter = LayerMeshBoundsCenter(layer.mesh);
    }
    editor.layerTransformStartMouse = canvasPoint;
    editor.layerScaleAxisX = axisX;
    editor.layerScaleAxisY = axisY;
}

static void ApplyLayerScaleTransform(EditorState& editor, EditorLayer& layer, Vec2 canvasPoint, bool centered) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(editor.layerTransformStartMesh, x0, y0, x1, y1)) {
        return;
    }

    const Vec2 center = editor.layerTransformCenter;
    const Vec2 startMouse = editor.layerTransformStartMouse;
    const bool uniformScale =
        centered &&
        editor.layerScaleAxisX != 0 &&
        editor.layerScaleAxisY != 0;

    float originX = center.x;
    float originY = center.y;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (editor.layerScaleAxisX != 0) {
        originX = centered ? center.x : (editor.layerScaleAxisX < 0 ? x1 : x0);
        const float startDistance = startMouse.x - originX;
        const float currentDistance = canvasPoint.x - originX;
        if (std::abs(startDistance) > 0.0001f) {
            scaleX = currentDistance / startDistance;
        }
    }

    if (editor.layerScaleAxisY != 0) {
        originY = centered ? center.y : (editor.layerScaleAxisY < 0 ? y1 : y0);
        const float startDistance = startMouse.y - originY;
        const float currentDistance = canvasPoint.y - originY;
        if (std::abs(startDistance) > 0.0001f) {
            scaleY = currentDistance / startDistance;
        }
    }

    if (uniformScale) {
        const float startDx = startMouse.x - originX;
        const float startDy = startMouse.y - originY;
        const float currentDx = canvasPoint.x - originX;
        const float currentDy = canvasPoint.y - originY;
        const float startLenSq = startDx * startDx + startDy * startDy;

        if (startLenSq > 0.0001f) {
            const float projectedScale =
                (currentDx * startDx + currentDy * startDy) / startLenSq;
            scaleX = projectedScale;
            scaleY = projectedScale;
        }
    }

    if (std::abs(scaleX) < 0.01f) {
        scaleX = scaleX < 0.0f ? -0.01f : 0.01f;
    }

    if (std::abs(scaleY) < 0.01f) {
        scaleY = scaleY < 0.0f ? -0.01f : 0.01f;
    }

    layer.mesh = editor.layerTransformStartMesh;

    for (MeshVertex& vertex : layer.mesh.vertices) {
        vertex.position.x = originX + (vertex.position.x - originX) * scaleX;
        vertex.position.y = originY + (vertex.position.y - originY) * scaleY;
    }

    UpdateLayerBoundsFromMesh(layer);
}

static void ApplyLayerRotateTransform(EditorState& editor, EditorLayer& layer, Vec2 canvasPoint) {
    const Vec2 center = editor.layerTransformCenter;
    const float startAngle = std::atan2(
        editor.layerTransformStartMouse.y - center.y,
        editor.layerTransformStartMouse.x - center.x
    );
    const float currentAngle = std::atan2(
        canvasPoint.y - center.y,
        canvasPoint.x - center.x
    );
    const float angle = currentAngle - startAngle;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    layer.mesh = editor.layerTransformStartMesh;

    for (MeshVertex& vertex : layer.mesh.vertices) {
        const Vec2 delta = vertex.position - center;
        vertex.position.x = center.x + delta.x * c - delta.y * s;
        vertex.position.y = center.y + delta.x * s + delta.y * c;
    }

    UpdateLayerBoundsFromMesh(layer);
}

static bool GetLayerTransformStartBounds(
    const EditorState& editor,
    float& x0,
    float& y0,
    float& x1,
    float& y1
) {
    bool found = false;
    for (const LayerHistoryState& state : editor.layerTransformStartStates) {
        float lx0 = 0.0f;
        float ly0 = 0.0f;
        float lx1 = 0.0f;
        float ly1 = 0.0f;
        if (GetMeshBounds(state.mesh, lx0, ly0, lx1, ly1)) {
            ExpandBounds(x0, y0, x1, y1, lx0, ly0, lx1, ly1, found);
        }
    }

    return found;
}

static void ApplyLayerScaleTransformToSelection(EditorState& editor, Vec2 canvasPoint, bool centered) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetLayerTransformStartBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    const Vec2 center = editor.layerTransformCenter;
    const Vec2 startMouse = editor.layerTransformStartMouse;
    const bool uniformScale =
        centered &&
        editor.layerScaleAxisX != 0 &&
        editor.layerScaleAxisY != 0;

    float originX = center.x;
    float originY = center.y;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (editor.layerScaleAxisX != 0) {
        originX = centered ? center.x : (editor.layerScaleAxisX < 0 ? x1 : x0);
        const float startDistance = startMouse.x - originX;
        const float currentDistance = canvasPoint.x - originX;
        if (std::abs(startDistance) > 0.0001f) {
            scaleX = currentDistance / startDistance;
        }
    }

    if (editor.layerScaleAxisY != 0) {
        originY = centered ? center.y : (editor.layerScaleAxisY < 0 ? y1 : y0);
        const float startDistance = startMouse.y - originY;
        const float currentDistance = canvasPoint.y - originY;
        if (std::abs(startDistance) > 0.0001f) {
            scaleY = currentDistance / startDistance;
        }
    }

    if (uniformScale) {
        const float startDx = startMouse.x - originX;
        const float startDy = startMouse.y - originY;
        const float currentDx = canvasPoint.x - originX;
        const float currentDy = canvasPoint.y - originY;
        const float startLenSq = startDx * startDx + startDy * startDy;

        if (startLenSq > 0.0001f) {
            const float projectedScale =
                (currentDx * startDx + currentDy * startDy) / startLenSq;
            scaleX = projectedScale;
            scaleY = projectedScale;
        }
    }

    if (std::abs(scaleX) < 0.01f) {
        scaleX = scaleX < 0.0f ? -0.01f : 0.01f;
    }

    if (std::abs(scaleY) < 0.01f) {
        scaleY = scaleY < 0.0f ? -0.01f : 0.01f;
    }

    for (std::size_t i = 0; i < editor.layerTransformLayerIndices.size(); ++i) {
        const int layerIndex = editor.layerTransformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.layerTransformStartStates.size()
        ) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        layer.mesh = editor.layerTransformStartStates[i].mesh;

        for (MeshVertex& vertex : layer.mesh.vertices) {
            vertex.position.x = originX + (vertex.position.x - originX) * scaleX;
            vertex.position.y = originY + (vertex.position.y - originY) * scaleY;
        }

        UpdateLayerBoundsFromMesh(layer);
    }
}

static void ApplyLayerRotateTransformToSelection(EditorState& editor, Vec2 canvasPoint) {
    const Vec2 center = editor.layerTransformCenter;
    const float startAngle = std::atan2(
        editor.layerTransformStartMouse.y - center.y,
        editor.layerTransformStartMouse.x - center.x
    );
    const float currentAngle = std::atan2(
        canvasPoint.y - center.y,
        canvasPoint.x - center.x
    );
    const float angle = currentAngle - startAngle;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    for (std::size_t i = 0; i < editor.layerTransformLayerIndices.size(); ++i) {
        const int layerIndex = editor.layerTransformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.layerTransformStartStates.size()
        ) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[layerIndex];
        layer.mesh = editor.layerTransformStartStates[i].mesh;

        for (MeshVertex& vertex : layer.mesh.vertices) {
            const Vec2 delta = vertex.position - center;
            vertex.position.x = center.x + delta.x * c - delta.y * s;
            vertex.position.y = center.y + delta.x * s + delta.y * c;
        }

        UpdateLayerBoundsFromMesh(layer);
    }
}

static void BeginDeformVertexTransform(
    EditorState& editor,
    EditorLayer& layer,
    Vec2 canvasPoint,
    LayerTransformMode mode,
    int axisX,
    int axisY
) {
    editor.deformingVertices = true;
    editor.deformVerticesMoved = false;
    editor.deformTransformMode = mode;
    editor.deformStartLayerState = CaptureLayerHistoryState(layer);
    editor.deformLayerIndices.clear();
    editor.deformStartLayerStates.clear();
    for (const int selectedLayerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, selectedLayerIndex)) {
            continue;
        }

        editor.deformLayerIndices.push_back(selectedLayerIndex);
        editor.deformStartLayerStates.push_back(
            CaptureLayerHistoryState(editor.document.layers[selectedLayerIndex])
        );
    }
    editor.deformStartMesh = layer.mesh;
    editor.deformTransformStartMouse = canvasPoint;
    editor.lastDeformDragCanvasPos = canvasPoint;
    editor.deformScaleAxisX = axisX;
    editor.deformScaleAxisY = axisY;

    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    if (GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        editor.deformTransformCenter = Vec2{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f};
    } else {
        editor.deformTransformCenter = canvasPoint;
    }
}

static void ApplyDeformVertexScaleTransform(
    EditorState& editor,
    EditorLayer& layer,
    Vec2 canvasPoint,
    bool centered
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    const Vec2 center = editor.deformTransformCenter;
    const Vec2 startMouse = editor.deformTransformStartMouse;
    const bool uniformScale =
        centered &&
        editor.deformScaleAxisX != 0 &&
        editor.deformScaleAxisY != 0;

    float originX = center.x;
    float originY = center.y;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (editor.deformScaleAxisX != 0) {
        originX = centered ? center.x : (editor.deformScaleAxisX < 0 ? x1 : x0);
        const float startDistance = startMouse.x - originX;
        const float currentDistance = canvasPoint.x - originX;
        if (std::abs(startDistance) > 0.0001f) {
            scaleX = currentDistance / startDistance;
        }
    }

    if (editor.deformScaleAxisY != 0) {
        originY = centered ? center.y : (editor.deformScaleAxisY < 0 ? y1 : y0);
        const float startDistance = startMouse.y - originY;
        const float currentDistance = canvasPoint.y - originY;
        if (std::abs(startDistance) > 0.0001f) {
            scaleY = currentDistance / startDistance;
        }
    }

    if (uniformScale) {
        const float startDx = startMouse.x - originX;
        const float startDy = startMouse.y - originY;
        const float currentDx = canvasPoint.x - originX;
        const float currentDy = canvasPoint.y - originY;
        const float startLenSq = startDx * startDx + startDy * startDy;

        if (startLenSq > 0.0001f) {
            const float projectedScale =
                (currentDx * startDx + currentDy * startDy) / startLenSq;
            scaleX = projectedScale;
            scaleY = projectedScale;
        }
    }

    if (std::abs(scaleX) < 0.01f) {
        scaleX = scaleX < 0.0f ? -0.01f : 0.01f;
    }

    if (std::abs(scaleY) < 0.01f) {
        scaleY = scaleY < 0.0f ? -0.01f : 0.01f;
    }

    for (std::size_t i = 0; i < editor.deformLayerIndices.size(); ++i) {
        const int layerIndex = editor.deformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.deformStartLayerStates.size()
        ) {
            continue;
        }
        editor.document.layers[layerIndex].mesh = editor.deformStartLayerStates[i].mesh;
    }

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        EditorLayer& targetLayer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= targetLayer.mesh.vertices.size()) {
            continue;
        }

        MeshVertex& vertex = targetLayer.mesh.vertices[ref.vertexIndex];
        vertex.position.x = originX + (vertex.position.x - originX) * scaleX;
        vertex.position.y = originY + (vertex.position.y - originY) * scaleY;
    }

    for (const int layerIndex : editor.deformLayerIndices) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
        }
    }
}

static void ApplyDeformVertexRotateTransform(
    EditorState& editor,
    EditorLayer& layer,
    Vec2 canvasPoint
) {
    const Vec2 center = editor.deformTransformCenter;
    const float startAngle = std::atan2(
        editor.deformTransformStartMouse.y - center.y,
        editor.deformTransformStartMouse.x - center.x
    );
    const float currentAngle = std::atan2(
        canvasPoint.y - center.y,
        canvasPoint.x - center.x
    );
    const float angle = currentAngle - startAngle;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    for (std::size_t i = 0; i < editor.deformLayerIndices.size(); ++i) {
        const int layerIndex = editor.deformLayerIndices[i];
        if (
            !IsValidLayerIndex(editor, layerIndex) ||
            i >= editor.deformStartLayerStates.size()
        ) {
            continue;
        }
        editor.document.layers[layerIndex].mesh = editor.deformStartLayerStates[i].mesh;
    }

    for (const SelectedVertexRef ref : editor.selectedDeformVertices) {
        if (!IsValidLayerIndex(editor, ref.layerIndex)) {
            continue;
        }

        EditorLayer& targetLayer = editor.document.layers[ref.layerIndex];
        if (ref.vertexIndex >= targetLayer.mesh.vertices.size()) {
            continue;
        }

        MeshVertex& vertex = targetLayer.mesh.vertices[ref.vertexIndex];
        const Vec2 delta = vertex.position - center;
        vertex.position.x = center.x + delta.x * c - delta.y * s;
        vertex.position.y = center.y + delta.x * s + delta.y * c;
    }

    for (const int layerIndex : editor.deformLayerIndices) {
        if (IsValidLayerIndex(editor, layerIndex)) {
            UpdateLayerBoundsFromMesh(editor.document.layers[layerIndex]);
        }
    }
}

