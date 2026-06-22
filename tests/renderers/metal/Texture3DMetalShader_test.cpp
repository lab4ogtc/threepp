#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("texture3d Metal support stays in a dedicated example") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto glSource = readFile(projectRoot / "examples/textures/texture3d.cpp");
    const auto metalSource = readFile(projectRoot / "examples/textures/texture3d_metal.cpp");

    CHECK(glSource.find("ShaderLanguage::SLANG") == std::string::npos);
    CHECK(glSource.find("uniformLayout") == std::string::npos);
    CHECK(glSource.find("Texture3D<float>") == std::string::npos);
    CHECK(glSource.find("createRenderer(canvas, GraphicsAPI::OpenGL)") != std::string::npos);

    REQUIRE(metalSource.find("ShaderLanguage::SLANG") != std::string::npos);
    REQUIRE(metalSource.find("shaderSource") != std::string::npos);
    CHECK(metalSource.find("Texture3D<float>") != std::string::npos);
    CHECK(metalSource.find("SamplerState") != std::string::npos);
    CHECK(metalSource.find("register(t0)") != std::string::npos);
    CHECK(metalSource.find("register(s0, space1)") != std::string::npos);
    CHECK(metalSource.find("register(b4)") != std::string::npos);
    CHECK(metalSource.find("register(b11)") != std::string::npos);
    CHECK(metalSource.find(R"(m->uniformLayout = {"base", "map", "params0", "params1"};)") != std::string::npos);
    CHECK(metalSource.find("float4 params0;") != std::string::npos);
    CHECK(metalSource.find("float4 params1;") != std::string::npos);
    CHECK(metalSource.find("mul(sysUniforms.modelViewMatrix, float4(input.position, 1.0))") != std::string::npos);
    CHECK(metalSource.find("mul(sysUniforms.modelMatrixInverse, sysUniforms.cameraPos)") != std::string::npos);
}
