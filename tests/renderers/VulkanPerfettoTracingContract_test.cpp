#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}// namespace

TEST_CASE("Vulkan startup tracing is backed by Perfetto SDK") {
    const auto root = std::filesystem::path{PROJECT_SOURCE_DIR};

    REQUIRE(std::filesystem::exists(root / "examples/external/perfetto/perfetto.h"));
    REQUIRE(std::filesystem::exists(root / "examples/external/perfetto/perfetto.cc"));

    const auto cmake = readFile(root / "src/CMakeLists.txt");
    REQUIRE(cmake.find("THREEPP_WITH_PERFETTO") != std::string::npos);
    REQUIRE(cmake.find("examples/external/perfetto/perfetto.cc") != std::string::npos);

    const auto renderer = readFile(root / "src/threepp/renderers/VulkanRenderer.cpp");
    REQUIRE(renderer.find("#include \"perfetto.h\"") != std::string::npos);
    REQUIRE(renderer.find("TRACE_EVENT") != std::string::npos);
    REQUIRE(renderer.find("THREEPP_VULKAN_STARTUP_TRACE") != std::string::npos);
    REQUIRE(renderer.find("Impl.ensureDeferredShade.createDeferredShade") != std::string::npos);
    REQUIRE(renderer.find("Impl.ensureDeferredShade.rewriteDescriptors") != std::string::npos);
}

TEST_CASE("Vulkan custom ShaderMaterial pipeline cache is checked before compiling") {
    const auto root = std::filesystem::path{PROJECT_SOURCE_DIR};
    const auto renderer = readFile(root / "src/threepp/renderers/VulkanRenderer.cpp");

    const auto function = renderer.find("CustomShaderPipelineRecord& getOrCreateCustomShaderPipeline");
    REQUIRE(function != std::string::npos);

    const auto key = renderer.find("makeVulkanShaderMaterialKey", function);
    const auto cacheLoop = renderer.find("for (auto& record : customShaderPipelines_)", function);
    const auto compiler = renderer.find("ensureCustomShaderCompiler", function);
    REQUIRE(key != std::string::npos);
    REQUIRE(cacheLoop != std::string::npos);
    REQUIRE(compiler != std::string::npos);
    CHECK(key < cacheLoop);
    CHECK(cacheLoop < compiler);
}
