#include "MeshGenerator.hpp"

#include "EditorDocument.hpp"
#include "EditorHistory.hpp"
#include "delaunator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct GridPoint {
    int x = 0;
    int y = 0;
};

struct BoundaryEdge {
    GridPoint a;
    GridPoint b;
};

struct GridPointHash {
    std::size_t operator()(const GridPoint& p) const noexcept {
        return (static_cast<std::size_t>(static_cast<std::uint32_t>(p.x)) << 32u) ^
               static_cast<std::size_t>(static_cast<std::uint32_t>(p.y));
    }
};

struct GridPointEq {
    bool operator()(const GridPoint& a, const GridPoint& b) const noexcept {
        return a.x == b.x && a.y == b.y;
    }
};

struct VertexEdge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

struct VertexEdgeHash {
    std::size_t operator()(const VertexEdge& edge) const noexcept {
        return (static_cast<std::size_t>(edge.a) << 32u) ^
               static_cast<std::size_t>(edge.b);
    }
};

struct VertexEdgeEq {
    bool operator()(const VertexEdge& a, const VertexEdge& b) const noexcept {
        return a.a == b.a && a.b == b.b;
    }
};

static VertexEdge NormalizeEdge(std::uint32_t a, std::uint32_t b) {
    if (a > b) {
        std::swap(a, b);
    }

    return VertexEdge{a, b};
}

static std::size_t MaskIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

static bool MaskAt(const std::vector<std::uint8_t>& mask, int width, int height, int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return false;
    }

    return mask[MaskIndex(x, y, width)] != 0;
}

static float PolygonArea(const std::vector<Vec2>& polygon) {
    double area = 0.0;

    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[(i + 1u) % polygon.size()];
        area += static_cast<double>(a.x) * static_cast<double>(b.y) -
                static_cast<double>(b.x) * static_cast<double>(a.y);
    }

    return static_cast<float>(area * 0.5);
}

static float TriangleSignedArea(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool PointInTriangle(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c) {
    const float ab = TriangleSignedArea(a, b, p);
    const float bc = TriangleSignedArea(b, c, p);
    const float ca = TriangleSignedArea(c, a, p);

    const bool hasNeg = ab < 0.0f || bc < 0.0f || ca < 0.0f;
    const bool hasPos = ab > 0.0f || bc > 0.0f || ca > 0.0f;
    return !(hasNeg && hasPos);
}

static float DistanceSquared(const Vec2& a, const Vec2& b) {
    const Vec2 delta = a - b;
    return Dot(delta, delta);
}

static bool ContainsPointExact(const std::vector<Vec2>& points, const Vec2& point) {
    return std::any_of(points.begin(), points.end(), [&](const Vec2& existing) {
        return existing.x == point.x && existing.y == point.y;
    });
}

static void PushUniquePoint(std::vector<Vec2>& points, const Vec2& point) {
    if (!ContainsPointExact(points, point)) {
        points.push_back(point);
    }
}

static std::vector<std::uint8_t> BuildBufferedAlphaMask(
    const EditorLayer& layer,
    std::uint8_t alphaThreshold,
    float bufferPx
) {
    std::vector<std::uint8_t> base(
        static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height),
        0u
    );

    std::vector<GridPoint> opaquePixels;
    opaquePixels.reserve(base.size() / 2u);

    for (int y = 0; y < layer.height; ++y) {
        for (int x = 0; x < layer.width; ++x) {
            if (LayerAlphaAt(layer, x, y) > alphaThreshold) {
                base[MaskIndex(x, y, layer.width)] = 1u;
                opaquePixels.push_back(GridPoint{x, y});
            }
        }
    }

    if (bufferPx <= 0.0f || opaquePixels.empty()) {
        return base;
    }

    std::vector<std::uint8_t> buffered = base;

    const int radius = static_cast<int>(std::ceil(bufferPx));
    const float radiusSq = bufferPx * bufferPx;

    for (const GridPoint& pixel : opaquePixels) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (static_cast<float>(dx * dx + dy * dy) > radiusSq) {
                    continue;
                }

                const int x = pixel.x + dx;
                const int y = pixel.y + dy;

                if (x < 0 || x >= layer.width || y < 0 || y >= layer.height) {
                    continue;
                }

                buffered[MaskIndex(x, y, layer.width)] = 1u;
            }
        }
    }

    return buffered;
}

static std::vector<BoundaryEdge> ExtractBoundaryEdges(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height
) {
    std::vector<BoundaryEdge> edges;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!MaskAt(mask, width, height, x, y)) {
                continue;
            }

            if (!MaskAt(mask, width, height, x, y - 1)) {
                edges.push_back(BoundaryEdge{{x, y}, {x + 1, y}});
            }
            if (!MaskAt(mask, width, height, x + 1, y)) {
                edges.push_back(BoundaryEdge{{x + 1, y}, {x + 1, y + 1}});
            }
            if (!MaskAt(mask, width, height, x, y + 1)) {
                edges.push_back(BoundaryEdge{{x + 1, y + 1}, {x, y + 1}});
            }
            if (!MaskAt(mask, width, height, x - 1, y)) {
                edges.push_back(BoundaryEdge{{x, y + 1}, {x, y}});
            }
        }
    }

    return edges;
}

static std::vector<std::vector<Vec2>> TraceBoundaryLoops(const std::vector<BoundaryEdge>& edges) {
    std::unordered_map<GridPoint, std::vector<std::size_t>, GridPointHash, GridPointEq> edgesByStart;
    edgesByStart.reserve(edges.size());

    for (std::size_t i = 0; i < edges.size(); ++i) {
        edgesByStart[edges[i].a].push_back(i);
    }

    std::vector<std::uint8_t> used(edges.size(), 0u);
    std::vector<std::vector<Vec2>> loops;

    for (std::size_t startEdge = 0; startEdge < edges.size(); ++startEdge) {
        if (used[startEdge]) {
            continue;
        }

        const GridPoint loopStart = edges[startEdge].a;
        std::size_t edgeIndex = startEdge;
        std::vector<Vec2> loop;

        for (;;) {
            if (used[edgeIndex]) {
                break;
            }

            used[edgeIndex] = 1u;
            const BoundaryEdge& edge = edges[edgeIndex];
            loop.push_back(Vec2{static_cast<float>(edge.a.x), static_cast<float>(edge.a.y)});

            if (GridPointEq{}(edge.b, loopStart)) {
                break;
            }

            auto it = edgesByStart.find(edge.b);
            if (it == edgesByStart.end()) {
                break;
            }

            std::size_t nextEdge = edges.size();
            for (const std::size_t candidate : it->second) {
                if (!used[candidate]) {
                    nextEdge = candidate;
                    break;
                }
            }

            if (nextEdge == edges.size()) {
                break;
            }

            edgeIndex = nextEdge;
        }

        if (loop.size() >= 3) {
            loops.push_back(std::move(loop));
        }
    }

    return loops;
}

static std::vector<Vec2> SimplifyClosedPolylineBySpacing(
    const std::vector<Vec2>& loop,
    float spacingPx,
    int maxPoints
) {
    if (loop.size() <= 3) {
        return loop;
    }

    std::vector<Vec2> simplified;
    simplified.reserve(loop.size());
    simplified.push_back(loop.front());

    Vec2 last = loop.front();
    float accum = 0.0f;

    for (std::size_t i = 1; i < loop.size(); ++i) {
        const Vec2 point = loop[i];
        accum += Len(point - last);

        if (accum >= spacingPx) {
            PushUniquePoint(simplified, point);
            last = point;
            accum = 0.0f;
        }
    }

    if (simplified.size() < 3) {
        simplified = loop;
    }

    const int cappedMax = std::clamp(maxPoints, 3, 4000);
    if (static_cast<int>(simplified.size()) > cappedMax) {
        std::vector<Vec2> capped;
        capped.reserve(static_cast<std::size_t>(cappedMax));

        for (int i = 0; i < cappedMax; ++i) {
            PushUniquePoint(
                capped,
                simplified[(static_cast<std::size_t>(i) * simplified.size()) /
                           static_cast<std::size_t>(cappedMax)]
            );
        }

        simplified.swap(capped);
    }

    return simplified;
}

static std::vector<Vec2> ExtractPrimaryContourPolygon(
    const EditorLayer& layer,
    const std::vector<std::uint8_t>& mask,
    float spacingPx,
    int maxPerimeterPoints
) {
    const std::vector<BoundaryEdge> edges = ExtractBoundaryEdges(mask, layer.width, layer.height);
    std::vector<std::vector<Vec2>> loops = TraceBoundaryLoops(edges);

    if (loops.empty()) {
        throw std::runtime_error("Not enough boundary pixels to generate a mesh.");
    }

    auto largest = std::max_element(loops.begin(), loops.end(), [](const auto& a, const auto& b) {
        return std::abs(PolygonArea(a)) < std::abs(PolygonArea(b));
    });

    std::vector<Vec2> polygon = SimplifyClosedPolylineBySpacing(
        *largest,
        spacingPx,
        maxPerimeterPoints
    );

    if (polygon.size() < 3) {
        throw std::runtime_error("Not enough perimeter points to generate a mesh.");
    }

    if (PolygonArea(polygon) < 0.0f) {
        std::reverse(polygon.begin(), polygon.end());
    }

    return polygon;
}

static std::vector<float> ComputeDistanceFromMaskEdge(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height
) {
    constexpr float kInf = 1.0e20f;
    const float diagonal = std::sqrt(2.0f);

    std::vector<float> distance(mask.size(), kInf);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!MaskAt(mask, width, height, x, y)) {
                distance[MaskIndex(x, y, width)] = 0.0f;
            }
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = MaskIndex(x, y, width);
            float best = distance[i];

            if (x > 0) {
                best = std::min(best, distance[MaskIndex(x - 1, y, width)] + 1.0f);
            }
            if (y > 0) {
                best = std::min(best, distance[MaskIndex(x, y - 1, width)] + 1.0f);
            }
            if (x > 0 && y > 0) {
                best = std::min(best, distance[MaskIndex(x - 1, y - 1, width)] + diagonal);
            }
            if (x + 1 < width && y > 0) {
                best = std::min(best, distance[MaskIndex(x + 1, y - 1, width)] + diagonal);
            }

            distance[i] = best;
        }
    }

    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            const std::size_t i = MaskIndex(x, y, width);
            float best = distance[i];

            if (x + 1 < width) {
                best = std::min(best, distance[MaskIndex(x + 1, y, width)] + 1.0f);
            }
            if (y + 1 < height) {
                best = std::min(best, distance[MaskIndex(x, y + 1, width)] + 1.0f);
            }
            if (x + 1 < width && y + 1 < height) {
                best = std::min(best, distance[MaskIndex(x + 1, y + 1, width)] + diagonal);
            }
            if (x > 0 && y + 1 < height) {
                best = std::min(best, distance[MaskIndex(x - 1, y + 1, width)] + diagonal);
            }

            distance[i] = best;
        }
    }

    return distance;
}

static std::vector<std::uint8_t> BuildDistanceBandMask(
    const std::vector<float>& distance,
    int width,
    int height,
    float minDistance
) {
    std::vector<std::uint8_t> bandMask(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        0u
    );

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = MaskIndex(x, y, width);
            if (distance[i] >= minDistance) {
                bandMask[i] = 1u;
            }
        }
    }

    return bandMask;
}

static std::vector<std::vector<Vec2>> ExtractInteriorContourRings(
    const EditorLayer& layer,
    const std::vector<std::uint8_t>& mask,
    float depthSpacing,
    float pointSpacing,
    int maxInteriorPoints
) {
    std::vector<std::vector<Vec2>> rings;

    if (maxInteriorPoints <= 0 || depthSpacing <= 0.0f || pointSpacing <= 0.0f) {
        return rings;
    }

    const std::vector<float> distance = ComputeDistanceFromMaskEdge(
        mask,
        layer.width,
        layer.height
    );

    const float maxDistance = *std::max_element(distance.begin(), distance.end());
    int remainingInteriorPoints = maxInteriorPoints;

    for (
        float depth = depthSpacing;
        depth <= maxDistance && remainingInteriorPoints >= 3;
        depth += depthSpacing
    ) {
        const std::vector<std::uint8_t> bandMask = BuildDistanceBandMask(
            distance,
            layer.width,
            layer.height,
            depth
        );

        try {
            std::vector<Vec2> ring = ExtractPrimaryContourPolygon(
                layer,
                bandMask,
                pointSpacing,
                remainingInteriorPoints
            );

            if (ring.size() < 3) {
                break;
            }

            remainingInteriorPoints -= static_cast<int>(ring.size());
            rings.push_back(std::move(ring));
        } catch (const std::exception&) {
            break;
        }
    }

    return rings;
}

static std::vector<std::uint32_t> TriangulateSimplePolygon(const std::vector<Vec2>& polygon) {
    std::vector<std::uint32_t> indices;

    if (polygon.size() < 3) {
        return indices;
    }

    std::vector<std::uint32_t> remaining;
    remaining.reserve(polygon.size());

    for (std::uint32_t i = 0; i < polygon.size(); ++i) {
        remaining.push_back(i);
    }

    int guard = 0;
    const int maxIterations = static_cast<int>(polygon.size() * polygon.size());

    while (remaining.size() > 3 && guard++ < maxIterations) {
        bool clippedEar = false;

        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const std::uint32_t ia = remaining[(i + remaining.size() - 1u) % remaining.size()];
            const std::uint32_t ib = remaining[i];
            const std::uint32_t ic = remaining[(i + 1u) % remaining.size()];

            const Vec2& a = polygon[ia];
            const Vec2& b = polygon[ib];
            const Vec2& c = polygon[ic];

            if (TriangleSignedArea(a, b, c) <= 0.0001f) {
                continue;
            }

            bool containsOtherPoint = false;
            for (const std::uint32_t candidate : remaining) {
                if (candidate == ia || candidate == ib || candidate == ic) {
                    continue;
                }

                if (PointInTriangle(polygon[candidate], a, b, c)) {
                    containsOtherPoint = true;
                    break;
                }
            }

            if (containsOtherPoint) {
                continue;
            }

            indices.push_back(ia);
            indices.push_back(ib);
            indices.push_back(ic);
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clippedEar = true;
            break;
        }

        if (!clippedEar) {
            break;
        }
    }

    if (remaining.size() == 3) {
        indices.push_back(remaining[0]);
        indices.push_back(remaining[1]);
        indices.push_back(remaining[2]);
    }

    return indices;
}

static std::uint32_t AddMeshVertex(LayerMesh& mesh, const EditorLayer& layer, const Vec2& point) {
    const float invW = 1.0f / static_cast<float>(std::max(1, layer.width));
    const float invH = 1.0f / static_cast<float>(std::max(1, layer.height));

    mesh.vertices.push_back(MeshVertex{
        point,
        Vec2{
            std::clamp(point.x * invW, 0.0f, 1.0f),
            std::clamp(point.y * invH, 0.0f, 1.0f)
        }
    });

    return static_cast<std::uint32_t>(mesh.vertices.size() - 1u);
}

static std::vector<std::uint32_t> AddRingVertices(
    LayerMesh& mesh,
    const EditorLayer& layer,
    const std::vector<Vec2>& ring
) {
    std::vector<std::uint32_t> indices;
    indices.reserve(ring.size());

    for (const Vec2& point : ring) {
        indices.push_back(AddMeshVertex(mesh, layer, point));
    }

    return indices;
}

static void AddTriangleIfNonDegenerate(
    LayerMesh& mesh,
    std::uint32_t ia,
    std::uint32_t ib,
    std::uint32_t ic
) {
    if (ia == ib || ib == ic || ic == ia) {
        return;
    }

    const Vec2& a = mesh.vertices[ia].position;
    const Vec2& b = mesh.vertices[ib].position;
    const Vec2& c = mesh.vertices[ic].position;

    if (std::abs(TriangleSignedArea(a, b, c)) <= 0.0001f) {
        return;
    }

    if (TriangleSignedArea(a, b, c) > 0.0f) {
        mesh.indices.push_back(ia);
        mesh.indices.push_back(ib);
        mesh.indices.push_back(ic);
    } else {
        mesh.indices.push_back(ia);
        mesh.indices.push_back(ic);
        mesh.indices.push_back(ib);
    }
}

static std::size_t FindClosestRingIndex(
    const LayerMesh& mesh,
    const std::vector<std::uint32_t>& ring,
    const Vec2& point
) {
    std::size_t closest = 0;
    float bestDistanceSq = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < ring.size(); ++i) {
        const float distanceSq = DistanceSquared(mesh.vertices[ring[i]].position, point);
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            closest = i;
        }
    }

    return closest;
}

static std::vector<std::uint32_t> RotateRing(
    const std::vector<std::uint32_t>& ring,
    std::size_t start
) {
    std::vector<std::uint32_t> rotated;
    rotated.reserve(ring.size());

    for (std::size_t i = 0; i < ring.size(); ++i) {
        rotated.push_back(ring[(start + i) % ring.size()]);
    }

    return rotated;
}

static std::vector<float> BuildRingArcFractions(
    const LayerMesh& mesh,
    const std::vector<std::uint32_t>& ring
) {
    std::vector<float> fractions(ring.size() + 1u, 0.0f);

    float totalLength = 0.0f;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2& a = mesh.vertices[ring[i]].position;
        const Vec2& b = mesh.vertices[ring[(i + 1u) % ring.size()]].position;
        totalLength += Len(b - a);
        fractions[i + 1u] = totalLength;
    }

    if (totalLength <= 0.0001f) {
        for (std::size_t i = 0; i < fractions.size(); ++i) {
            fractions[i] = static_cast<float>(i) / static_cast<float>(ring.size());
        }
        return fractions;
    }

    for (float& fraction : fractions) {
        fraction /= totalLength;
    }

    fractions.back() = 1.0f;
    return fractions;
}

static void StitchRings(
    LayerMesh& mesh,
    const std::vector<std::uint32_t>& outerRing,
    const std::vector<std::uint32_t>& innerRing
) {
    if (outerRing.size() < 3 || innerRing.size() < 3) {
        return;
    }

    const std::size_t innerStart = FindClosestRingIndex(
        mesh,
        innerRing,
        mesh.vertices[outerRing.front()].position
    );

    const std::vector<std::uint32_t> inner = RotateRing(innerRing, innerStart);

    const std::vector<float> outerArc = BuildRingArcFractions(mesh, outerRing);
    const std::vector<float> innerArc = BuildRingArcFractions(mesh, inner);

    std::size_t outerStep = 0;
    std::size_t innerStep = 0;

    while (outerStep < outerRing.size() || innerStep < inner.size()) {
        if (outerStep >= outerRing.size()) {
            AddTriangleIfNonDegenerate(
                mesh,
                outerRing[outerStep % outerRing.size()],
                inner[innerStep % inner.size()],
                inner[(innerStep + 1u) % inner.size()]
            );
            ++innerStep;
            continue;
        }

        if (innerStep >= inner.size()) {
            AddTriangleIfNonDegenerate(
                mesh,
                outerRing[outerStep % outerRing.size()],
                outerRing[(outerStep + 1u) % outerRing.size()],
                inner[innerStep % inner.size()]
            );
            ++outerStep;
            continue;
        }

        const float nextOuter = outerArc[outerStep + 1u];
        const float nextInner = innerArc[innerStep + 1u];

        if (nextOuter <= nextInner) {
            AddTriangleIfNonDegenerate(
                mesh,
                outerRing[outerStep % outerRing.size()],
                outerRing[(outerStep + 1u) % outerRing.size()],
                inner[innerStep % inner.size()]
            );
            ++outerStep;
        } else {
            AddTriangleIfNonDegenerate(
                mesh,
                outerRing[outerStep % outerRing.size()],
                inner[innerStep % inner.size()],
                inner[(innerStep + 1u) % inner.size()]
            );
            ++innerStep;
        }
    }
}

static void FillRingInterior(
    LayerMesh& mesh,
    const std::vector<Vec2>& ring,
    const std::vector<std::uint32_t>& ringIndices
) {
    const std::vector<std::uint32_t> localTriangles = TriangulateSimplePolygon(ring);

    for (std::size_t i = 0; i + 2 < localTriangles.size(); i += 3) {
        AddTriangleIfNonDegenerate(
            mesh,
            ringIndices[localTriangles[i + 0u]],
            ringIndices[localTriangles[i + 1u]],
            ringIndices[localTriangles[i + 2u]]
        );
    }
}

static LayerMesh BuildRingMesh(
    const EditorLayer& layer,
    const std::vector<Vec2>& perimeter,
    const std::vector<std::vector<Vec2>>& interiorRings
) {
    LayerMesh mesh;

    std::size_t vertexCount = perimeter.size();
    for (const std::vector<Vec2>& ring : interiorRings) {
        vertexCount += ring.size();
    }

    mesh.vertices.reserve(vertexCount);

    std::vector<std::vector<std::uint32_t>> ringIndices;
    ringIndices.reserve(interiorRings.size() + 1u);
    ringIndices.push_back(AddRingVertices(mesh, layer, perimeter));

    for (const std::vector<Vec2>& ring : interiorRings) {
        ringIndices.push_back(AddRingVertices(mesh, layer, ring));
    }

    for (std::size_t i = 0; i + 1u < ringIndices.size(); ++i) {
        StitchRings(mesh, ringIndices[i], ringIndices[i + 1u]);
    }

    if (interiorRings.empty()) {
        FillRingInterior(mesh, perimeter, ringIndices.front());
    } else {
        FillRingInterior(mesh, interiorRings.back(), ringIndices.back());
    }

    return mesh;
}

static void AddRingEdges(
    std::unordered_set<VertexEdge, VertexEdgeHash, VertexEdgeEq>& edges,
    std::uint32_t firstIndex,
    std::size_t pointCount
) {
    if (pointCount < 2) {
        return;
    }

    for (std::size_t i = 0; i < pointCount; ++i) {
        const std::uint32_t a = firstIndex + static_cast<std::uint32_t>(i);
        const std::uint32_t b =
            firstIndex + static_cast<std::uint32_t>((i + 1u) % pointCount);
        edges.insert(NormalizeEdge(a, b));
    }
}

static bool TriangleHasRequiredEdge(
    const std::unordered_set<VertexEdge, VertexEdgeHash, VertexEdgeEq>& requiredEdges,
    std::uint32_t ia,
    std::uint32_t ib,
    std::uint32_t ic
) {
    return
        requiredEdges.contains(NormalizeEdge(ia, ib)) ||
        requiredEdges.contains(NormalizeEdge(ib, ic)) ||
        requiredEdges.contains(NormalizeEdge(ic, ia));
}

static bool SegmentMostlyInsideMask(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height,
    const Vec2& a,
    const Vec2& b
) {
    const float length = Len(b - a);
    const int steps = std::max(2, static_cast<int>(std::ceil(length / 6.0f)));

    for (int i = 1; i < steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = a.x + (b.x - a.x) * t;
        const float y = a.y + (b.y - a.y) * t;
        const int px = std::clamp(static_cast<int>(std::floor(x)), 0, width - 1);
        const int py = std::clamp(static_cast<int>(std::floor(y)), 0, height - 1);

        if (!MaskAt(mask, width, height, px, py)) {
            return false;
        }
    }

    return true;
}

static bool TriangleMostlyInsideMask(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height,
    const Vec2& a,
    const Vec2& b,
    const Vec2& c
) {
    constexpr int kSteps = 3;

    for (int y = 0; y <= kSteps; ++y) {
        for (int x = 0; x <= kSteps - y; ++x) {
            const float wb = static_cast<float>(x) / static_cast<float>(kSteps);
            const float wc = static_cast<float>(y) / static_cast<float>(kSteps);
            const float wa = 1.0f - wb - wc;

            const Vec2 sample{
                a.x * wa + b.x * wb + c.x * wc,
                a.y * wa + b.y * wb + c.y * wc
            };

            const int px = std::clamp(static_cast<int>(std::floor(sample.x)), 0, width - 1);
            const int py = std::clamp(static_cast<int>(std::floor(sample.y)), 0, height - 1);

            if (!MaskAt(mask, width, height, px, py)) {
                return false;
            }
        }
    }

    return true;
}

static bool TriangleEdgesAreReasonable(
    const Vec2& a,
    const Vec2& b,
    const Vec2& c,
    float maxEdgeLength
) {
    const float maxEdgeLengthSq = maxEdgeLength * maxEdgeLength;
    return
        DistanceSquared(a, b) <= maxEdgeLengthSq &&
        DistanceSquared(b, c) <= maxEdgeLengthSq &&
        DistanceSquared(c, a) <= maxEdgeLengthSq;
}

static LayerMesh BuildDelaunayMesh(
    const EditorLayer& layer,
    const std::vector<std::uint8_t>& mask,
    const std::vector<Vec2>& perimeter,
    const std::vector<std::vector<Vec2>>& interiorRings,
    float perimeterSpacing,
    float interiorDepthSpacing,
    float interiorPointSpacing
) {
    LayerMesh mesh;

    std::size_t vertexCount = perimeter.size();
    for (const std::vector<Vec2>& ring : interiorRings) {
        vertexCount += ring.size();
    }

    mesh.vertices.reserve(vertexCount);

    std::unordered_set<VertexEdge, VertexEdgeHash, VertexEdgeEq> requiredEdges;
    requiredEdges.reserve(vertexCount * 2u);

    std::uint32_t ringStart = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec2& point : perimeter) {
        AddMeshVertex(mesh, layer, point);
    }
    AddRingEdges(requiredEdges, ringStart, perimeter.size());

    for (const std::vector<Vec2>& ring : interiorRings) {
        ringStart = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const Vec2& point : ring) {
            AddMeshVertex(mesh, layer, point);
        }
        AddRingEdges(requiredEdges, ringStart, ring.size());
    }

    if (mesh.vertices.size() < 3) {
        return mesh;
    }

    std::vector<double> coords;
    coords.reserve(mesh.vertices.size() * 2u);

    for (const MeshVertex& vertex : mesh.vertices) {
        coords.push_back(static_cast<double>(vertex.position.x));
        coords.push_back(static_cast<double>(vertex.position.y));
    }

    delaunator::Delaunator triangulation(coords);

    const float maxEdgeLength = std::max(
        12.0f,
        std::max(perimeterSpacing, std::max(interiorDepthSpacing, interiorPointSpacing)) * 2.75f
    );

    mesh.indices.reserve(triangulation.triangles.size());

    for (std::size_t i = 0; i + 2 < triangulation.triangles.size(); i += 3) {
        const std::uint32_t ia = static_cast<std::uint32_t>(triangulation.triangles[i + 0u]);
        const std::uint32_t ib = static_cast<std::uint32_t>(triangulation.triangles[i + 1u]);
        const std::uint32_t ic = static_cast<std::uint32_t>(triangulation.triangles[i + 2u]);

        if (
            ia >= mesh.vertices.size() ||
            ib >= mesh.vertices.size() ||
            ic >= mesh.vertices.size()
        ) {
            continue;
        }

        const Vec2& a = mesh.vertices[ia].position;
        const Vec2& b = mesh.vertices[ib].position;
        const Vec2& c = mesh.vertices[ic].position;

        const bool hasRequiredEdge = TriangleHasRequiredEdge(requiredEdges, ia, ib, ic);
        const bool keepTriangle =
            hasRequiredEdge ||
            (
                TriangleEdgesAreReasonable(a, b, c, maxEdgeLength) &&
                SegmentMostlyInsideMask(mask, layer.width, layer.height, a, b) &&
                SegmentMostlyInsideMask(mask, layer.width, layer.height, b, c) &&
                SegmentMostlyInsideMask(mask, layer.width, layer.height, c, a) &&
                TriangleMostlyInsideMask(mask, layer.width, layer.height, a, b, c)
            );

        if (!keepTriangle) {
            continue;
        }

        AddTriangleIfNonDegenerate(mesh, ia, ib, ic);
    }

    std::unordered_set<VertexEdge, VertexEdgeHash, VertexEdgeEq> meshEdges;
    meshEdges.reserve(mesh.indices.size());

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t ia = mesh.indices[i + 0u];
        const std::uint32_t ib = mesh.indices[i + 1u];
        const std::uint32_t ic = mesh.indices[i + 2u];

        meshEdges.insert(NormalizeEdge(ia, ib));
        meshEdges.insert(NormalizeEdge(ib, ic));
        meshEdges.insert(NormalizeEdge(ic, ia));
    }

    mesh.edges.reserve(meshEdges.size());
    for (const VertexEdge& edge : meshEdges) {
        mesh.edges.push_back(MeshEdge{edge.a, edge.b});
    }

    return mesh;
}

} // namespace

std::uint8_t LayerAlphaAt(const EditorLayer& layer, int x, int y) {
    if (
        x < 0 ||
        x >= layer.width ||
        y < 0 ||
        y >= layer.height ||
        layer.alpha.empty()
    ) {
        return 0;
    }

    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width) +
        static_cast<std::size_t>(x);

    if (index >= layer.alpha.size()) {
        return 0;
    }

    return layer.alpha[index];
}

void UpdateLayerBoundsFromMesh(EditorLayer& layer) {
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

LayerMesh GenerateLayerMesh(
    const EditorLayer& layer,
    const MeshGeneratorSettings& settings
) {
    if (layer.width <= 0 || layer.height <= 0 || layer.alpha.empty()) {
        throw std::runtime_error("Selected layer has no alpha data.");
    }

    const std::uint8_t alphaThreshold = static_cast<std::uint8_t>(
        std::clamp(settings.alphaThreshold, 0, 255)
    );

    const int meshDetail = std::clamp(settings.meshDetail, 1, 5);
    const float detailScale = 1.75f - static_cast<float>(meshDetail) * 0.25f;
    const float perimeterSpacing = std::max(1.0f, settings.perimeterSpacing * detailScale);
    const float perimeterBuffer = static_cast<float>(std::max(0, settings.perimeterBuffer));
    const float interiorDepthSpacing =
        static_cast<float>(std::max(1, settings.interiorDepthSpacing));
    const float interiorPointSpacing =
        static_cast<float>(std::max(1, settings.interiorPointSpacing));
    const int maxInteriorPoints = std::max(0, settings.maxInteriorPoints);

    const std::vector<std::uint8_t> mask = BuildBufferedAlphaMask(
        layer,
        alphaThreshold,
        perimeterBuffer
    );

    const std::vector<Vec2> perimeter = ExtractPrimaryContourPolygon(
        layer,
        mask,
        perimeterSpacing,
        settings.maxPerimeterPoints
    );

    const std::vector<std::vector<Vec2>> interiorRings = ExtractInteriorContourRings(
        layer,
        mask,
        interiorDepthSpacing,
        interiorPointSpacing,
        maxInteriorPoints
    );

    LayerMesh mesh = BuildDelaunayMesh(
        layer,
        mask,
        perimeter,
        interiorRings,
        perimeterSpacing,
        interiorDepthSpacing,
        interiorPointSpacing
    );

    if (mesh.indices.empty()) {
        throw std::runtime_error("Polygon triangulation produced no triangles.");
    }

    return mesh;
}

bool GenerateMeshForSelectedLayer(EditorState& editor) {
    if (!IsValidLayerIndex(editor, editor.selectedLayer)) {
        return false;
    }

    EditorLayer& layer = editor.document.layers[editor.selectedLayer];
    const LayerHistoryState before = CaptureLayerHistoryState(layer);

    try {
        layer.mesh = GenerateLayerMesh(layer, editor.meshSettings);
        UpdateLayerBoundsFromMesh(layer);

        PushLayerOperation(
            editor,
            editor.selectedLayer,
            before,
            CaptureLayerHistoryState(layer),
            "Generate mesh"
        );

        editor.selectedVertices.clear();
        editor.selectedEdges.clear();
        editor.statusText =
            "Generated mesh: " +
            std::to_string(layer.mesh.vertices.size()) +
            " vertices, " +
            std::to_string(layer.mesh.indices.size() / 3u) +
            " triangles.";
        editor.errorText.clear();
        return true;
    } catch (const std::exception& ex) {
        ApplyLayerHistoryState(layer, before);
        editor.errorText = ex.what();
        editor.statusText = "Mesh generation failed.";
        return false;
    }
}
