#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/materials/PointsMaterial.hpp"
#include "threepp/objects/Points.hpp"
#include "threepp/renderers/metal/MetalRenderObjects.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/scenes/Fog.hpp"
#include "threepp/scenes/Scene.hpp"

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
