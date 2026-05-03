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
    std::vector<float> parameterValues;
    LayerMesh mesh;
};

struct DeformParameterLayerSetpoint {
    float value = 0.0f;
    LayerMesh mesh;
    int textureIndex = -1;
    float opacity = 1.0f;
    float hueShift = 0.0f;
    float saturationScale = 100.0f;
    float lightnessShift = 0.0f;
    std::string renderOrderOverride;
    std::vector<int> maskLayerIndices;
};

struct DeformParameterLayerState {
    int layerIndex = -1;
    LayerMesh meshAt0;
    LayerMesh meshAt1;
    int textureIndexAt0 = -1;
    int textureIndexAt1 = -1;
    std::vector<DeformParameterMeshCorner> meshCorners;
    std::vector<DeformParameterLayerSetpoint> setpoints;
    float opacityAt0 = 1.0f;
    float opacityAt1 = 1.0f;
    float hueShiftAt0 = 0.0f;
    float hueShiftAt1 = 0.0f;
    float saturationScaleAt0 = 100.0f;
    float saturationScaleAt1 = 100.0f;
    float lightnessShiftAt0 = 0.0f;
    float lightnessShiftAt1 = 0.0f;
    std::string renderOrderOverrideAt0;
    std::string renderOrderOverrideAt1;
    std::vector<int> maskLayerIndicesAt0;
    std::vector<int> maskLayerIndicesAt1;
};

enum class DeformParameterType {
    Slider,
    State
};

struct DeformParameter {
    std::string name;
    DeformParameterType type = DeformParameterType::Slider;
    float value = 0.0f;
    int selectedState = 0;
    std::vector<std::string> stateNames;
    bool affectsMesh = true;
    bool affectsRenderOrder = true;
    bool affectsMasking = true;
    bool affectsOpacity = true;
    bool affectsTexture = false;
    bool affectsColor = false;
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
    int textureIndex = -1;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool visible = true;
    float opacity = 1.0f;
    float hueShift = 0.0f;
    float saturationScale = 100.0f;
    float lightnessShift = 0.0f;
    std::string renderOrderOverride;
    std::vector<int> maskLayerIndices;
    LayerMesh mesh;
};

struct LayerListHistoryState {
    std::string name;
    LayerHistoryState state;
};

struct LayerOperation {
    int layerIndex = -1;
    std::string description;
    LayerHistoryState before;
    LayerHistoryState after;
    bool hasLayerListSnapshot = false;
    std::vector<LayerListHistoryState> layersBefore;
    std::vector<LayerListHistoryState> layersAfter;
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

enum class SelectionDragShape {
    Rectangle,
    Circle
};

struct EditorLayer {
    std::string name;
    int textureIndex = -1;

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width = 0;
    int height = 0;

    GLuint texture = 0;
    bool visible = true;
    float opacity = 1.0f;
    float hueShift = 0.0f;
    float saturationScale = 100.0f;
    float lightnessShift = 0.0f;
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

struct EditorTexture {
    std::string name;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int width = 0;
    int height = 0;
    GLuint texture = 0;
    float previewU0 = 0.0f;
    float previewV0 = 0.0f;
    float previewU1 = 1.0f;
    float previewV1 = 1.0f;
    std::vector<std::uint8_t> alpha;
    std::vector<std::uint8_t> baseRgba;
    std::vector<std::uint8_t> renderedRgba;
};

struct EditorDocument {
    std::string path;
    std::string projectPath;
    std::string psdPath;

    int canvasWidth = 0;
    int canvasHeight = 0;

    std::vector<EditorTexture> textures;
    std::vector<EditorLayer> layers;
};

struct EditorState {
    EditorDocument document;

    std::string pendingPath;
    std::string pendingProjectPath;
    std::string pendingPsdPath;
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
    SelectionDragShape selectionDragShape = SelectionDragShape::Rectangle;
    std::vector<CopiedMeshVertexPosition> meshPositionClipboard;

    std::vector<DeformParameter> parameters;
    int selectedParameter = -1;
    int selectedParameterSetpointParameter = -1;
    float selectedParameterSetpointValue = -1.0f;
    bool draggingParameterSetpoint = false;
    bool pendingParameterSetpointDrag = false;
    float parameterSetpointDragStartMouseX = 0.0f;
    float parameterSetpointDragStartValue = 0.0f;
    double parameterSetpointDragStartTime = 0.0;
    bool parameterEditSnapActive = false;
    float parameterEditSnapTarget = 0.0f;
    std::vector<DeformParameter> parameterSetpointDragBefore;
    int parameterSetpointDragSelectedBefore = -1;
    bool showAddParameterTypePopup = false;
    bool showAddParameterStatePopup = false;
    int addParameterStateTarget = -1;
    char addParameterStateName[128] = {};

    EditHistory history;
    MeshGeneratorSettings meshSettings;
    bool showMeshGeneratorSettings = false;
    bool showHistoryPanel = false;
    bool showLoadProjectDialog = false;
    bool showSaveProjectAsDialog = false;
    bool showAttachPsdDialog = false;
    bool duplicateLayerCopyParameters = false;
    bool showAlignMeshesPopup = false;
    int alignMeshesTargetLayer = -1;
    int alignMeshesSourceLayer = -1;
    int alignMeshesParameter = -1;
    float alignMeshesStartSetpoint = 0.0f;
    bool showLayerColorDialog = false;
    int colorDialogLayer = -1;
    float colorDialogHue = 0.0f;
    float colorDialogSaturation = 100.0f;
    float colorDialogLightness = 0.0f;
    float colorDialogOriginalHue = 0.0f;
    float colorDialogOriginalSaturation = 100.0f;
    float colorDialogOriginalLightness = 0.0f;
    bool hasLayerColorClipboard = false;
    float colorClipboardHue = 0.0f;
    float colorClipboardSaturation = 100.0f;
    float colorClipboardLightness = 0.0f;
    bool pendingGenerateMeshConfirmation = false;
    std::vector<int> pendingGenerateMeshLayers;
};
