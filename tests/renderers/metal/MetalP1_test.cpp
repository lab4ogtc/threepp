#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/objects/Reflector.hpp"
#include "threepp/objects/Water.hpp"
#include "threepp/renderers/RenderJob.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "threepp/textures/Image.hpp"

#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/metal/MetalBufferManager.hpp"
#include "threepp/renderers/metal/MetalCameraUtils.hpp"
#include "threepp/renderers/metal/MetalPipelineCache.hpp"
#include "threepp/renderers/metal/MetalRenderStateUtils.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <type_traits>

using namespace threepp;

namespace {

    template<class T>
    concept HasMetalViewport = requires(T& renderer, const Vector4& v) {
        renderer.setViewport(v);
        renderer.setViewport(0, 0, 1, 1);
        renderer.setViewport(std::pair<int, int>{0, 0}, std::pair<int, int>{1, 1});
        renderer.setScissor(v);
        renderer.setScissor(0, 0, 1, 1);
        renderer.setScissor(std::pair<int, int>{0, 0}, std::pair<int, int>{1, 1});
        renderer.setScissorTest(true);
    };

    template<class T>
    concept HasBaseViewport = requires(T& renderer) {
        renderer.setViewport(0, 0, 1, 1);
        renderer.setScissor(0, 0, 1, 1);
        renderer.setScissorTest(true);
    };

    template<class T>
    concept HasBasePreRenderQueue = requires(T& renderer, const RenderJob& job) {
        renderer.addPreRenderJob(job);
    };

    template<class T>
    concept HasRendererSize = requires(const T& renderer) {
        { renderer.size() } -> std::same_as<WindowSize>;
    };

    template<class T>
    concept HasMetalShadowMap = requires(T& renderer) {
        renderer.shadowMap().enabled = true;
        renderer.shadowMap().autoUpdate = false;
        renderer.shadowMap().needsUpdate = true;
        renderer.shadowMap().type = ShadowMap::PFCSoft;
    };

    template<class T>
    concept HasMetalExternalFrameHandles = requires(const T& renderer) {
        { renderer.device() } -> std::same_as<void*>;
        { renderer.currentCommandBuffer() } -> std::same_as<void*>;
        { renderer.currentDrawableTexture() } -> std::same_as<void*>;
    };

}// namespace

TEST_CASE("MetalRenderer exposes P1 viewport and scissor API") {

    STATIC_REQUIRE(HasMetalViewport<MetalRenderer>);
}

TEST_CASE("Renderer base exposes backend-independent viewport and scissor API") {

    STATIC_REQUIRE(HasBaseViewport<Renderer>);
}

TEST_CASE("Renderer base exposes backend-independent pre-render job API") {

    STATIC_REQUIRE(HasBasePreRenderQueue<Renderer>);
}

TEST_CASE("RenderTarget factory creates backend-neutral targets") {

    RenderTarget::Options options;
    options.format = Format::RGBA;
    options.depthTexture = DepthTexture::create(Type::Float);

    auto target = RenderTarget::create(16, 8, options);
    REQUIRE(target != nullptr);
    REQUIRE(target->width == 16);
    REQUIRE(target->height == 8);
    REQUIRE(target->texture != nullptr);
    REQUIRE(target->depthTexture != nullptr);

    target->setSize(4, 2);
    REQUIRE(target->width == 4);
    REQUIRE(target->height == 2);
    REQUIRE(target->viewport.z == 4);
    REQUIRE(target->viewport.w == 2);
}

TEST_CASE("Renderer base exposes backend-independent size API") {

    STATIC_REQUIRE(HasRendererSize<Renderer>);
    STATIC_REQUIRE(HasRendererSize<MetalRenderer>);
}

TEST_CASE("Image exposes const pixel data for read-only texture upload") {

    Image image{{std::vector<unsigned char>{1, 2, 3, 4}}, 1, 1};
    const auto& constImage = image;

    const auto& data = constImage.data<unsigned char>();

    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 1);
}

TEST_CASE("Metal P1 cache keys include shader features and vertex layout") {

    metal::ShaderProgramKey textured{};
    textured.useMap = true;

    metal::ShaderProgramKey vertexColored{};
    vertexColored.useVertexColors = true;

    REQUIRE_FALSE(textured == vertexColored);
    REQUIRE(metal::ShaderProgramKeyHash{}(textured) != metal::ShaderProgramKeyHash{}(vertexColored));

    metal::ShaderProgramKey doubleSided{};
    doubleSided.doubleSided = true;

    metal::ShaderProgramKey flipSided{};
    flipSided.flipSided = true;

    REQUIRE_FALSE(doubleSided == flipSided);
    REQUIRE(metal::ShaderProgramKeyHash{}(doubleSided) != metal::ShaderProgramKeyHash{}(flipSided));

    metal::PipelineKey withUv{};
    withUv.vertexFunction = reinterpret_cast<void*>(0x1);
    withUv.fragmentFunction = reinterpret_cast<void*>(0x2);
    withUv.vertexLayoutBitmask = 0b0101;

    metal::PipelineKey withoutUv = withUv;
    withoutUv.vertexLayoutBitmask = 0b0001;

    REQUIRE_FALSE(withUv == withoutUv);
    REQUIRE(metal::PipelineKeyHash{}(withUv) != metal::PipelineKeyHash{}(withoutUv));
}

TEST_CASE("Metal P2 shader keys include skinning and lighting variants") {

    metal::ShaderProgramKey skinned{};
    skinned.useSkinning = true;

    metal::ShaderProgramKey lit{};
    lit.useLights = true;

    REQUIRE_FALSE(skinned == lit);
    REQUIRE(metal::ShaderProgramKeyHash{}(skinned) != metal::ShaderProgramKeyHash{}(lit));
}

TEST_CASE("Metal P4 shader manager exposes dedicated built-in material entry points") {

    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSpriteVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSpriteFragmentFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateLineVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateLineFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointsVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointsFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateRawShaderVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateRawShaderFragmentFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointDepthVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointDepthFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSkyVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSkyFragmentFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateWaterVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateWaterFragmentFunction())>);
}

TEST_CASE("Metal P4 sprite shader keeps billboard expansion outside the PBR variant path") {

    const std::string_view vertexSource{metal::sprite_vertex};
    const std::string_view fragmentSource{metal::sprite_fragment};

    REQUIRE(vertexSource.find("vertex SpriteVertexOutput sprite_vertex") != std::string_view::npos);
    REQUIRE(vertexSource.find("modelViewMatrix * float4(0.0, 0.0, 0.0, 1.0)") != std::string_view::npos);
    REQUIRE(vertexSource.find("length(uniforms.modelMatrix[0].xyz)") != std::string_view::npos);
    REQUIRE(vertexSource.find("uniforms.center") != std::string_view::npos);
    REQUIRE(vertexSource.find("uniforms.rotation") != std::string_view::npos);
    REQUIRE(fragmentSource.find("fragment float4 sprite_fragment") != std::string_view::npos);
    REQUIRE(fragmentSource.find("texture2d<float> map [[texture(0)]]") != std::string_view::npos);
}

TEST_CASE("Metal P4 line and points shaders use dedicated primitive outputs") {

    const std::string_view lineVertex{metal::line_vertex};
    const std::string_view pointsVertex{metal::points_vertex};
    const std::string_view rawFragment{metal::raw_shader_fragment};

    REQUIRE(lineVertex.find("vertex LineVertexOutput line_vertex") != std::string_view::npos);
    REQUIRE(lineVertex.find("uniforms.mvp * float4(in.position, 1.0)") != std::string_view::npos);
    REQUIRE(pointsVertex.find("float pointSize [[point_size]]") != std::string_view::npos);
    REQUIRE(pointsVertex.find("uniforms.scale / max(projected.w") != std::string_view::npos);
    REQUIRE(rawFragment.find("fragment float4 raw_shader_fragment") != std::string_view::npos);
    REQUIRE(rawFragment.find("sin(in.localPosition.x * 10.0 + uniforms.time)") != std::string_view::npos);
}

TEST_CASE("Metal P4 point light shadows use tiled depth maps without reusing attenuation params") {

    const std::string_view source{metal::basic_fragment};
    const std::string_view pointDepthFragment{metal::point_depth_fragment};

    REQUIRE(source.find("struct PointLightUniform") != std::string_view::npos);
    REQUIRE(source.find("float4 shadowParams;") != std::string_view::npos);
    REQUIRE(source.find("float4 shadowMapSize;") != std::string_view::npos);
    REQUIRE(source.find("float getPointShadow") != std::string_view::npos);
    REQUIRE(source.find("depth2d<float> pointShadowMap0 [[texture(15)]]") != std::string_view::npos);
    REQUIRE(source.find("depth2d<float> pointShadowMap3 [[texture(18)]]") != std::string_view::npos);
    REQUIRE(source.find("light.params.x") != std::string_view::npos);
    REQUIRE(pointDepthFragment.find("[[depth(any)]]") != std::string_view::npos);
    REQUIRE(pointDepthFragment.find("length(in.worldPosition - transforms.lightPosition.xyz)") != std::string_view::npos);
}

TEST_CASE("Metal P4 built-in Sky and Water shaders are available as dedicated MSL sources") {

    REQUIRE(std::string_view{metal::sky_vertex}.find("vertex SkyVertexOutput sky_vertex") != std::string_view::npos);
    REQUIRE(std::string_view{metal::sky_fragment}.find("fragment float4 sky_fragment") != std::string_view::npos);
    REQUIRE(std::string_view{metal::water_vertex}.find("vertex WaterVertexOutput water_vertex") != std::string_view::npos);
    REQUIRE(std::string_view{metal::water_fragment}.find("fragment float4 water_fragment") != std::string_view::npos);
}

TEST_CASE("Metal P2 fragment shader applies shadow runtime controls") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("smoothstep(light.params.z, light.params.w, angleCos)") != std::string_view::npos);
    REQUIRE(source.find("params.textureFlags1.w") != std::string_view::npos);
    REQUIRE(source.find("in.worldPosition + n * light.shadowMapSize.z") != std::string_view::npos);
}

TEST_CASE("Metal P2 direct light uses GL default non-physical intensity scale") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("radiance *= PI;") != std::string_view::npos);
}

TEST_CASE("Metal P2 shadow bias follows GL shadow depth convention") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("coord.z += bias;") != std::string_view::npos);
}

TEST_CASE("Metal P2 directional and spot shadows sample Metal texture y orientation") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("float2 uv = float2(coord.x, 1.0 - coord.y);") != std::string_view::npos);
    REQUIRE(source.find("sample_compare(shadowSampler, uv + offset, coord.z)") != std::string_view::npos);
}

TEST_CASE("Metal P2 skinning applies bind matrices like GL") {

    const std::string_view vertexSource{metal::basic_vertex};
    REQUIRE(vertexSource.find("transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix") != std::string_view::npos);
    REQUIRE(vertexSource.find("localPosition = skinMatrix * localPosition") != std::string_view::npos);

    const std::string_view depthSource{metal::depth_vertex};
    REQUIRE(depthSource.find("transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix") != std::string_view::npos);
    REQUIRE(depthSource.find("localPosition = skinMatrix * localPosition") != std::string_view::npos);
}

TEST_CASE("Metal P2 env map path is independent from UV texture variants") {

    const std::string_view source{metal::basic_fragment};

    const auto textureBlockStart = source.find("#if USE_MAP\n    , texture2d<float> map");
    REQUIRE(textureBlockStart != std::string_view::npos);
    const auto textureBlockEnd = source.find("#endif", textureBlockStart);
    REQUIRE(textureBlockEnd != std::string_view::npos);
    const auto textureBlock = source.substr(textureBlockStart, textureBlockEnd - textureBlockStart);
    REQUIRE(textureBlock.find("texturecube<float> envMap") == std::string_view::npos);

    const auto uvSamplingBlockStart = source.find("#if USE_MAP\n    if (params.textureFlags1.x");
    REQUIRE(uvSamplingBlockStart != std::string_view::npos);
    const auto uvSamplingBlockEnd = source.find("#endif", uvSamplingBlockStart);
    REQUIRE(uvSamplingBlockEnd != std::string_view::npos);
    const auto uvSamplingBlock = source.substr(uvSamplingBlockStart, uvSamplingBlockEnd - uvSamplingBlockStart);
    REQUIRE(uvSamplingBlock.find("envMap.sample") == std::string_view::npos);

    REQUIRE(source.find("texturecube<float> envMap [[texture(6)]]") != std::string_view::npos);
    REQUIRE(source.find("envMap.sample(mapSampler, reflected") != std::string_view::npos);
}

TEST_CASE("MetalRenderer exposes shadow map controls for example parity") {

    STATIC_REQUIRE(HasMetalShadowMap<MetalRenderer>);
}

TEST_CASE("MetalRenderer exposes opaque frame handles for external encoders") {

    STATIC_REQUIRE(HasMetalExternalFrameHandles<MetalRenderer>);
}

TEST_CASE("Metal P1 managers keep Objective-C types hidden behind void pointers") {

    STATIC_REQUIRE(std::is_constructible_v<metal::MetalShaderManager, void*>);
    STATIC_REQUIRE(std::is_constructible_v<metal::MetalTextureManager, void*, void*>);
    STATIC_REQUIRE(std::is_same_v<void, decltype(std::declval<metal::MetalBufferManager&>().remove(std::declval<BufferAttribute&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalBufferManager&>().getDynamicBuffer(nullptr, std::size_t{}, nullptr))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalBufferManager&>().getTransientBuffer(std::size_t{}, nullptr))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateVertexFunction(std::declval<const metal::ShaderProgramKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateFragmentFunction(std::declval<const metal::ShaderProgramKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthVertexFunction(true))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalPipelineCache&>().getOrCreateDepthOnlyPipelineState(nullptr, std::uint8_t{0b0001}))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalPipelineCache&>().getOrCreateDepthOnlyPipelineState(nullptr, nullptr, std::uint8_t{0b0001}))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalTextureManager&>().getOrCreateTexture(std::declval<Texture&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalTextureManager&>().getOrCreateSampler(std::declval<Texture&>()))>);
}

TEST_CASE("Metal render preparation refreshes standalone camera matrices") {

    PerspectiveCamera camera{60, 1, 1, 10};
    camera.position.z = 4;

    REQUIRE(camera.matrixWorld->elements[14] == 0);

    metal::prepareCameraForRender(camera);

    REQUIRE(camera.matrixWorld->elements[14] == 4);
    REQUIRE(camera.matrixWorldInverse.elements[14] == -4);
}

TEST_CASE("Metal render preparation preserves Water oblique reflection projection") {

    auto water = Water::create(PlaneGeometry::create(10.f, 10.f));
    water->rotateX(-math::PI / 2.f);
    water->updateMatrixWorld(true);

    PerspectiveCamera camera{60, 1, 0.1f, 100.f};
    camera.position.set(0.f, 5.f, 5.f);
    camera.lookAt(0.f, 0.f, 0.f);
    camera.updateMatrixWorld(true);

    const auto originalProjection = camera.projectionMatrix;
    REQUIRE(water->updateReflection(camera));

    auto& reflectionCamera = water->reflectionCamera();
    const auto obliqueProjection = reflectionCamera.projectionMatrix;

    REQUIRE(obliqueProjection.elements[10] != Catch::Approx(originalProjection.elements[10]));
    REQUIRE(obliqueProjection.elements[14] != Catch::Approx(originalProjection.elements[14]));

    metal::prepareCameraForRender(reflectionCamera);

    REQUIRE(reflectionCamera.projectionMatrix.elements[2] == Catch::Approx(obliqueProjection.elements[2]));
    REQUIRE(reflectionCamera.projectionMatrix.elements[6] == Catch::Approx(obliqueProjection.elements[6]));
    REQUIRE(reflectionCamera.projectionMatrix.elements[10] == Catch::Approx(obliqueProjection.elements[10]));
    REQUIRE(reflectionCamera.projectionMatrix.elements[14] == Catch::Approx(obliqueProjection.elements[14]));
}

TEST_CASE("Water registers a pre-render job without renderer-specific callbacks") {

    auto water = Water::create(PlaneGeometry::create(10.f, 10.f));
    water->rotateX(-math::PI / 2.f);
    water->updateMatrixWorld(true);

    auto material = water->material();
    REQUIRE(material != nullptr);
    REQUIRE(material->polygonOffset);
    REQUIRE(material->polygonOffsetFactor == Catch::Approx(1.f));
    REQUIRE(material->polygonOffsetUnits == Catch::Approx(1.f));
    REQUIRE_FALSE(water->onBeforeRender.has_value());

    PerspectiveCamera camera{60, 1, 0.1f, 100.f};
    camera.position.set(0.f, 5.f, 5.f);
    camera.lookAt(0.f, 0.f, 0.f);
    camera.updateMatrixWorld(true);

    auto job = water->getPreRenderJob(camera);
    REQUIRE(job.has_value());
    REQUIRE(job->initiator == water.get());
    REQUIRE(job->camera == &water->reflectionCamera());
    REQUIRE(job->renderTarget == water->reflectionRenderTarget());
    REQUIRE(job->renderTarget->texture->encoding == Encoding::Linear);
}

TEST_CASE("Water shader samples the GL reflection target in native orientation") {

    auto water = Water::create(PlaneGeometry::create(10.f, 10.f));
    auto* material = water->material()->as<ShaderMaterial>();
    REQUIRE(material != nullptr);

    REQUIRE(material->fragmentShader.find("texture2D( mirrorSampler, mirrorCoord.xy / mirrorCoord.w + distortion )") != std::string::npos);
    REQUIRE(material->fragmentShader.find("mirrorUv.y = 1.0 - mirrorUv.y;") == std::string::npos);
}

TEST_CASE("Reflector registers a pre-render job without renderer-specific callbacks") {

    auto reflector = Reflector::create(PlaneGeometry::create(4.f, 4.f));
    reflector->updateMatrixWorld(true);
    REQUIRE_FALSE(reflector->onBeforeRender.has_value());

    PerspectiveCamera camera{60, 1, 0.1f, 100.f};
    camera.position.set(0.f, 0.f, 5.f);
    camera.lookAt(0.f, 0.f, 0.f);
    camera.updateMatrixWorld(true);

    auto job = reflector->getPreRenderJob(camera);
    REQUIRE(job.has_value());
    REQUIRE(job->initiator == reflector.get());
    REQUIRE(job->camera == &reflector->reflectionCamera());
    REQUIRE(job->renderTarget == reflector->reflectionRenderTarget());
}

TEST_CASE("Metal projection maps OpenGL depth clip range to Metal depth clip range") {

    PerspectiveCamera camera{60, 1, 1, 10};
    const auto metalProjection = metal::convertProjectionToMetalClipSpace(camera.projectionMatrix);

    Vector4 nearClip{0, 0, -1, 1};
    nearClip.applyMatrix4(camera.projectionMatrix);
    REQUIRE(nearClip.z / nearClip.w == Catch::Approx(-1.f));

    nearClip.set(0, 0, -1, 1).applyMatrix4(metalProjection);
    REQUIRE(nearClip.z / nearClip.w == Catch::Approx(0.f));

    Vector4 farClip{0, 0, -10, 1};
    farClip.applyMatrix4(metalProjection);
    REQUIRE(farClip.z / farClip.w == Catch::Approx(1.f));
}

TEST_CASE("Metal face culling state matches OpenGL material side semantics") {

    auto front = metal::computeFaceCullingState(Side::Front, false);
    REQUIRE(front.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(front.cullMode == metal::CullMode::Back);

    auto back = metal::computeFaceCullingState(Side::Back, false);
    REQUIRE(back.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(back.cullMode == metal::CullMode::Back);

    auto flippedFront = metal::computeFaceCullingState(Side::Front, true);
    REQUIRE(flippedFront.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(flippedFront.cullMode == metal::CullMode::Back);

    auto flippedBack = metal::computeFaceCullingState(Side::Back, true);
    REQUIRE(flippedBack.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(flippedBack.cullMode == metal::CullMode::Back);

    auto doubleSided = metal::computeFaceCullingState(Side::Double, false);
    REQUIRE(doubleSided.cullMode == metal::CullMode::None);
}

TEST_CASE("Metal shadow face culling state matches OpenGL shadow caster semantics") {

    const auto front = metal::computeShadowFaceCullingState(Side::Front, std::nullopt, false, false, false);
    REQUIRE(front.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(front.cullMode == metal::CullMode::Back);

    const auto back = metal::computeShadowFaceCullingState(Side::Back, std::nullopt, false, false, false);
    REQUIRE(back.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(back.cullMode == metal::CullMode::Back);

    const auto explicitFront = metal::computeShadowFaceCullingState(Side::Back, Side::Front, false, false, false);
    REQUIRE(explicitFront.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(explicitFront.cullMode == metal::CullMode::Back);

    const auto vsmFront = metal::computeShadowFaceCullingState(Side::Front, std::nullopt, false, false, true);
    REQUIRE(vsmFront.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(vsmFront.cullMode == metal::CullMode::Back);
}

TEST_CASE("Metal wireframe rendering disables triangle culling like GL line wireframes") {

    auto frontWireframe = metal::computeFaceCullingState(Side::Front, false, true);
    REQUIRE(frontWireframe.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(frontWireframe.cullMode == metal::CullMode::None);

    auto backWireframe = metal::computeFaceCullingState(Side::Back, false, true);
    REQUIRE(backWireframe.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(backWireframe.cullMode == metal::CullMode::None);
}
