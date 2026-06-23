#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream input(path);
        REQUIRE(input.is_open());

        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    std::filesystem::path projectRoot() {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    }

}// namespace

TEST_CASE("Metal directional shadow pass refreshes shadow camera projection") {

    const auto source = readFile(projectRoot() / "src/threepp/renderers/metal/MetalShadowRenderer.mm");
    const auto functionStart = source.find("void MetalRenderer::Impl::renderShadowForLight(");
    REQUIRE(functionStart != std::string::npos);

    const auto nextFunction = source.find("void MetalRenderer::Impl::renderPointLightShadow(", functionStart);
    REQUIRE(nextFunction != std::string::npos);

    const auto functionBody = source.substr(functionStart, nextFunction - functionStart);
    const auto projectionUpdate = functionBody.find("shadow.camera->updateProjectionMatrix();");
    const auto matrixUpdate = functionBody.find("shadow.updateMatrices(light);");

    REQUIRE(projectionUpdate != std::string::npos);
    REQUIRE(matrixUpdate != std::string::npos);
    CHECK(projectionUpdate < matrixUpdate);
}
