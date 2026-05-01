#include "PsdLoader.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef _WIN32
    #include <Windows.h>
#endif

#include "Psd.h"
#include "PsdMallocAllocator.h"
#include "PsdNativeFile.h"
#include "PsdDocument.h"
#include "PsdColorMode.h"
#include "PsdLayerMaskSection.h"
#include "PsdParseDocument.h"
#include "PsdParseLayerMaskSection.h"
#include "PsdLayerCanvasCopy.h"
#include "PsdInterleave.h"
#include "PsdChannelType.h"
#include "PsdChannel.h"
#include "PsdLayer.h"

PSD_USING_NAMESPACE;

namespace {

constexpr unsigned int CHANNEL_NOT_FOUND = 0xFFFFFFFFu;

unsigned int FindChannel(Layer* layer, int16_t channelType) {
    for (unsigned int i = 0; i < layer->channelCount; ++i) {
        Channel* channel = &layer->channels[i];
        if (channel->data && channel->type == channelType) {
            return i;
        }
    }

    return CHANNEL_NOT_FOUND;
}

template <typename T>
void* ExpandChannelToCanvas(
    Allocator* allocator,
    const Layer* layer,
    const void* data,
    unsigned int canvasW,
    unsigned int canvasH
) {
    T* canvasData = static_cast<T*>(
        allocator->Allocate(sizeof(T) * canvasW * canvasH, 16u)
    );

    std::memset(canvasData, 0, sizeof(T) * canvasW * canvasH);

    imageUtil::CopyLayerData(
        static_cast<const T*>(data),
        canvasData,
        layer->left,
        layer->top,
        layer->right,
        layer->bottom,
        canvasW,
        canvasH
    );

    return canvasData;
}

void* ExpandChannelToCanvas(
    const Document* doc,
    Allocator* allocator,
    Layer* layer,
    Channel* channel
) {
    if (doc->bitsPerChannel == 8) {
        return ExpandChannelToCanvas<std::uint8_t>(
            allocator,
            layer,
            channel->data,
            doc->width,
            doc->height
        );
    }

    return nullptr;
}

std::string LayerNameToUtf8(const Layer* layer) {
    if (layer->utf16Name) {
#ifdef _WIN32
        const wchar_t* w = reinterpret_cast<const wchar_t*>(layer->utf16Name);

        int needed = WideCharToMultiByte(
            CP_UTF8,
            0,
            w,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (needed <= 0) {
            return layer->name.c_str();
        }

        std::string out;
        out.resize(static_cast<std::size_t>(needed - 1));

        WideCharToMultiByte(
            CP_UTF8,
            0,
            w,
            -1,
            out.data(),
            needed,
            nullptr,
            nullptr
        );

        return out;
#else
        return layer->name.c_str();
#endif
    }

    return layer->name.c_str();
}

bool IsLayerEffectivelyVisible(const Layer* layer) {
    for (const Layer* current = layer; current; current = current->parent) {
        if (!current->isVisible) {
            return false;
        }
    }

    return true;
}

std::wstring Utf8ToWide(const std::string& s) {
#ifdef _WIN32
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return {};
    }

    std::wstring out;
    out.resize(static_cast<std::size_t>(needed));

    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), needed);

    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }

    return out;
#else
    return std::wstring(s.begin(), s.end());
#endif
}

} // namespace

bool PsdLoader::LoadPsd(
    const std::string& psdPath,
    PsdDocumentRGBA& out,
    std::string& error
) {
    out = PsdDocumentRGBA{};
    error.clear();

    MallocAllocator allocator;
    NativeFile file(&allocator);

#ifdef _WIN32
    const std::wstring wpath = Utf8ToWide(psdPath);

    if (!file.OpenRead(wpath.c_str())) {
        error = "psd_sdk: cannot open PSD file: " + psdPath;
        return false;
    }
#else
    error = "This psd_sdk loader path is currently implemented for Windows only.";
    return false;
#endif

    Document* document = CreateDocument(&file, &allocator);
    if (!document) {
        error = "psd_sdk: CreateDocument failed.";
        file.Close();
        return false;
    }

    if (document->colorMode != colorMode::RGB) {
        error = "PSD must be RGB color mode.";
        DestroyDocument(document, &allocator);
        file.Close();
        return false;
    }

    if (document->bitsPerChannel != 8) {
        error = "PSD must be 8 bits per channel for this first editor scaffold.";
        DestroyDocument(document, &allocator);
        file.Close();
        return false;
    }

    out.canvasWidth = static_cast<int>(document->width);
    out.canvasHeight = static_cast<int>(document->height);
    out.bitsPerChannel = static_cast<int>(document->bitsPerChannel);

    LayerMaskSection* layerMaskSection =
        ParseLayerMaskSection(document, &file, &allocator);

    if (!layerMaskSection) {
        error = "psd_sdk: ParseLayerMaskSection failed.";
        DestroyDocument(document, &allocator);
        file.Close();
        return false;
    }

    for (unsigned int i = 0; i < layerMaskSection->layerCount; ++i) {
        Layer* layer = &layerMaskSection->layers[i];

        ExtractLayer(document, &file, &allocator, layer);

        const unsigned int indexR = FindChannel(layer, channelType::R);
        const unsigned int indexG = FindChannel(layer, channelType::G);
        const unsigned int indexB = FindChannel(layer, channelType::B);
        const unsigned int indexA = FindChannel(layer, channelType::TRANSPARENCY_MASK);

        if (
            indexR == CHANNEL_NOT_FOUND ||
            indexG == CHANNEL_NOT_FOUND ||
            indexB == CHANNEL_NOT_FOUND
        ) {
            continue;
        }

        void* canvasR = ExpandChannelToCanvas(
            document,
            &allocator,
            layer,
            &layer->channels[indexR]
        );

        void* canvasG = ExpandChannelToCanvas(
            document,
            &allocator,
            layer,
            &layer->channels[indexG]
        );

        void* canvasB = ExpandChannelToCanvas(
            document,
            &allocator,
            layer,
            &layer->channels[indexB]
        );

        void* canvasA = nullptr;

        if (indexA != CHANNEL_NOT_FOUND) {
            canvasA = ExpandChannelToCanvas(
                document,
                &allocator,
                layer,
                &layer->channels[indexA]
            );
        }

        if (!canvasR || !canvasG || !canvasB) {
            allocator.Free(canvasR);
            allocator.Free(canvasG);
            allocator.Free(canvasB);
            allocator.Free(canvasA);
            continue;
        }

        std::vector<std::uint8_t> rgba;
        rgba.resize(
            static_cast<std::size_t>(document->width) *
            static_cast<std::size_t>(document->height) *
            4u
        );

        const std::uint8_t* r = static_cast<const std::uint8_t*>(canvasR);
        const std::uint8_t* g = static_cast<const std::uint8_t*>(canvasG);
        const std::uint8_t* b = static_cast<const std::uint8_t*>(canvasB);

        if (canvasA) {
            const std::uint8_t* a = static_cast<const std::uint8_t*>(canvasA);
            imageUtil::InterleaveRGBA(
                r,
                g,
                b,
                a,
                rgba.data(),
                document->width,
                document->height
            );
        } else {
            imageUtil::InterleaveRGB(
                r,
                g,
                b,
                std::uint8_t(255),
                rgba.data(),
                document->width,
                document->height
            );
        }

        allocator.Free(canvasR);
        allocator.Free(canvasG);
        allocator.Free(canvasB);
        allocator.Free(canvasA);

        LayerImageRGBA li;
        li.name = LayerNameToUtf8(layer);
        li.left = layer->left;
        li.top = layer->top;
        li.right = layer->right;
        li.bottom = layer->bottom;
        li.width = static_cast<int>(document->width);
        li.height = static_cast<int>(document->height);
        li.visible = IsLayerEffectivelyVisible(layer);
        li.rgba = std::move(rgba);

        if (li.name.empty()) {
            li.name = "Layer " + std::to_string(out.layers.size());
        }

        out.layers.push_back(std::move(li));
    }

    DestroyLayerMaskSection(layerMaskSection, &allocator);
    DestroyDocument(document, &allocator);
    file.Close();

    if (out.layers.empty()) {
        error = "No pixel layers with RGB channels were extracted.";
        return false;
    }

    return true;
}
