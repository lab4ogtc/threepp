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

    std::string metalStringSource(const std::string& source, const std::string& name) {
        const auto begin = source.find("constexpr auto " + name);
        REQUIRE(begin != std::string::npos);

        const auto end = source.find(")metal\";", begin);
        REQUIRE(end != std::string::npos);

        return source.substr(begin, end - begin);
    }

}// namespace

TEST_CASE("Metal reflector and water shaders rely on textureMatrix for render-target Y orientation") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto reflectorFragment = reflectorFragmentSource(shaderSource);
    const auto waterFragment = metalStringSource(shaderSource, "water_fragment");

    REQUIRE(reflectorFragment.find("texture2d<float> tDiffuse") != std::string::npos);
    REQUIRE(reflectorFragment.find("tDiffuse.sample") != std::string::npos);
    CHECK(reflectorFragment.find("uv.y = 1.0 - uv.y") == std::string::npos);
    REQUIRE(waterFragment.find("texture2d<float> mirrorSampler") != std::string::npos);
    REQUIRE(waterFragment.find("mirrorSampler.sample") != std::string::npos);
    CHECK(waterFragment.find("mirrorUv.y = 1.0 - mirrorUv.y") == std::string::npos);
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
    CHECK(basicFragment.find("reflectedIndirectDiffuse += lights.ambientColor.rgb * albedo * (1.0 - metalness) * diffuseIrradianceScale;") != std::string::npos);
    CHECK(basicFragment.find("reflectedIndirectDiffuse *= ao;") != std::string::npos);
    CHECK(basicFragment.find("reflectedIndirectSpecular *= computeSpecularOcclusion(dotNV, ao, roughness);") != std::string::npos);
    CHECK(basicFragment.find("color = reflectedDirectDiffuse + reflectedIndirectDiffuse + reflectedDirectSpecular + reflectedIndirectSpecular;") != std::string::npos);
}

TEST_CASE("Metal Phong and Lambert diffuse terms match GL Lambert BRDF scaling") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto basicFragment = basicFragmentSource(shaderSource);

    CHECK(basicFragment.find("float diffuseIrradianceScale = params.useLegacyLights != 0 ? 1.0 : (1.0 / PI);") != std::string::npos);
    CHECK(basicFragment.find("float3 directLambert(float3 radiance, float3 n, float3 l, float3 albedo, bool useLegacyLights)") != std::string::npos);
    CHECK(basicFragment.find("float3 directBlinnPhong(float3 radiance, float3 n, float3 v, float3 l, float3 albedo, float3 specularColor, float shininess, float specularMapStrength, bool useLegacyLights)") != std::string::npos);
    CHECK(basicFragment.find("return irradiance * albedo * (1.0 / PI);") != std::string::npos);
    CHECK(basicFragment.find("float3 diffuse = irradiance * albedo * (1.0 / PI);") != std::string::npos);
    CHECK(basicFragment.find("float specularStrength = 0.25 * (1.0 / PI) * (shininess * 0.5 + 1.0)") != std::string::npos);
    CHECK(basicFragment.find("directBlinnPhong(light.color.rgb, n, v, l, albedo, params.specularColor.rgb, params.specularColor.a, specularStrength, params.useLegacyLights != 0)") != std::string::npos);
    CHECK(basicFragment.find("directLambert(light.color.rgb, n, l, albedo, params.useLegacyLights != 0)") != std::string::npos);
    CHECK(basicFragment.find("reflectedIndirectDiffuse += shColor * (1.0 - metalness) * (1.0 / PI);") != std::string::npos);
    CHECK(basicFragment.find("color += shColor * (1.0 / PI);") != std::string::npos);
}

TEST_CASE("Metal basic fog is mixed in GL output color space") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto basicFragment = basicFragmentSource(shaderSource);

    CHECK(basicFragment.find("uint outputColorSpaceSRGB;") != std::string::npos);
    CHECK(basicFragment.find("float3 outputToLinearColor(float3 value, uint outputDecodeSRGB)") != std::string::npos);
    CHECK(basicFragment.find("float3 applyOutputSpaceFog(float3 linearColor, float fogDepth, constant ShadingParams& params)") != std::string::npos);
    CHECK(basicFragment.find("float3 outputColor = linearToOutputColor(linearColor, params.outputColorSpaceSRGB);") != std::string::npos);
    CHECK(basicFragment.find("outputColor = applyFog(outputColor, fogDepth, params.fogColor, params.fogParams);") != std::string::npos);
    CHECK(basicFragment.find("return outputToLinearColor(outputColor, params.outputColorSpaceSRGB != 0 && params.outputEncodeSRGB == 0 ? 1 : 0);") != std::string::npos);
    CHECK(basicFragment.find("color = applyOutputSpaceFog(color, in.fogDepth, params);") != std::string::npos);
    CHECK(basicFragment.find("color = applyFog(color, in.fogDepth, params);\n    color = linearToOutputColor(color, params.outputEncodeSRGB);") == std::string::npos);
}

TEST_CASE("Metal line shader applies scene fog like GL basic lines") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto shaderManagerSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaderManager.mm");
    const auto lineVertex = metalStringSource(shaderSource, "line_vertex");
    const auto lineFragment = metalStringSource(shaderSource, "line_fragment");

    CHECK(shaderManagerSource.find("source += fog_functions;\n            source += line_vertex;") != std::string::npos);
    CHECK(lineVertex.find("float4x4 modelViewMatrix;") != std::string::npos);
    CHECK(lineVertex.find("float4 fogColor;") != std::string::npos);
    CHECK(lineVertex.find("float4 fogParams;") != std::string::npos);
    CHECK(lineVertex.find("uint outputColorSpaceSRGB;") != std::string::npos);
    CHECK(lineVertex.find("float3 outputPadding") == std::string::npos);
    CHECK(lineVertex.find("float outputPadding0;\n    float outputPadding1;\n    float outputPadding2;") != std::string::npos);
    CHECK(lineVertex.find("float fogDepth;") != std::string::npos);
    CHECK(lineVertex.find("float4 modelViewPosition = uniforms.modelViewMatrix * float4(in.position, 1.0);") != std::string::npos);
    CHECK(lineVertex.find("out.fogDepth = -modelViewPosition.z;") != std::string::npos);
    CHECK(lineFragment.find("float3 applyLineOutputSpaceFog(float3 linearColor, float fogDepth, constant LineUniforms& uniforms)") != std::string::npos);
    CHECK(lineFragment.find("float3 outputColor = linearToOutputColor(linearColor, uniforms.outputColorSpaceSRGB);") != std::string::npos);
    CHECK(lineFragment.find("outputColor = applyFog(outputColor, fogDepth, uniforms.fogColor, uniforms.fogParams);") != std::string::npos);
    CHECK(lineFragment.find("return outputToLinearColor(outputColor, uniforms.outputColorSpaceSRGB != 0 && uniforms.outputEncodeSRGB == 0 ? 1 : 0);") != std::string::npos);
    CHECK(lineFragment.find("color.rgb = applyLineOutputSpaceFog(color.rgb, in.fogDepth, uniforms);") != std::string::npos);
}

TEST_CASE("Metal points fog uses GL view-space depth and output-space mixing") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto pointsVertex = metalStringSource(shaderSource, "points_vertex");
    const auto pointsFragment = metalStringSource(shaderSource, "points_fragment");

    CHECK(pointsVertex.find("float4x4 modelViewMatrix;") != std::string::npos);
    CHECK(pointsVertex.find("uint outputColorSpaceSRGB;") != std::string::npos);
    CHECK(pointsVertex.find("float4 modelViewPosition = uniforms.modelViewMatrix * float4(localPosition, 1.0);") != std::string::npos);
    CHECK(pointsVertex.find("out.fogDepth = -modelViewPosition.z;") != std::string::npos);
    CHECK(pointsVertex.find("out.fogDepth = projected.w;") == std::string::npos);
    CHECK(pointsFragment.find("float3 applyPointOutputSpaceFog(float3 linearColor, float fogDepth, constant PointUniforms& uniforms)") != std::string::npos);
    CHECK(pointsFragment.find("float3 outputColor = linearToOutputColor(linearColor, uniforms.outputColorSpaceSRGB);") != std::string::npos);
    CHECK(pointsFragment.find("outputColor = applyFog(outputColor, fogDepth, uniforms.fogColor, uniforms.fogParams);") != std::string::npos);
    CHECK(pointsFragment.find("return outputToLinearColor(outputColor, uniforms.outputColorSpaceSRGB != 0 && uniforms.outputEncodeSRGB == 0 ? 1 : 0);") != std::string::npos);
    CHECK(pointsFragment.find("color.rgb = applyPointOutputSpaceFog(color.rgb, in.fogDepth, uniforms);") != std::string::npos);
}

TEST_CASE("Metal points shader has no-map and texture variants") {

    void* device = MTLCreateSystemDefaultDevice();
    if (device == nullptr) {
        SKIP("default Metal device is unavailable");
    }

    threepp::metal::MetalShaderManager shaderManager(device);

    CHECK_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(true, false, false));
    CHECK_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(true, true, false));
    CHECK_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(true, false, true));
}

TEST_CASE("Metal sprite and water fog are mixed in GL output color space") {

    const auto testFile = std::filesystem::path(__FILE__);
    const auto projectRoot = testFile.parent_path().parent_path().parent_path().parent_path();
    const auto shaderSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaders.hpp");
    const auto shaderManagerSource = readFile(projectRoot / "src/threepp/renderers/metal/MetalShaderManager.mm");
    const auto spriteVertex = metalStringSource(shaderSource, "sprite_vertex");
    const auto spriteFragment = metalStringSource(shaderSource, "sprite_fragment");
    const auto waterFragment = metalStringSource(shaderSource, "water_fragment");

    CHECK(spriteVertex.find("uint outputColorSpaceSRGB;") != std::string::npos);
    CHECK(spriteFragment.find("float3 applySpriteOutputSpaceFog(float3 linearColor, float fogDepth, constant SpriteUniforms& uniforms)") != std::string::npos);
    CHECK(spriteFragment.find("color.rgb = applySpriteOutputSpaceFog(color.rgb, in.fogDepth, uniforms);") != std::string::npos);
    CHECK(spriteFragment.find("color.rgb = applyFog(color.rgb, in.fogDepth, uniforms.fogColor, uniforms.fogParams);\n#endif\n    color.rgb = linearToOutputColor(color.rgb, uniforms.outputEncodeSRGB);") == std::string::npos);

    CHECK(waterFragment.find("uint outputColorSpaceSRGB;") != std::string::npos);
    CHECK(waterFragment.find("float3 toneMapping(float3 color, uint toneMappingType, float exposure)") != std::string::npos);
    CHECK(waterFragment.find("float3 linearToOutputColor(float3 value, uint outputEncodeSRGB)") != std::string::npos);
    CHECK(shaderManagerSource.find("source += fog_functions;\n        source += water_fragment;") != std::string::npos);
    CHECK(waterFragment.find("float3 applyWaterOutputSpaceFog(float3 linearColor, float fogDepth, constant WaterUniforms& uniforms)") != std::string::npos);
    CHECK(waterFragment.find("albedo = applyWaterOutputSpaceFog(albedo, in.fogDepth, uniforms);") != std::string::npos);
    CHECK(waterFragment.find("albedo = applyFog(albedo, in.fogDepth, uniforms);\n    albedo = linearToOutputColor(albedo, uniforms.outputEncodeSRGB);") == std::string::npos);
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
