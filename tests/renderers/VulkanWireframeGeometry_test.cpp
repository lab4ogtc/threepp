#include <catch2/catch_test_macros.hpp>

#include "threepp/core/BufferGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/renderers/vulkan/VulkanWireframeGeometry.hpp"

using namespace threepp;
using namespace threepp::vulkan;

TEST_CASE("Vulkan wireframe indices deduplicate shared indexed triangle edges") {
    const auto geometry = PlaneGeometry::create(1, 1, 1, 1);

    const auto indices = buildWireframeIndices(*geometry);

    CHECK(indices == std::vector<unsigned int>{
                             0, 2, 2, 1, 1, 0,
                             2, 3, 3, 1});
}

TEST_CASE("Vulkan wireframe indices expand non-indexed triangle triplets") {
    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(
                                               std::vector<float>{
                                                       0, 0, 0,
                                                       1, 0, 0,
                                                       0, 1, 0},
                                               3));

    const auto indices = buildWireframeIndices(*geometry);

    CHECK(indices == std::vector<unsigned int>{0, 1, 1, 2, 2, 0});
}
