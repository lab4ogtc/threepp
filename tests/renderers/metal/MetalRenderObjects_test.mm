#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/materials/PointsMaterial.hpp"
#include "threepp/materials/SpriteMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/Points.hpp"
#include "threepp/objects/Sprite.hpp"
#include "threepp/renderers/metal/MetalMorphTargets.hpp"
#include "threepp/renderers/metal/MetalRenderObjects.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/scenes/Fog.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

using namespace threepp;

namespace {

    class TestRenderer: public Renderer {

    public:
        void render(Scene&, Camera&) override {}

        void setSize(std::pair<int, int>) override {}

        [[nodiscard]] WindowSize size() const override {
            return {1, 1};
        }

        void setClearColor(const Color&, float) override {}

        void clear(bool, bool, bool) override {}

        void setViewport(int, int, int, int) override {}

        void setScissor(int, int, int, int) override {}

        void setScissorTest(bool) override {}

        using Renderer::setRenderTarget;

        void setRenderTarget(RenderTarget*, int, int, int) override {}

        [[nodiscard]] RenderTarget* getRenderTarget() override {
            return nullptr;
        }

        void copyTextureToImage(Texture&) override {}

        void addPreRenderJob(const RenderJob&) override {}

        RendererShadowMap& shadowMap() override {
            return shadowMap_;
        }

        [[nodiscard]] const RendererShadowMap& shadowMap() const override {
            return shadowMap_;
        }

    private:
        RendererShadowMap shadowMap_;
    };

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

TEST_CASE("Metal point uniforms copy alpha test and point sprite map transform") {

    PerspectiveCamera camera;
    auto material = PointsMaterial::create();
    material->alphaTest = 0.5f;
    material->map = Texture::create();
    material->map->offset.set(0.25f, 0.5f);
    material->map->repeat.set(2.f, 3.f);
    material->map->center.set(0.5f, 0.25f);
    material->map->rotation = 0.5f;
    auto points = Points::create(BufferGeometry::create(), material);

    PointUniforms uniforms{};
    computePointUniforms(camera, *points, *material, 100.f, false, 1.f, uniforms);

    const auto& expected = material->map->matrix.elements;
    REQUIRE(uniforms.useMap == 1u);
    REQUIRE(uniforms.useAlphaMap == 0u);
    REQUIRE(uniforms.alphaTest == Catch::Approx(0.5f));
    for (std::size_t column = 0; column < 3; ++column) {
        REQUIRE(uniforms.uvTransform[column * 4 + 0] == Catch::Approx(expected[column * 3 + 0]));
        REQUIRE(uniforms.uvTransform[column * 4 + 1] == Catch::Approx(expected[column * 3 + 1]));
        REQUIRE(uniforms.uvTransform[column * 4 + 2] == Catch::Approx(expected[column * 3 + 2]));
        REQUIRE(uniforms.uvTransform[column * 4 + 3] == Catch::Approx(0.f));
    }
}

TEST_CASE("Metal particle uniforms expose model-view matrix beside MVP") {

    STATIC_REQUIRE(alignof(ParticleUniforms) == 16);
    STATIC_REQUIRE(sizeof(ParticleUniforms) == 144);
    STATIC_REQUIRE(offsetof(ParticleUniforms, mvp) == 0);
    STATIC_REQUIRE(offsetof(ParticleUniforms, modelViewMatrix) == 64);
    STATIC_REQUIRE(offsetof(ParticleUniforms, toneMappingType) == 128);
    STATIC_REQUIRE(offsetof(ParticleUniforms, toneMappingExposure) == 132);
    STATIC_REQUIRE(offsetof(ParticleUniforms, toneMapped) == 136);
    STATIC_REQUIRE(sizeof(ParticleUniforms) % 16 == 0);

    PerspectiveCamera camera;
    camera.updateMatrixWorld(true);

    auto points = Points::create(BufferGeometry::create(), PointsMaterial::create());
    points->updateMatrixWorld(true);

    ParticleUniforms uniforms{};
    computeParticleUniforms(camera, *points, uniforms);

    CHECK(uniforms.modelViewMatrix[0] == Catch::Approx(1.f));
    CHECK(uniforms.modelViewMatrix[5] == Catch::Approx(1.f));
    CHECK(uniforms.modelViewMatrix[10] == Catch::Approx(1.f));
    CHECK(uniforms.modelViewMatrix[15] == Catch::Approx(1.f));
}

TEST_CASE("Metal morph targets select GL-compatible active influences") {

    auto geometry = BufferGeometry::create();
    auto positionMorphs = geometry->getOrCreateMorphAttribute("position");
    auto normalMorphs = geometry->getOrCreateMorphAttribute("normal");
    for (int i = 0; i < 10; ++i) {
        positionMorphs->emplace_back(FloatBufferAttribute::create({static_cast<float>(i), 0.f, 0.f}, 3));
        normalMorphs->emplace_back(FloatBufferAttribute::create({0.f, static_cast<float>(i), 0.f}, 3));
    }

    auto material = MeshBasicMaterial::create();
    material->morphTargets = true;
    material->morphNormals = true;
    auto mesh = Mesh::create(geometry, material);
    mesh->morphTargetInfluences() = {0.1f, 0.9f, 0.3f, 0.8f, 0.2f, 0.7f, 0.4f, 0.6f, 0.5f, 0.05f};

    metal::MetalMorphTargets morphTargets;
    morphTargets.update(mesh.get(), geometry.get(), material.get());

    CHECK(geometry->getAttribute("morphTarget0") == positionMorphs->at(1).get());
    CHECK(geometry->getAttribute("morphTarget1") == positionMorphs->at(3).get());
    CHECK_FALSE(geometry->hasAttribute("morphTarget4"));
    CHECK(geometry->getAttribute("morphNormal0") == normalMorphs->at(1).get());
    CHECK(geometry->getAttribute("morphNormal3") == normalMorphs->at(7).get());
    CHECK_FALSE(geometry->hasAttribute("morphNormal4"));

    CHECK(morphTargets.morphTargetBaseInfluence() == Catch::Approx(-2.f));
    const auto& influences = morphTargets.morphTargetInfluences();
    REQUIRE(influences.size() == 8);
    CHECK(influences[0] == Catch::Approx(0.9f));
    CHECK(influences[1] == Catch::Approx(0.8f));
    CHECK(influences[3] == Catch::Approx(0.6f));
    CHECK(influences[4] == Catch::Approx(0.f));

    morphTargets.update(mesh.get(), geometry.get(), material.get(), false);
    CHECK(geometry->getAttribute("morphTarget7") == positionMorphs->at(8).get());
    CHECK(morphTargets.morphTargetBaseInfluence() == Catch::Approx(-3.4f));
    CHECK(morphTargets.morphTargetInfluences()[7] == Catch::Approx(0.5f));

    morphTargets.removeGeometry(geometry->id);
    CHECK_FALSE(morphTargets.influencesList.contains(geometry->id));
}

TEST_CASE("Metal shading params expose 16-byte aligned texture flag fields") {

    STATIC_REQUIRE(alignof(ShadingParams) == 16);
    STATIC_REQUIRE(sizeof(ShadingParams) == 320);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags0) == 48);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags1) == 64);
    STATIC_REQUIRE(offsetof(ShadingParams, cameraPosition) == 80);
    STATIC_REQUIRE(offsetof(ShadingParams, specularColor) == 112);
    STATIC_REQUIRE(offsetof(ShadingParams, fogColor) == 128);
    STATIC_REQUIRE(offsetof(ShadingParams, fogParams) == 144);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags2) == 160);
    STATIC_REQUIRE(offsetof(ShadingParams, clippingPlanes) == 176);
    STATIC_REQUIRE(offsetof(ShadingParams, numClippingPlanes) == 304);
    STATIC_REQUIRE(offsetof(ShadingParams, numUnionClippingPlanes) == 308);
    STATIC_REQUIRE(offsetof(ShadingParams, clipIntersection) == 312);
    STATIC_REQUIRE(offsetof(ShadingParams, pad) == 316);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags0) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags1) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, cameraPosition) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, specularColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, fogColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, fogParams) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, textureFlags2) % 16 == 0);
    STATIC_REQUIRE(offsetof(ShadingParams, clippingPlanes) % 16 == 0);
}

TEST_CASE("Metal depth transform uniforms expose model-view matrix for clipping") {

    STATIC_REQUIRE(alignof(DepthTransformUniforms) == 16);
    STATIC_REQUIRE(sizeof(DepthTransformUniforms) == 320);
    STATIC_REQUIRE(offsetof(DepthTransformUniforms, shadowMatrix) == 0);
    STATIC_REQUIRE(offsetof(DepthTransformUniforms, modelViewMatrix) == 64);
    STATIC_REQUIRE(offsetof(DepthTransformUniforms, bindMatrix) == 128);
    STATIC_REQUIRE(offsetof(DepthTransformUniforms, bindMatrixInverse) == 192);
    STATIC_REQUIRE(offsetof(DepthTransformUniforms, morphTargetBaseInfluence) == 256);
    STATIC_REQUIRE(offsetof(DepthTransformUniforms, morphTargetInfluences) == 260);
    STATIC_REQUIRE(sizeof(DepthTransformUniforms) % 16 == 0);

    STATIC_REQUIRE(alignof(PointDepthTransformUniforms) == 16);
    STATIC_REQUIRE(sizeof(PointDepthTransformUniforms) == 416);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, shadowMatrix) == 0);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, modelMatrix) == 64);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, modelViewMatrix) == 128);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, bindMatrix) == 192);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, bindMatrixInverse) == 256);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, lightPosition) == 320);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, params) == 336);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, morphTargetBaseInfluence) == 352);
    STATIC_REQUIRE(offsetof(PointDepthTransformUniforms, morphTargetInfluences) == 356);
    STATIC_REQUIRE(sizeof(PointDepthTransformUniforms) % 16 == 0);
}

TEST_CASE("Metal shading params project global and local clipping planes into camera space") {

    TestRenderer renderer;
    renderer.clippingPlanes.emplace_back(Vector3(0, 0, 1), -3.f);
    renderer.localClippingEnabled = true;

    Scene scene;
    PerspectiveCamera camera;
    camera.position.z = 5.f;
    camera.updateMatrixWorld(true);

    auto material = MeshBasicMaterial::create();
    material->clippingPlanes.emplace_back(Vector3(0, 1, 0), -0.25f);
    material->clipIntersection = true;

    const auto params = extractShadingParams(renderer, scene, *material, camera, false);

    REQUIRE(params.numClippingPlanes == 2u);
    REQUIRE(params.numUnionClippingPlanes == 1u);
    REQUIRE(params.clipIntersection == 1u);
    REQUIRE(params.clippingPlanes[0][0] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[0][1] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[0][2] == Catch::Approx(1.f));
    REQUIRE(params.clippingPlanes[0][3] == Catch::Approx(2.f));
    REQUIRE(params.clippingPlanes[1][0] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[1][1] == Catch::Approx(1.f));
    REQUIRE(params.clippingPlanes[1][2] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[1][3] == Catch::Approx(-0.25f));
}

TEST_CASE("Metal shading params ignore local clipping when renderer local clipping is disabled") {

    TestRenderer renderer;
    renderer.clippingPlanes.emplace_back(Vector3(1, 0, 0), 0.5f);
    renderer.localClippingEnabled = false;

    Scene scene;
    PerspectiveCamera camera;
    auto material = MeshBasicMaterial::create();
    material->clippingPlanes.emplace_back(Vector3(0, 1, 0), 0.25f);

    const auto params = extractShadingParams(renderer, scene, *material, camera, false);

    REQUIRE(params.numClippingPlanes == 1u);
    REQUIRE(params.numUnionClippingPlanes == 1u);
    REQUIRE(params.clippingPlanes[0][0] == Catch::Approx(1.f));
    REQUIRE(params.clippingPlanes[0][1] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[0][2] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[0][3] == Catch::Approx(0.5f));
}

TEST_CASE("Metal shadow shading params follow GL clipping scope") {

    TestRenderer renderer;
    renderer.clippingPlanes.emplace_back(Vector3(1, 0, 0), 0.5f);
    renderer.localClippingEnabled = true;

    Scene scene;
    PerspectiveCamera shadowCamera;
    auto material = MeshBasicMaterial::create();
    material->clippingPlanes.emplace_back(Vector3(0, 1, 0), -0.25f);

    ClippingExtractionOptions shadowClipping;
    shadowClipping.includeGlobal = false;
    shadowClipping.includeLocal = material->clipShadows;

    auto params = extractShadingParams(renderer, scene, *material, shadowCamera, false, shadowClipping);
    REQUIRE(params.numClippingPlanes == 0u);

    material->clipShadows = true;
    shadowClipping.includeLocal = material->clipShadows;
    params = extractShadingParams(renderer, scene, *material, shadowCamera, false, shadowClipping);

    REQUIRE(params.numClippingPlanes == 1u);
    REQUIRE(params.numUnionClippingPlanes == 1u);
    REQUIRE(params.clippingPlanes[0][0] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[0][1] == Catch::Approx(1.f));
    REQUIRE(params.clippingPlanes[0][2] == Catch::Approx(0.f));
    REQUIRE(params.clippingPlanes[0][3] == Catch::Approx(-0.25f));
}

TEST_CASE("Metal point uniforms expose 16-byte aligned sprite and fog fields") {

    STATIC_REQUIRE(alignof(PointUniforms) == 16);
    STATIC_REQUIRE(sizeof(PointUniforms) == 272);
    STATIC_REQUIRE(offsetof(PointUniforms, alphaTest) == 112);
    STATIC_REQUIRE(offsetof(PointUniforms, uvTransform) == 128);
    STATIC_REQUIRE(offsetof(PointUniforms, fogColor) == 176);
    STATIC_REQUIRE(offsetof(PointUniforms, fogParams) == 192);
    STATIC_REQUIRE(offsetof(PointUniforms, morphTargetBaseInfluence) == 208);
    STATIC_REQUIRE(offsetof(PointUniforms, morphTargetInfluences) == 212);
    STATIC_REQUIRE(offsetof(PointUniforms, uvTransform) % 16 == 0);
    STATIC_REQUIRE(offsetof(PointUniforms, fogColor) % 16 == 0);
    STATIC_REQUIRE(offsetof(PointUniforms, fogParams) % 16 == 0);
    STATIC_REQUIRE(sizeof(PointUniforms) % 16 == 0);

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

TEST_CASE("Metal shadow pass accepts the same object classes as GL") {

    auto mesh = Mesh::create(BufferGeometry::create(), MeshBasicMaterial::create());
    auto line = Line::create(BufferGeometry::create(), LineBasicMaterial::create());
    auto points = Points::create(BufferGeometry::create(), PointsMaterial::create());
    auto sprite = Sprite::create(SpriteMaterial::create());

    CHECK(isShadowMapRenderable(*mesh));
    CHECK(isShadowMapRenderable(*line));
    CHECK(isShadowMapRenderable(*points));
    CHECK_FALSE(isShadowMapRenderable(*sprite));
}

TEST_CASE("Metal transform uniforms expose morph fields at 16-byte aligned size") {

    STATIC_REQUIRE(alignof(TransformUniforms) == 16);
    STATIC_REQUIRE(sizeof(TransformUniforms) == 448);
    STATIC_REQUIRE(offsetof(TransformUniforms, morphTargetBaseInfluence) == 384);
    STATIC_REQUIRE(offsetof(TransformUniforms, morphTargetInfluences) == 388);
    STATIC_REQUIRE(sizeof(TransformUniforms) % 16 == 0);
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

TEST_CASE("Metal points shaders sample point sprite maps and alpha test") {

    const std::string_view vertexSource{metal::points_vertex};
    const std::string_view fragmentSource{metal::points_fragment};

    REQUIRE(contains(vertexSource, "float alphaTest;"));
    REQUIRE(contains(vertexSource, "float3x3 uvTransform;"));
    REQUIRE(contains(fragmentSource, "pointCoord [[point_coord]]"));
    REQUIRE(contains(fragmentSource, "pointCoord.x, 1.0 - pointCoord.y"));
    REQUIRE(contains(fragmentSource, "map.sample(mapSampler, pointUv)"));
    REQUIRE(contains(fragmentSource, "alphaMap.sample(alphaMapSampler, pointUv).g"));
    REQUIRE(contains(fragmentSource, "if (color.a < uniforms.alphaTest)"));
    REQUIRE(contains(fragmentSource, "discard_fragment()"));
}

TEST_CASE("Metal mesh shader computes flat shaded normals per fragment") {

    const std::string_view fragmentSource{metal::basic_fragment};

    REQUIRE(contains(fragmentSource, "USE_FLAT_SHADING"));
    REQUIRE(contains(fragmentSource, "float3 flatShadedNormal(float3 worldPosition)"));
    REQUIRE(contains(fragmentSource, "dfdx(worldPosition)"));
    REQUIRE(contains(fragmentSource, "dfdy(worldPosition)"));
    REQUIRE(contains(fragmentSource, "normalize(cross(positionDy, positionDx))"));
    REQUIRE(contains(fragmentSource, "flatShadedNormal(in.worldPosition)"));
}

TEST_CASE("Metal mesh and shadow shaders expose clipping discard paths") {

    const std::string_view vertexSource{metal::basic_vertex};
    const std::string_view fragmentSource{metal::basic_fragment};
    const std::string_view depthVertexSource{metal::depth_vertex};
    const std::string_view depthFragmentSource{metal::depth_fragment};
    const std::string_view pointDepthVertexSource{metal::point_depth_vertex};
    const std::string_view pointDepthFragmentSource{metal::point_depth_fragment};

    REQUIRE(contains(vertexSource, "#if USE_CLIPPING"));
    REQUIRE(contains(vertexSource, "float3 viewPosition;"));
    REQUIRE(contains(vertexSource, "out.viewPosition = -(transforms.modelViewMatrix * localPosition).xyz;"));

    REQUIRE(contains(fragmentSource, "float4 clippingPlanes[8];"));
    REQUIRE(contains(fragmentSource, "uint numClippingPlanes;"));
    REQUIRE(contains(fragmentSource, "uint numUnionClippingPlanes;"));
    REQUIRE(contains(fragmentSource, "uint clipIntersection;"));
    REQUIRE(contains(fragmentSource, "applyClipping(in.viewPosition, params)"));

    REQUIRE(contains(depthVertexSource, "struct DepthVertexOutput"));
    REQUIRE(contains(depthVertexSource, "out.viewPosition = -(transforms.modelViewMatrix * localPosition).xyz;"));
    REQUIRE(contains(depthFragmentSource, "fragment void depth_fragment"));
    REQUIRE(contains(depthFragmentSource, "applyClipping(in.viewPosition, params)"));

    REQUIRE(contains(pointDepthVertexSource, "float3 viewPosition;"));
    REQUIRE(contains(pointDepthVertexSource, "out.viewPosition = -(transforms.modelViewMatrix * localPosition).xyz;"));
    REQUIRE(contains(pointDepthFragmentSource, "constant ShadingParams& params [[buffer(0)]]"));
    REQUIRE(contains(pointDepthFragmentSource, "applyClipping(in.viewPosition, params)"));
}

TEST_CASE("Metal shaders expose morph target attributes and uniforms") {

    const std::string_view vertexSource{metal::basic_vertex};
    const std::string_view pointsVertexSource{metal::points_vertex};
    const std::string_view depthVertexSource{metal::depth_vertex};
    const std::string_view pointDepthVertexSource{metal::point_depth_vertex};

    REQUIRE(contains(vertexSource, "#if USE_MORPHTARGETS"));
    REQUIRE(contains(vertexSource, "float3 morphTarget0 [[attribute(7)]];"));
    REQUIRE(contains(vertexSource, "float3 morphNormal0 [[attribute(11)]];"));
    REQUIRE(contains(vertexSource, "float morphTargetBaseInfluence;"));
    REQUIRE(contains(vertexSource, "morphTarget4 * transforms.morphTargetInfluences[4]"));
    REQUIRE(contains(vertexSource, "morphNormal3 * transforms.morphTargetInfluences[3]"));

    REQUIRE(contains(pointsVertexSource, "float3 morphTarget0 [[attribute(7)]];"));
    REQUIRE(contains(pointsVertexSource, "morphTarget7 * uniforms.morphTargetInfluences[7]"));

    REQUIRE(contains(depthVertexSource, "float3 morphTarget0 [[attribute(7)]];"));
    REQUIRE(contains(depthVertexSource, "morphTarget7 * transforms.morphTargetInfluences[7]"));

    REQUIRE(contains(pointDepthVertexSource, "float3 morphTarget0 [[attribute(7)]];"));
    REQUIRE(contains(pointDepthVertexSource, "morphTarget7 * transforms.morphTargetInfluences[7]"));
}
