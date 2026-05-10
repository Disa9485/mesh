#include "EditorSettings.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

struct EditorSettingsData {
    std::string lastProjectPath;
    std::string legacyLastPsdPath;
    EditorWindowSettings window;
};

static std::filesystem::path GetSettingsPath() {
    return std::filesystem::current_path() / "mesh_editor_settings.txt";
}

static bool ParseBool(const std::string& value) {
    return value == "1" || value == "true" || value == "True";
}

static int ParseIntOr(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

static EditorSettingsData LoadSettingsData() {
    EditorSettingsData data;

    std::ifstream file(GetSettingsPath(), std::ios::binary);
    if (!file) {
        return data;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    if (content.find('=') == std::string::npos) {
        data.legacyLastPsdPath = content;
        while (!data.legacyLastPsdPath.empty() && (data.legacyLastPsdPath.back() == '\0' || data.legacyLastPsdPath.back() == '\n' || data.legacyLastPsdPath.back() == '\r')) {
            data.legacyLastPsdPath.pop_back();
        }
        return data;
    }

    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t split = line.find('=');
        if (split == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1u);
        if (key == "last_project") {
            data.lastProjectPath = value;
        } else if (key == "last_psd") {
            data.legacyLastPsdPath = value;
        } else if (key == "window_x") {
            data.window.x = ParseIntOr(value, data.window.x);
            data.window.hasPlacement = true;
        } else if (key == "window_y") {
            data.window.y = ParseIntOr(value, data.window.y);
            data.window.hasPlacement = true;
        } else if (key == "window_width") {
            data.window.width = std::max(320, ParseIntOr(value, data.window.width));
            data.window.hasPlacement = true;
        } else if (key == "window_height") {
            data.window.height = std::max(240, ParseIntOr(value, data.window.height));
            data.window.hasPlacement = true;
        } else if (key == "window_maximized") {
            data.window.maximized = ParseBool(value);
            data.window.hasPlacement = true;
        } else if (key == "window_monitor_name") {
            data.window.monitorName = value;
            data.window.hasMonitor = !value.empty() || data.window.hasMonitor;
        } else if (key == "window_monitor_x") {
            data.window.monitorX = ParseIntOr(value, data.window.monitorX);
            data.window.hasMonitor = true;
        } else if (key == "window_monitor_y") {
            data.window.monitorY = ParseIntOr(value, data.window.monitorY);
            data.window.hasMonitor = true;
        } else if (key == "window_monitor_width") {
            data.window.monitorWidth = std::max(0, ParseIntOr(value, data.window.monitorWidth));
            data.window.hasMonitor = true;
        } else if (key == "window_monitor_height") {
            data.window.monitorHeight = std::max(0, ParseIntOr(value, data.window.monitorHeight));
            data.window.hasMonitor = true;
        }
    }

    if (data.window.x <= -30000 || data.window.y <= -30000) {
        data.window.x = 80;
        data.window.y = 80;
        data.window.width = std::max(1280, data.window.width);
        data.window.height = std::max(800, data.window.height);
        data.window.maximized = false;
        data.window.hasPlacement = true;
    }

    return data;
}

static void SaveSettingsData(const EditorSettingsData& data) {
    std::ofstream file(GetSettingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file << "last_project=" << data.lastProjectPath << "\n";
    file << "window_x=" << data.window.x << "\n";
    file << "window_y=" << data.window.y << "\n";
    file << "window_width=" << data.window.width << "\n";
    file << "window_height=" << data.window.height << "\n";
    file << "window_maximized=" << (data.window.maximized ? 1 : 0) << "\n";
    file << "window_monitor_name=" << data.window.monitorName << "\n";
    file << "window_monitor_x=" << data.window.monitorX << "\n";
    file << "window_monitor_y=" << data.window.monitorY << "\n";
    file << "window_monitor_width=" << data.window.monitorWidth << "\n";
    file << "window_monitor_height=" << data.window.monitorHeight << "\n";
}

void SaveLastPsdPath(const std::string&) {
    // Kept for older call sites. Project files are now the focused launch target.
}

std::string LoadLastPsdPath() {
    return LoadSettingsData().legacyLastPsdPath;
}

void SaveLastProjectPath(const std::string& path) {
    EditorSettingsData data = LoadSettingsData();
    data.lastProjectPath = path;
    SaveSettingsData(data);
}

std::string LoadLastProjectPath() {
    return LoadSettingsData().lastProjectPath;
}

EditorWindowSettings LoadEditorWindowSettings() {
    return LoadSettingsData().window;
}

void SaveEditorWindowSettings(const EditorWindowSettings& settings) {
    EditorSettingsData data = LoadSettingsData();
    data.window = settings;
    SaveSettingsData(data);
}
