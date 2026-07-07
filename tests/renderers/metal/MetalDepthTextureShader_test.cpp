#include <catch2/catch_test_macros.hpp>

#include "threepp/renderers/metal/MetalDepthMaterialUtils.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream input(path);
        REQUIRE(input.is_open());

        std::ostringstream out;
        out << input.rdbuf();
        return out.str();
    }

}// namespace

TEST_CASE("Metal depth material routing separates visual depth from packed readback") {

    auto visualMaterial = threepp::ShaderMaterial::create();
    visualMaterial->fragmentShader = R"(
        uniform sampler2D tDepth;
        void main() {
            float depth = 0.5;
            gl_FragColor.rgb = vec3(depth);
            gl_FragColor.a = 1.0;
        }
    )";

    auto packedMaterial = threepp::ShaderMaterial::create();
    packedMaterial->fragmentShader = R"(
        void main() {
            float r = 0.0;
            float g = 0.0;
            gl_FragColor = vec4(r, g, 0.0, 1.0);
        }
    )";

    auto compactPackedMaterial = threepp::ShaderMaterial::create();
    compactPackedMaterial->fragmentShader = "gl_FragColor=vec4(r,g,0.0,1.0);";

    CHECK_FALSE(threepp::metal::isPackedLinearDepthMaterial(*visualMaterial));
    CHECK(threepp::metal::isPackedLinearDepthMaterial(*packedMaterial));
    CHECK(threepp::metal::isPackedLinearDepthMaterial(*compactPackedMaterial));
}

TEST_CASE("Metal depth texture example remains a no-tDiffuse visual material") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto exampleSource = readFile(projectRoot / "examples/textures/depth_texture.cpp");
    const auto rendererSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalRenderer.mm");

    REQUIRE(exampleSource.find("\"tDepth\"") != std::string::npos);
    REQUIRE(exampleSource.find("\"cameraNear\"") != std::string::npos);
    REQUIRE(exampleSource.find("\"cameraFar\"") != std::string::npos);
    REQUIRE(exampleSource.find("\"tDiffuse\"") == std::string::npos);

    CHECK(rendererSource.find("shaderMaterial->uniforms.count(\"tDiffuse\")") == std::string::npos);
    CHECK(rendererSource.find("metal::isPackedLinearDepthMaterial(*shaderMaterial)") != std::string::npos);
}

TEST_CASE("Metal depth texture uniforms carry render target UV orientation") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto objectHeader = readFile(projectRoot / "src/threepp/renderers/metal/MetalRenderObjects.hpp");
    const auto objectsSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalObjectsRenderer.mm");

    CHECK(objectHeader.find("float flipUv;") != std::string::npos);
    CHECK(objectsSource.find("uniformFloat(depthMaterial->uniforms, \"flipUv\", 0.f)") != std::string::npos);
}
