#import <Metal/Metal.h>

#include "threepp/core/BufferAttribute.hpp"
#include "threepp/renderers/metal/MetalBufferManager.hpp"
#include "threepp/renderers/metal/MetalDynamicShaderCache.hpp"
#include "threepp/renderers/metal/MetalPipelineCache.hpp"
#include "threepp/renderers/metal/MetalRenderObjects.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"
#ifdef THREEPP_HAS_SLANG
#include "threepp/renderers/shaders/SlangShaderCompiler.hpp"
#endif
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/DataTexture3D.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/textures/Texture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

using namespace threepp;

namespace {

    class TestBufferAttribute: public BufferAttribute {

    public:
        explicit TestBufferAttribute(int count)
            : BufferAttribute(1, false), count_(count) {}

        [[nodiscard]] int count() const override {
            return count_;
        }

    private:
        int count_;
    };

    std::vector<Image> makeCubeFaces(unsigned int size = 1) {
        std::vector<Image> faces;
        faces.reserve(6);
        for (unsigned int face = 0; face < 6; ++face) {
            const unsigned char value = static_cast<unsigned char>(32u + face * 24u);
            std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3u, value);
            faces.emplace_back(std::move(pixels), size, size);
        }
        return faces;
    }

    bool contains(std::string_view source, std::string_view token) {
        return source.find(token) != std::string_view::npos;
    }

    class CountingShaderCompiler: public ShaderCompiler {

    public:
        int calls = 0;

        CompileResult compile(std::string_view source, ShaderStage, TargetLanguage) override {
            ++calls;
            return {std::string(source) + "\n// compiled", {}, true};
        }
    };

#ifdef THREEPP_HAS_SLANG
    constexpr const char* simpleSlangShader = R"(
struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return float4(1.0, 0.0, 0.0, 1.0);
}
)";
#endif

}// namespace

TEST_CASE("Metal special shaders bind uniforms away from vertex attributes") {

    CHECK(contains(metal::sky_vertex, "constant SkyUniforms& uniforms [[buffer(4)]]"));
    CHECK(contains(metal::sky_fragment, "constant SkyUniforms& uniforms [[buffer(4)]]"));
    CHECK(contains(metal::water_vertex, "constant WaterUniforms& uniforms [[buffer(4)]]"));
    CHECK(contains(metal::water_fragment, "constant WaterUniforms& uniforms [[buffer(4)]]"));
    CHECK(contains(metal::water_fragment, "sampler normalMapSampler [[sampler(0)]]"));
    CHECK(contains(metal::water_fragment, "sampler mirrorMapSampler [[sampler(1)]]"));
    CHECK(contains(metal::water_fragment, "float2 mirrorUv = in.mirrorCoord.xy / in.mirrorCoord.w + distortion;"));
    CHECK(contains(metal::water_fragment, "mirrorUv.y = 1.0 - mirrorUv.y"));
    CHECK(contains(metal::water_fragment, "mirrorSampler.sample(mirrorMapSampler, mirrorUv)"));
    CHECK(contains(metal::water_fragment, "applyFog("));
    CHECK(contains(metal::reflector_vertex, "constant ReflectorUniforms& uniforms [[buffer(4)]]"));
    CHECK(contains(metal::reflector_fragment, "constant ReflectorUniforms& uniforms [[buffer(4)]]"));
    CHECK(contains(metal::reflector_fragment, "sampler tDiffuseSampler [[sampler(0)]]"));
    CHECK(contains(metal::reflector_fragment, "uv.y = 1.0 - uv.y"));
    CHECK(contains(metal::reflector_fragment, "toneMapping("));
    CHECK(contains(metal::basic_fragment, "directBlinnPhong("));
    CHECK(contains(metal::basic_fragment, "params.materialType == 2"));
    CHECK(contains(metal::basic_fragment, "float4 specularColor"));
    CHECK(contains(metal::particle_system_vertex, "constant ParticleUniforms& uniforms [[buffer(6)]]"));
    CHECK(contains(metal::particle_system_vertex, "float customVisible [[attribute(1)]]"));
    CHECK(contains(metal::particle_system_vertex, "float customAngle [[attribute(2)]]"));
    CHECK(contains(metal::particle_system_vertex, "float customSize [[attribute(3)]]"));
    CHECK(contains(metal::particle_system_vertex, "float3 customColor [[attribute(4)]]"));
    CHECK(contains(metal::particle_system_vertex, "float customOpacity [[attribute(5)]]"));
    CHECK(contains(metal::particle_system_vertex, "customSize * (300.0 / length(mvPosition.xyz))"));
    CHECK_FALSE(contains(metal::particle_system_vertex, "max(in.customSize"));
    CHECK(contains(metal::particle_system_fragment, "float2 pointCoord [[point_coord]]"));
    CHECK_FALSE(contains(metal::particle_system_fragment, "1.0 - pointCoord.y"));
}

TEST_CASE("Metal shader keys include morph target variant bits") {

    metal::ShaderProgramKey baseProgram;
    metal::ShaderProgramKey morphProgram = baseProgram;
    morphProgram.useMorphTargets = true;
    CHECK_FALSE(baseProgram == morphProgram);
    CHECK(metal::ShaderProgramKeyHash{}(baseProgram) != metal::ShaderProgramKeyHash{}(morphProgram));

    metal::ShaderProgramKey flatProgram = baseProgram;
    flatProgram.flatShading = true;
    CHECK_FALSE(baseProgram == flatProgram);
    CHECK(metal::ShaderProgramKeyHash{}(baseProgram) != metal::ShaderProgramKeyHash{}(flatProgram));

    metal::ShaderProgramKey morphNormalsProgram = morphProgram;
    morphNormalsProgram.useMorphNormals = true;
    CHECK_FALSE(morphProgram == morphNormalsProgram);
    CHECK(metal::ShaderProgramKeyHash{}(morphProgram) != metal::ShaderProgramKeyHash{}(morphNormalsProgram));

    metal::DepthShaderKey baseDepth;
    metal::DepthShaderKey morphDepth = baseDepth;
    morphDepth.useMorphTargets = true;
    CHECK_FALSE(baseDepth == morphDepth);
    CHECK(metal::DepthShaderKeyHash{}(baseDepth) != metal::DepthShaderKeyHash{}(morphDepth));
}

TEST_CASE("Metal P2 shader manager compiles every configured variant") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalShaderManager shaderManager((__bridge void*) device);

        for (unsigned int mask = 0; mask < 1024; ++mask) {
            metal::ShaderProgramKey key;
            key.useMap = (mask & 1u) != 0u;
            key.useVertexColors = (mask & 2u) != 0u;
            key.useNormal = (mask & 4u) != 0u;
            key.useSkinning = (mask & 8u) != 0u;
            key.useLights = (mask & 16u) != 0u;
            key.useInstancing = (mask & 32u) != 0u;
            key.useInstanceColor = (mask & 64u) != 0u;
            key.doubleSided = (mask & 128u) != 0u;
            key.flipSided = (mask & 256u) != 0u;
            key.useClipping = (mask & 512u) != 0u;

            if ((key.useInstancing && key.useSkinning) ||
                (key.useInstanceColor && !key.useInstancing) ||
                (key.doubleSided && key.flipSided)) {
                continue;
            }

            REQUIRE_NOTHROW(shaderManager.getOrCreateVertexFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreateFragmentFunction(key));
        }

        for (unsigned int mask = 0; mask < 8; ++mask) {
            metal::DepthShaderKey key;
            key.useSkinning = (mask & 1u) != 0u;
            key.useInstancing = (mask & 2u) != 0u;
            key.useClipping = (mask & 4u) != 0u;
            if (key.useSkinning && key.useInstancing) continue;

            REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreateDepthFragmentFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthVertexFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthFragmentFunction(key));
        }

        metal::ShaderProgramKey morphMeshKey;
        morphMeshKey.useNormal = true;
        morphMeshKey.useMorphTargets = true;
        morphMeshKey.useMorphNormals = true;
        REQUIRE_NOTHROW(shaderManager.getOrCreateVertexFunction(morphMeshKey));
        REQUIRE_NOTHROW(shaderManager.getOrCreateFragmentFunction(morphMeshKey));

        metal::ShaderProgramKey flatLightingKey;
        flatLightingKey.useNormal = true;
        flatLightingKey.flatShading = true;
        flatLightingKey.useLights = true;
        REQUIRE_NOTHROW(shaderManager.getOrCreateVertexFunction(flatLightingKey));
        REQUIRE_NOTHROW(shaderManager.getOrCreateFragmentFunction(flatLightingKey));

        metal::DepthShaderKey morphDepthKey;
        morphDepthKey.useMorphTargets = true;
        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(morphDepthKey));
        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthFragmentFunction(morphDepthKey));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthVertexFunction(morphDepthKey));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthFragmentFunction(morphDepthKey));

        for (unsigned int mask = 0; mask < 16; ++mask) {
            metal::SpriteShaderKey key;
            key.useSizeAttenuation = (mask & 1u) != 0u;
            key.useAlphaMap = (mask & 2u) != 0u;
            key.useAlphaTest = (mask & 4u) != 0u;
            key.useFog = (mask & 8u) != 0u;

            REQUIRE_NOTHROW(shaderManager.getOrCreateSpriteVertexFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreateSpriteFragmentFunction(key));
        }
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineVertexFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineFragmentFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineVertexFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineFragmentFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsVertexFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsVertexFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsVertexFunction(false, true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsVertexFunction(true, true));
        REQUIRE_NOTHROW(shaderManager.getOrCreateParticleVertexFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateParticleFragmentFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateParticleVertexFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreateParticleFragmentFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreateRawShaderVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateRawShaderFragmentFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateSkyVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateSkyFragmentFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateWaterVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateWaterFragmentFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateReflectorVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateReflectorFragmentFunction());
    }
}

TEST_CASE("Metal P3 shader manager rejects invalid instancing variants") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalShaderManager shaderManager((__bridge void*) device);

        metal::ShaderProgramKey instancedSkinning;
        instancedSkinning.useInstancing = true;
        instancedSkinning.useSkinning = true;
        REQUIRE_THROWS_AS(shaderManager.getOrCreateVertexFunction(instancedSkinning), std::runtime_error);

        metal::ShaderProgramKey orphanInstanceColor;
        orphanInstanceColor.useInstanceColor = true;
        REQUIRE_THROWS_AS(shaderManager.getOrCreateFragmentFunction(orphanInstanceColor), std::runtime_error);

        metal::ShaderProgramKey conflictingSide;
        conflictingSide.doubleSided = true;
        conflictingSide.flipSided = true;
        REQUIRE_THROWS_AS(shaderManager.getOrCreateFragmentFunction(conflictingSide), std::runtime_error);

        metal::DepthShaderKey instancedSkinningDepth;
        instancedSkinningDepth.useSkinning = true;
        instancedSkinningDepth.useInstancing = true;
        REQUIRE_THROWS_AS(shaderManager.getOrCreateDepthVertexFunction(instancedSkinningDepth), std::runtime_error);
    }
}

TEST_CASE("Metal P3 buffer manager rotates only dynamic buffers across frames") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalBufferManager bufferManager((__bridge void*) device);

        std::vector<float> vertices{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
        auto position = FloatBufferAttribute::create(vertices, 3);
        auto* static0 = bufferManager.getBuffer(*position, vertices.size() * sizeof(float), vertices.data());
        bufferManager.beginFrame();
        auto* static1 = bufferManager.getBuffer(*position, vertices.size() * sizeof(float), vertices.data());
        REQUIRE(static0 == static1);

        std::vector<float> dynamic{1.f, 2.f, 3.f, 4.f};
        auto* dynamic0 = bufferManager.getDynamicBuffer(&dynamic, dynamic.size() * sizeof(float), dynamic.data());
        bufferManager.beginFrame();
        auto* dynamic1 = bufferManager.getDynamicBuffer(&dynamic, dynamic.size() * sizeof(float), dynamic.data());
        bufferManager.beginFrame();
        auto* dynamic2 = bufferManager.getDynamicBuffer(&dynamic, dynamic.size() * sizeof(float), dynamic.data());
        bufferManager.beginFrame();
        auto* dynamic3 = bufferManager.getDynamicBuffer(&dynamic, dynamic.size() * sizeof(float), dynamic.data());

        REQUIRE(dynamic0 != dynamic1);
        REQUIRE(dynamic1 != dynamic2);
        REQUIRE(dynamic2 != dynamic3);
        REQUIRE(dynamic0 == dynamic3);
    }
}

TEST_CASE("Metal dynamic shader cache reuses compile results") {

    metal::MetalDynamicShaderCache cache(nullptr, 2);
    CountingShaderCompiler compiler;

    const auto first = cache.compile(compiler, "source-a", ShaderStage::Vertex, TargetLanguage::MSL);
    const auto second = cache.compile(compiler, "source-a", ShaderStage::Vertex, TargetLanguage::MSL);

    REQUIRE(first.success);
    REQUIRE(second.success);
    CHECK(first.code == second.code);
    CHECK(compiler.calls == 1);

    static_cast<void>(cache.compile(compiler, "source-b", ShaderStage::Vertex, TargetLanguage::MSL));
    static_cast<void>(cache.compile(compiler, "source-c", ShaderStage::Vertex, TargetLanguage::MSL));
    static_cast<void>(cache.compile(compiler, "source-a", ShaderStage::Vertex, TargetLanguage::MSL));
    CHECK(compiler.calls == 4);
}

TEST_CASE("Metal buffer manager refreshes reused attribute addresses") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalBufferManager bufferManager((__bridge void*) device);

        alignas(TestBufferAttribute) std::byte storage[sizeof(TestBufferAttribute)];
        std::vector<float> first{1.f, 2.f, 3.f};
        std::vector<float> second{4.f, 5.f, 6.f};

        auto* firstAttribute = new (storage) TestBufferAttribute(static_cast<int>(first.size()));
        auto* firstBuffer = (__bridge id<MTLBuffer>) bufferManager.getBuffer(*firstAttribute, first.size() * sizeof(float), first.data());
        REQUIRE(firstBuffer != nil);
        REQUIRE(static_cast<const float*>(firstBuffer.contents)[0] == 1.f);
        firstAttribute->~TestBufferAttribute();

        auto* secondAttribute = new (storage) TestBufferAttribute(static_cast<int>(second.size()));
        REQUIRE(static_cast<void*>(secondAttribute) == static_cast<void*>(storage));
        auto* secondBuffer = (__bridge id<MTLBuffer>) bufferManager.getBuffer(*secondAttribute, second.size() * sizeof(float), second.data());
        REQUIRE(secondBuffer != nil);
        REQUIRE(static_cast<const float*>(secondBuffer.contents)[0] == 4.f);
        secondAttribute->~TestBufferAttribute();
    }
}

TEST_CASE("Metal texture manager uploads DataTexture3D as 3D texture") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];

        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) queue);

        std::vector<unsigned char> data(4u * 4u * 4u);
        auto texture = DataTexture3D::create(data, 4, 4, 4);
        texture->format = Format::Red;
        texture->type = Type::UnsignedByte;
        texture->unpackAlignment = 1;

        auto* mtlTexture = (__bridge id<MTLTexture>) textureManager.getOrCreateTexture(*texture);

        REQUIRE(mtlTexture != nil);
        CHECK(mtlTexture.textureType == MTLTextureType3D);
        CHECK(mtlTexture.width == 4);
        CHECK(mtlTexture.height == 4);
        CHECK(mtlTexture.depth == 4);
        CHECK(mtlTexture.pixelFormat == MTLPixelFormatR8Unorm);
    }
}

TEST_CASE("Metal texture manager uploads manual DataTexture3D mipmaps") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];

        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) queue);

        std::vector<unsigned char> data(4u * 4u * 4u, 1u);
        auto texture = DataTexture3D::create(data, 4, 4, 4);
        texture->format = Format::Red;
        texture->type = Type::UnsignedByte;
        texture->generateMipmaps = false;
        texture->unpackAlignment = 1;
        texture->mipmaps().emplace_back(std::vector<unsigned char>(2u * 2u * 2u, 123u), 2, 2, 2);

        auto* mtlTexture = (__bridge id<MTLTexture>) textureManager.getOrCreateTexture(*texture);

        REQUIRE(mtlTexture != nil);
        REQUIRE(mtlTexture.mipmapLevelCount >= 2);

        std::vector<unsigned char> uploaded(2u * 2u * 2u);
        [mtlTexture getBytes:uploaded.data()
                  bytesPerRow:2u
                bytesPerImage:4u
                   fromRegion:MTLRegionMake3D(0, 0, 0, 2, 2, 2)
                  mipmapLevel:1
                        slice:0];
        CHECK(uploaded.front() == 123u);
        CHECK(uploaded.back() == 123u);
    }
}

#ifdef THREEPP_HAS_SLANG
TEST_CASE("Metal can load dynamic MSL emitted by SlangShaderCompiler") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        SlangShaderCompiler compiler;
        const auto vertex = compiler.compile(simpleSlangShader, ShaderStage::Vertex, TargetLanguage::MSL);
        const auto fragment = compiler.compile(simpleSlangShader, ShaderStage::Fragment, TargetLanguage::MSL);

        REQUIRE(vertex.success);
        REQUIRE(fragment.success);

        NSError* vertexError = nil;
        id<MTLLibrary> vertexLibrary = [device newLibraryWithSource:[NSString stringWithUTF8String:vertex.code.c_str()] options:nil error:&vertexError];
        REQUIRE(vertexLibrary != nil);
        REQUIRE([vertexLibrary newFunctionWithName:@"vertexMain"] != nil);

        NSError* fragmentError = nil;
        id<MTLLibrary> fragmentLibrary = [device newLibraryWithSource:[NSString stringWithUTF8String:fragment.code.c_str()] options:nil error:&fragmentError];
        REQUIRE(fragmentLibrary != nil);
        REQUIRE([fragmentLibrary newFunctionWithName:@"fragmentMain"] != nil);
    }
}

TEST_CASE("Metal dynamic shader cache reuses functions and evicts least-recently-used entries") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        SlangShaderCompiler compiler;
        const auto vertex = compiler.compile(simpleSlangShader, ShaderStage::Vertex, TargetLanguage::MSL);
        REQUIRE(vertex.success);

        metal::MetalDynamicShaderCache cache((__bridge void*) device, 2);
        int evictedFunctions = 0;
        cache.setEvictFunctionCallback([&](void* function) {
            if (function) {
                ++evictedFunctions;
            }
        });

        const auto msl0 = vertex.code + "\n// cache-entry-0\n";
        const auto msl1 = vertex.code + "\n// cache-entry-1\n";
        const auto msl2 = vertex.code + "\n// cache-entry-2\n";

        auto first = cache.getFunction(msl0, @"vertexMain");
        auto firstAgain = cache.getFunction(msl0, @"vertexMain");
        REQUIRE(first != nil);
        CHECK(first == firstAgain);

        REQUIRE(cache.getFunction(msl1, @"vertexMain") != nil);
        REQUIRE(cache.getFunction(msl2, @"vertexMain") != nil);
        CHECK(evictedFunctions > 0);
        REQUIRE(cache.getFunction(msl0, @"vertexMain") != nil);
    }
}

TEST_CASE("Metal pipeline cache can remove states referencing dynamic functions") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        SlangShaderCompiler compiler;
        const auto vertex = compiler.compile(simpleSlangShader, ShaderStage::Vertex, TargetLanguage::MSL);
        const auto fragment = compiler.compile(simpleSlangShader, ShaderStage::Fragment, TargetLanguage::MSL);

        REQUIRE(vertex.success);
        REQUIRE(fragment.success);

        metal::MetalDynamicShaderCache shaderCache((__bridge void*) device, 4);
        auto vertexFunction = shaderCache.getFunction(vertex.code, @"vertexMain");
        auto fragmentFunction = shaderCache.getFunction(fragment.code, @"fragmentMain");
        REQUIRE(vertexFunction != nil);
        REQUIRE(fragmentFunction != nil);

        metal::MetalPipelineCache pipelineCache((__bridge void*) device);
        metal::PipelineKey key;
        key.vertexFunction = (__bridge void*) vertexFunction;
        key.fragmentFunction = (__bridge void*) fragmentFunction;
        key.colorPixelFormat = static_cast<std::uint64_t>(MTLPixelFormatBGRA8Unorm);

        REQUIRE(pipelineCache.getOrCreatePipelineState(key) != nullptr);
        REQUIRE_NOTHROW(pipelineCache.removePipelineStatesReferencing((__bridge void*) vertexFunction));
        REQUIRE(pipelineCache.getOrCreatePipelineState(key) != nullptr);
    }
}
#endif

TEST_CASE("Metal buffer manager drops removed static buffers") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalBufferManager bufferManager((__bridge void*) device);

        std::vector<float> first{1.f, 2.f, 3.f};
        std::vector<float> second{4.f, 5.f, 6.f};
        auto attribute = FloatBufferAttribute::create(first, 1);

        auto* firstBuffer = (__bridge id<MTLBuffer>) bufferManager.getBuffer(*attribute, first.size() * sizeof(float), first.data());
        REQUIRE(firstBuffer != nil);
        REQUIRE(static_cast<const float*>(firstBuffer.contents)[0] == 1.f);

        bufferManager.remove(*attribute);

        attribute->array() = second;
        auto* secondBuffer = (__bridge id<MTLBuffer>) bufferManager.getBuffer(*attribute, attribute->array().size() * sizeof(float), attribute->array().data());
        REQUIRE(secondBuffer != nil);
        REQUIRE(static_cast<const float*>(secondBuffer.contents)[0] == 4.f);
    }
}

TEST_CASE("Metal P3 buffer manager allocates transient buffers per draw") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalBufferManager bufferManager((__bridge void*) device);

        std::vector<unsigned int> indices{0u, 1u, 2u, 0u};
        auto* transient0 = bufferManager.getTransientBuffer(indices.size() * sizeof(unsigned int), indices.data());
        auto* transient1 = bufferManager.getTransientBuffer(indices.size() * sizeof(unsigned int), indices.data());

        REQUIRE(transient0 != nullptr);
        REQUIRE(transient1 != nullptr);
        REQUIRE(transient0 != transient1);
    }
}

TEST_CASE("Metal P2 texture manager uploads CubeTexture as a Metal cube texture") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto cubeTexture = CubeTexture::create(makeCubeFaces());
        cubeTexture->generateMipmaps = false;

        auto* rawTexture = textureManager.getOrCreateTexture(*cubeTexture);
        auto metalTexture = (__bridge id<MTLTexture>) rawTexture;

        REQUIRE(metalTexture != nil);
        REQUIRE(metalTexture.textureType == MTLTextureTypeCube);
        REQUIRE(metalTexture.width == 1);
        REQUIRE(metalTexture.height == 1);
        REQUIRE(metalTexture.depth == 1);
    }
}

TEST_CASE("Metal RenderTarget color pixel format respects texture encoding") {

    auto rgba = Texture::create();
    rgba->format = Format::RGBA;
    rgba->encoding = Encoding::sRGB;
    REQUIRE(toRenderTargetColorPixelFormat(*rgba) == MTLPixelFormatRGBA8Unorm_sRGB);

    auto rgb = Texture::create();
    rgb->format = Format::RGB;
    rgb->encoding = Encoding::Gamma;
    REQUIRE(toRenderTargetColorPixelFormat(*rgb) == MTLPixelFormatRGBA8Unorm_sRGB);

    auto bgra = Texture::create();
    bgra->format = Format::BGRA;
    bgra->encoding = Encoding::Gamma;
    REQUIRE(toRenderTargetColorPixelFormat(*bgra) == MTLPixelFormatBGRA8Unorm_sRGB);

    auto hdrFallback = Texture::create();
    hdrFallback->format = Format::RGBA;
    hdrFallback->encoding = Encoding::RGBE;
    REQUIRE(toRenderTargetColorPixelFormat(*hdrFallback) == MTLPixelFormatRGBA8Unorm);
}

TEST_CASE("Metal texture manager creates sRGB Metal textures for sRGB and Gamma encodings") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto srgbTexture = Texture::create(Image{std::vector<unsigned char>{255, 128, 64, 255}, 1, 1});
        srgbTexture->encoding = Encoding::sRGB;
        srgbTexture->generateMipmaps = false;
        auto srgbMetalTexture = (__bridge id<MTLTexture>) textureManager.getOrCreateTexture(*srgbTexture);
        REQUIRE(srgbMetalTexture.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB);

        auto gammaCubeTexture = CubeTexture::create(makeCubeFaces());
        gammaCubeTexture->encoding = Encoding::Gamma;
        gammaCubeTexture->generateMipmaps = false;
        auto gammaMetalTexture = (__bridge id<MTLTexture>) textureManager.getOrCreateTexture(*gammaCubeTexture);
        REQUIRE(gammaMetalTexture.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB);

        auto hdrFallbackTexture = Texture::create(Image{std::vector<unsigned char>{255, 128, 64, 255}, 1, 1});
        hdrFallbackTexture->encoding = Encoding::RGBE;
        hdrFallbackTexture->generateMipmaps = false;
        auto hdrFallbackMetalTexture = (__bridge id<MTLTexture>) textureManager.getOrCreateTexture(*hdrFallbackTexture);
        REQUIRE(hdrFallbackMetalTexture.pixelFormat == MTLPixelFormatRGBA8Unorm);
    }
}

TEST_CASE("Metal P3 texture manager can register an external Metal texture") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                        width:4
                                                                                       height:4
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        id<MTLTexture> externalTexture = [device newTextureWithDescriptor:desc];
        REQUIRE(externalTexture != nil);

        auto texture = Texture::create(Image({}, 4, 4));
        textureManager.registerExternalTexture(*texture, (__bridge void*) externalTexture);

        auto* rawTexture = textureManager.getOrCreateTexture(*texture);
        REQUIRE(rawTexture == (__bridge void*) externalTexture);
    }
}

TEST_CASE("Metal P4 texture manager requires explicit placeholder fallback for incomplete image data") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto texture = Texture::create(Image{std::vector<unsigned char>{}, 4, 4});

        std::ostringstream strictDiagnostics;
        auto* previous = std::cerr.rdbuf(strictDiagnostics.rdbuf());
        REQUIRE_THROWS_AS(textureManager.getOrCreateTexture(*texture), std::runtime_error);
        std::cerr.rdbuf(previous);

        std::ostringstream diagnostics;
        previous = std::cerr.rdbuf(diagnostics.rdbuf());
        auto* rawTexture0 = textureManager.getOrCreateTexture(*texture, true);
        auto* rawTexture1 = textureManager.getOrCreateTexture(*texture, true);
        std::cerr.rdbuf(previous);

        REQUIRE(rawTexture0 == nullptr);
        REQUIRE(rawTexture1 == nullptr);

        const auto message = diagnostics.str();
        REQUIRE(message.find("using placeholder texture") != std::string::npos);
        REQUIRE(message.find("using placeholder texture", message.find("using placeholder texture") + 1) == std::string::npos);
    }
}

TEST_CASE("Metal P4 texture manager uploads float texture data without reading unsigned byte data") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto texture = Texture::create(Image{std::vector<float>(4, 1.f), 1, 1});
        texture->type = Type::Float;

        auto* mtlTexture = (__bridge id<MTLTexture>) textureManager.getOrCreateTexture(*texture);

        REQUIRE(mtlTexture != nil);
        CHECK(mtlTexture.pixelFormat == MTLPixelFormatRGBA32Float);
    }
}

TEST_CASE("Metal P2 texture manager does not allocate undefined cube mip levels") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto cubeTexture = CubeTexture::create(makeCubeFaces(4));
        cubeTexture->generateMipmaps = false;

        auto* rawTexture = textureManager.getOrCreateTexture(*cubeTexture);
        auto metalTexture = (__bridge id<MTLTexture>) rawTexture;

        REQUIRE(metalTexture != nil);
        REQUIRE(metalTexture.mipmapLevelCount == 1);
    }
}

TEST_CASE("Metal P2 texture manager allocates explicit mip levels") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto texture = Texture::create(Image{std::vector<unsigned char>(4u * 4u * 4u, 64u), 4, 4});
        texture->generateMipmaps = false;
        texture->minFilter = Filter::LinearMipmapLinear;
        texture->mipmaps().emplace_back(std::vector<unsigned char>(2u * 2u * 4u, 128u), 2, 2);

        auto* rawTexture = textureManager.getOrCreateTexture(*texture);
        auto metalTexture = (__bridge id<MTLTexture>) rawTexture;
        auto* rawSampler = textureManager.getOrCreateSampler(*texture);
        auto sampler = (__bridge id<MTLSamplerState>) rawSampler;

        REQUIRE(metalTexture != nil);
        REQUIRE(metalTexture.mipmapLevelCount > 1);
        REQUIRE(sampler != nil);
    }
}
