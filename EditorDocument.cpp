#include "EditorDocument.hpp"

#include "EditorSettings.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::uint64_t MeshEdgeKey(std::uint32_t a, std::uint32_t b) {
    if (a > b) {
        std::swap(a, b);
    }

    return (static_cast<std::uint64_t>(a) << 32u) | static_cast<std::uint64_t>(b);
}

static float TriangleSignedArea(const LayerMesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const Vec2& pa = mesh.vertices[a].position;
    const Vec2& pb = mesh.vertices[b].position;
    const Vec2& pc = mesh.vertices[c].position;

    return
        (pb.x - pa.x) * (pc.y - pa.y) -
        (pb.y - pa.y) * (pc.x - pa.x);
}

static void PremultiplyAlpha(std::vector<std::uint8_t>& rgba) {
    const std::size_t pixelCount = rgba.size() / 4u;

    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t* p = &rgba[i * 4u];

        const unsigned int a = p[3];

        p[0] = static_cast<std::uint8_t>((static_cast<unsigned int>(p[0]) * a + 127u) / 255u);
        p[1] = static_cast<std::uint8_t>((static_cast<unsigned int>(p[1]) * a + 127u) / 255u);
        p[2] = static_cast<std::uint8_t>((static_cast<unsigned int>(p[2]) * a + 127u) / 255u);
    }
}

static std::vector<std::uint8_t> ExtractAlphaChannel(
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height
) {
    std::vector<std::uint8_t> alpha;

    if (
        width <= 0 ||
        height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height) * 4u
    ) {
        return alpha;
    }

    alpha.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixelIndex =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);

            alpha[pixelIndex] = rgba[pixelIndex * 4u + 3u];
        }
    }

    return alpha;
}

static void ComputeTexturePreviewUvs(EditorTexture& texture) {
    if (texture.width <= 0 || texture.height <= 0 || texture.alpha.empty()) {
        texture.previewU0 = 0.0f;
        texture.previewV0 = 0.0f;
        texture.previewU1 = 1.0f;
        texture.previewV1 = 1.0f;
        return;
    }

    int minX = texture.width;
    int minY = texture.height;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                static_cast<std::size_t>(x);

            if (index >= texture.alpha.size() || texture.alpha[index] <= kOpaquePixelAlphaThreshold) {
                continue;
            }

            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        texture.previewU0 = 0.0f;
        texture.previewV0 = 0.0f;
        texture.previewU1 = 1.0f;
        texture.previewV1 = 1.0f;
        return;
    }

    const float cropMinX = static_cast<float>(minX);
    const float cropMinY = static_cast<float>(minY);
    const float cropMaxX = static_cast<float>(maxX + 1);
    const float cropMaxY = static_cast<float>(maxY + 1);
    const float centerX = (cropMinX + cropMaxX) * 0.5f;
    const float centerY = (cropMinY + cropMaxY) * 0.5f;
    const float side = std::max(cropMaxX - cropMinX, cropMaxY - cropMinY);
    const float halfSide = side * 0.5f;

    const float x0 = std::clamp(centerX - halfSide, 0.0f, static_cast<float>(texture.width));
    const float y0 = std::clamp(centerY - halfSide, 0.0f, static_cast<float>(texture.height));
    const float x1 = std::clamp(centerX + halfSide, 0.0f, static_cast<float>(texture.width));
    const float y1 = std::clamp(centerY + halfSide, 0.0f, static_cast<float>(texture.height));

    const float invW = 1.0f / static_cast<float>(std::max(1, texture.width));
    const float invH = 1.0f / static_cast<float>(std::max(1, texture.height));
    texture.previewU0 = x0 * invW;
    texture.previewV0 = y0 * invH;
    texture.previewU1 = x1 * invW;
    texture.previewV1 = y1 * invH;
}

static GLuint UploadTextureRGBA(const std::uint8_t* rgba, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        w,
        h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    );

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static void UpdateTextureRGBA(GLuint texture, const std::uint8_t* rgba, int w, int h) {
    if (!texture || !rgba || w <= 0 || h <= 0) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        w,
        h,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    );
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DestroyDocument(EditorDocument& doc) {
    for (EditorTexture& texture : doc.textures) {
        if (texture.texture) {
            glDeleteTextures(1, &texture.texture);
            texture.texture = 0;
        }
    }

    doc = EditorDocument{};
}

static void DestroyDocumentTextures(EditorDocument& doc) {
    for (EditorTexture& texture : doc.textures) {
        if (texture.texture) {
            glDeleteTextures(1, &texture.texture);
            texture.texture = 0;
        }
    }
    doc.textures.clear();
}

static bool LoadPsdTextures(
    const std::string& path,
    EditorDocument& document,
    std::string& error
) {
    PsdDocumentRGBA psd;
    if (!PsdLoader::LoadPsd(path, psd, error)) {
        return false;
    }

    std::reverse(psd.layers.begin(), psd.layers.end());
    DestroyDocumentTextures(document);

    document.psdPath = path;
    document.canvasWidth = psd.canvasWidth;
    document.canvasHeight = psd.canvasHeight;
    document.textures.reserve(psd.layers.size());

    for (const LayerImageRGBA& src : psd.layers) {
        EditorTexture texture;
        texture.name = src.name;
        texture.left = src.left;
        texture.top = src.top;
        texture.right = src.right;
        texture.bottom = src.bottom;
        texture.width = src.width;
        texture.height = src.height;
        texture.alpha = ExtractAlphaChannel(src.rgba, src.width, src.height);
        std::vector<std::uint8_t> premultipliedRgba = src.rgba;
        PremultiplyAlpha(premultipliedRgba);
        texture.baseRgba = premultipliedRgba;
        texture.renderedRgba = premultipliedRgba;
        texture.texture = UploadTextureRGBA(premultipliedRgba.data(), src.width, src.height);
        ComputeTexturePreviewUvs(texture);
        document.textures.push_back(std::move(texture));
    }

    return true;
}

static EditorLayer CreateLayerForTexture(const EditorTexture& texture, int textureIndex) {
    EditorLayer layer;
    layer.name = texture.name;
    layer.left = texture.left;
    layer.top = texture.top;
    layer.right = texture.right;
    layer.bottom = texture.bottom;
    layer.visible = true;
    return layer;
}

static LayerMesh CreateInitialQuadMeshForEditorTexture(const EditorTexture& texture) {
    LayerMesh mesh;
    if (texture.width <= 0 || texture.height <= 0 || texture.alpha.empty()) {
        return mesh;
    }

    int minX = texture.width;
    int minY = texture.height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                static_cast<std::size_t>(x);
            if (index < texture.alpha.size() && texture.alpha[index] > kOpaquePixelAlphaThreshold) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return mesh;
    }

    const float x0 = static_cast<float>(minX);
    const float y0 = static_cast<float>(minY);
    const float x1 = static_cast<float>(maxX + 1);
    const float y1 = static_cast<float>(maxY + 1);
    const float w = static_cast<float>(std::max(1, texture.width));
    const float h = static_cast<float>(std::max(1, texture.height));
    mesh.vertices = {
        {{x0, y0}, {x0 / w, y0 / h}},
        {{x1, y0}, {x1 / w, y0 / h}},
        {{x1, y1}, {x1 / w, y1 / h}},
        {{x0, y1}, {x0 / w, y1 / h}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.edges = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}};
    return mesh;
}

void ApplyTextureToLayer(EditorDocument& document, EditorLayer& layer, int textureIndex) {
    if (textureIndex < 0 || textureIndex >= static_cast<int>(document.textures.size())) {
        layer.textureIndex = -1;
        layer.width = 0;
        layer.height = 0;
        layer.texture = 0;
        layer.previewU0 = 0.0f;
        layer.previewV0 = 0.0f;
        layer.previewU1 = 1.0f;
        layer.previewV1 = 1.0f;
        layer.alpha.clear();
        layer.baseRgba.clear();
        layer.renderedRgba.clear();
        return;
    }

    const EditorTexture& texture = document.textures[textureIndex];
    layer.textureIndex = textureIndex;
    layer.width = texture.width;
    layer.height = texture.height;
    layer.texture = texture.texture;
    layer.previewU0 = texture.previewU0;
    layer.previewV0 = texture.previewV0;
    layer.previewU1 = texture.previewU1;
    layer.previewV1 = texture.previewV1;
    layer.alpha = texture.alpha;
    layer.baseRgba = texture.baseRgba;
    layer.renderedRgba = texture.renderedRgba;
}

static std::uint8_t LayerMaskAlphaAtCanvasPoint(
    const EditorLayer& layer,
    Vec2 canvasPoint
) {
    if (
        layer.alpha.empty() ||
        layer.width <= 0 ||
        layer.height <= 0 ||
        layer.mesh.vertices.empty() ||
        layer.mesh.indices.empty()
    ) {
        return 0;
    }

    auto barycentric = [](Vec2 p, Vec2 a, Vec2 b, Vec2 c, float& u, float& v, float& w) {
        const Vec2 v0{b.x - a.x, b.y - a.y};
        const Vec2 v1{c.x - a.x, c.y - a.y};
        const Vec2 v2{p.x - a.x, p.y - a.y};
        const float d00 = Dot(v0, v0);
        const float d01 = Dot(v0, v1);
        const float d11 = Dot(v1, v1);
        const float d20 = Dot(v2, v0);
        const float d21 = Dot(v2, v1);
        const float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) <= 0.000001f) {
            return false;
        }

        v = (d11 * d20 - d01 * d21) / denom;
        w = (d00 * d21 - d01 * d20) / denom;
        u = 1.0f - v - w;
        constexpr float kEpsilon = -0.0005f;
        return u >= kEpsilon && v >= kEpsilon && w >= kEpsilon;
    };

    for (std::size_t i = 0; i + 2u < layer.mesh.indices.size(); i += 3u) {
        const std::uint32_t ia = layer.mesh.indices[i + 0u];
        const std::uint32_t ib = layer.mesh.indices[i + 1u];
        const std::uint32_t ic = layer.mesh.indices[i + 2u];
        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size()
        ) {
            continue;
        }

        const MeshVertex& a = layer.mesh.vertices[ia];
        const MeshVertex& b = layer.mesh.vertices[ib];
        const MeshVertex& c = layer.mesh.vertices[ic];

        float u = 0.0f;
        float v = 0.0f;
        float w = 0.0f;
        if (!barycentric(canvasPoint, a.position, b.position, c.position, u, v, w)) {
            continue;
        }

        const float texU = a.uv.x * u + b.uv.x * v + c.uv.x * w;
        const float texV = a.uv.y * u + b.uv.y * v + c.uv.y * w;
        const int x = static_cast<int>(std::floor(texU * static_cast<float>(layer.width)));
        const int y = static_cast<int>(std::floor(texV * static_cast<float>(layer.height)));
        if (x < 0 || x >= layer.width || y < 0 || y >= layer.height) {
            return 0;
        }

        const std::size_t index =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) +
            static_cast<std::size_t>(x);

        return index < layer.alpha.size() ? layer.alpha[index] : 0;
    }

    return 0;
}

static bool LayerTexturePointToCanvasPoint(
    const EditorLayer& layer,
    float textureX,
    float textureY,
    Vec2& canvasPoint
) {
    if (
        layer.width <= 0 ||
        layer.height <= 0 ||
        layer.mesh.vertices.empty() ||
        layer.mesh.indices.empty()
    ) {
        return false;
    }

    auto barycentric = [](Vec2 p, Vec2 a, Vec2 b, Vec2 c, float& u, float& v, float& w) {
        const Vec2 v0{b.x - a.x, b.y - a.y};
        const Vec2 v1{c.x - a.x, c.y - a.y};
        const Vec2 v2{p.x - a.x, p.y - a.y};
        const float d00 = Dot(v0, v0);
        const float d01 = Dot(v0, v1);
        const float d11 = Dot(v1, v1);
        const float d20 = Dot(v2, v0);
        const float d21 = Dot(v2, v1);
        const float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) <= 0.000001f) {
            return false;
        }

        v = (d11 * d20 - d01 * d21) / denom;
        w = (d00 * d21 - d01 * d20) / denom;
        u = 1.0f - v - w;
        constexpr float kEpsilon = -0.0005f;
        return u >= kEpsilon && v >= kEpsilon && w >= kEpsilon;
    };

    const Vec2 texturePoint{
        textureX / static_cast<float>(layer.width),
        textureY / static_cast<float>(layer.height)
    };

    for (std::size_t i = 0; i + 2u < layer.mesh.indices.size(); i += 3u) {
        const std::uint32_t ia = layer.mesh.indices[i + 0u];
        const std::uint32_t ib = layer.mesh.indices[i + 1u];
        const std::uint32_t ic = layer.mesh.indices[i + 2u];
        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size()
        ) {
            continue;
        }

        const MeshVertex& a = layer.mesh.vertices[ia];
        const MeshVertex& b = layer.mesh.vertices[ib];
        const MeshVertex& c = layer.mesh.vertices[ic];

        float u = 0.0f;
        float v = 0.0f;
        float w = 0.0f;
        if (!barycentric(texturePoint, a.uv, b.uv, c.uv, u, v, w)) {
            continue;
        }

        canvasPoint.x = a.position.x * u + b.position.x * v + c.position.x * w;
        canvasPoint.y = a.position.y * u + b.position.y * v + c.position.y * w;
        return true;
    }

    return false;
}

void RebuildLayerRenderedTexture(EditorState& editor, int layerIndex) {
    if (
        layerIndex < 0 ||
        layerIndex >= static_cast<int>(editor.document.layers.size())
    ) {
        return;
    }

    EditorLayer& layer = editor.document.layers[layerIndex];
    if (
        layer.width <= 0 ||
        layer.height <= 0 ||
        layer.baseRgba.size() !=
            static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height) * 4u
    ) {
        return;
    }

    layer.renderedRgba = layer.baseRgba;
    UpdateTextureRGBA(layer.texture, layer.renderedRgba.data(), layer.width, layer.height);
}

LayerMesh CreateInitialQuadMeshForLayer(
    const LayerImageRGBA& layer,
    std::uint8_t alphaThreshold
) {
    LayerMesh mesh;

    if (
        layer.width <= 0 ||
        layer.height <= 0 ||
        layer.rgba.size() != static_cast<std::size_t>(layer.width) *
                             static_cast<std::size_t>(layer.height) * 4u
    ) {
        return mesh;
    }

    int minX = layer.width;
    int minY = layer.height;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < layer.height; ++y) {
        for (int x = 0; x < layer.width; ++x) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(layer.width) +
                 static_cast<std::size_t>(x)) * 4u;

            const std::uint8_t alpha = layer.rgba[idx + 3];

            if (alpha >= alphaThreshold) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return mesh;
    }

    const float x0 = static_cast<float>(minX);
    const float y0 = static_cast<float>(minY);
    const float x1 = static_cast<float>(maxX + 1);
    const float y1 = static_cast<float>(maxY + 1);

    const float cw = static_cast<float>(std::max(1, layer.width));
    const float ch = static_cast<float>(std::max(1, layer.height));

    mesh.vertices = {
        {{x0, y0}, {x0 / cw, y0 / ch}},
        {{x1, y0}, {x1 / cw, y0 / ch}},
        {{x1, y1}, {x1 / cw, y1 / ch}},
        {{x0, y1}, {x0 / cw, y1 / ch}},
    };

    mesh.indices = {0, 1, 2, 2, 3, 0};
    mesh.edges = {
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0},
    };

    return mesh;
}

bool GetMeshBounds(const LayerMesh& mesh, float& x0, float& y0, float& x1, float& y1) {
    if (mesh.vertices.empty()) {
        return false;
    }

    x0 = mesh.vertices[0].position.x;
    y0 = mesh.vertices[0].position.y;
    x1 = mesh.vertices[0].position.x;
    y1 = mesh.vertices[0].position.y;

    for (const MeshVertex& v : mesh.vertices) {
        x0 = std::min(x0, v.position.x);
        y0 = std::min(y0, v.position.y);
        x1 = std::max(x1, v.position.x);
        y1 = std::max(y1, v.position.y);
    }

    return true;
}

void RebuildMeshTrianglesFromEdges(LayerMesh& mesh) {
    mesh.indices.clear();

    const std::size_t vertexCount = mesh.vertices.size();
    if (vertexCount < 3u) {
        mesh.edges.clear();
        return;
    }

    std::vector<std::vector<std::uint32_t>> adjacency(vertexCount);
    std::vector<MeshEdge> cleanEdges;
    cleanEdges.reserve(mesh.edges.size());

    std::unordered_set<std::uint64_t> seenEdges;
    seenEdges.reserve(mesh.edges.size());

    for (MeshEdge edge : mesh.edges) {
        if (
            edge.a == edge.b ||
            edge.a >= vertexCount ||
            edge.b >= vertexCount
        ) {
            continue;
        }

        if (edge.a > edge.b) {
            std::swap(edge.a, edge.b);
        }

        const std::uint64_t key = MeshEdgeKey(edge.a, edge.b);
        if (!seenEdges.insert(key).second) {
            continue;
        }

        cleanEdges.push_back(edge);
        adjacency[edge.a].push_back(edge.b);
        adjacency[edge.b].push_back(edge.a);
    }

    mesh.edges = std::move(cleanEdges);

    for (std::vector<std::uint32_t>& neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
    }

    for (std::uint32_t a = 0; a < adjacency.size(); ++a) {
        for (const std::uint32_t b : adjacency[a]) {
            if (b <= a) {
                continue;
            }

            std::vector<std::uint32_t> shared;
            std::set_intersection(
                adjacency[a].begin(),
                adjacency[a].end(),
                adjacency[b].begin(),
                adjacency[b].end(),
                std::back_inserter(shared)
            );

            for (const std::uint32_t c : shared) {
                if (c <= b) {
                    continue;
                }

                const float area = TriangleSignedArea(mesh, a, b, c);
                if (std::abs(area) <= 0.0001f) {
                    continue;
                }

                mesh.indices.push_back(a);
                mesh.indices.push_back(area < 0.0f ? b : c);
                mesh.indices.push_back(area < 0.0f ? c : b);
            }
        }
    }
}

static std::filesystem::path GetMeshSidecarPath(const std::string& psdPath) {
    std::filesystem::path path(psdPath);
    path.replace_extension(".mesh.json");
    return path;
}

static std::filesystem::path GetBinaryMeshSidecarPath(const std::string& psdPath) {
    std::filesystem::path path(psdPath);
    path.replace_extension(".mesh.bin");
    return path;
}

static std::filesystem::path GetDefaultProjectPathForPsd(const std::string& psdPath) {
    return GetBinaryMeshSidecarPath(psdPath);
}

template <typename T>
static bool WriteBinaryValue(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(out);
}

template <typename T>
static bool ReadBinaryValue(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

static bool WriteBinaryString(std::ostream& out, const std::string& value) {
    if (value.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }

    const std::uint32_t size = static_cast<std::uint32_t>(value.size());
    if (!WriteBinaryValue(out, size)) {
        return false;
    }

    out.write(value.data(), size);
    return static_cast<bool>(out);
}

static bool ReadBinaryString(std::istream& in, std::string& value) {
    std::uint32_t size = 0;
    if (!ReadBinaryValue(in, size)) {
        return false;
    }

    constexpr std::uint32_t kMaxStringBytes = 1024u * 1024u;
    if (size > kMaxStringBytes) {
        return false;
    }

    value.assign(size, '\0');
    in.read(value.data(), size);
    return static_cast<bool>(in);
}

static bool gReadBinaryMeshHasPhysics = false;

static bool WriteBinaryMesh(std::ostream& out, LayerMesh mesh) {
    RebuildMeshTrianglesFromEdges(mesh);

    if (
        mesh.vertices.size() > (std::numeric_limits<std::uint32_t>::max)() ||
        mesh.indices.size() > (std::numeric_limits<std::uint32_t>::max)() ||
        mesh.edges.size() > (std::numeric_limits<std::uint32_t>::max)()
    ) {
        return false;
    }

    const std::uint32_t vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    if (!WriteBinaryValue(out, vertexCount)) {
        return false;
    }

    for (const MeshVertex& vertex : mesh.vertices) {
        if (
            !WriteBinaryValue(out, vertex.position.x) ||
            !WriteBinaryValue(out, vertex.position.y) ||
            !WriteBinaryValue(out, vertex.uv.x) ||
            !WriteBinaryValue(out, vertex.uv.y)
        ) {
            return false;
        }

        const std::uint8_t physicsEnabled = vertex.physicsEnabled ? 1u : 0u;
        if (!WriteBinaryValue(out, physicsEnabled)) {
            return false;
        }
    }

    const std::uint32_t indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    if (!WriteBinaryValue(out, indexCount)) {
        return false;
    }

    for (const std::uint32_t index : mesh.indices) {
        if (!WriteBinaryValue(out, index)) {
            return false;
        }
    }

    const std::uint32_t edgeCount = static_cast<std::uint32_t>(mesh.edges.size());
    if (!WriteBinaryValue(out, edgeCount)) {
        return false;
    }

    for (const MeshEdge& edge : mesh.edges) {
        if (!WriteBinaryValue(out, edge.a) || !WriteBinaryValue(out, edge.b)) {
            return false;
        }
    }

    return true;
}

static bool ReadBinaryMesh(std::istream& in, LayerMesh& mesh) {
    LayerMesh parsed;

    std::uint32_t vertexCount = 0;
    if (!ReadBinaryValue(in, vertexCount)) {
        return false;
    }

    constexpr std::uint32_t kMaxVertices = 10'000'000u;
    if (vertexCount > kMaxVertices) {
        return false;
    }

    parsed.vertices.resize(vertexCount);
    for (MeshVertex& vertex : parsed.vertices) {
        if (
            !ReadBinaryValue(in, vertex.position.x) ||
            !ReadBinaryValue(in, vertex.position.y) ||
            !ReadBinaryValue(in, vertex.uv.x) ||
            !ReadBinaryValue(in, vertex.uv.y)
        ) {
            return false;
        }

        if (gReadBinaryMeshHasPhysics) {
            std::uint8_t physicsEnabled = 0u;
            if (!ReadBinaryValue(in, physicsEnabled)) {
                return false;
            }
            vertex.physicsEnabled = physicsEnabled != 0u;
        }
    }

    std::uint32_t indexCount = 0;
    if (!ReadBinaryValue(in, indexCount)) {
        return false;
    }

    constexpr std::uint32_t kMaxIndices = 60'000'000u;
    if (indexCount > kMaxIndices) {
        return false;
    }

    parsed.indices.resize(indexCount);
    for (std::uint32_t& index : parsed.indices) {
        if (!ReadBinaryValue(in, index) || index >= parsed.vertices.size()) {
            return false;
        }
    }

    std::uint32_t edgeCount = 0;
    if (!ReadBinaryValue(in, edgeCount)) {
        return false;
    }

    constexpr std::uint32_t kMaxEdges = 30'000'000u;
    if (edgeCount > kMaxEdges) {
        return false;
    }

    parsed.edges.resize(edgeCount);
    for (MeshEdge& edge : parsed.edges) {
        if (
            !ReadBinaryValue(in, edge.a) ||
            !ReadBinaryValue(in, edge.b) ||
            edge.a >= parsed.vertices.size() ||
            edge.b >= parsed.vertices.size()
        ) {
            return false;
        }
    }

    RebuildMeshTrianglesFromEdges(parsed);
    mesh = std::move(parsed);
    return true;
}

static json MeshToJson(const LayerMesh& mesh) {
    json vertices = json::array();
    for (const MeshVertex& vertex : mesh.vertices) {
        vertices.push_back({
            {"x", vertex.position.x},
            {"y", vertex.position.y},
            {"u", vertex.uv.x},
            {"v", vertex.uv.y},
            {"physics", vertex.physicsEnabled},
        });
    }

    json edges = json::array();
    for (const MeshEdge& edge : mesh.edges) {
        edges.push_back({{"a", edge.a}, {"b", edge.b}});
    }

    return {
        {"vertices", vertices},
        {"indices", mesh.indices},
        {"edges", edges},
    };
}

static bool JsonToMesh(const json& value, LayerMesh& mesh) {
    if (!value.is_object() || !value.contains("vertices") || !value["vertices"].is_array()) {
        return false;
    }

    LayerMesh parsed;

    for (const json& vertexJson : value["vertices"]) {
        if (!vertexJson.is_object()) {
            return false;
        }

        MeshVertex vertex;
        vertex.position.x = vertexJson.value("x", 0.0f);
        vertex.position.y = vertexJson.value("y", 0.0f);
        vertex.uv.x = vertexJson.value("u", 0.0f);
        vertex.uv.y = vertexJson.value("v", 0.0f);
        vertex.physicsEnabled = vertexJson.value("physics", false);
        parsed.vertices.push_back(vertex);
    }

    if (value.contains("indices") && value["indices"].is_array()) {
        for (const json& indexJson : value["indices"]) {
            parsed.indices.push_back(indexJson.get<std::uint32_t>());
        }
    }

    if (value.contains("edges") && value["edges"].is_array()) {
        for (const json& edgeJson : value["edges"]) {
            MeshEdge edge;
            edge.a = edgeJson.value("a", 0u);
            edge.b = edgeJson.value("b", 0u);
            if (edge.a < parsed.vertices.size() && edge.b < parsed.vertices.size()) {
                parsed.edges.push_back(edge);
            }
        }
    }

    for (const std::uint32_t index : parsed.indices) {
        if (index >= parsed.vertices.size()) {
            return false;
        }
    }

    if (parsed.edges.empty()) {
        for (std::size_t i = 0; i + 2 < parsed.indices.size(); i += 3) {
            const std::uint32_t a = parsed.indices[i + 0u];
            const std::uint32_t b = parsed.indices[i + 1u];
            const std::uint32_t c = parsed.indices[i + 2u];

            parsed.edges.push_back(MeshEdge{a, b});
            parsed.edges.push_back(MeshEdge{b, c});
            parsed.edges.push_back(MeshEdge{c, a});
        }
    }

    RebuildMeshTrianglesFromEdges(parsed);

    mesh = std::move(parsed);
    return !mesh.vertices.empty();
}

static json LayerHistoryStateToJson(const LayerHistoryState& state) {
    LayerMesh mesh = state.mesh;
    RebuildMeshTrianglesFromEdges(mesh);

    return {
        {"left", state.left},
        {"top", state.top},
        {"right", state.right},
        {"bottom", state.bottom},
        {"visible", state.visible},
        {"opacity", state.opacity},
        {"hueShift", state.hueShift},
        {"saturationScale", state.saturationScale},
        {"lightnessShift", state.lightnessShift},
        {"renderOrderOverride", state.renderOrderOverride},
        {"masks", state.maskLayerIndices},
        {"mesh", MeshToJson(mesh)},
    };
}

static bool JsonToLayerHistoryState(const json& value, LayerHistoryState& state) {
    if (!value.is_object() || !value.contains("mesh")) {
        return false;
    }

    LayerHistoryState parsed;
    parsed.left = value.value("left", 0);
    parsed.top = value.value("top", 0);
    parsed.right = value.value("right", 0);
    parsed.bottom = value.value("bottom", 0);
    parsed.visible = value.value("visible", true);
    parsed.opacity = value.value("opacity", 1.0f);
    parsed.hueShift = value.value("hueShift", 0.0f);
    parsed.saturationScale = value.value("saturationScale", 100.0f);
    parsed.lightnessShift = value.value("lightnessShift", 0.0f);
    parsed.renderOrderOverride = value.value("renderOrderOverride", std::string{});
    if (value.contains("masks") && value["masks"].is_array()) {
        for (const json& maskJson : value["masks"]) {
            parsed.maskLayerIndices.push_back(maskJson.get<int>());
        }
    }

    if (!JsonToMesh(value["mesh"], parsed.mesh)) {
        return false;
    }

    state = std::move(parsed);
    return true;
}

static int ResolveSavedLayerIndex(
    const EditorState& editor,
    int savedIndex,
    const std::string& savedName
) {
    if (
        savedIndex >= 0 &&
        savedIndex < static_cast<int>(editor.document.layers.size()) &&
        editor.document.layers[savedIndex].name == savedName
    ) {
        return savedIndex;
    }

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        if (editor.document.layers[i].name == savedName) {
            return i;
        }
    }

    return -1;
}

static json LayerOperationToJson(const EditorState& editor, const LayerOperation& operation) {
    std::string layerName;
    if (
        operation.layerIndex >= 0 &&
        operation.layerIndex < static_cast<int>(editor.document.layers.size())
    ) {
        layerName = editor.document.layers[operation.layerIndex].name;
    }

    return {
        {"layerIndex", operation.layerIndex},
        {"layerName", layerName},
        {"description", operation.description},
        {"before", LayerHistoryStateToJson(operation.before)},
        {"after", LayerHistoryStateToJson(operation.after)},
    };
}

static bool JsonToLayerOperation(
    const EditorState& editor,
    const json& value,
    LayerOperation& operation
) {
    if (
        !value.is_object() ||
        !value.contains("before") ||
        !value.contains("after")
    ) {
        return false;
    }

    const int savedIndex = value.value("layerIndex", -1);
    const std::string savedName = value.value("layerName", std::string{});
    const bool parameterOnlyOperation = savedIndex < 0 && savedName.empty();
    const int targetIndex = parameterOnlyOperation
        ? -1
        : ResolveSavedLayerIndex(editor, savedIndex, savedName);
    if (targetIndex < 0 && !parameterOnlyOperation) {
        return false;
    }

    LayerOperation parsed;
    parsed.layerIndex = targetIndex;
    parsed.description = value.value("description", std::string{});

    if (
        !JsonToLayerHistoryState(value["before"], parsed.before) ||
        !JsonToLayerHistoryState(value["after"], parsed.after)
    ) {
        return false;
    }

    operation = std::move(parsed);
    return true;
}

static json HistoryToJson(const EditorState& editor) {
    json undoStack = json::array();
    for (const LayerOperation& operation : editor.history.undoStack) {
        undoStack.push_back(LayerOperationToJson(editor, operation));
    }

    json redoStack = json::array();
    for (const LayerOperation& operation : editor.history.redoStack) {
        redoStack.push_back(LayerOperationToJson(editor, operation));
    }

    return {
        {"undoStack", undoStack},
        {"redoStack", redoStack},
    };
}

static void LoadHistoryForEditor(const json& root, EditorState& editor) {
    editor.history = EditHistory{};

    if (!root.contains("history") || !root["history"].is_object()) {
        return;
    }

    const json& historyJson = root["history"];

    if (historyJson.contains("undoStack") && historyJson["undoStack"].is_array()) {
        for (const json& operationJson : historyJson["undoStack"]) {
            LayerOperation operation;
            if (JsonToLayerOperation(editor, operationJson, operation)) {
                editor.history.undoStack.push_back(std::move(operation));
            }
        }
    }

    if (historyJson.contains("redoStack") && historyJson["redoStack"].is_array()) {
        for (const json& operationJson : historyJson["redoStack"]) {
            LayerOperation operation;
            if (JsonToLayerOperation(editor, operationJson, operation)) {
                editor.history.redoStack.push_back(std::move(operation));
            }
        }
    }
}

static bool WriteBinaryLayerHistoryState(std::ostream& out, const LayerHistoryState& state) {
    const std::uint8_t visible = state.visible ? 1u : 0u;
    if (
        !(
        WriteBinaryValue(out, state.left) &&
        WriteBinaryValue(out, state.top) &&
        WriteBinaryValue(out, state.right) &&
        WriteBinaryValue(out, state.bottom) &&
        WriteBinaryValue(out, visible) &&
        WriteBinaryValue(out, state.opacity) &&
        WriteBinaryValue(out, state.hueShift) &&
        WriteBinaryValue(out, state.saturationScale) &&
        WriteBinaryValue(out, state.lightnessShift)
        )
    ) {
        return false;
    }

    if (state.maskLayerIndices.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }
    const std::uint32_t maskCount = static_cast<std::uint32_t>(state.maskLayerIndices.size());
    if (!WriteBinaryValue(out, maskCount)) {
        return false;
    }
    for (const int maskLayerIndex : state.maskLayerIndices) {
        if (!WriteBinaryValue(out, maskLayerIndex)) {
            return false;
        }
    }

    return
        WriteBinaryString(out, state.renderOrderOverride) &&
        WriteBinaryValue(out, state.textureIndex) &&
        WriteBinaryMesh(out, state.mesh);
}

static bool WriteBinaryLayerListHistoryState(
    std::ostream& out,
    const std::vector<LayerListHistoryState>& layers
) {
    if (layers.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }

    const std::uint32_t layerCount = static_cast<std::uint32_t>(layers.size());
    if (!WriteBinaryValue(out, layerCount)) {
        return false;
    }

    for (const LayerListHistoryState& layer : layers) {
        if (
            !WriteBinaryString(out, layer.name) ||
            !WriteBinaryLayerHistoryState(out, layer.state)
        ) {
            return false;
        }
    }

    return true;
}

static bool ReadBinaryLayerHistoryState(
    std::istream& in,
    LayerHistoryState& state,
    bool hasLayerMetadata,
    bool hasRenderOrderOverrideMetadata,
    bool hasLayerTextureIndex,
    bool hasLayerColor
) {
    LayerHistoryState parsed;
    std::uint8_t visible = 1u;

    if (
        !ReadBinaryValue(in, parsed.left) ||
        !ReadBinaryValue(in, parsed.top) ||
        !ReadBinaryValue(in, parsed.right) ||
        !ReadBinaryValue(in, parsed.bottom) ||
        !ReadBinaryValue(in, visible)
    ) {
        return false;
    }

    if (hasLayerMetadata) {
        if (!ReadBinaryValue(in, parsed.opacity)) {
            return false;
        }
        if (hasLayerColor) {
            if (
                !ReadBinaryValue(in, parsed.hueShift) ||
                !ReadBinaryValue(in, parsed.saturationScale) ||
                !ReadBinaryValue(in, parsed.lightnessShift)
            ) {
                return false;
            }
        }

        std::uint32_t maskCount = 0;
        if (!ReadBinaryValue(in, maskCount) || maskCount > 100'000u) {
            return false;
        }

        parsed.maskLayerIndices.resize(maskCount);
        for (int& maskLayerIndex : parsed.maskLayerIndices) {
            if (!ReadBinaryValue(in, maskLayerIndex)) {
                return false;
            }
        }
    }

    if (hasRenderOrderOverrideMetadata && !ReadBinaryString(in, parsed.renderOrderOverride)) {
        return false;
    }

    if (hasLayerTextureIndex && !ReadBinaryValue(in, parsed.textureIndex)) {
        return false;
    }

    if (!ReadBinaryMesh(in, parsed.mesh)) {
        return false;
    }

    parsed.visible = visible != 0u;
    state = std::move(parsed);
    return true;
}

static bool ReadBinaryLayerListHistoryState(
    std::istream& in,
    std::vector<LayerListHistoryState>& layers,
    bool hasLayerMetadata,
    bool hasRenderOrderOverrideMetadata,
    bool hasLayerTextureIndex,
    bool hasLayerColor
) {
    std::uint32_t layerCount = 0;
    if (!ReadBinaryValue(in, layerCount) || layerCount > 100'000u) {
        return false;
    }

    layers.clear();
    layers.reserve(layerCount);
    for (std::uint32_t i = 0; i < layerCount; ++i) {
        LayerListHistoryState layer;
        if (
            !ReadBinaryString(in, layer.name) ||
            !ReadBinaryLayerHistoryState(
                in,
                layer.state,
                hasLayerMetadata,
                hasRenderOrderOverrideMetadata,
                hasLayerTextureIndex,
                hasLayerColor
            )
        ) {
            return false;
        }
        layers.push_back(std::move(layer));
    }

    return true;
}

static int FindTextureIndexByName(const EditorDocument& document, const std::string& name);

static bool WriteBinaryParameterList(
    std::ostream& out,
    const EditorState& editor,
    const std::vector<DeformParameter>& parameters
) {
    if (parameters.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return false;
    }

    const std::uint32_t parameterCount = static_cast<std::uint32_t>(parameters.size());
    if (!WriteBinaryValue(out, parameterCount)) {
        return false;
    }

    for (const DeformParameter& parameter : parameters) {
        if (
            !WriteBinaryString(out, parameter.name) ||
            !WriteBinaryValue(out, parameter.value)
        ) {
            return false;
        }

        const std::uint8_t parameterType = parameter.type == DeformParameterType::State ? 1u : 0u;
        if (
            !WriteBinaryValue(out, parameterType) ||
            !WriteBinaryValue(out, parameter.selectedState)
        ) {
            return false;
        }

        if (parameter.stateNames.size() > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        const std::uint32_t stateNameCount = static_cast<std::uint32_t>(parameter.stateNames.size());
        if (!WriteBinaryValue(out, stateNameCount)) {
            return false;
        }
        for (const std::string& stateName : parameter.stateNames) {
            if (!WriteBinaryString(out, stateName)) {
                return false;
            }
        }

        const std::uint8_t affectsMesh = parameter.affectsMesh ? 1u : 0u;
        const std::uint8_t affectsRenderOrder = parameter.affectsRenderOrder ? 1u : 0u;
        const std::uint8_t affectsMasking = parameter.affectsMasking ? 1u : 0u;
        const std::uint8_t affectsOpacity = parameter.affectsOpacity ? 1u : 0u;
        const std::uint8_t affectsTexture = parameter.affectsTexture ? 1u : 0u;
        const std::uint8_t affectsColor = parameter.affectsColor ? 1u : 0u;
        if (
            !WriteBinaryValue(out, affectsMesh) ||
            !WriteBinaryValue(out, affectsRenderOrder) ||
            !WriteBinaryValue(out, affectsMasking) ||
            !WriteBinaryValue(out, affectsOpacity) ||
            !WriteBinaryValue(out, affectsTexture) ||
            !WriteBinaryValue(out, affectsColor)
        ) {
            return false;
        }

        if (parameter.layers.size() > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }

        const std::uint32_t layerCount = static_cast<std::uint32_t>(parameter.layers.size());
        if (!WriteBinaryValue(out, layerCount)) {
            return false;
        }

        for (const DeformParameterLayerState& state : parameter.layers) {
            std::string layerName;
            if (state.layerIndex >= 0 && state.layerIndex < static_cast<int>(editor.document.layers.size())) {
                layerName = editor.document.layers[state.layerIndex].name;
            }
            const auto textureNameForIndex = [&](int textureIndex) -> std::string {
                return textureIndex >= 0 && textureIndex < static_cast<int>(editor.document.textures.size())
                    ? editor.document.textures[textureIndex].name
                    : std::string{};
            };

            if (
                !WriteBinaryValue(out, state.layerIndex) ||
                !WriteBinaryString(out, layerName) ||
                !WriteBinaryMesh(out, state.meshAt0) ||
                !WriteBinaryMesh(out, state.meshAt1) ||
                !WriteBinaryValue(out, state.textureIndexAt0) ||
                !WriteBinaryString(out, textureNameForIndex(state.textureIndexAt0)) ||
                !WriteBinaryValue(out, state.textureIndexAt1) ||
                !WriteBinaryString(out, textureNameForIndex(state.textureIndexAt1))
            ) {
                return false;
            }

            if (state.meshCorners.size() > (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }

            const std::uint32_t meshCornerCount = static_cast<std::uint32_t>(state.meshCorners.size());
            if (!WriteBinaryValue(out, meshCornerCount)) {
                return false;
            }

            for (const DeformParameterMeshCorner& corner : state.meshCorners) {
                if (
                    corner.parameterIndices.size() != corner.parameterValues.size() ||
                    corner.parameterIndices.size() > (std::numeric_limits<std::uint32_t>::max)()
                ) {
                    return false;
                }

                const std::uint32_t parameterIndexCount = static_cast<std::uint32_t>(corner.parameterIndices.size());
                if (!WriteBinaryValue(out, parameterIndexCount)) {
                    return false;
                }

                for (std::size_t i = 0; i < corner.parameterIndices.size(); ++i) {
                    const float cornerValue = std::clamp(corner.parameterValues[i], 0.0f, 1.0f);
                    if (
                        !WriteBinaryValue(out, corner.parameterIndices[i]) ||
                        !WriteBinaryValue(out, cornerValue)
                    ) {
                        return false;
                    }
                }

                if (!WriteBinaryMesh(out, corner.mesh)) {
                    return false;
                }
            }

            if (
                !WriteBinaryValue(out, state.opacityAt0) ||
                !WriteBinaryValue(out, state.opacityAt1) ||
                !WriteBinaryValue(out, state.hueShiftAt0) ||
                !WriteBinaryValue(out, state.hueShiftAt1) ||
                !WriteBinaryValue(out, state.saturationScaleAt0) ||
                !WriteBinaryValue(out, state.saturationScaleAt1) ||
                !WriteBinaryValue(out, state.lightnessShiftAt0) ||
                !WriteBinaryValue(out, state.lightnessShiftAt1) ||
                !WriteBinaryString(out, state.renderOrderOverrideAt0) ||
                !WriteBinaryString(out, state.renderOrderOverrideAt1)
            ) {
                return false;
            }

            if (
                state.maskLayerIndicesAt0.size() > (std::numeric_limits<std::uint32_t>::max)() ||
                state.maskLayerIndicesAt1.size() > (std::numeric_limits<std::uint32_t>::max)()
            ) {
                return false;
            }

            const std::uint32_t maskCountAt0 = static_cast<std::uint32_t>(state.maskLayerIndicesAt0.size());
            if (!WriteBinaryValue(out, maskCountAt0)) {
                return false;
            }
            for (const int maskLayerIndex : state.maskLayerIndicesAt0) {
                if (!WriteBinaryValue(out, maskLayerIndex)) {
                    return false;
                }
            }

            const std::uint32_t maskCountAt1 = static_cast<std::uint32_t>(state.maskLayerIndicesAt1.size());
            if (!WriteBinaryValue(out, maskCountAt1)) {
                return false;
            }
            for (const int maskLayerIndex : state.maskLayerIndicesAt1) {
                if (!WriteBinaryValue(out, maskLayerIndex)) {
                    return false;
                }
            }

            if (state.setpoints.size() > (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }

            const std::uint32_t setpointCount = static_cast<std::uint32_t>(state.setpoints.size());
            if (!WriteBinaryValue(out, setpointCount)) {
                return false;
            }

            for (const DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                if (
                    setpoint.maskLayerIndices.size() > (std::numeric_limits<std::uint32_t>::max)() ||
                    !WriteBinaryValue(out, setpoint.value) ||
                    !WriteBinaryMesh(out, setpoint.mesh) ||
                    !WriteBinaryValue(out, setpoint.textureIndex) ||
                    !WriteBinaryString(out, textureNameForIndex(setpoint.textureIndex)) ||
                    !WriteBinaryValue(out, setpoint.opacity) ||
                    !WriteBinaryValue(out, setpoint.hueShift) ||
                    !WriteBinaryValue(out, setpoint.saturationScale) ||
                    !WriteBinaryValue(out, setpoint.lightnessShift) ||
                    !WriteBinaryString(out, setpoint.renderOrderOverride)
                ) {
                    return false;
                }

                const std::uint32_t setpointMaskCount = static_cast<std::uint32_t>(setpoint.maskLayerIndices.size());
                if (!WriteBinaryValue(out, setpointMaskCount)) {
                    return false;
                }
                for (const int maskLayerIndex : setpoint.maskLayerIndices) {
                    if (!WriteBinaryValue(out, maskLayerIndex)) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

static bool ReadBinaryParameterList(
    std::istream& in,
    EditorState& editor,
    std::vector<DeformParameter>& parameters,
    bool hasLayerVisualSetpoints,
    bool hasMaskSetpoints,
    bool hasParameterChannels,
    bool hasParameterMeshCorners,
    bool hasParameterSetpoints,
    bool hasParameterTypes,
    bool hasParameterTextures,
    bool hasParameterColor
) {
    parameters.clear();

    std::uint32_t parameterCount = 0;
    if (!ReadBinaryValue(in, parameterCount)) {
        if (in.eof()) {
            in.clear();
            return true;
        }
        return false;
    }

    constexpr std::uint32_t kMaxParameters = 10'000u;
    constexpr std::uint32_t kMaxParameterLayers = 100'000u;
    if (parameterCount > kMaxParameters) {
        return false;
    }

    for (std::uint32_t i = 0; i < parameterCount; ++i) {
        DeformParameter parameter;
        std::uint32_t layerCount = 0;

        if (
            !ReadBinaryString(in, parameter.name) ||
            !ReadBinaryValue(in, parameter.value)
        ) {
            return false;
        }

        parameter.value = std::clamp(parameter.value, 0.0f, 1.0f);
        if (hasParameterTypes) {
            std::uint8_t parameterType = 0u;
            int selectedState = 0;
            std::uint32_t stateNameCount = 0;
            if (
                !ReadBinaryValue(in, parameterType) ||
                !ReadBinaryValue(in, selectedState) ||
                !ReadBinaryValue(in, stateNameCount) ||
                stateNameCount > 10'000u
            ) {
                return false;
            }

            parameter.type = parameterType ? DeformParameterType::State : DeformParameterType::Slider;
            parameter.selectedState = selectedState;
            parameter.stateNames.clear();
            parameter.stateNames.reserve(stateNameCount);
            for (std::uint32_t stateNameIndex = 0; stateNameIndex < stateNameCount; ++stateNameIndex) {
                std::string stateName;
                if (!ReadBinaryString(in, stateName)) {
                    return false;
                }
                parameter.stateNames.push_back(std::move(stateName));
            }
            if (parameter.type == DeformParameterType::State) {
                if (parameter.stateNames.empty()) {
                    parameter.stateNames.push_back("Default");
                }
                parameter.selectedState = std::clamp(
                    parameter.selectedState,
                    0,
                    static_cast<int>(parameter.stateNames.size()) - 1
                );
                parameter.value = static_cast<float>(parameter.selectedState);
            }
        }
        if (hasParameterChannels) {
            std::uint8_t affectsMesh = 1u;
            std::uint8_t affectsRenderOrder = 1u;
            std::uint8_t affectsMasking = 1u;
            std::uint8_t affectsOpacity = 1u;
            std::uint8_t affectsTexture = 0u;
            std::uint8_t affectsColor = 0u;
            if (
                !ReadBinaryValue(in, affectsMesh) ||
                !ReadBinaryValue(in, affectsRenderOrder) ||
                !ReadBinaryValue(in, affectsMasking) ||
                !ReadBinaryValue(in, affectsOpacity)
            ) {
                return false;
            }
            if (hasParameterTextures && !ReadBinaryValue(in, affectsTexture)) {
                return false;
            }
            if (hasParameterColor && !ReadBinaryValue(in, affectsColor)) {
                return false;
            }
            parameter.affectsMesh = affectsMesh != 0u;
            parameter.affectsRenderOrder = affectsRenderOrder != 0u;
            parameter.affectsMasking = affectsMasking != 0u;
            parameter.affectsOpacity = affectsOpacity != 0u;
            parameter.affectsTexture = affectsTexture != 0u;
            parameter.affectsColor = affectsColor != 0u;
        }

        if (!ReadBinaryValue(in, layerCount)) {
            return false;
        }
        if (layerCount > kMaxParameterLayers) {
            return false;
        }

        for (std::uint32_t j = 0; j < layerCount; ++j) {
            int savedIndex = -1;
            std::string savedName;
            LayerMesh meshAt0;
            LayerMesh meshAt1;
            int textureIndexAt0 = -1;
            int textureIndexAt1 = -1;
            std::string textureNameAt0;
            std::string textureNameAt1;
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

            if (
                !ReadBinaryValue(in, savedIndex) ||
                !ReadBinaryString(in, savedName) ||
                !ReadBinaryMesh(in, meshAt0) ||
                !ReadBinaryMesh(in, meshAt1)
            ) {
                return false;
            }
            if (hasParameterTextures) {
                if (
                    !ReadBinaryValue(in, textureIndexAt0) ||
                    !ReadBinaryString(in, textureNameAt0) ||
                    !ReadBinaryValue(in, textureIndexAt1) ||
                    !ReadBinaryString(in, textureNameAt1)
                ) {
                    return false;
                }
                if (!textureNameAt0.empty()) {
                    textureIndexAt0 = FindTextureIndexByName(editor.document, textureNameAt0);
                }
                if (!textureNameAt1.empty()) {
                    textureIndexAt1 = FindTextureIndexByName(editor.document, textureNameAt1);
                }
            }

            if (hasParameterMeshCorners) {
                std::uint32_t meshCornerCount = 0;
                if (!ReadBinaryValue(in, meshCornerCount) || meshCornerCount > 1'000'000u) {
                    return false;
                }

                meshCorners.reserve(meshCornerCount);
                for (std::uint32_t cornerIndex = 0; cornerIndex < meshCornerCount; ++cornerIndex) {
                    std::uint32_t parameterIndexCount = 0;
                    if (!ReadBinaryValue(in, parameterIndexCount) || parameterIndexCount > 32u) {
                        return false;
                    }

                    DeformParameterMeshCorner corner;
                    corner.parameterIndices.resize(parameterIndexCount);
                    corner.parameterValues.resize(parameterIndexCount);
                    for (std::uint32_t parameterValueIndex = 0; parameterValueIndex < parameterIndexCount; ++parameterValueIndex) {
                        if (!ReadBinaryValue(in, corner.parameterIndices[parameterValueIndex])) {
                            return false;
                        }
                        if (hasParameterSetpoints) {
                            float cornerValue = 0.0f;
                            if (!ReadBinaryValue(in, cornerValue)) {
                                return false;
                            }
                            corner.parameterValues[parameterValueIndex] = std::clamp(cornerValue, 0.0f, 1.0f);
                        } else {
                            std::uint8_t cornerValue = 0u;
                            if (!ReadBinaryValue(in, cornerValue)) {
                                return false;
                            }
                            corner.parameterValues[parameterValueIndex] = cornerValue ? 1.0f : 0.0f;
                        }
                    }

                    if (!ReadBinaryMesh(in, corner.mesh)) {
                        return false;
                    }
                    meshCorners.push_back(std::move(corner));
                }
            }

            if (hasLayerVisualSetpoints) {
                if (
                    !ReadBinaryValue(in, opacityAt0) ||
                    !ReadBinaryValue(in, opacityAt1)
                ) {
                    return false;
                }
                if (hasParameterColor) {
                    if (
                        !ReadBinaryValue(in, hueShiftAt0) ||
                        !ReadBinaryValue(in, hueShiftAt1) ||
                        !ReadBinaryValue(in, saturationScaleAt0) ||
                        !ReadBinaryValue(in, saturationScaleAt1) ||
                        !ReadBinaryValue(in, lightnessShiftAt0) ||
                        !ReadBinaryValue(in, lightnessShiftAt1)
                    ) {
                        return false;
                    }
                }
                if (
                    !ReadBinaryString(in, renderOrderOverrideAt0) ||
                    !ReadBinaryString(in, renderOrderOverrideAt1)
                ) {
                    return false;
                }
            }

            if (hasMaskSetpoints) {
                std::uint32_t maskCountAt0 = 0;
                if (!ReadBinaryValue(in, maskCountAt0) || maskCountAt0 > 100'000u) {
                    return false;
                }
                maskLayerIndicesAt0.resize(maskCountAt0);
                for (int& maskLayerIndex : maskLayerIndicesAt0) {
                    if (!ReadBinaryValue(in, maskLayerIndex)) {
                        return false;
                    }
                }

                std::uint32_t maskCountAt1 = 0;
                if (!ReadBinaryValue(in, maskCountAt1) || maskCountAt1 > 100'000u) {
                    return false;
                }
                maskLayerIndicesAt1.resize(maskCountAt1);
                for (int& maskLayerIndex : maskLayerIndicesAt1) {
                    if (!ReadBinaryValue(in, maskLayerIndex)) {
                        return false;
                    }
                }
            }

            if (hasParameterSetpoints) {
                std::uint32_t setpointCount = 0;
                if (!ReadBinaryValue(in, setpointCount) || setpointCount > 100'000u) {
                    return false;
                }

                setpoints.reserve(setpointCount);
                for (std::uint32_t setpointIndex = 0; setpointIndex < setpointCount; ++setpointIndex) {
                    DeformParameterLayerSetpoint setpoint;
                    std::string setpointTextureName;
                    if (
                        !ReadBinaryValue(in, setpoint.value) ||
                        !ReadBinaryMesh(in, setpoint.mesh) ||
                        (hasParameterTextures && !ReadBinaryValue(in, setpoint.textureIndex)) ||
                        (hasParameterTextures && !ReadBinaryString(in, setpointTextureName)) ||
                        !ReadBinaryValue(in, setpoint.opacity)
                    ) {
                        return false;
                    }
                    if (hasParameterColor) {
                        if (
                            !ReadBinaryValue(in, setpoint.hueShift) ||
                            !ReadBinaryValue(in, setpoint.saturationScale) ||
                            !ReadBinaryValue(in, setpoint.lightnessShift)
                        ) {
                            return false;
                        }
                    }
                    if (
                        !ReadBinaryString(in, setpoint.renderOrderOverride)
                    ) {
                        return false;
                    }
                    if (hasParameterTextures && !setpointTextureName.empty()) {
                        setpoint.textureIndex = FindTextureIndexByName(editor.document, setpointTextureName);
                    }

                    if (parameter.type != DeformParameterType::State) {
                        setpoint.value = std::clamp(setpoint.value, 0.0f, 1.0f);
                    }
                    setpoint.opacity = std::clamp(setpoint.opacity, 0.0f, 1.0f);

                    std::uint32_t setpointMaskCount = 0;
                    if (!ReadBinaryValue(in, setpointMaskCount) || setpointMaskCount > 100'000u) {
                        return false;
                    }
                    setpoint.maskLayerIndices.resize(setpointMaskCount);
                    for (int& maskLayerIndex : setpoint.maskLayerIndices) {
                        if (!ReadBinaryValue(in, maskLayerIndex)) {
                            return false;
                        }
                    }
                    setpoints.push_back(std::move(setpoint));
                }
            }

            const int targetIndex = ResolveSavedLayerIndex(editor, savedIndex, savedName);
            if (targetIndex < 0) {
                continue;
            }

            DeformParameterLayerState state;
            state.layerIndex = targetIndex;
            state.meshAt0 = std::move(meshAt0);
            state.meshAt1 = std::move(meshAt1);
            if (!hasParameterTextures) {
                textureIndexAt0 = editor.document.layers[targetIndex].textureIndex;
                textureIndexAt1 = editor.document.layers[targetIndex].textureIndex;
                for (DeformParameterLayerSetpoint& setpoint : setpoints) {
                    setpoint.textureIndex = editor.document.layers[targetIndex].textureIndex;
                }
            }
            state.textureIndexAt0 = textureIndexAt0;
            state.textureIndexAt1 = textureIndexAt1;
            state.meshCorners = std::move(meshCorners);
            state.setpoints = std::move(setpoints);
            if (hasLayerVisualSetpoints) {
                state.opacityAt0 = std::clamp(opacityAt0, 0.0f, 1.0f);
                state.opacityAt1 = std::clamp(opacityAt1, 0.0f, 1.0f);
                state.hueShiftAt0 = std::clamp(hueShiftAt0, -100.0f, 100.0f);
                state.hueShiftAt1 = std::clamp(hueShiftAt1, -100.0f, 100.0f);
                state.saturationScaleAt0 = std::clamp(saturationScaleAt0, 0.0f, 200.0f);
                state.saturationScaleAt1 = std::clamp(saturationScaleAt1, 0.0f, 200.0f);
                state.lightnessShiftAt0 = std::clamp(lightnessShiftAt0, -100.0f, 100.0f);
                state.lightnessShiftAt1 = std::clamp(lightnessShiftAt1, -100.0f, 100.0f);
                state.renderOrderOverrideAt0 = std::move(renderOrderOverrideAt0);
                state.renderOrderOverrideAt1 = std::move(renderOrderOverrideAt1);
                if (hasMaskSetpoints) {
                    state.maskLayerIndicesAt0 = std::move(maskLayerIndicesAt0);
                    state.maskLayerIndicesAt1 = std::move(maskLayerIndicesAt1);
                } else {
                    const EditorLayer& layer = editor.document.layers[targetIndex];
                    state.maskLayerIndicesAt0 = layer.maskLayerIndices;
                    state.maskLayerIndicesAt1 = layer.maskLayerIndices;
                }
            } else {
                const EditorLayer& layer = editor.document.layers[targetIndex];
                state.opacityAt0 = layer.opacity;
                state.opacityAt1 = layer.opacity;
                state.hueShiftAt0 = layer.hueShift;
                state.hueShiftAt1 = layer.hueShift;
                state.saturationScaleAt0 = layer.saturationScale;
                state.saturationScaleAt1 = layer.saturationScale;
                state.lightnessShiftAt0 = layer.lightnessShift;
                state.lightnessShiftAt1 = layer.lightnessShift;
                state.renderOrderOverrideAt0 = layer.renderOrderOverride;
                state.renderOrderOverrideAt1 = layer.renderOrderOverride;
                state.maskLayerIndicesAt0 = layer.maskLayerIndices;
                state.maskLayerIndicesAt1 = layer.maskLayerIndices;
            }
            if (state.setpoints.empty()) {
                DeformParameterLayerSetpoint zero;
                zero.value = 0.0f;
                zero.mesh = state.meshAt0;
                zero.textureIndex = state.textureIndexAt0;
                zero.opacity = state.opacityAt0;
                zero.hueShift = state.hueShiftAt0;
                zero.saturationScale = state.saturationScaleAt0;
                zero.lightnessShift = state.lightnessShiftAt0;
                zero.renderOrderOverride = state.renderOrderOverrideAt0;
                zero.maskLayerIndices = state.maskLayerIndicesAt0;
                state.setpoints.push_back(std::move(zero));

                DeformParameterLayerSetpoint one;
                one.value = 1.0f;
                one.mesh = state.meshAt1;
                one.textureIndex = state.textureIndexAt1;
                one.opacity = state.opacityAt1;
                one.hueShift = state.hueShiftAt1;
                one.saturationScale = state.saturationScaleAt1;
                one.lightnessShift = state.lightnessShiftAt1;
                one.renderOrderOverride = state.renderOrderOverrideAt1;
                one.maskLayerIndices = state.maskLayerIndicesAt1;
                state.setpoints.push_back(std::move(one));
            }
            parameter.layers.push_back(std::move(state));
        }

        if (!parameter.layers.empty()) {
            parameters.push_back(std::move(parameter));
        }
    }

    return true;
}

static bool WriteBinaryLayerOperation(
    std::ostream& out,
    const EditorState& editor,
    const LayerOperation& operation
) {
    std::string layerName;
    if (
        operation.layerIndex >= 0 &&
        operation.layerIndex < static_cast<int>(editor.document.layers.size())
    ) {
        layerName = editor.document.layers[operation.layerIndex].name;
    }

    const std::uint8_t hasParameterSnapshot = operation.hasParameterSnapshot ? 1u : 0u;
    const std::uint8_t hasLayerListSnapshot = operation.hasLayerListSnapshot ? 1u : 0u;
    if (!(
        WriteBinaryValue(out, operation.layerIndex) &&
        WriteBinaryString(out, layerName) &&
        WriteBinaryString(out, operation.description) &&
        WriteBinaryLayerHistoryState(out, operation.before) &&
        WriteBinaryLayerHistoryState(out, operation.after) &&
        WriteBinaryValue(out, hasParameterSnapshot)
    )) {
        return false;
    }

    if (
        !WriteBinaryValue(out, operation.selectedParameterBefore) ||
        !WriteBinaryValue(out, operation.selectedParameterAfter) ||
        !WriteBinaryValue(out, hasLayerListSnapshot)
    ) {
        return false;
    }

    if (operation.hasLayerListSnapshot) {
        if (
            !WriteBinaryLayerListHistoryState(out, operation.layersBefore) ||
            !WriteBinaryLayerListHistoryState(out, operation.layersAfter)
        ) {
            return false;
        }
    }

    if (!operation.hasParameterSnapshot) {
        return true;
    }

    return
        WriteBinaryParameterList(out, editor, operation.parametersBefore) &&
        WriteBinaryParameterList(out, editor, operation.parametersAfter);
}

static bool ReadBinaryLayerOperation(
    std::istream& in,
    EditorState& editor,
    LayerOperation& operation,
    bool hasLayerMetadata,
    bool hasRenderOrderOverrideMetadata,
    bool hasMaskSetpoints,
    bool hasOperationSelectionMetadata,
    bool hasParameterChannels,
    bool hasParameterMeshCorners,
    bool hasParameterSetpoints,
    bool hasLayerTextureIndex,
    bool hasParameterSnapshots,
    bool hasParameterTypes,
    bool hasParameterTextures,
    bool hasParameterColor,
    bool hasLayerColor
) {
    int savedIndex = -1;
    std::string savedName;
    std::string description;

    if (
        !ReadBinaryValue(in, savedIndex) ||
        !ReadBinaryString(in, savedName) ||
        !ReadBinaryString(in, description)
    ) {
        return false;
    }

    const bool parameterOnlyOperation = savedIndex < 0 && savedName.empty();
    const int targetIndex = parameterOnlyOperation
        ? -1
        : ResolveSavedLayerIndex(editor, savedIndex, savedName);
    if (targetIndex < 0 && !parameterOnlyOperation) {
        return false;
    }

    LayerOperation parsed;
    parsed.layerIndex = targetIndex;
    parsed.description = std::move(description);

    if (
        !ReadBinaryLayerHistoryState(in, parsed.before, hasLayerMetadata, hasRenderOrderOverrideMetadata, hasLayerTextureIndex, hasLayerColor) ||
        !ReadBinaryLayerHistoryState(in, parsed.after, hasLayerMetadata, hasRenderOrderOverrideMetadata, hasLayerTextureIndex, hasLayerColor)
    ) {
        return false;
    }

    if (hasParameterSnapshots) {
        std::uint8_t hasParameterSnapshot = 0u;
        if (!ReadBinaryValue(in, hasParameterSnapshot)) {
            return false;
        }

        parsed.hasParameterSnapshot = hasParameterSnapshot != 0u;
        if (hasOperationSelectionMetadata) {
            if (
                !ReadBinaryValue(in, parsed.selectedParameterBefore) ||
                !ReadBinaryValue(in, parsed.selectedParameterAfter)
            ) {
                return false;
            }
        }
        if (hasLayerTextureIndex) {
            std::uint8_t hasLayerListSnapshot = 0u;
            if (!ReadBinaryValue(in, hasLayerListSnapshot)) {
                return false;
            }
            parsed.hasLayerListSnapshot = hasLayerListSnapshot != 0u;
            if (parsed.hasLayerListSnapshot) {
                if (
                    !ReadBinaryLayerListHistoryState(
                        in,
                        parsed.layersBefore,
                        hasLayerMetadata,
                        hasRenderOrderOverrideMetadata,
                        hasLayerTextureIndex,
                        hasLayerColor
                    ) ||
                    !ReadBinaryLayerListHistoryState(
                        in,
                        parsed.layersAfter,
                        hasLayerMetadata,
                        hasRenderOrderOverrideMetadata,
                        hasLayerTextureIndex,
                        hasLayerColor
                    )
                ) {
                    return false;
                }
            }
        }
        if (parsed.hasParameterSnapshot) {
            if (
                !ReadBinaryParameterList(in, editor, parsed.parametersBefore, hasRenderOrderOverrideMetadata, hasMaskSetpoints, hasParameterChannels, hasParameterMeshCorners, hasParameterSetpoints, hasParameterTypes, hasParameterTextures, hasParameterColor) ||
                !ReadBinaryParameterList(in, editor, parsed.parametersAfter, hasRenderOrderOverrideMetadata, hasMaskSetpoints, hasParameterChannels, hasParameterMeshCorners, hasParameterSetpoints, hasParameterTypes, hasParameterTextures, hasParameterColor)
            ) {
                return false;
            }
        }
    }

    operation = std::move(parsed);
    return true;
}

static bool WriteBinaryHistory(std::ostream& out, const EditorState& editor) {
    if (
        editor.history.undoStack.size() > (std::numeric_limits<std::uint32_t>::max)() ||
        editor.history.redoStack.size() > (std::numeric_limits<std::uint32_t>::max)()
    ) {
        return false;
    }

    const std::uint32_t undoCount = static_cast<std::uint32_t>(editor.history.undoStack.size());
    if (!WriteBinaryValue(out, undoCount)) {
        return false;
    }

    for (const LayerOperation& operation : editor.history.undoStack) {
        if (!WriteBinaryLayerOperation(out, editor, operation)) {
            return false;
        }
    }

    const std::uint32_t redoCount = static_cast<std::uint32_t>(editor.history.redoStack.size());
    if (!WriteBinaryValue(out, redoCount)) {
        return false;
    }

    for (const LayerOperation& operation : editor.history.redoStack) {
        if (!WriteBinaryLayerOperation(out, editor, operation)) {
            return false;
        }
    }

    return true;
}

static bool ReadBinaryHistory(
    std::istream& in,
    EditorState& editor,
    bool hasLayerMetadata,
    bool hasRenderOrderOverrideMetadata,
    bool hasMaskSetpoints,
    bool hasOperationSelectionMetadata,
    bool hasParameterChannels,
    bool hasParameterMeshCorners,
    bool hasParameterSetpoints,
    bool hasLayerTextureIndex,
    bool hasParameterSnapshots,
    bool hasParameterTypes,
    bool hasParameterTextures,
    bool hasParameterColor,
    bool hasLayerColor
) {
    editor.history = EditHistory{};

    std::uint32_t undoCount = 0;
    if (!ReadBinaryValue(in, undoCount)) {
        return false;
    }

    constexpr std::uint32_t kMaxHistoryOps = 100'000u;
    if (undoCount > kMaxHistoryOps) {
        return false;
    }

    for (std::uint32_t i = 0; i < undoCount; ++i) {
        LayerOperation operation;
        if (!ReadBinaryLayerOperation(
            in,
            editor,
            operation,
            hasLayerMetadata,
            hasRenderOrderOverrideMetadata,
            hasMaskSetpoints,
            hasOperationSelectionMetadata,
            hasParameterChannels,
            hasParameterMeshCorners,
            hasParameterSetpoints,
            hasLayerTextureIndex,
            hasParameterSnapshots,
            hasParameterTypes,
            hasParameterTextures,
            hasParameterColor,
            hasLayerColor
        )) {
            return false;
        }
        editor.history.undoStack.push_back(std::move(operation));
    }

    std::uint32_t redoCount = 0;
    if (!ReadBinaryValue(in, redoCount)) {
        return false;
    }

    if (redoCount > kMaxHistoryOps) {
        return false;
    }

    for (std::uint32_t i = 0; i < redoCount; ++i) {
        LayerOperation operation;
        if (!ReadBinaryLayerOperation(
            in,
            editor,
            operation,
            hasLayerMetadata,
            hasRenderOrderOverrideMetadata,
            hasMaskSetpoints,
            hasOperationSelectionMetadata,
            hasParameterChannels,
            hasParameterMeshCorners,
            hasParameterSetpoints,
            hasLayerTextureIndex,
            hasParameterSnapshots,
            hasParameterTypes,
            hasParameterTextures,
            hasParameterColor,
            hasLayerColor
        )) {
            return false;
        }
        editor.history.redoStack.push_back(std::move(operation));
    }

    return true;
}

static const DeformParameterLayerState* FindParameterLayerStateForDocument(
    const DeformParameter& parameter,
    int layerIndex
) {
    for (const DeformParameterLayerState& state : parameter.layers) {
        if (state.layerIndex == layerIndex) {
            return &state;
        }
    }

    return nullptr;
}

static bool MeshTopologyMatchesForDocument(const LayerMesh& a, const LayerMesh& b) {
    return a.vertices.size() == b.vertices.size();
}

static LayerMesh MeshWithoutOtherActiveParameterDeltasForSave(
    const EditorState& editor,
    const std::vector<DeformParameter>& parameters,
    int selectedParameterIndex,
    int layerIndex,
    const LayerMesh& currentMesh
) {
    LayerMesh endpointMesh = currentMesh;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex) {
        if (parameterIndex == selectedParameterIndex) {
            continue;
        }

        const DeformParameter& otherParameter = parameters[parameterIndex];
        if (!otherParameter.affectsMesh) {
            continue;
        }

        const DeformParameterLayerState* otherState =
            FindParameterLayerStateForDocument(otherParameter, layerIndex);
        if (
            !otherState ||
            !MeshTopologyMatchesForDocument(endpointMesh, otherState->meshAt0) ||
            !MeshTopologyMatchesForDocument(endpointMesh, otherState->meshAt1)
        ) {
            continue;
        }

        const float value = std::clamp(otherParameter.value, 0.0f, 1.0f);
        for (std::size_t i = 0; i < endpointMesh.vertices.size(); ++i) {
            endpointMesh.vertices[i].position.x -=
                (otherState->meshAt1.vertices[i].position.x - otherState->meshAt0.vertices[i].position.x) * value;
            endpointMesh.vertices[i].position.y -=
                (otherState->meshAt1.vertices[i].position.y - otherState->meshAt0.vertices[i].position.y) * value;
        }
    }

    return endpointMesh;
}

static float OpacityWithoutOtherActiveParameterDeltasForSave(
    const std::vector<DeformParameter>& parameters,
    int selectedParameterIndex,
    int layerIndex,
    float currentOpacity
) {
    float endpointOpacity = currentOpacity;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex) {
        if (parameterIndex == selectedParameterIndex) {
            continue;
        }

        const DeformParameter& otherParameter = parameters[parameterIndex];
        if (!otherParameter.affectsOpacity) {
            continue;
        }

        const DeformParameterLayerState* otherState =
            FindParameterLayerStateForDocument(otherParameter, layerIndex);
        if (!otherState) {
            continue;
        }

        const float value = std::clamp(otherParameter.value, 0.0f, 1.0f);
        endpointOpacity -= (otherState->opacityAt1 - otherState->opacityAt0) * value;
    }

    return std::clamp(endpointOpacity, 0.0f, 1.0f);
}

static std::vector<int> CollectMeshParametersForLayerForSave(
    const std::vector<DeformParameter>& parameters,
    int layerIndex
) {
    std::vector<int> parameterIndices;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex) {
        const DeformParameter& parameter = parameters[parameterIndex];
        if (!parameter.affectsMesh || !FindParameterLayerStateForDocument(parameter, layerIndex)) {
            continue;
        }

        parameterIndices.push_back(parameterIndex);
    }

    return parameterIndices;
}

static bool CurrentValuesAreMeshCornerForSave(
    const std::vector<DeformParameter>& parameters,
    const std::vector<int>& parameterIndices,
    std::vector<float>& cornerValues
) {
    cornerValues.clear();
    cornerValues.reserve(parameterIndices.size());

    for (const int parameterIndex : parameterIndices) {
        const float value = std::clamp(parameters[parameterIndex].value, 0.0f, 1.0f);
        cornerValues.push_back(value);
    }

    return true;
}

static void StoreMeshCornerForSave(
    DeformParameterLayerState& state,
    const std::vector<int>& parameterIndices,
    const std::vector<float>& cornerValues,
    const LayerMesh& mesh
) {
    for (DeformParameterMeshCorner& corner : state.meshCorners) {
        if (corner.parameterIndices == parameterIndices && corner.parameterValues == cornerValues) {
            corner.mesh = mesh;
            return;
        }
    }

    DeformParameterMeshCorner corner;
    corner.parameterIndices = parameterIndices;
    corner.parameterValues = cornerValues;
    corner.mesh = mesh;
    state.meshCorners.push_back(std::move(corner));
}

static bool StoreCurrentMeshCornerForSave(
    std::vector<DeformParameter>& parameters,
    int layerIndex,
    const LayerMesh& mesh
) {
    const std::vector<int> parameterIndices = CollectMeshParametersForLayerForSave(parameters, layerIndex);
    if (parameterIndices.size() < 2u) {
        return false;
    }

    std::vector<float> cornerValues;
    if (!CurrentValuesAreMeshCornerForSave(parameters, parameterIndices, cornerValues)) {
        return false;
    }

    for (const int parameterIndex : parameterIndices) {
        DeformParameterLayerState* state = nullptr;
        for (DeformParameterLayerState& candidate : parameters[parameterIndex].layers) {
            if (candidate.layerIndex == layerIndex) {
                state = &candidate;
                break;
            }
        }
        if (state) {
            StoreMeshCornerForSave(*state, parameterIndices, cornerValues, mesh);
        }
    }

    return true;
}

static std::vector<DeformParameter> CaptureParametersForSave(const EditorState& editor) {
    std::vector<DeformParameter> parameters = editor.parameters;

    for (int parameterIndex = 0; parameterIndex < static_cast<int>(parameters.size()); ++parameterIndex) {
        DeformParameter& parameter = parameters[parameterIndex];
        if (parameter.type == DeformParameterType::State) {
            const float stateValue = static_cast<float>(std::max(0, parameter.selectedState));
            for (DeformParameterLayerState& state : parameter.layers) {
                if (
                    state.layerIndex < 0 ||
                    state.layerIndex >= static_cast<int>(editor.document.layers.size())
                ) {
                    continue;
                }

                const EditorLayer& layer = editor.document.layers[state.layerIndex];
                DeformParameterLayerSetpoint* targetSetpoint = nullptr;
                for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                    if (std::abs(setpoint.value - stateValue) <= 0.001f) {
                        targetSetpoint = &setpoint;
                        break;
                    }
                }
                if (!targetSetpoint) {
                    DeformParameterLayerSetpoint setpoint;
                    setpoint.value = stateValue;
                    setpoint.mesh = layer.mesh;
                    setpoint.textureIndex = layer.textureIndex;
                    setpoint.opacity = layer.opacity;
                    setpoint.renderOrderOverride = layer.renderOrderOverride;
                    setpoint.maskLayerIndices = layer.maskLayerIndices;
                    state.setpoints.push_back(std::move(setpoint));
                    targetSetpoint = &state.setpoints.back();
                }
                if (parameter.affectsMesh) {
                    targetSetpoint->mesh = layer.mesh;
                }
                if (parameter.affectsOpacity) {
                    targetSetpoint->opacity = layer.opacity;
                }
                if (parameter.affectsColor) {
                    targetSetpoint->hueShift = layer.hueShift;
                    targetSetpoint->saturationScale = layer.saturationScale;
                    targetSetpoint->lightnessShift = layer.lightnessShift;
                }
                if (parameter.affectsRenderOrder) {
                    targetSetpoint->renderOrderOverride = layer.renderOrderOverride;
                }
                if (parameter.affectsMasking) {
                    targetSetpoint->maskLayerIndices = layer.maskLayerIndices;
                }
                if (parameter.affectsTexture) {
                    targetSetpoint->textureIndex = layer.textureIndex;
                }
            }
            continue;
        }

        const bool updateZero = parameter.value <= 0.001f;
        const bool updateOne = parameter.value >= 0.999f;
        if (!updateZero && !updateOne) {
            continue;
        }

        for (DeformParameterLayerState& state : parameter.layers) {
            if (
                state.layerIndex < 0 ||
                state.layerIndex >= static_cast<int>(editor.document.layers.size())
            ) {
                continue;
            }

            const EditorLayer& layer = editor.document.layers[state.layerIndex];
            const bool storedMultilinearMeshCorner =
                parameter.affectsMesh && StoreCurrentMeshCornerForSave(parameters, state.layerIndex, layer.mesh);
            if (updateZero) {
                if (parameter.affectsMesh && !storedMultilinearMeshCorner && MeshTopologyMatchesForDocument(layer.mesh, state.meshAt0)) {
                    state.meshAt0 = MeshWithoutOtherActiveParameterDeltasForSave(
                        editor,
                        parameters,
                        parameterIndex,
                        state.layerIndex,
                        layer.mesh
                    );
                }
                if (parameter.affectsOpacity) {
                    state.opacityAt0 = OpacityWithoutOtherActiveParameterDeltasForSave(
                        parameters,
                        parameterIndex,
                        state.layerIndex,
                        layer.opacity
                    );
                }
                if (parameter.affectsColor) {
                    state.hueShiftAt0 = layer.hueShift;
                    state.saturationScaleAt0 = layer.saturationScale;
                    state.lightnessShiftAt0 = layer.lightnessShift;
                }
                if (parameter.affectsRenderOrder) {
                    state.renderOrderOverrideAt0 = layer.renderOrderOverride;
                }
                if (parameter.affectsMasking) {
                    state.maskLayerIndicesAt0 = layer.maskLayerIndices;
                }
                if (parameter.affectsTexture) {
                    state.textureIndexAt0 = layer.textureIndex;
                }
            } else if (updateOne) {
                if (parameter.affectsMesh && !storedMultilinearMeshCorner && MeshTopologyMatchesForDocument(layer.mesh, state.meshAt1)) {
                    state.meshAt1 = MeshWithoutOtherActiveParameterDeltasForSave(
                        editor,
                        parameters,
                        parameterIndex,
                        state.layerIndex,
                        layer.mesh
                    );
                }
                if (parameter.affectsOpacity) {
                    state.opacityAt1 = OpacityWithoutOtherActiveParameterDeltasForSave(
                        parameters,
                        parameterIndex,
                        state.layerIndex,
                        layer.opacity
                    );
                }
                if (parameter.affectsColor) {
                    state.hueShiftAt1 = layer.hueShift;
                    state.saturationScaleAt1 = layer.saturationScale;
                    state.lightnessShiftAt1 = layer.lightnessShift;
                }
                if (parameter.affectsRenderOrder) {
                    state.renderOrderOverrideAt1 = layer.renderOrderOverride;
                }
                if (parameter.affectsMasking) {
                    state.maskLayerIndicesAt1 = layer.maskLayerIndices;
                }
                if (parameter.affectsTexture) {
                    state.textureIndexAt1 = layer.textureIndex;
                }
            }
        }
    }

    return parameters;
}

static bool WriteBinaryParameters(std::ostream& out, const EditorState& editor) {
    const std::vector<DeformParameter> parameters = CaptureParametersForSave(editor);
    return WriteBinaryParameterList(out, editor, parameters);
}

static bool ReadBinaryParameters(
    std::istream& in,
    EditorState& editor,
    bool hasLayerVisualSetpoints,
    bool hasMaskSetpoints,
    bool hasParameterChannels,
    bool hasParameterMeshCorners,
    bool hasParameterSetpoints,
    bool hasParameterTypes,
    bool hasParameterTextures,
    bool hasParameterColor
) {
    editor.parameters.clear();
    editor.selectedParameter = -1;
    if (!ReadBinaryParameterList(in, editor, editor.parameters, hasLayerVisualSetpoints, hasMaskSetpoints, hasParameterChannels, hasParameterMeshCorners, hasParameterSetpoints, hasParameterTypes, hasParameterTextures, hasParameterColor)) {
        return false;
    }

    if (!editor.parameters.empty()) {
        editor.selectedParameter = 0;
    }

    return true;
}

static int FindTextureIndexByName(const EditorDocument& document, const std::string& name);

static void UpdateLayerBoundsFromCurrentMesh(EditorLayer& layer) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(layer.mesh, x0, y0, x1, y1)) {
        return;
    }

    layer.left = static_cast<int>(std::floor(x0));
    layer.top = static_cast<int>(std::floor(y0));
    layer.right = static_cast<int>(std::ceil(x1));
    layer.bottom = static_cast<int>(std::ceil(y1));
}

static bool MeshesHaveSameEditableTopology(const LayerMesh& a, const LayerMesh& b) {
    return a.vertices.size() == b.vertices.size();
}

static void ReconcileLoadedParameterMeshEndpoints(EditorState& editor) {
    for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
        const EditorLayer& layer = editor.document.layers[layerIndex];

        int activeOneParameter = -1;
        int activeZeroParameter = -1;
        int activeOneCount = 0;
        int activeZeroCount = 0;

        for (int parameterIndex = 0; parameterIndex < static_cast<int>(editor.parameters.size()); ++parameterIndex) {
            const DeformParameter& parameter = editor.parameters[parameterIndex];
            if (parameter.type == DeformParameterType::State || !parameter.affectsMesh) {
                continue;
            }

            const bool hasLayer = std::any_of(
                parameter.layers.begin(),
                parameter.layers.end(),
                [&](const DeformParameterLayerState& state) {
                    return state.layerIndex == layerIndex;
                }
            );
            if (!hasLayer) {
                continue;
            }

            if (parameter.value >= 0.999f) {
                activeOneParameter = parameterIndex;
                ++activeOneCount;
            } else if (parameter.value <= 0.001f) {
                activeZeroParameter = parameterIndex;
                ++activeZeroCount;
            }
        }

        if (activeOneCount == 1) {
            DeformParameter& parameter = editor.parameters[activeOneParameter];
            for (DeformParameterLayerState& state : parameter.layers) {
                if (
                    state.layerIndex == layerIndex &&
                    MeshesHaveSameEditableTopology(layer.mesh, state.meshAt1)
                ) {
                    state.meshAt1 = layer.mesh;
                }
            }
        } else if (activeOneCount == 0 && activeZeroCount == 1) {
            DeformParameter& parameter = editor.parameters[activeZeroParameter];
            for (DeformParameterLayerState& state : parameter.layers) {
                if (
                    state.layerIndex == layerIndex &&
                    MeshesHaveSameEditableTopology(layer.mesh, state.meshAt0)
                ) {
                    state.meshAt0 = layer.mesh;
                }
            }
        }
    }
}

static bool LoadBinaryMeshesForEditor(EditorState& editor, const std::filesystem::path& meshPath, std::string& error) {
    if (!std::filesystem::exists(meshPath)) {
        return true;
    }

    std::ifstream file(meshPath, std::ios::binary);
    if (!file) {
        error = "Could not open binary mesh file: " + meshPath.string();
        return false;
    }

    char magic[8] = {};
    file.read(magic, sizeof(magic));
    const std::string magicString(magic, sizeof(magic));
    const bool isVersion1 = magicString == std::string("MESHEDB1", 8);
    const bool isVersion2 = magicString == std::string("MESHEDB2", 8);
    const bool isVersion3 = magicString == std::string("MESHEDB3", 8);
    const bool isVersion4 = magicString == std::string("MESHEDB4", 8);
    const bool isVersion5 = magicString == std::string("MESHEDB5", 8);
    const bool isVersion6 = magicString == std::string("MESHEDB6", 8);
    const bool isVersion7 = magicString == std::string("MESHEDB7", 8);
    const bool isVersion8 = magicString == std::string("MESHEDB8", 8);
    const bool isVersion9 = magicString == std::string("MESHEDB9", 8);
    const bool isVersion10 = magicString == std::string("MESHEDBA", 8);
    const bool isVersion11 = magicString == std::string("MESHEDBB", 8);
    const bool isVersion12 = magicString == std::string("MESHEDBC", 8);
    const bool isVersion13 = magicString == std::string("MESHEDBD", 8);
    const bool isVersion14 = magicString == std::string("MESHEDBE", 8);
    const bool isVersion15 = magicString == std::string("MESHEDBF", 8);
    if (!file || (!isVersion1 && !isVersion2 && !isVersion3 && !isVersion4 && !isVersion5 && !isVersion6 && !isVersion7 && !isVersion8 && !isVersion9 && !isVersion10 && !isVersion11 && !isVersion12 && !isVersion13 && !isVersion14 && !isVersion15)) {
        error = "Binary mesh file has an invalid header.";
        return false;
    }

    gReadBinaryMeshHasPhysics = isVersion15;

    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    std::string psdName;
    std::uint32_t layerCount = 0;
    if (
        !ReadBinaryValue(file, canvasWidth) ||
        !ReadBinaryValue(file, canvasHeight) ||
        !ReadBinaryString(file, psdName)
    ) {
        error = "Binary mesh file is truncated.";
        return false;
    }

    if ((isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) && !ReadBinaryString(file, editor.document.psdPath)) {
        error = "Project file has an invalid PSD path.";
        return false;
    }

    if (!ReadBinaryValue(file, layerCount)) {
        error = "Binary mesh file is truncated.";
        return false;
    }

    constexpr std::uint32_t kMaxLayers = 100'000u;
    if (layerCount > kMaxLayers) {
        error = "Binary mesh file has too many layers.";
        return false;
    }

    struct LoadedLayerRecord {
        int targetIndex = -1;
        std::string savedName;
        LayerMesh mesh;
        bool visible = true;
        float opacity = 1.0f;
        float hueShift = 0.0f;
        float saturationScale = 100.0f;
        float lightnessShift = 0.0f;
        std::string renderOrderOverride;
        std::vector<int> masks;
        int textureIndex = -1;
        std::string textureName;
    };

    std::vector<LoadedLayerRecord> loadedRecords;
    loadedRecords.reserve(layerCount);

    for (std::uint32_t i = 0; i < layerCount; ++i) {
        int savedIndex = -1;
        std::string savedName;
        LayerMesh mesh;

        if (
            !ReadBinaryValue(file, savedIndex) ||
            !ReadBinaryString(file, savedName) ||
            !ReadBinaryMesh(file, mesh)
        ) {
            error = "Binary mesh file has invalid layer data.";
            return false;
        }

        const int targetIndex = ResolveSavedLayerIndex(editor, savedIndex, savedName);
        bool visible = true;
        float opacity = 1.0f;
        float hueShift = 0.0f;
        float saturationScale = 100.0f;
        float lightnessShift = 0.0f;
        std::vector<int> masks;
        if (isVersion2 || isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) {
            if (!ReadBinaryValue(file, opacity)) {
                error = "Binary mesh file has invalid layer opacity.";
                return false;
            }
            if (isVersion14 || isVersion15) {
                if (
                    !ReadBinaryValue(file, hueShift) ||
                    !ReadBinaryValue(file, saturationScale) ||
                    !ReadBinaryValue(file, lightnessShift)
                ) {
                    error = "Binary mesh file has invalid layer color settings.";
                    return false;
                }
            }

            std::uint32_t maskCount = 0;
            if (!ReadBinaryValue(file, maskCount) || maskCount > 100'000u) {
                error = "Binary mesh file has invalid layer masks.";
                return false;
            }
            masks.resize(maskCount);
            for (int& maskLayerIndex : masks) {
                if (!ReadBinaryValue(file, maskLayerIndex)) {
                    error = "Binary mesh file has invalid layer mask data.";
                    return false;
                }
            }
        }

        std::string renderOrderOverride;
        if ((isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) && !ReadBinaryString(file, renderOrderOverride)) {
            error = "Binary mesh file has invalid layer render order override.";
            return false;
        }

        if (isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) {
            std::uint8_t savedVisible = 1u;
            if (!ReadBinaryValue(file, savedVisible)) {
                error = "Binary mesh file has invalid layer visibility.";
                return false;
            }
            visible = savedVisible != 0u;
        }

        int textureIndex = savedIndex;
        if ((isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) && !ReadBinaryValue(file, textureIndex)) {
            error = "Binary mesh file has invalid layer texture index.";
            return false;
        }
        std::string textureName;
        if ((isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) && !ReadBinaryString(file, textureName)) {
            error = "Project file has invalid layer texture name.";
            return false;
        }
        if ((isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) && !textureName.empty()) {
            const int namedTextureIndex = FindTextureIndexByName(editor.document, textureName);
            if (namedTextureIndex >= 0) {
                textureIndex = namedTextureIndex;
            }
        }

        if (!isVersion10 && !isVersion11 && !isVersion12 && !isVersion13 && !isVersion14 && !isVersion15 && targetIndex < 0) {
            continue;
        }

        LoadedLayerRecord record;
        record.targetIndex = targetIndex;
        record.savedName = std::move(savedName);
        record.mesh = std::move(mesh);
        record.visible = visible;
        record.opacity = opacity;
        record.hueShift = hueShift;
        record.saturationScale = saturationScale;
        record.lightnessShift = lightnessShift;
        record.renderOrderOverride = std::move(renderOrderOverride);
        record.masks = std::move(masks);
        record.textureIndex = textureIndex;
        record.textureName = std::move(textureName);
        loadedRecords.push_back(std::move(record));
    }

    if (isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) {
        editor.document.layers.clear();
        editor.document.layers.reserve(loadedRecords.size());
        for (const LoadedLayerRecord& record : loadedRecords) {
            EditorLayer layer;
            layer.name = record.savedName;
            ApplyTextureToLayer(editor.document, layer, record.textureIndex);
            if (layer.textureIndex < 0 && !editor.document.textures.empty()) {
                ApplyTextureToLayer(editor.document, layer, 0);
            }
            editor.document.layers.push_back(std::move(layer));
        }
    } else if ((isVersion2 || isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9) && !loadedRecords.empty()) {
        std::vector<EditorLayer> reordered;
        reordered.reserve(editor.document.layers.size());
        std::vector<std::uint8_t> used(editor.document.layers.size(), 0u);

        for (const LoadedLayerRecord& record : loadedRecords) {
            if (record.targetIndex >= 0 && record.targetIndex < static_cast<int>(used.size()) && !used[record.targetIndex]) {
                reordered.push_back(std::move(editor.document.layers[record.targetIndex]));
                used[record.targetIndex] = 1u;
            }
        }

        for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
            if (!used[i]) {
                reordered.push_back(std::move(editor.document.layers[i]));
            }
        }

        editor.document.layers = std::move(reordered);
    }

    int loadedCount = 0;
    for (std::size_t i = 0; i < loadedRecords.size(); ++i) {
        const int applyIndex = (isVersion2 || isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15)
            ? static_cast<int>(i)
            : loadedRecords[i].targetIndex;
        if (!(
            applyIndex >= 0 &&
            applyIndex < static_cast<int>(editor.document.layers.size())
        )) {
            continue;
        }

        EditorLayer& layer = editor.document.layers[applyIndex];
        layer.mesh = std::move(loadedRecords[i].mesh);
        layer.visible = loadedRecords[i].visible;
        layer.opacity = std::clamp(loadedRecords[i].opacity, 0.0f, 1.0f);
        layer.hueShift = std::clamp(loadedRecords[i].hueShift, -100.0f, 100.0f);
        layer.saturationScale = std::clamp(loadedRecords[i].saturationScale, 0.0f, 200.0f);
        layer.lightnessShift = std::clamp(loadedRecords[i].lightnessShift, -100.0f, 100.0f);
        layer.renderOrderOverride = std::move(loadedRecords[i].renderOrderOverride);
        layer.maskLayerIndices = std::move(loadedRecords[i].masks);
        if (isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15) {
            ApplyTextureToLayer(editor.document, layer, loadedRecords[i].textureIndex);
        }
        UpdateLayerBoundsFromCurrentMesh(layer);
        ++loadedCount;
    }

    if (!ReadBinaryHistory(
        file,
        editor,
        isVersion2 || isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion2 || isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion13 || isVersion14 || isVersion15,
        isVersion14 || isVersion15,
        isVersion14 || isVersion15
    )) {
        error = "Binary mesh file has invalid history data.";
        return false;
    }

    if (!ReadBinaryParameters(
        file,
        editor,
        isVersion3 || isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion4 || isVersion5 || isVersion6 || isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion7 || isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion8 || isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion9 || isVersion10 || isVersion11 || isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion12 || isVersion13 || isVersion14 || isVersion15,
        isVersion13 || isVersion14 || isVersion15,
        isVersion14 || isVersion15
    )) {
        error = "Binary mesh file has invalid parameter data.";
        return false;
    }
    ReconcileLoadedParameterMeshEndpoints(editor);

    if (loadedCount > 0) {
        editor.statusText += " Loaded binary meshes for " + std::to_string(loadedCount) + " layers.";
    }

    if (!editor.history.undoStack.empty() || !editor.history.redoStack.empty()) {
        editor.statusText += " Loaded edit history.";
    }

    if (!editor.parameters.empty()) {
        editor.statusText += " Loaded parameters.";
    }

    return true;
}

static bool LoadJsonMeshesForEditor(EditorState& editor, std::string& error) {
    const std::filesystem::path meshPath = GetMeshSidecarPath(editor.document.path);
    if (!std::filesystem::exists(meshPath)) {
        return true;
    }

    std::ifstream file(meshPath, std::ios::binary);
    if (!file) {
        error = "Could not open mesh file: " + meshPath.string();
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& ex) {
        error = std::string("Could not parse mesh file: ") + ex.what();
        return false;
    }

    if (!root.is_object() || !root.contains("layers") || !root["layers"].is_array()) {
        error = "Mesh file has an invalid format.";
        return false;
    }

    int loadedCount = 0;
    for (const json& layerJson : root["layers"]) {
        const int savedIndex = layerJson.value("index", -1);
        const std::string savedName = layerJson.value("name", std::string{});

        const int targetIndex = ResolveSavedLayerIndex(editor, savedIndex, savedName);

        if (targetIndex < 0 || !layerJson.contains("mesh")) {
            continue;
        }

        LayerMesh mesh;
        if (JsonToMesh(layerJson["mesh"], mesh)) {
            EditorLayer& layer = editor.document.layers[targetIndex];
            layer.mesh = std::move(mesh);
            layer.visible = layerJson.value("visible", layer.visible);
            layer.opacity = std::clamp(layerJson.value("opacity", layer.opacity), 0.0f, 1.0f);
            layer.renderOrderOverride = layerJson.value("renderOrderOverride", layer.renderOrderOverride);
            layer.maskLayerIndices.clear();
            if (layerJson.contains("masks") && layerJson["masks"].is_array()) {
                for (const json& maskJson : layerJson["masks"]) {
                    if (maskJson.is_number_integer()) {
                        layer.maskLayerIndices.push_back(maskJson.get<int>());
                    }
                }
            }
            UpdateLayerBoundsFromCurrentMesh(layer);
            ++loadedCount;
        }
    }

    if (loadedCount > 0) {
        editor.statusText += " Loaded saved meshes for " + std::to_string(loadedCount) + " layers.";
    }

    LoadHistoryForEditor(root, editor);
    if (!editor.history.undoStack.empty() || !editor.history.redoStack.empty()) {
        editor.statusText += " Loaded edit history.";
    }

    return true;
}

static bool LoadMeshesForEditor(EditorState& editor, std::string& error) {
    if (std::filesystem::exists(GetBinaryMeshSidecarPath(editor.document.path))) {
        return LoadBinaryMeshesForEditor(editor, GetBinaryMeshSidecarPath(editor.document.path), error);
    }

    return LoadJsonMeshesForEditor(editor, error);
}

static int FindTextureIndexByName(const EditorDocument& document, const std::string& name) {
    for (int i = 0; i < static_cast<int>(document.textures.size()); ++i) {
        if (document.textures[i].name == name) {
            return i;
        }
    }
    return -1;
}

static std::vector<std::string> CaptureTextureNamesByIndex(const EditorDocument& document) {
    std::vector<std::string> names;
    names.reserve(document.textures.size());
    for (const EditorTexture& texture : document.textures) {
        names.push_back(texture.name);
    }
    return names;
}

static int RemapTextureIndexByName(
    const EditorDocument& document,
    const std::vector<std::string>& oldTextureNames,
    int oldTextureIndex
) {
    if (oldTextureIndex < 0 || oldTextureIndex >= static_cast<int>(oldTextureNames.size())) {
        return -1;
    }

    return FindTextureIndexByName(document, oldTextureNames[oldTextureIndex]);
}

static void RemapParameterTextureIndicesAfterPsdReload(
    EditorState& editor,
    const std::vector<std::string>& oldTextureNames
) {
    for (DeformParameter& parameter : editor.parameters) {
        for (DeformParameterLayerState& state : parameter.layers) {
            state.textureIndexAt0 = RemapTextureIndexByName(editor.document, oldTextureNames, state.textureIndexAt0);
            state.textureIndexAt1 = RemapTextureIndexByName(editor.document, oldTextureNames, state.textureIndexAt1);
            for (DeformParameterLayerSetpoint& setpoint : state.setpoints) {
                setpoint.textureIndex = RemapTextureIndexByName(editor.document, oldTextureNames, setpoint.textureIndex);
            }
        }
    }
}

static void ResetEditorInteractionState(EditorState& editor) {
    editor.selectedLayer = editor.document.layers.empty() ? -1 : 0;
    editor.selectedLayers.clear();
    if (editor.selectedLayer >= 0) {
        editor.selectedLayers.push_back(editor.selectedLayer);
    }
    editor.selectedVertices.clear();
    editor.selectedEdges.clear();
    editor.selectedDeformVertices.clear();
    editor.draggingLayer = false;
    editor.draggedLayer = -1;
    editor.layerTransformMode = LayerTransformMode::None;
    editor.dragLayerMoved = false;
    editor.layerDragThresholdPassed = false;
    editor.draggingMeshSelection = false;
    editor.meshSelectionMoved = false;
    editor.deformingVertices = false;
    editor.deformVerticesMoved = false;
    editor.deformTransformMode = LayerTransformMode::None;
    editor.boxSelectingMesh = false;
    editor.boxSelectionMoved = false;
    editor.requestFitView = true;
}

bool AttachPsdToProject(
    const std::string& path,
    EditorState& editor,
    GLFWwindow*,
    std::string& error
) {
    const bool hadNoLayers = editor.document.layers.empty();
    const std::string projectPath = editor.document.projectPath;
    const std::string focusedPath = editor.document.path;
    const std::vector<std::string> oldTextureNames = CaptureTextureNamesByIndex(editor.document);

    std::vector<std::string> layerTextureNames;
    layerTextureNames.reserve(editor.document.layers.size());
    for (const EditorLayer& layer : editor.document.layers) {
        if (layer.textureIndex >= 0 && layer.textureIndex < static_cast<int>(editor.document.textures.size())) {
            layerTextureNames.push_back(editor.document.textures[layer.textureIndex].name);
        } else {
            layerTextureNames.push_back(std::string{});
        }
    }

    if (!LoadPsdTextures(path, editor.document, error)) {
        return false;
    }

    editor.document.projectPath = projectPath;
    editor.document.path = focusedPath.empty() ? projectPath : focusedPath;
    RemapParameterTextureIndicesAfterPsdReload(editor, oldTextureNames);

    if (hadNoLayers) {
        editor.document.layers.clear();
        editor.document.layers.reserve(editor.document.textures.size());
        for (int textureIndex = 0; textureIndex < static_cast<int>(editor.document.textures.size()); ++textureIndex) {
            const EditorTexture& texture = editor.document.textures[textureIndex];
            EditorLayer layer = CreateLayerForTexture(texture, textureIndex);
            layer.mesh = CreateInitialQuadMeshForEditorTexture(texture);
            ApplyTextureToLayer(editor.document, layer, textureIndex);
            editor.document.layers.push_back(std::move(layer));
        }
    } else {
        for (int layerIndex = 0; layerIndex < static_cast<int>(editor.document.layers.size()); ++layerIndex) {
            std::string textureName;
            if (layerIndex < static_cast<int>(layerTextureNames.size())) {
                textureName = layerTextureNames[layerIndex];
            }
            ApplyTextureToLayer(
                editor.document,
                editor.document.layers[layerIndex],
                FindTextureIndexByName(editor.document, textureName)
            );
        }
    }

    ResetEditorInteractionState(editor);
    editor.statusText = "Attached PSD: " + path;
    editor.errorText.clear();
    return true;
}

bool LoadProjectForEditor(
    const std::string& path,
    EditorState& editor,
    GLFWwindow* window,
    std::string& error
) {
    DestroyDocument(editor.document);
    editor.parameters.clear();
    editor.selectedParameter = -1;
    editor.history = EditHistory{};

    editor.document.projectPath = path;
    editor.document.path = path;

    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
        error = "Could not open project file: " + path;
        return false;
    }
    char magic[8] = {};
    probe.read(magic, sizeof(magic));
    const bool hasProjectPsdField =
        std::string(magic, sizeof(magic)) == std::string("MESHEDBB", 8) ||
        std::string(magic, sizeof(magic)) == std::string("MESHEDBC", 8) ||
        std::string(magic, sizeof(magic)) == std::string("MESHEDBD", 8) ||
        std::string(magic, sizeof(magic)) == std::string("MESHEDBE", 8) ||
        std::string(magic, sizeof(magic)) == std::string("MESHEDBF", 8);
    std::string projectPsdPath;
    if (hasProjectPsdField) {
        std::uint32_t canvasWidth = 0;
        std::uint32_t canvasHeight = 0;
        std::string psdName;
        if (
            !ReadBinaryValue(probe, canvasWidth) ||
            !ReadBinaryValue(probe, canvasHeight) ||
            !ReadBinaryString(probe, psdName) ||
            !ReadBinaryString(probe, projectPsdPath)
        ) {
            error = "Project file has an invalid header.";
            return false;
        }
    }
    probe.close();

    std::string psdPath;
    if (!hasProjectPsdField) {
        psdPath = std::filesystem::path(path).replace_extension(".psd").string();
        if (std::filesystem::exists(psdPath)) {
            if (!AttachPsdToProject(psdPath, editor, window, error)) {
                return false;
            }
            editor.document.projectPath = path;
            editor.document.path = path;
        }
    } else if (!projectPsdPath.empty() && std::filesystem::exists(projectPsdPath)) {
        if (!AttachPsdToProject(projectPsdPath, editor, window, error)) {
            return false;
        }
        editor.document.projectPath = path;
        editor.document.path = path;
    } else if (!projectPsdPath.empty()) {
        editor.document.psdPath = projectPsdPath;
    }

    if (!LoadBinaryMeshesForEditor(editor, path, error)) {
        return false;
    }

    editor.document.projectPath = path;
    editor.document.path = path;
    SaveLastProjectPath(path);
    ResetEditorInteractionState(editor);
    editor.statusText = "Loaded project: " + path;
    editor.errorText.clear();
    return true;
}

bool SaveProjectForEditor(const EditorState& editor, const std::string& path, std::string& error) {
    if (path.empty()) {
        error = "No project path is set.";
        return false;
    }

    const std::filesystem::path meshPath(path);
    if (!meshPath.parent_path().empty()) {
        std::filesystem::create_directories(meshPath.parent_path());
    }

    std::ofstream file(meshPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Could not write binary mesh file: " + meshPath.string();
        return false;
    }

    file.write("MESHEDBF", 8);

    const std::uint32_t canvasWidth = static_cast<std::uint32_t>(std::max(0, editor.document.canvasWidth));
    const std::uint32_t canvasHeight = static_cast<std::uint32_t>(std::max(0, editor.document.canvasHeight));
    const std::string psdName = std::filesystem::path(editor.document.psdPath).filename().string();

    if (
        !WriteBinaryValue(file, canvasWidth) ||
        !WriteBinaryValue(file, canvasHeight) ||
        !WriteBinaryString(file, psdName) ||
        !WriteBinaryString(file, editor.document.psdPath)
    ) {
        error = "Could not write binary mesh header.";
        return false;
    }

    if (editor.document.layers.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        error = "Too many layers to save.";
        return false;
    }

    const std::uint32_t layerCount = static_cast<std::uint32_t>(editor.document.layers.size());
    if (!WriteBinaryValue(file, layerCount)) {
        error = "Could not write layer count.";
        return false;
    }

    for (int i = 0; i < static_cast<int>(editor.document.layers.size()); ++i) {
        const EditorLayer& layer = editor.document.layers[i];
        if (
            !WriteBinaryValue(file, i) ||
            !WriteBinaryString(file, layer.name) ||
            !WriteBinaryMesh(file, layer.mesh)
        ) {
            error = "Could not write layer mesh data.";
            return false;
        }

        if (
            !WriteBinaryValue(file, layer.opacity) ||
            !WriteBinaryValue(file, layer.hueShift) ||
            !WriteBinaryValue(file, layer.saturationScale) ||
            !WriteBinaryValue(file, layer.lightnessShift) ||
            layer.maskLayerIndices.size() > (std::numeric_limits<std::uint32_t>::max)()
        ) {
            error = "Could not write layer data.";
            return false;
        }

        const std::uint32_t maskCount = static_cast<std::uint32_t>(layer.maskLayerIndices.size());
        if (!WriteBinaryValue(file, maskCount)) {
            error = "Could not write layer mask count.";
            return false;
        }

        for (const int maskLayerIndex : layer.maskLayerIndices) {
            if (!WriteBinaryValue(file, maskLayerIndex)) {
                error = "Could not write layer mask data.";
                return false;
            }
        }

        if (!WriteBinaryString(file, layer.renderOrderOverride)) {
            error = "Could not write layer render order override.";
            return false;
        }

        const std::uint8_t visible = layer.visible ? 1u : 0u;
        if (!WriteBinaryValue(file, visible)) {
            error = "Could not write layer visibility.";
            return false;
        }

        if (!WriteBinaryValue(file, layer.textureIndex)) {
            error = "Could not write layer texture index.";
            return false;
        }

        const std::string textureName =
            layer.textureIndex >= 0 && layer.textureIndex < static_cast<int>(editor.document.textures.size())
                ? editor.document.textures[layer.textureIndex].name
                : std::string{};
        if (!WriteBinaryString(file, textureName)) {
            error = "Could not write layer texture name.";
            return false;
        }
    }

    if (!WriteBinaryHistory(file, editor)) {
        error = "Could not write edit history.";
        return false;
    }

    if (!WriteBinaryParameters(file, editor)) {
        error = "Could not write deformation parameters.";
        return false;
    }

    return true;
}

bool SaveMeshesForEditor(const EditorState& editor, std::string& error) {
    const std::string path = !editor.document.projectPath.empty()
        ? editor.document.projectPath
        : (!editor.document.path.empty()
            ? GetDefaultProjectPathForPsd(editor.document.path).string()
            : std::string{});
    return SaveProjectForEditor(editor, path, error);
}

bool LoadPsdIntoEditor(
    const std::string& path,
    EditorState& editor,
    GLFWwindow*,
    std::string& error
) {
    PsdDocumentRGBA psd;

    if (!PsdLoader::LoadPsd(path, psd, error)) {
        return false;
    }

    std::reverse(psd.layers.begin(), psd.layers.end());

    DestroyDocument(editor.document);
    editor.parameters.clear();
    editor.selectedParameter = -1;

    editor.document.path = path;
    editor.document.psdPath = path;
    editor.document.projectPath = GetDefaultProjectPathForPsd(path).string();
    editor.document.canvasWidth = psd.canvasWidth;
    editor.document.canvasHeight = psd.canvasHeight;

    editor.document.textures.reserve(psd.layers.size());
    editor.document.layers.reserve(psd.layers.size());

    for (int textureIndex = 0; textureIndex < static_cast<int>(psd.layers.size()); ++textureIndex) {
        const LayerImageRGBA& src = psd.layers[textureIndex];
        EditorTexture texture;
        texture.name = src.name;
        texture.left = src.left;
        texture.top = src.top;
        texture.right = src.right;
        texture.bottom = src.bottom;
        texture.width = src.width;
        texture.height = src.height;
        texture.alpha = ExtractAlphaChannel(src.rgba, src.width, src.height);
        std::vector<std::uint8_t> premultipliedRgba = src.rgba;
        PremultiplyAlpha(premultipliedRgba);
        texture.baseRgba = premultipliedRgba;
        texture.renderedRgba = premultipliedRgba;
        texture.texture = UploadTextureRGBA(premultipliedRgba.data(), src.width, src.height);
        ComputeTexturePreviewUvs(texture);
        editor.document.textures.push_back(std::move(texture));

        EditorLayer dst;
        dst.name = src.name;
        dst.left = src.left;
        dst.top = src.top;
        dst.right = src.right;
        dst.bottom = src.bottom;
        dst.visible = src.visible;
        dst.mesh = CreateInitialQuadMeshForLayer(src);
        ApplyTextureToLayer(editor.document, dst, textureIndex);

        editor.document.layers.push_back(std::move(dst));
    }

    editor.selectedLayer = editor.document.layers.empty() ? -1 : 0;
    editor.selectedLayers.clear();
    if (editor.selectedLayer >= 0) {
        editor.selectedLayers.push_back(editor.selectedLayer);
    }
    editor.zoom = 1.0f;
    editor.pan = ImVec2(40.0f, 40.0f);
    editor.requestFitView = true;
    editor.draggingLayer = false;
    editor.draggedLayer = -1;
    editor.layerTransformMode = LayerTransformMode::None;
    editor.dragLayerMoved = false;
    editor.layerDragThresholdPassed = false;
    editor.layerTransformStartMesh = {};
    editor.selectedVertices.clear();
    editor.selectedEdges.clear();
    editor.draggingMeshSelection = false;
    editor.meshSelectionMoved = false;
    editor.deformingVertices = false;
    editor.deformVerticesMoved = false;
    editor.deformTransformMode = LayerTransformMode::None;
    editor.deformStartMesh = {};
    editor.boxSelectingMesh = false;
    editor.boxSelectionMoved = false;
    editor.history = EditHistory{};

    editor.statusText =
        "Loaded " + std::to_string(editor.document.layers.size()) + " layers.";
    editor.errorText.clear();

    std::string meshError;
    if (!LoadMeshesForEditor(editor, meshError)) {
        editor.errorText = meshError;
    }

    SaveLastProjectPath(editor.document.projectPath);

    return true;
}

void TranslateLayerMesh(EditorLayer& layer, float dx, float dy) {
    for (MeshVertex& vertex : layer.mesh.vertices) {
        vertex.position.x += dx;
        vertex.position.y += dy;
    }

    layer.left += static_cast<int>(std::round(dx));
    layer.right += static_cast<int>(std::round(dx));
    layer.top += static_cast<int>(std::round(dy));
    layer.bottom += static_cast<int>(std::round(dy));
}


