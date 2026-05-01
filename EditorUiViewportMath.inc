static Vec2 ScreenToCanvasPoint(
    ImVec2 mouseScreen,
    ImVec2 imagePos,
    float zoom
) {
    if (zoom <= 0.0001f) {
        return {};
    }

    return Vec2{
        (mouseScreen.x - imagePos.x) / zoom,
        (mouseScreen.y - imagePos.y) / zoom
    };
}

