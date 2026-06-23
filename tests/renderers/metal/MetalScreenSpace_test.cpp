#include <catch2/catch_test_macros.hpp>

#include "threepp/renderers/metal/MetalScreenSpace.hpp"

using namespace threepp::metal;

TEST_CASE("Metal screen-space sprites use logical units on the default framebuffer") {

    const auto layout = computeScreenSpaceSpriteLayout(1600.f, 1200.f, 2.f, false);

    CHECK(layout.logicalWidth == 800.f);
    CHECK(layout.logicalHeight == 600.f);
    CHECK(layout.viewportWidth == 1600.f);
    CHECK(layout.viewportHeight == 1200.f);
}

TEST_CASE("Metal screen-space sprites use render target pixels without window pixel ratio") {

    const auto layout = computeScreenSpaceSpriteLayout(512.f, 256.f, 2.f, true);

    CHECK(layout.logicalWidth == 512.f);
    CHECK(layout.logicalHeight == 256.f);
    CHECK(layout.viewportWidth == 512.f);
    CHECK(layout.viewportHeight == 256.f);
}

TEST_CASE("Metal screen-space sprites preserve logical units with fractional pixel ratio") {

    const auto layout = computeScreenSpaceSpriteLayout(400.f, 300.f, 0.5f, false);

    CHECK(layout.logicalWidth == 800.f);
    CHECK(layout.logicalHeight == 600.f);
    CHECK(layout.viewportWidth == 400.f);
    CHECK(layout.viewportHeight == 300.f);
}
