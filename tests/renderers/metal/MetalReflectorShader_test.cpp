#include <catch2/catch_test_macros.hpp>

#include "threepp/renderers/metal/MetalShaderManager.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" void* MTLCreateSystemDefaultDevice(void);

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

    std::string basicFragmentSource(const std::string& source) {
        const auto begin = source.find("constexpr auto basic_fragment");
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

TEST_CASE("Metal basic shader includes RectAreaLight LTC path") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto basicFragment = basicFragmentSource(shaderSource);

    REQUIRE(basicFragment.find("USE_RECT_AREA_LIGHTS") != std::string::npos);
    CHECK(basicFragment.find("RECT_AREA_LIGHT_COUNT") != std::string::npos);
    CHECK(basicFragment.find("texture2d<float> ltc1 [[texture(24)]]") != std::string::npos);
    CHECK(basicFragment.find("texture2d<float> ltc2 [[texture(25)]]") != std::string::npos);
    CHECK(basicFragment.find("sampler ltcSampler [[sampler(4)]]") != std::string::npos);
    CHECK(basicFragment.find("constant RectAreaLightUniform* rectAreaLights [[buffer(2)]]") != std::string::npos);
    CHECK(basicFragment.find("ltcEvaluate") != std::string::npos);
    CHECK(basicFragment.find("lights.rectAreaLights") == std::string::npos);
    CHECK(basicFragment.find("MAX_RECT_AREA_LIGHTS") == std::string::npos);
    CHECK(basicFragment.find("rectAreaViewPosition") != std::string::npos);
    CHECK(basicFragment.find("transforms.viewMatrix") != std::string::npos);
    CHECK(basicFragment.find("params.isOrthographicCamera") != std::string::npos);
    CHECK(basicFragment.find("mix(float3(0.04), albedo, metalness)") == std::string::npos);
    CHECK(basicFragment.find("params.materialType == 0") != std::string::npos);
}

TEST_CASE("Metal RectAreaLight LTC contribution matches GL shader") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto basicFragment = basicFragmentSource(shaderSource);

    CHECK(basicFragment.find("mInv.x * local.x + mInv.z * local.z") != std::string::npos);
    CHECK(basicFragment.find("mInv.y * local.x + mInv.w * local.z") != std::string::npos);
    CHECK(basicFragment.find("mInv.x * local.x + mInv.y * local.z") == std::string::npos);
    CHECK(basicFragment.find("mInv.z * local.x + mInv.w * local.z") == std::string::npos);
    CHECK(basicFragment.find("float3 directDiffuse = light.color.rgb * albedo * (1.0 - metalness) * ltcDiffuse;") != std::string::npos);
    CHECK(basicFragment.find("float3 directSpecular = light.color.rgb * fresnel * ltcSpecular;") != std::string::npos);
    CHECK(basicFragment.find("reflectedDirectDiffuse += directDiffuse;") != std::string::npos);
    CHECK(basicFragment.find("reflectedDirectSpecular += directSpecular;") != std::string::npos);
    CHECK(basicFragment.find("color += directDiffuse + directSpecular;") == std::string::npos);
    CHECK(basicFragment.find("float3 directDiffuse = float3(0.0);") == std::string::npos);
    CHECK(basicFragment.find("float3 directSpecular = float3(0.0);") == std::string::npos);
}

TEST_CASE("Metal PBR light accumulation follows GL reflectedLight composition") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto basicFragment = basicFragmentSource(shaderSource);

    CHECK(basicFragment.find("float3 reflectedDirectDiffuse = float3(0.0);") != std::string::npos);
    CHECK(basicFragment.find("float3 reflectedDirectSpecular = float3(0.0);") != std::string::npos);
    CHECK(basicFragment.find("float3 reflectedIndirectDiffuse = float3(0.0);") != std::string::npos);
    CHECK(basicFragment.find("float3 reflectedIndirectSpecular = float3(0.0);") != std::string::npos);
    CHECK(basicFragment.find("reflectedIndirectDiffuse += lights.ambientColor.rgb * albedo * (1.0 - metalness) * pbrDiffuseIrradianceScale;") != std::string::npos);
    CHECK(basicFragment.find("reflectedIndirectDiffuse *= ao;") != std::string::npos);
    CHECK(basicFragment.find("reflectedIndirectSpecular *= computeSpecularOcclusion(dotNV, ao, roughness);") != std::string::npos);
    CHECK(basicFragment.find("color = reflectedDirectDiffuse + reflectedIndirectDiffuse + reflectedDirectSpecular + reflectedIndirectSpecular;") != std::string::npos);
}

TEST_CASE("Metal light uniforms avoid uint3 padding mismatch") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto basicFragment = basicFragmentSource(shaderSource);

    CHECK(basicFragment.find("uint4 rectAreaParams;") != std::string::npos);
    CHECK(basicFragment.find("uint rectAreaCount;") == std::string::npos);
    CHECK(basicFragment.find("uint3 lightPadding;") == std::string::npos);
    CHECK(basicFragment.find("lights.rectAreaParams.x") != std::string::npos);
}

TEST_CASE("Metal RectAreaLight shader variant compiles") {

    void* device = MTLCreateSystemDefaultDevice();
    if (device == nullptr) {
        SKIP("default Metal device is unavailable");
    }

    threepp::metal::MetalShaderManager shaderManager(device);
    threepp::metal::ShaderProgramKey key;
    key.useLights = true;
    key.useNormal = true;
    key.rectAreaLightCount = 1;

    CHECK_NOTHROW(shaderManager.getOrCreateVertexFunction(key));
    CHECK_NOTHROW(shaderManager.getOrCreateFragmentFunction(key));
}

TEST_CASE("Metal shader key tracks RectAreaLight variants") {

    threepp::metal::ShaderProgramKey withoutRectArea;
    threepp::metal::ShaderProgramKey oneRectArea;
    threepp::metal::ShaderProgramKey fiveRectAreas;
    oneRectArea.rectAreaLightCount = 1;
    fiveRectAreas.rectAreaLightCount = 5;

    CHECK_FALSE(withoutRectArea == oneRectArea);
    CHECK_FALSE(oneRectArea == fiveRectAreas);
    CHECK(threepp::metal::ShaderProgramKeyHash{}(withoutRectArea) !=
          threepp::metal::ShaderProgramKeyHash{}(oneRectArea));
    CHECK(threepp::metal::ShaderProgramKeyHash{}(oneRectArea) !=
          threepp::metal::ShaderProgramKeyHash{}(fiveRectAreas));
}
