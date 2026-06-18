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

    std::string reflectorFragmentSource(const std::string& source) {
        const auto begin = source.find("constexpr auto reflector_fragment");
        REQUIRE(begin != std::string::npos);

        const auto end = source.find(")metal\";", begin);
        REQUIRE(end != std::string::npos);

        return source.substr(begin, end - begin);
    }

}// namespace

TEST_CASE("Metal reflector shader relies on textureMatrix for render-target Y orientation") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto reflectorFragment = reflectorFragmentSource(shaderSource);

    REQUIRE(reflectorFragment.find("texture2d<float> tDiffuse") != std::string::npos);
    REQUIRE(reflectorFragment.find("tDiffuse.sample") != std::string::npos);
    CHECK(reflectorFragment.find("uv.y = 1.0 - uv.y") == std::string::npos);
}
