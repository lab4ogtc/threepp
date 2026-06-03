#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/materials/PointsMaterial.hpp"
#include "threepp/materials/SpriteMaterial.hpp"
#include "threepp/objects/Points.hpp"
#include "threepp/objects/Sprite.hpp"
#include "threepp/renderers/metal/MetalRenderObjects.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/scenes/Fog.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>

using namespace threepp;

namespace {

    bool contains(std::string_view source, std::string_view token) {
        return source.find(token) != std::string_view::npos;
    }

}// namespace

TEST_CASE("Metal point uniforms scale point size by renderer pixel ratio") {

    PerspectiveCamera camera;
    auto material = PointsMaterial::create();
    material->size = 7.f;
    auto points = Points::create(BufferGeometry::create(), material);

    PointUniforms uniforms{};
    computePointUniforms(camera, *points, *material, 100.f, true, 2.5f, uniforms);

    REQUIRE(uniforms.pointSize == Catch::Approx(17.5f));
    REQUIRE(uniforms.scale == Catch::Approx(100.f));
    REQUIRE(uniforms.sizeAttenuation == 1u);
}

TEST_CASE("Metal shading params expose 16-byte aligned texture flag fields") {

    STATIC_REQUIRE(alignof(ShadingParams) == 16);
    STATIC_REQUIRE(sizeof(ShadingParams) == 176);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags0) == 48);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags1) == 64);
    STATIC_REQUIRE(offsetof(ShadingParams, cameraPosition) == 80);
    STATIC_REQUIRE(offsetof(ShadingParams, specularColor) == 112);
    STATIC_REQUIRE(offsetof(ShadingParams, fogColor) == 128);
    STATIC_REQUIRE(offsetof(ShadingParams, fogParams) == 144);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags2) == 160);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags0) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags1) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, cameraPosition) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, specularColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, fogColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, fogParams) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags2) % 16 == 0);
}

TEST_CASE("Metal point uniforms expose 16-byte aligned fog fields") {

    STATIC_REQUIRE(alignof(PointUniforms) == 16);
    STATIC_REQUIRE(sizeof(PointUniforms) == 144);
    STATIC_REQUIRE(offsetof(PointUniforms, fogColor) == 112);
    STATIC_REQUIRE(offsetof(PointUniforms, fogParams) == 128);
    STATIC_REQUIRE(offsetof(PointUniforms, fogColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(PointUniforms, fogParams) % 16 == 0);

    Scene scene;
    scene.fog = Fog(Color(0x336699), 5.f, 50.f);
    auto material = PointsMaterial::create();
    PointUniforms uniforms{};

    fillFogUniforms(scene, *material, uniforms);

    const Color fogColor(0x336699);
    REQUIRE(uniforms.fogColor[0] == Catch::Approx(fogColor.r));
    REQUIRE(uniforms.fogColor[1] == Catch::Approx(fogColor.g));
    REQUIRE(uniforms.fogColor[2] == Catch::Approx(fogColor.b));
    REQUIRE(uniforms.fogParams[0] == Catch::Approx(5.f));
    REQUIRE(uniforms.fogParams[1] == Catch::Approx(50.f));
    REQUIRE(uniforms.fogParams[3] == Catch::Approx(1.f));
}

TEST_CASE("Metal sprite uniforms expose 16-byte aligned uv and fog fields") {

    STATIC_REQUIRE(alignof(SpriteUniforms) == 16);
    STATIC_REQUIRE(sizeof(SpriteUniforms) == 336);
    STATIC_REQUIRE(offsetof(SpriteUniforms, alphaTest) == 236);
    STATIC_REQUIRE(offsetof(SpriteUniforms, uvTransform) == 240);
    STATIC_REQUIRE(offsetof(SpriteUniforms, fogColor) == 288);
    STATIC_REQUIRE(offsetof(SpriteUniforms, fogParams) == 304);
    STATIC_REQUIRE(offsetof(SpriteUniforms, uvTransform) % 16 == 0);
    STATIC_REQUIRE(offsetof(SpriteUniforms, fogColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(SpriteUniforms, fogParams) % 16 == 0);
}

TEST_CASE("Metal sprite uniforms copy alpha test and map uv transform") {

    PerspectiveCamera camera;
    auto material = SpriteMaterial::create();
    material->alphaTest = 0.35f;
    material->map = Texture::create();
    material->map->offset.set(0.25f, 0.5f);
    material->map->repeat.set(2.f, 3.f);
    material->map->center.set(0.5f, 0.25f);
    material->map->rotation = 0.5f;
    auto sprite = Sprite::create(material);

    SpriteUniforms uniforms{};
    computeSpriteUniforms(camera, *sprite, *material, uniforms);

    const auto& expected = material->map->matrix.elements;
    REQUIRE(uniforms.alphaTest == Catch::Approx(0.35f));
    for (std::size_t column = 0; column < 3; ++column) {
        REQUIRE(uniforms.uvTransform[column * 4 + 0] == Catch::Approx(expected[column * 3 + 0]));
        REQUIRE(uniforms.uvTransform[column * 4 + 1] == Catch::Approx(expected[column * 3 + 1]));
        REQUIRE(uniforms.uvTransform[column * 4 + 2] == Catch::Approx(expected[column * 3 + 2]));
        REQUIRE(uniforms.uvTransform[column * 4 + 3] == Catch::Approx(0.f));
    }
}

TEST_CASE("Metal sprite uniforms use alpha map transform when no color map exists") {

    PerspectiveCamera camera;
    auto material = SpriteMaterial::create();
    material->alphaMap = Texture::create();
    material->alphaMap->offset.set(0.125f, 0.375f);
    material->alphaMap->repeat.set(4.f, 2.f);
    auto sprite = Sprite::create(material);

    SpriteUniforms uniforms{};
    computeSpriteUniforms(camera, *sprite, *material, uniforms);

    const auto& expected = material->alphaMap->matrix.elements;
    for (std::size_t column = 0; column < 3; ++column) {
        REQUIRE(uniforms.uvTransform[column * 4 + 0] == Catch::Approx(expected[column * 3 + 0]));
        REQUIRE(uniforms.uvTransform[column * 4 + 1] == Catch::Approx(expected[column * 3 + 1]));
        REQUIRE(uniforms.uvTransform[column * 4 + 2] == Catch::Approx(expected[column * 3 + 2]));
    }
}

TEST_CASE("Metal points shaders carry fog depth and apply fog") {

    const std::string_view vertexSource{metal::points_vertex};
    const std::string_view fragmentSource{metal::points_fragment};

    REQUIRE(contains(vertexSource, "float4 fogColor;"));
    REQUIRE(contains(vertexSource, "float4 fogParams;"));
    REQUIRE(contains(vertexSource, "float fogDepth;"));
    REQUIRE(contains(vertexSource, "out.fogDepth = projected.w;"));
    REQUIRE(contains(vertexSource, "out.pointSize = max(out.pointSize, 1.0);"));
    REQUIRE(contains(fragmentSource, "float fogDepth;"));
    REQUIRE(contains(fragmentSource, "applyFog(color.rgb, in.fogDepth"));
}
