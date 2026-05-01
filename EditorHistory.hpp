#pragma once

#include "EditorTypes.hpp"

#include <string>

LayerHistoryState CaptureLayerHistoryState(const EditorLayer& layer);
void ApplyLayerHistoryState(EditorLayer& layer, const LayerHistoryState& state);
bool IsValidLayerIndex(const EditorState& editor, int layerIndex);
void PushLayerOperation(
    EditorState& editor,
    int layerIndex,
    const LayerHistoryState& before,
    const LayerHistoryState& after,
    const std::string& description
);
void PushLayerOperationWithParameters(
    EditorState& editor,
    int layerIndex,
    const LayerHistoryState& before,
    const LayerHistoryState& after,
    const std::vector<DeformParameter>& parametersBefore,
    const std::vector<DeformParameter>& parametersAfter,
    const std::string& description
);
void PushParameterOperation(
    EditorState& editor,
    const std::vector<DeformParameter>& parametersBefore,
    int selectedParameterBefore,
    const std::vector<DeformParameter>& parametersAfter,
    int selectedParameterAfter,
    const std::string& description
);
bool UndoLastOperation(EditorState& editor);
bool RedoLastOperation(EditorState& editor);
