static void FitDocumentToViewport(EditorState& editor, ImVec2 viewportSize) {
    if (
        editor.document.canvasWidth <= 0 ||
        editor.document.canvasHeight <= 0 ||
        viewportSize.x <= 1.0f ||
        viewportSize.y <= 1.0f
    ) {
        return;
    }

    const float padding = 40.0f;

    const float availableW = std::max(1.0f, viewportSize.x - padding * 2.0f);
    const float availableH = std::max(1.0f, viewportSize.y - padding * 2.0f);

    const float scaleX = availableW / static_cast<float>(editor.document.canvasWidth);
    const float scaleY = availableH / static_cast<float>(editor.document.canvasHeight);

    editor.zoom = std::clamp(std::min(scaleX, scaleY), 0.05f, 8.0f);

    const float imageW = static_cast<float>(editor.document.canvasWidth) * editor.zoom;
    const float imageH = static_cast<float>(editor.document.canvasHeight) * editor.zoom;

    editor.pan.x = (viewportSize.x - imageW) * 0.5f;
    editor.pan.y = (viewportSize.y - imageH) * 0.5f;
}

static bool GetQuadMeshBoundsAndUv(
    const EditorLayer& layer,
    float& x0,
    float& y0,
    float& x1,
    float& y1,
    float& u0,
    float& v0,
    float& u1,
    float& v1
) {
    if (layer.mesh.vertices.size() < 4) {
        return false;
    }

    x0 = layer.mesh.vertices[0].position.x;
    y0 = layer.mesh.vertices[0].position.y;
    x1 = layer.mesh.vertices[0].position.x;
    y1 = layer.mesh.vertices[0].position.y;

    u0 = layer.mesh.vertices[0].uv.x;
    v0 = layer.mesh.vertices[0].uv.y;
    u1 = layer.mesh.vertices[0].uv.x;
    v1 = layer.mesh.vertices[0].uv.y;

    for (const MeshVertex& vertex : layer.mesh.vertices) {
        x0 = std::min(x0, vertex.position.x);
        y0 = std::min(y0, vertex.position.y);
        x1 = std::max(x1, vertex.position.x);
        y1 = std::max(y1, vertex.position.y);

        u0 = std::min(u0, vertex.uv.x);
        v0 = std::min(v0, vertex.uv.y);
        u1 = std::max(u1, vertex.uv.x);
        v1 = std::max(v1, vertex.uv.y);
    }

    return x1 > x0 && y1 > y0 && u1 > u0 && v1 > v0;
}

static bool LayerAlphaHitTest(
    const EditorLayer& layer,
    float canvasX,
    float canvasY,
    std::uint8_t alphaThreshold = kOpaquePixelAlphaThreshold
) {
    if (
        !layer.visible ||
        layer.opacity <= 0.001f ||
        layer.alpha.empty() ||
        layer.width <= 0 ||
        layer.height <= 0
    ) {
        return false;
    }

    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;

    if (!GetQuadMeshBoundsAndUv(layer, x0, y0, x1, y1, u0, v0, u1, v1)) {
        return false;
    }

    if (canvasX < x0 || canvasX >= x1 || canvasY < y0 || canvasY >= y1) {
        return false;
    }

    const float tx = (canvasX - x0) / (x1 - x0);
    const float ty = (canvasY - y0) / (y1 - y0);

    const float u = u0 + tx * (u1 - u0);
    const float v = v0 + ty * (v1 - v0);

    int px = static_cast<int>(u * static_cast<float>(layer.width));
    int py = static_cast<int>(v * static_cast<float>(layer.height));

    px = std::clamp(px, 0, layer.width - 1);
    py = std::clamp(py, 0, layer.height - 1);

    return LayerAlphaAt(layer, px, py) >= alphaThreshold;
}

static int PickTopmostLayerAtCanvasPoint(
    const EditorState& editor,
    float canvasX,
    float canvasY
) {
    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        const EditorLayer& layer = editor.document.layers[i];

        if (LayerAlphaHitTest(layer, canvasX, canvasY)) {
            return i;
        }
    }

    return -1;
}

static int PickSelectedLayerAtCanvasPoint(
    const EditorState& editor,
    float canvasX,
    float canvasY
) {
    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (LayerAlphaHitTest(layer, canvasX, canvasY)) {
            return layerIndex;
        }
    }

    return -1;
}

static int PickLayerForLayerModeDrag(
    const EditorState& editor,
    float canvasX,
    float canvasY
) {
    const int selectedLayerHit = PickSelectedLayerAtCanvasPoint(editor, canvasX, canvasY);
    if (selectedLayerHit >= 0) {
        return selectedLayerHit;
    }

    return PickTopmostLayerAtCanvasPoint(editor, canvasX, canvasY);
}

static bool PickVertexInSelectedLayers(
    const EditorState& editor,
    Vec2 canvasPoint,
    float zoom,
    SelectedVertexRef& picked
) {
    for (const int layerIndex : editor.selectedLayers) {
        if (!IsValidLayerIndex(editor, layerIndex)) {
            continue;
        }

        const EditorLayer& layer = editor.document.layers[layerIndex];
        if (!layer.visible) {
            continue;
        }

        const int vertexIndex = PickVertexAtCanvasPoint(layer, canvasPoint, zoom);
        if (vertexIndex >= 0) {
            picked = SelectedVertexRef{layerIndex, static_cast<std::uint32_t>(vertexIndex)};
            return true;
        }
    }

    return false;
}
