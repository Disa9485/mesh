#pragma once

#include "EditorTypes.hpp"

struct GLFWwindow;

void DrawEditor(EditorState& editor, GLFWwindow* window);
void DropCallback(GLFWwindow* window, int count, const char** paths);
