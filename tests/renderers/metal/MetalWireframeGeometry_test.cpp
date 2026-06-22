#include <catch2/catch_test_macros.hpp>

#include "threepp/core/BufferGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/renderers/metal/MetalWireframeGeometry.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace threepp;
using namespace threepp::metal;

TEST_CASE("Metal wireframe indices deduplicate shared indexed triangle edges") {

    const auto geometry = PlaneGeometry::create(1, 1, 1, 1);

    const auto indices = buildWireframeIndices(*geometry);

    CHECK(indices == std::vector<unsigned int>{
                             0, 2, 2, 1, 1, 0,
                             2, 3, 3, 1});
}

TEST_CASE("Metal wireframe indices expand non-indexed triangle triplets") {

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

TEST_CASE("Metal wireframe cache for indexed geometry ignores position-only updates") {

    const auto geometry = PlaneGeometry::create(1, 1, 1, 1);
    WireframeIndexAttribute cache;

    auto& first = getOrUpdateWireframeAttribute(*geometry, cache);
    const auto* firstPtr = &first;
    const auto firstVersion = first.version;

    geometry->getAttribute<float>("position")->needsUpdate();
    auto& second = getOrUpdateWireframeAttribute(*geometry, cache);

    CHECK(&second == firstPtr);
    CHECK(second.version == firstVersion);
}

TEST_CASE("Metal wireframe indices match deduplicated LUT-sized plane edges") {

    const auto geometry = PlaneGeometry::create(4, 4, 50, 50);

    const auto indices = buildWireframeIndices(*geometry);

    CHECK(indices.size() == 15200);
    REQUIRE(geometry->getIndex() != nullptr);
    CHECK(indices.size() < static_cast<std::size_t>(geometry->getIndex()->count() * 2));
}

TEST_CASE("Metal shadow wireframe path avoids native triangle line fill mode") {

    const auto shadowRendererPath = std::filesystem::path(__FILE__)
                                            .parent_path()
                                            .parent_path()
                                            .parent_path()
                                            .parent_path() /
                                    "src/threepp/renderers/metal/MetalShadowRenderer.mm";
    std::ifstream input(shadowRendererPath);
    REQUIRE(input.is_open());

    std::ostringstream buffer;
    buffer << input.rdbuf();

    CHECK(buffer.str().find("MTLTriangleFillModeLines") == std::string::npos);
}
