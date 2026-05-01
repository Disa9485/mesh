#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LayerImageRGBA {
    std::string name;

    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width = 0;
    int height = 0;

    bool visible = true;

    std::vector<std::uint8_t> rgba;
};

struct PsdDocumentRGBA {
    int canvasWidth = 0;
    int canvasHeight = 0;
    int bitsPerChannel = 0;

    // Stored in PSD layer-stack order as returned by psd_sdk.
    std::vector<LayerImageRGBA> layers;
};

class PsdLoader {
public:
    static bool LoadPsd(const std::string& psdPath, PsdDocumentRGBA& out, std::string& error);
};
