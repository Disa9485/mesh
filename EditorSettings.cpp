#include "EditorSettings.hpp"

#include <filesystem>
#include <fstream>

static std::filesystem::path GetSettingsPath() {
    return std::filesystem::current_path() / "mesh_editor_settings.txt";
}

void SaveLastPsdPath(const std::string& path) {
    std::ofstream file(GetSettingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file << path;
}

std::string LoadLastPsdPath() {
    std::ifstream file(GetSettingsPath(), std::ios::binary);
    if (!file) {
        return {};
    }

    std::string path;
    std::getline(file, path, '\0');
    return path;
}
