#import <Metal/Metal.h>

#include "threepp/core/BufferAttribute.hpp"
#include "threepp/renderers/metal/MetalBufferManager.hpp"
#include "threepp/renderers/metal/MetalRenderObjects.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"
#include "threepp/textures/CubeTexture.hpp"
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
}

TEST_CASE("Metal P2 shader manager compiles every configured variant") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalShaderManager shaderManager((__bridge void*) device);

        for (unsigned int mask = 0; mask < 512; ++mask) {
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

            if ((key.useInstancing && key.useSkinning) ||
                (key.useInstanceColor && !key.useInstancing) ||
                (key.doubleSided && key.flipSided)) {
                continue;
            }

            REQUIRE_NOTHROW(shaderManager.getOrCreateVertexFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreateFragmentFunction(key));
        }

        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(false, false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(true, false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(false, true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthVertexFunction(false, false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthFragmentFunction(false, false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthVertexFunction(true, false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthFragmentFunction(true, false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthVertexFunction(false, true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointDepthFragmentFunction(false, true));

        REQUIRE_NOTHROW(shaderManager.getOrCreateSpriteVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateSpriteFragmentFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineVertexFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineFragmentFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineVertexFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreateLineFragmentFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsVertexFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsVertexFunction(true));
        REQUIRE_NOTHROW(shaderManager.getOrCreatePointsFragmentFunction(true));
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

        REQUIRE_THROWS_AS(shaderManager.getOrCreateDepthVertexFunction(true, true), std::runtime_error);
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

TEST_CASE("Metal P4 texture manager checks texture type before reading unsigned byte image data") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        metal::MetalTextureManager textureManager((__bridge void*) device, (__bridge void*) commandQueue);

        auto texture = Texture::create(Image{std::vector<float>(4, 1.f), 1, 1});
        texture->type = Type::Float;

        bool sawExpectedError = false;
        try {
            textureManager.getOrCreateTexture(*texture);
        } catch (const std::runtime_error& e) {
            sawExpectedError = std::string{e.what()} == "MetalTextureManager currently supports unsigned byte textures";
        }
        REQUIRE(sawExpectedError);
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
