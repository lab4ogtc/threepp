#include "threepp/renderers/vulkan/VulkanWireframeGeometry.hpp"

#include "threepp/core/BufferGeometry.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace {

    std::uint64_t edgeKey(unsigned int a, unsigned int b) {
        const auto lo = std::min(a, b);
        const auto hi = std::max(a, b);
        return (static_cast<std::uint64_t>(lo) << 32u) | hi;
    }

}// namespace

std::vector<unsigned int> threepp::vulkan::buildWireframeIndices(const BufferGeometry& geometry) {
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
            addEdge(i, i + 1);
            addEdge(i + 1, i + 2);
            addEdge(i + 2, i);
        }
    }

    return indices;
}
