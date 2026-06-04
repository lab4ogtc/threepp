#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/MathUtils.hpp"
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
#include "threepp/math/Plane.hpp"
#include "threepp/renderers/metal/MetalBufferManager.hpp"
#include "threepp/renderers/metal/MetalCameraUtils.hpp"
#include "threepp/renderers/metal/MetalPipelineCache.hpp"
#include "threepp/renderers/metal/MetalRenderStateUtils.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

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
    concept HasBaseOutputEncoding = requires(T& renderer) {
        renderer.outputEncoding = Encoding::sRGB;
        { renderer.outputEncoding } -> std::same_as<Encoding&>;
    };

    template<class T>
    concept HasBaseClippingState = requires(T& renderer) {
        renderer.clippingPlanes.emplace_back(Vector3(1, 0, 0), 0.f);
        renderer.localClippingEnabled = true;
        { renderer.clippingPlanes } -> std::same_as<std::vector<Plane>&>;
        { renderer.localClippingEnabled } -> std::same_as<bool&>;
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

    std::string readProjectFile(const std::filesystem::path& relativePath) {
        const auto projectRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        std::ifstream file(projectRoot / relativePath);
        REQUIRE(file.is_open());
        return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    std::size_t countOccurrences(std::string_view source, std::string_view needle) {
        std::size_t count = 0;
        for (auto pos = source.find(needle); pos != std::string_view::npos; pos = source.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }

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

TEST_CASE("Renderer base exposes backend-independent output encoding") {

    STATIC_REQUIRE(HasBaseOutputEncoding<Renderer>);
    STATIC_REQUIRE(HasBaseOutputEncoding<MetalRenderer>);
}

TEST_CASE("Renderer base exposes backend-independent clipping state") {

    STATIC_REQUIRE(HasBaseClippingState<Renderer>);
    STATIC_REQUIRE(HasBaseClippingState<MetalRenderer>);
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

    metal::PipelineKey normalBlend = withUv;
    normalBlend.alphaBlending = true;
    normalBlend.blending = Blending::Normal;

    metal::PipelineKey additiveBlend = normalBlend;
    additiveBlend.blending = Blending::Additive;

    REQUIRE_FALSE(normalBlend == additiveBlend);
    REQUIRE(metal::PipelineKeyHash{}(normalBlend) != metal::PipelineKeyHash{}(additiveBlend));

    metal::PipelineKey opaqueNormal = withUv;
    opaqueNormal.alphaBlending = false;
    opaqueNormal.blending = Blending::Normal;

    metal::PipelineKey opaqueAdditive = opaqueNormal;
    opaqueAdditive.blending = Blending::Additive;
    opaqueAdditive.blendDst = BlendFactor::One;

    REQUIRE(opaqueNormal == opaqueAdditive);
    REQUIRE(metal::PipelineKeyHash{}(opaqueNormal) == metal::PipelineKeyHash{}(opaqueAdditive));

    metal::PipelineKey customBlend = normalBlend;
    customBlend.blending = Blending::Custom;
    customBlend.blendEquation = BlendEquation::Subtract;
    customBlend.blendSrc = BlendFactor::One;
    customBlend.blendDst = BlendFactor::DstColor;

    REQUIRE_FALSE(normalBlend == customBlend);
    REQUIRE(metal::PipelineKeyHash{}(normalBlend) != metal::PipelineKeyHash{}(customBlend));
}

TEST_CASE("Metal particle points bind dedicated attributes and uniform slot") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalObjectsRenderer.mm");

    REQUIRE(source.find("if (auto* particleMaterial = material.as<ParticleMaterial>())") != std::string::npos);
    CHECK(source.find("[encoder setVertexBuffer:posBuf offset:0 atIndex:0]") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customVisible\", 1, 1)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customAngle\", 2, 1)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customSize\", 3, 1)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customColor\", 4, 3)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customOpacity\", 5, 1)") != std::string::npos);
    CHECK(source.find("[encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:6]") != std::string::npos);
    CHECK(source.find("[encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:6]") != std::string::npos);
}

TEST_CASE("Metal depth texture ShaderMaterial path is wired as a dedicated built-in shader") {

    const std::string_view vertexSource{metal::depth_texture_vertex};
    REQUIRE(vertexSource.find("float3 position [[attribute(0)]]") != std::string_view::npos);
    REQUIRE(vertexSource.find("float2 uv [[attribute(2)]]") != std::string_view::npos);
    REQUIRE(vertexSource.find("constant DepthTextureUniforms& uniforms [[buffer(4)]]") != std::string_view::npos);

    const std::string_view fragmentSource{metal::depth_texture_fragment};
    REQUIRE(fragmentSource.find("depth2d<float> tDepth [[texture(1)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("float fragCoordZ = tDepth.sample(tDepthSampler, in.uv);") != std::string_view::npos);
    REQUIRE(fragmentSource.find("perspectiveDepthToViewZ") != std::string_view::npos);
    REQUIRE(fragmentSource.find("viewZToOrthographicDepth") != std::string_view::npos);

    const auto implHeader = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    REQUIRE(implHeader.find("void renderDepthTexture(id<MTLRenderCommandEncoder> encoder,") != std::string::npos);

    const auto shaderManagerHeader = readProjectFile("src/threepp/renderers/metal/MetalShaderManager.hpp");
    REQUIRE(shaderManagerHeader.find("void* getOrCreateDepthTextureVertexFunction();") != std::string::npos);
    REQUIRE(shaderManagerHeader.find("void* getOrCreateDepthTextureFragmentFunction();") != std::string::npos);

    const auto rendererSource = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    const auto intercept = rendererSource.find("shaderMaterial->uniforms.count(\"tDepth\") > 0");
    REQUIRE(intercept != std::string::npos);
    REQUIRE(rendererSource.find("shaderMaterial->uniforms.count(\"cameraNear\") > 0", intercept) != std::string::npos);
    REQUIRE(rendererSource.find("shaderMaterial->uniforms.count(\"cameraFar\") > 0", intercept) != std::string::npos);
    REQUIRE(rendererSource.find("renderDepthTexture(encoder, *mesh, *geometry, *shaderMaterial", intercept) != std::string::npos);

    const auto objectsSource = readProjectFile("src/threepp/renderers/metal/MetalObjectsRenderer.mm");
    const auto method = objectsSource.find("void MetalRenderer::Impl::renderDepthTexture");
    REQUIRE(method != std::string::npos);
    REQUIRE(objectsSource.find("vertexLayoutPosition | vertexLayoutUv", method) != std::string::npos);
    REQUIRE(objectsSource.find("pipelineCache->getOrCreateDepthStencilState(false, false", method) != std::string::npos);
    REQUIRE(objectsSource.find("bindDrawAttributes(encoder, geometry, *posAttr, nullptr, uvAttr", method) != std::string::npos);
    REQUIRE(objectsSource.find("uniformTexture(depthMaterial->uniforms, \"tDiffuse\")", method) != std::string::npos);
    REQUIRE(objectsSource.find("uniformTexture(depthMaterial->uniforms, \"tDepth\")", method) != std::string::npos);
    REQUIRE(objectsSource.find("whiteDepthTexture", method) != std::string::npos);
    REQUIRE(objectsSource.find("[encoder setFragmentTexture:depthTexture atIndex:1]", method) != std::string::npos);
    REQUIRE(objectsSource.find("[encoder setFragmentSamplerState:depthSampler atIndex:1]", method) != std::string::npos);

    const auto exampleSource = readProjectFile("examples/textures/depth_texture_metal.cpp");
    REQUIRE(exampleSource.find("MetalRenderer renderer") != std::string::npos);
    REQUIRE(exampleSource.find("RenderTarget::create") != std::string::npos);
    REQUIRE(exampleSource.find("GLRenderTarget") == std::string::npos);
    REQUIRE(exampleSource.find("postMaterial->uniforms.at(\"tDepth\").setValue(target->depthTexture.get())") != std::string::npos);

    const auto cmakeSource = readProjectFile("examples/textures/CMakeLists.txt");
    REQUIRE(cmakeSource.find("add_example(NAME \"depth_texture_metal\")") != std::string::npos);

    const auto alignmentDoc = readProjectFile("docs/examples_metal_alignment.md");
    REQUIRE(alignmentDoc.find("depth_texture.cpp` | ✅ | ✅ | ✅ 已对齐") != std::string::npos);
    REQUIRE(alignmentDoc.find("深度纹理后处理 ShaderMaterial 由 MetalRenderer 内置 MSL 接管") != std::string::npos);
}

TEST_CASE("Metal P2 shader keys include skinning and lighting variants") {

    metal::ShaderProgramKey skinned{};
    skinned.useSkinning = true;

    metal::ShaderProgramKey lit{};
    lit.useLights = true;

    REQUIRE_FALSE(skinned == lit);
    REQUIRE(metal::ShaderProgramKeyHash{}(skinned) != metal::ShaderProgramKeyHash{}(lit));
}

TEST_CASE("Metal shader keys include clipping variants") {

    metal::ShaderProgramKey unclipped{};
    metal::ShaderProgramKey clipped{};
    clipped.useClipping = true;

    REQUIRE_FALSE(unclipped == clipped);
    REQUIRE(metal::ShaderProgramKeyHash{}(unclipped) != metal::ShaderProgramKeyHash{}(clipped));

    metal::DepthShaderKey unclippedDepth{};
    metal::DepthShaderKey clippedDepth{};
    clippedDepth.useClipping = true;

    REQUIRE_FALSE(unclippedDepth == clippedDepth);
    REQUIRE(metal::DepthShaderKeyHash{}(unclippedDepth) != metal::DepthShaderKeyHash{}(clippedDepth));
}

TEST_CASE("Metal sprite shader keys cover all sprite feature variants") {

    metal::SpriteShaderKey alphaMap{};
    alphaMap.useAlphaMap = true;

    metal::SpriteShaderKey alphaTest{};
    alphaTest.useAlphaTest = true;

    metal::SpriteShaderKey fog{};
    fog.useFog = true;

    metal::SpriteShaderKey sizeAttenuation{};
    sizeAttenuation.useSizeAttenuation = true;

    REQUIRE_FALSE(alphaMap == alphaTest);
    REQUIRE_FALSE(alphaMap == fog);
    REQUIRE_FALSE(sizeAttenuation == fog);
    REQUIRE(metal::SpriteShaderKeyHash{}(alphaMap) != metal::SpriteShaderKeyHash{}(alphaTest));
    REQUIRE(metal::SpriteShaderKeyHash{}(alphaMap) != metal::SpriteShaderKeyHash{}(fog));
    REQUIRE(metal::SpriteShaderKeyHash{}(sizeAttenuation) != metal::SpriteShaderKeyHash{}(fog));
}

TEST_CASE("Metal P4 shader manager exposes dedicated built-in material entry points") {

    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSpriteVertexFunction(std::declval<const metal::SpriteShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSpriteFragmentFunction(std::declval<const metal::SpriteShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateLineVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateLineFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointsVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointsFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateRawShaderVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateRawShaderFragmentFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthVertexFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthFragmentFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointDepthVertexFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointDepthFragmentFunction(std::declval<const metal::DepthShaderKey&>()))>);
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
    REQUIRE(vertexSource.find("#if USE_FOG") != std::string_view::npos);
    REQUIRE(vertexSource.find("float3 transformedUv = uniforms.uvTransform * float3(in.uv, 1.0)") != std::string_view::npos);
    REQUIRE(vertexSource.find("out.fogDepth = -mvPosition.z") != std::string_view::npos);
    REQUIRE(fragmentSource.find("fragment float4 sprite_fragment") != std::string_view::npos);
    REQUIRE(fragmentSource.find("texture2d<float> map [[texture(0)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("texture2d<float> alphaMap [[texture(1)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("sampler alphaMapSampler [[sampler(1)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("#if USE_ALPHAMAP") != std::string_view::npos);
    REQUIRE(fragmentSource.find("alphaMap.sample(alphaMapSampler, in.uv).g") != std::string_view::npos);
    REQUIRE(fragmentSource.find("#if USE_ALPHATEST") != std::string_view::npos);
    REQUIRE(fragmentSource.find("discard_fragment()") != std::string_view::npos);
    REQUIRE(fragmentSource.find("applyFog(color.rgb, in.fogDepth") != std::string_view::npos);
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

TEST_CASE("Metal P4 point light shadows sample Metal texture y orientation") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("float2 pointShadowUV(") != std::string_view::npos);
    REQUIRE(source.find("return float2(uv.x, 1.0 - uv.y);") != std::string_view::npos);
    REQUIRE(source.find("sample_compare(shadowSampler, pointShadowUV(bd3D, texelSize.y), dp)") != std::string_view::npos);
}

TEST_CASE("Metal P4 point light shadow atlas writes flip GL viewport rows for Metal") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalShadowRenderer.mm");

    REQUIRE(source.find("frameExtents.y - viewport.y - viewport.w") != std::string::npos);
}

TEST_CASE("Metal P4 point light shadow atlas pass uses per-face scissor and fresh depth bias state") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalShadowRenderer.mm");
    const auto methodStart = source.find("renderPointLightShadow");
    REQUIRE(methodStart != std::string::npos);

    const auto resetDepthBias = source.find("resetDepthBiasCache();", methodStart);
    const auto depthStencil = source.find("[encoder setDepthStencilState:depthStencilState];", methodStart);
    const auto frameExtents = source.find("const auto frameExtents = shadow.getFrameExtents();", methodStart);

    REQUIRE(resetDepthBias != std::string::npos);
    REQUIRE(depthStencil != std::string::npos);
    REQUIRE(frameExtents != std::string::npos);
    REQUIRE(resetDepthBias < depthStencil);
    REQUIRE(depthStencil < frameExtents);
    REQUIRE(source.find("[encoder setScissorRect:metalScissor];") != std::string::npos);
}

TEST_CASE("Metal point light example mirrors GL shadow receiver and bias setup") {

    const auto glSource = readProjectFile("examples/lights/point_light.cpp");
    const auto metalSource = readProjectFile("examples/lights/point_light_metal.cpp");

    REQUIRE(glSource.find("knot->receiveShadow = true") == std::string::npos);
    REQUIRE(metalSource.find("knot->receiveShadow = true") == std::string::npos);
    REQUIRE(countOccurrences(metalSource, "shadow->bias = -0.005f") == countOccurrences(glSource, "shadow->bias = -0.005f"));
    REQUIRE(metalSource.find("renderer->shadowMap().type") == std::string::npos);
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
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthVertexFunction(std::declval<const metal::DepthShaderKey&>()))>);
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
    REQUIRE(std::string_view{metal::water_fragment}.find("mirrorUv.y = 1.0 - mirrorUv.y") != std::string_view::npos);
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

TEST_CASE("Metal reflector shader samples the GL reflection target in native orientation") {

    REQUIRE(std::string_view{metal::reflector_fragment}.find("uv.y = 1.0 - uv.y") != std::string_view::npos);
}

TEST_CASE("Metal water example matches GL tone mapping") {

    const auto glSource = readProjectFile("examples/objects/water.cpp");
    const auto metalSource = readProjectFile("examples/objects/water_metal.cpp");

    REQUIRE(glSource.find("renderer.toneMapping = ToneMapping::ACESFilmic") != std::string::npos);
    REQUIRE(metalSource.find("renderer->toneMapping = ToneMapping::ACESFilmic") != std::string::npos);
}

TEST_CASE("Metal reflector example matches GL antialiasing") {

    const auto glSource = readProjectFile("examples/textures/texture2d.cpp");
    const auto metalSource = readProjectFile("examples/textures/texture2d_metal.cpp");

    REQUIRE(glSource.find("{{\"aa\", 8}}") != std::string::npos);
    REQUIRE(metalSource.find("{{\"aa\", 8}, {\"clientAPI\", \"Metal\"}}") != std::string::npos);
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
