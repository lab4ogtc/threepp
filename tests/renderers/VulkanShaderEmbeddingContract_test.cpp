#include <catch2/catch_test_macros.hpp>

#include "threepp/renderers/vulkan/shaders/deferred_shade.comp.pure_raster.spv.h"

#include <filesystem>
#include <fstream>
#include <iterator>
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

    bool isCheckedSource(const std::filesystem::path& path) {
        const auto ext = path.extension().string();
        return ext == ".cpp" || ext == ".hpp" || ext == ".h";
    }

}// namespace

TEST_CASE("Vulkan pure raster deferred shader is embedded", "[vulkan][shader]") {
    CHECK(std::size(kDeferredShadeCompPureRasterSpv) > 0);
}

TEST_CASE("Vulkan static shader compilation emits target-local headers", "[vulkan][shader]") {
    const auto root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const auto cmake = readText(root / "cmake" / "CompileVulkanShaders.cmake");

    CHECK(cmake.find("set(_target_gen_include_root \"${CMAKE_BINARY_DIR}/generated/${target}\")") != std::string::npos);
    CHECK(cmake.find("set(_gen_dir \"${_target_gen_include_root}/threepp/renderers/vulkan/shaders\")") != std::string::npos);
    CHECK(cmake.find("--vn \"${var_name}\"") != std::string::npos);
    CHECK(cmake.find("-o   \"${_out_header}\"") != std::string::npos);
}

TEST_CASE("Vulkan shader modules only consume embedded SPIR-V headers", "[vulkan][shader]") {
    const auto root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const std::vector<std::filesystem::path> roots = {
            root / "src" / "threepp" / "renderers" / "vulkan",
            root / "src" / "threepp" / "renderers" / "VulkanRenderer.cpp",
            root / "examples" / "vulkan",
    };

    for (const auto& entry : roots) {
        if (std::filesystem::is_regular_file(entry)) {
            const auto text = readText(entry);
            for (std::size_t pos = text.find(".spv\""); pos != std::string::npos; pos = text.find(".spv\"", pos + 4)) {
                INFO(entry.string());
                CHECK(text.compare(pos, 7, ".spv.h\"") == 0);
            }
            continue;
        }

        for (const auto& file : std::filesystem::recursive_directory_iterator(entry)) {
            if (!file.is_regular_file() || !isCheckedSource(file.path())) continue;

            const auto text = readText(file.path());
            for (std::size_t pos = text.find(".spv\""); pos != std::string::npos; pos = text.find(".spv\"", pos + 4)) {
                INFO(file.path().string());
                CHECK(text.compare(pos, 7, ".spv.h\"") == 0);
            }
        }
    }
}

TEST_CASE("Vulkan engine and example shader declarations stay on their owning targets", "[vulkan][shader]") {
    const auto root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const auto engineCmake = readText(root / "src" / "CMakeLists.txt");
    const auto examplesCmake = readText(root / "examples" / "vulkan" / "CMakeLists.txt");

    CHECK(engineCmake.find("compile_vulkan_shader(threepp") != std::string::npos);
    CHECK(examplesCmake.find("compile_vulkan_shader(threepp") == std::string::npos);
    CHECK(examplesCmake.find("compile_vulkan_shader(vulkan_") != std::string::npos);
}

TEST_CASE("Vulkan TLAS full build and refit share one material mask rule", "[vulkan][raytracing]") {
    const auto root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const auto source = readText(root / "src" / "threepp" / "renderers" / "VulkanRenderer.cpp");
    const std::string sharedCall = "rayMaskForMaterialDesc(";
    std::size_t count = 0;
    for (auto pos = source.find(sharedCall); pos != std::string::npos;
         pos = source.find(sharedCall, pos + sharedCall.size())) {
        ++count;
    }

    CHECK(count == 3u);// 一处定义，加 full build/refit 各一处调用
    CHECK(source.find("materialUsesAlphaRayMask") == std::string::npos);
}
