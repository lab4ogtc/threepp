#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

    std::string readText(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.is_open());
        std::ostringstream out;
        out << input.rdbuf();
        return out.str();
    }

}// namespace

TEST_CASE("Vulkan wireframe meshes use explicit line-list geometry") {
    const auto root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const std::vector<std::filesystem::path> sources = {
            root / "src" / "threepp" / "renderers" / "VulkanRenderer.cpp",
            root / "src" / "threepp" / "renderers" / "vulkan" / "OverlayPass.cpp",
    };

    for (const auto& source : sources) {
        INFO(source.string());
        const auto text = readText(source);
        CHECK(text.find("VK_POLYGON_MODE_LINE") == std::string::npos);
    }

    const auto context = readText(root / "src" / "threepp" / "renderers" / "vulkan" / "VulkanContext.cpp");
    CHECK(context.find("fillModeNonSolid = VK_TRUE") == std::string::npos);
}

TEST_CASE("Vulkan MSAA overlay composite uses premultiplied alpha blending") {
    const auto root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const auto renderer = readText(root / "src" / "threepp" / "renderers" / "VulkanRenderer.cpp");

    const auto composite = renderer.find("void createOverlayCompositePipeline()");
    REQUIRE(composite != std::string::npos);

    const auto srcColor = renderer.find("cbas.srcColorBlendFactor", composite);
    REQUIRE(srcColor != std::string::npos);
    const auto lineEnd = renderer.find('\n', srcColor);
    const auto line = renderer.substr(srcColor, lineEnd - srcColor);

    CHECK(line.find("VK_BLEND_FACTOR_ONE") != std::string::npos);
}
