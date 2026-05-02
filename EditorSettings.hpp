#pragma once

#include <string>

struct EditorWindowSettings {
    int x = 80;
    int y = 80;
    int width = 1280;
    int height = 800;
    bool maximized = false;
    bool hasPlacement = false;
};

void SaveLastPsdPath(const std::string& path);
std::string LoadLastPsdPath();
void SaveLastProjectPath(const std::string& path);
std::string LoadLastProjectPath();
EditorWindowSettings LoadEditorWindowSettings();
void SaveEditorWindowSettings(const EditorWindowSettings& settings);
