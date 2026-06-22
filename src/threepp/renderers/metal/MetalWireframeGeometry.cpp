#include "threepp/renderers/metal/MetalWireframeGeometry.hpp"

#include "threepp/core/BufferGeometry.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

using namespace threepp;

namespace {

    unsigned int versionOf(const BufferAttribute* attribute) {

        return attribute ? attribute->version : std::numeric_limits<unsigned int>::max();
    }

    std::uint64_t edgeKey(unsigned int a, unsigned int b) {

        const auto lo = std::min(a, b);
        const auto hi = std::max(a, b);
        return (static_cast<std::uint64_t>(lo) << 32u) | hi;
    }

}// namespace

std::vector<unsigned int> metal::buildWireframeIndices(const BufferGeometry& geometry) {

    std::vector<unsigned int> indices;
    std::unordered_set<std::uint64_t> edges;

    auto addEdge = [&](unsigned int a, unsigned int b) {
        if (edges.insert(edgeKey(a, b)).second) {
            indices.insert(indices.end(), {a, b});
        }
    };

    if (const auto* geometryIndex = geometry.getIndex()) {
        const auto& array = geometryIndex->array();
        indices.reserve(array.size() * 2);
        edges.reserve(array.size());

        for (unsigned int i = 0, l = static_cast<unsigned int>(array.size()); i + 2 < l; i += 3) {
            const auto a = array[i + 0];
            const auto b = array[i + 1];
            const auto c = array[i + 2];

            addEdge(a, b);
            addEdge(b, c);
            addEdge(c, a);
        }
    } else if (const auto* position = geometry.getAttribute<float>("position")) {
        const auto vertexCount = static_cast<unsigned int>(position->count());
        indices.reserve(vertexCount * 2);
        edges.reserve(vertexCount);

        for (unsigned int i = 0; i + 2 < vertexCount; i += 3) {
            const auto a = i + 0;
            const auto b = i + 1;
            const auto c = i + 2;

            addEdge(a, b);
            addEdge(b, c);
            addEdge(c, a);
        }
    }

    return indices;
}

IntBufferAttribute& metal::getOrUpdateWireframeAttribute(BufferGeometry& geometry, WireframeIndexAttribute& cache) {

    const auto* geometryIndex = geometry.getIndex();
    const auto* position = geometry.getAttribute<float>("position");
    const auto hasIndex = geometryIndex != nullptr;
    const auto indexVersion = hasIndex ? versionOf(geometryIndex) : std::numeric_limits<unsigned int>::max();
    const auto positionVersion = hasIndex ? std::numeric_limits<unsigned int>::max() : versionOf(position);
    const auto attributesVersion = geometry.attributesVersion();

    if (!cache.attribute ||
        cache.indexVersion != indexVersion ||
        cache.positionVersion != positionVersion ||
        cache.attributesVersion != attributesVersion) {

        const auto indices = buildWireframeIndices(geometry);
        if (!cache.attribute) {
            cache.attribute = IntBufferAttribute::create(indices, 1);
        } else {
            const auto updated = IntBufferAttribute::create(indices, 1);
            cache.attribute->copy(*updated);
            cache.attribute->needsUpdate();
        }
        cache.indexVersion = indexVersion;
        cache.positionVersion = positionVersion;
        cache.attributesVersion = attributesVersion;
    }

    return *cache.attribute;
}
