#include "threepp/renderers/vulkan/VulkanReadback.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("Vulkan readback packs BGRA source bytes into RGB output") {
    const unsigned char bgra[] = {
            1, 2, 3, 4,
            10, 20, 30, 40,
    };

    std::vector<unsigned char> rgb;
    threepp::vulkan::readback::packColorBytes(bgra, 2, 3, true, rgb);

    CHECK(rgb == std::vector<unsigned char>{3, 2, 1, 30, 20, 10});
}

TEST_CASE("Vulkan readback preserves RGBA source order and requested channel count") {
    const unsigned char rgba[] = {
            5, 6, 7, 8,
            50, 60, 70, 80,
    };

    std::vector<unsigned char> rg;
    threepp::vulkan::readback::packColorBytes(rgba, 2, 2, false, rg);

    CHECK(rg == std::vector<unsigned char>{5, 6, 50, 60});
}
