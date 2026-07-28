#include "threepp/geometries/ConvexGeometry.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace threepp;

TEST_CASE("ConvexGeometry provides one normal per vertex") {
    const auto geometry = ConvexGeometry::create({
            {0, 0, 0},
            {1, 0, 0},
            {0, 1, 0},
            {0, 0, 1},
    });

    REQUIRE(geometry->getAttribute<float>("normal")->count() ==
            geometry->getAttribute<float>("position")->count());
}
