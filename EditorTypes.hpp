#pragma once

#include <glad/glad.h>

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

static constexpr std::uint8_t kOpaquePixelAlphaThreshold = 1;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator-(const Vec2& a, const Vec2& b) {
    return Vec2{a.x - b.x, a.y - b.y};
}

inline float Dot(const Vec2& a, const Vec2& b) {
    return a.x * b.x + a.y * b.y;
}

inline float Len(const Vec2& v) {
    return std::sqrt(Dot(v, v));
}

struct MeshVertex {
    Vec2 position;
    Vec2 uv;
};

struct MeshEdge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;

    bool operator==(const MeshEdge&) const = default;
};

struct SelectedVertexRef {
    int layerIndex = -1;
    std::uint32_t vertexIndex = 0;

    bool operator==(const SelectedVertexRef&) const = default;
};

struct CopiedMeshVertexPosition {
    SelectedVertexRef ref;
    Vec2 position;
};

struct LayerMesh {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshEdge> edges;
};

struct DeformParameterMeshCorner {
    std::vector<int> parameterIndices;
    std::vector<std::uint8_t> parameterValues;
    LayerMesh mesh;
};

struct DeformParameterLayerState {
    int layerIndex = -1;
    LayerMesh meshAt0;
    LayerMesh meshAt1;
    std::vector<DeformParameterMeshCorner> meshCorners;
    float opacityAt0 = 1.0f;
    float opacityAt1 = 1.0f;
    std::string renderOrderOverrideAt0;
    std::string renderOrderOverrideAt1;
    std::vector<int> maskLayerIndicesAt0;
    std::vector<int> maskLayerIndicesAt1;
};

struct DeformParameter {
    std::string name;
    float value = 0.0f;
    bool affectsMesh = true;
    bool affectsRenderOrder = true;
    bool affectsMasking = true;
    bool affectsOpacity = true;
    std::vector<DeformParameterLayerState> layers;
};

struct MeshGeneratorSettings {
    int alphaThreshold = 2;
    int meshDetail = 1;
    int perimeterSpacing = 600;
    int perimeterBuffer = 25;
    int interiorDepthSpacing = 40;
    int interiorPointSpacing = 800;
    int maxPerimeterPoints = 500;
    int maxInteriorPoints = 1500;
};

struct LayerHistoryState {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool visible = true;
    float opacity = 1.0f;
    std::string renderOrderOverride;
    std::vector<int> maskLayerIndices;
    LayerMesh mesh;
};

struct LayerOperation {
    int layerIndex = -1;
    std::string description;
    LayerHistoryState before;
    LayerHistoryState after;
    bool hasParameterSnapshot = false;
    std::vector<DeformParameter> parametersBefore;
    std::vector<DeformParameter> parametersAfter;
    int selectedParameterBefore = -1;
    int selectedParameterAfter = -1;
};

struct EditHistory {
    std::vector<LayerOperation> undoStack;
    std::vector<LayerOperation> redoStack;
};

enum class EditorMode {
    Layer,
    Mesh
};

enum class LayerTransformMode {
    None,
    Move,
    Scale,
    Rotate
};

struct EditorLayer {
    std::string name;

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width = 0;
    int height = 0;

    GLuint texture = 0;
    bool visible = true;
    float opacity = 1.0f;
    std::string renderOrderOverride;
    std::vector<int> maskLayerIndices;
    float previewU0 = 0.0f;
    float previewV0 = 0.0f;
    float previewU1 = 1.0f;
    float previewV1 = 1.0f;

    std::vector<std::uint8_t> alpha;
    std::vector<std::uint8_t> baseRgba;
    std::vector<std::uint8_t> renderedRgba;

    LayerMesh mesh;
};

struct EditorDocument {
    std::string path;

    int canvasWidth = 0;
    int canvasHeight = 0;

    std::vector<EditorLayer> layers;
};

struct EditorState {
    EditorDocument document;

    std::string pendingPath;
    std::string statusText = "No PSD loaded.";
    std::string errorText;

    float zoom = 1.0f;
    ImVec2 pan = ImVec2(40.0f, 40.0f);

    int selectedLayer = -1;
    std::vector<int> selectedLayers;
    EditorMode mode = EditorMode::Layer;
    bool showCheckerboard = true;
    bool requestFitView = false;

    bool draggingLayer = false;
    int draggedLayer = -1;
    LayerTransformMode layerTransformMode = LayerTransformMode::None;
    Vec2 layerDragStartCanvasPos;
    Vec2 lastDragCanvasPos;
    LayerHistoryState dragStartLayerState;
    std::vector<int> layerTransformLayerIndices;
    std::vector<LayerHistoryState> layerTransformStartStates;
    bool dragLayerMoved = false;
    bool layerDragThresholdPassed = false;
    LayerMesh layerTransformStartMesh;
    Vec2 layerTransformCenter;
    Vec2 layerTransformStartMouse;
    int layerScaleAxisX = 0;
    int layerScaleAxisY = 0;

    std::vector<std::uint32_t> selectedVertices;
    std::vector<std::uint32_t> selectedEdges;
    std::vector<SelectedVertexRef> selectedDeformVertices;
    bool draggingMeshSelection = false;
    bool meshSelectionMoved = false;
    Vec2 lastMeshDragCanvasPos;
    LayerHistoryState meshEditStartLayerState;
    bool deformingVertices = false;
    bool deformVerticesMoved = false;
    LayerTransformMode deformTransformMode = LayerTransformMode::None;
    LayerHistoryState deformStartLayerState;
    std::vector<int> deformLayerIndices;
    std::vector<LayerHistoryState> deformStartLayerStates;
    LayerMesh deformStartMesh;
    Vec2 deformTransformCenter;
    Vec2 deformTransformStartMouse;
    Vec2 lastDeformDragCanvasPos;
    int deformScaleAxisX = 0;
    int deformScaleAxisY = 0;
    bool boxSelectingMesh = false;
    bool boxSelectAdditive = false;
    bool boxSelectionMoved = false;
    Vec2 boxSelectStartCanvasPos;
    Vec2 boxSelectCurrentCanvasPos;
    std::vector<CopiedMeshVertexPosition> meshPositionClipboard;

    std::vector<DeformParameter> parameters;
    int selectedParameter = -1;

    EditHistory history;
    MeshGeneratorSettings meshSettings;
    bool showMeshGeneratorSettings = false;
    bool pendingGenerateMeshConfirmation = false;
    std::vector<int> pendingGenerateMeshLayers;
};
