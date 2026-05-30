#import <Metal/Metal.h>

#include "threepp/core/BufferAttribute.hpp"
#include "threepp/renderers/metal/MetalBufferManager.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/textures/Texture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <stdexcept>
#include <sstream>
#include <utility>
#include <vector>

using namespace threepp;

namespace {

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

}// namespace

TEST_CASE("Metal P2 shader manager compiles every configured variant") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        metal::MetalShaderManager shaderManager((__bridge void*) device);

        for (unsigned int mask = 0; mask < 128; ++mask) {
            metal::ShaderProgramKey key;
            key.useMap = (mask & 1u) != 0u;
            key.useVertexColors = (mask & 2u) != 0u;
            key.useNormal = (mask & 4u) != 0u;
            key.useSkinning = (mask & 8u) != 0u;
            key.useLights = (mask & 16u) != 0u;
            key.useInstancing = (mask & 32u) != 0u;
            key.useInstanceColor = (mask & 64u) != 0u;

            if ((key.useInstancing && key.useSkinning) ||
                (key.useInstanceColor && !key.useInstancing)) {
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
        REQUIRE_NOTHROW(shaderManager.getOrCreateSkyVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateSkyFragmentFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateWaterVertexFunction());
        REQUIRE_NOTHROW(shaderManager.getOrCreateWaterFragmentFunction());
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
