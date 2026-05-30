#import <Metal/Metal.h>

#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/textures/Texture.hpp"

#include <catch2/catch_test_macros.hpp>

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

        for (unsigned int mask = 0; mask < 32; ++mask) {
            metal::ShaderProgramKey key;
            key.useMap = (mask & 1u) != 0u;
            key.useVertexColors = (mask & 2u) != 0u;
            key.useNormal = (mask & 4u) != 0u;
            key.useSkinning = (mask & 8u) != 0u;
            key.useLights = (mask & 16u) != 0u;

            REQUIRE_NOTHROW(shaderManager.getOrCreateVertexFunction(key));
            REQUIRE_NOTHROW(shaderManager.getOrCreateFragmentFunction(key));
        }

        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(false));
        REQUIRE_NOTHROW(shaderManager.getOrCreateDepthVertexFunction(true));
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
