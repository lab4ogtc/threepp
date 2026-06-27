#import "MetalPMREM.hpp"

#import "threepp/core/EventDispatcher.hpp"
#import "threepp/renderers/EnvMapUtils.hpp"
#import "threepp/textures/CubeTexture.hpp"
#import "threepp/textures/Texture.hpp"

#import <Metal/Metal.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace threepp::metal {

    namespace {

        constexpr auto pmremComputeSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct PMREMUniforms {
    float maxSourceMip;
    float roughFloorBase;
    float decodeSourceColor;
    float pad1;
};

float vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint i, uint count) {
    return float2(float(i) / float(count), vdc(i));
}

float3 directionFromEquirectUv(float2 uv) {
    float phi = (uv.x - 0.5) * 6.28318530718;
    float theta = (uv.y - 0.5) * 3.14159265359;
    float cosTheta = cos(theta);
    return normalize(float3(cosTheta * cos(phi), sin(theta), cosTheta * sin(phi)));
}

float2 equirectUv(float3 direction) {
    float3 dir = normalize(direction);
    float u = atan2(dir.z, dir.x) * 0.15915494309189535 + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) * 0.3183098861837907 + 0.5;
    return float2(u, v);
}

float3 importanceSampleGGX(float2 xi, float3 n, float roughness) {
    float a = roughness * roughness;
    float phi = 6.28318530718 * xi.x;
    float cosTheta = sqrt(max((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y), 0.0));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float3 hTangent = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, n));
    float3 bitangent = cross(n, tangent);
    return normalize(tangent * hTangent.x + bitangent * hTangent.y + n * hTangent.z);
}

float ggxD(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (nDotH * nDotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265359 * d * d);
}

float3 sampleEquirect(texture2d<float, access::sample> sourceTexture,
                      sampler sourceSampler,
                      float3 direction,
                      float mip) {
    return sourceTexture.sample(sourceSampler, equirectUv(direction), level(mip)).rgb;
}

float3 sampleCube(texturecube<float, access::sample> sourceTexture,
                  sampler sourceSampler,
                  float3 direction,
                  float mip) {
    return sourceTexture.sample(sourceSampler, normalize(direction), level(mip)).rgb;
}

float3 sRGBToLinear(float3 value) {
    float3 high = pow(value * 0.9478672986 + float3(0.0521327014), float3(2.4));
    float3 low = value * 0.0773993808;
    return select(high, low, value <= float3(0.04045));
}

kernel void pmremPrefilter(texture2d<float, access::sample> sourceTexture [[texture(0)]],
                           texture2d<float, access::write> outputTexture [[texture(1)]],
                           sampler sourceSampler [[sampler(0)]],
                           constant PMREMUniforms& uniforms [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]]) {
    constexpr uint EQ_STRIP_W = 512;
    constexpr uint EQ_STRIP_H = 256;
    constexpr uint EQ_N_LODS = 7;

    if (gid.x >= EQ_STRIP_W || gid.y >= EQ_STRIP_H * EQ_N_LODS) {
        return;
    }

    uint lodIndex = min(gid.y / EQ_STRIP_H, EQ_N_LODS - 1);
    uint stripY = gid.y - lodIndex * EQ_STRIP_H;
    float roughness = float(lodIndex) / float(EQ_N_LODS - 1);
    uint numSamples = lodIndex == 0 ? 1 : (roughness < 0.3 ? 256 : 128);

    float2 uv = (float2(gid.x, stripY) + 0.5) / float2(EQ_STRIP_W, EQ_STRIP_H);
    float3 n = directionFromEquirectUv(uv);

    if (roughness <= 0.0) {
        outputTexture.write(float4(sampleEquirect(sourceTexture, sourceSampler, n, 0.0), 1.0), gid);
        return;
    }

    float2 sourceSize = float2(sourceTexture.get_width(), sourceTexture.get_height());
    float saTexel = 4.0 * 3.14159265359 / (sourceSize.x * sourceSize.y);
    float roughFloor = roughness * max(uniforms.roughFloorBase, 0.0);

    float3 accumColor = float3(0.0);
    float accumWeight = 0.0;
    for (uint i = 0; i < numSamples; ++i) {
        float2 xi = hammersley(i, numSamples);
        float3 h = importanceSampleGGX(xi, n, roughness);
        float3 l = normalize(-n + 2.0 * dot(n, h) * h);
        float nDotL = max(dot(n, l), 0.0);
        if (nDotL > 0.0) {
            float nDotH = max(dot(n, h), 0.0);
            float pdf = ggxD(nDotH, roughness) * 0.25 + 1e-4;
            float saSample = 1.0 / (float(numSamples) * pdf);
            float mip = 0.5 * log2(saSample / saTexel);
            mip = clamp(max(mip, roughFloor), 0.0, uniforms.maxSourceMip);
            float3 sampleColor = min(sampleEquirect(sourceTexture, sourceSampler, l, mip), float3(50.0));
            accumColor += sampleColor * nDotL;
            accumWeight += nDotL;
        }
    }

    outputTexture.write(float4(accumColor / max(accumWeight, 0.001), 1.0), gid);
}

kernel void pmremPrefilterCube(texturecube<float, access::sample> sourceTexture [[texture(0)]],
                               texture2d<float, access::write> outputTexture [[texture(1)]],
                               sampler sourceSampler [[sampler(0)]],
                               constant PMREMUniforms& uniforms [[buffer(0)]],
                               uint2 gid [[thread_position_in_grid]]) {
    constexpr uint EQ_STRIP_W = 512;
    constexpr uint EQ_STRIP_H = 256;
    constexpr uint EQ_N_LODS = 7;

    if (gid.x >= EQ_STRIP_W || gid.y >= EQ_STRIP_H * EQ_N_LODS) {
        return;
    }

    uint lodIndex = min(gid.y / EQ_STRIP_H, EQ_N_LODS - 1);
    uint stripY = gid.y - lodIndex * EQ_STRIP_H;
    float roughness = float(lodIndex) / float(EQ_N_LODS - 1);

    float2 uv = (float2(gid.x, stripY) + 0.5) / float2(EQ_STRIP_W, EQ_STRIP_H);
    float3 n = directionFromEquirectUv(uv);

    if (roughness <= 0.0) {
        float3 sampleColor = sampleCube(sourceTexture, sourceSampler, n, 0.0);
        if (uniforms.decodeSourceColor != 0.0) {
            sampleColor = sRGBToLinear(sampleColor);
        }
        outputTexture.write(float4(sampleColor, 1.0), gid);
        return;
    }

    float faceSize = float(max(sourceTexture.get_width(), sourceTexture.get_height()));
    float saTexel = 4.0 * 3.14159265359 / max(6.0 * faceSize * faceSize, 1.0);
    float roughFloor = roughness * max(uniforms.roughFloorBase, 0.0);
    uint numSamples = roughness < 0.3 ? 256 : 128;

    float3 accumColor = float3(0.0);
    float accumWeight = 0.0;
    for (uint i = 0; i < numSamples; ++i) {
        float2 xi = hammersley(i, numSamples);
        float3 h = importanceSampleGGX(xi, n, roughness);
        float3 l = normalize(-n + 2.0 * dot(n, h) * h);
        float nDotL = max(dot(n, l), 0.0);
        if (nDotL > 0.0) {
            float nDotH = max(dot(n, h), 0.0);
            float pdf = ggxD(nDotH, roughness) * 0.25 + 1e-4;
            float saSample = 1.0 / (float(numSamples) * pdf);
            float mip = 0.5 * log2(saSample / saTexel);
            mip = clamp(max(mip, roughFloor), 0.0, uniforms.maxSourceMip);
            float3 sampleColor = min(sampleCube(sourceTexture, sourceSampler, l, mip), float3(50.0));
            if (uniforms.decodeSourceColor != 0.0) {
                sampleColor = sRGBToLinear(sampleColor);
            }
            accumColor += sampleColor * nDotL;
            accumWeight += nDotL;
        }
    }

    outputTexture.write(float4(accumColor / max(accumWeight, 0.001), 1.0), gid);
}
)metal";

        void releaseOwnedMetalObject(id object) {
            if (!object) {
                return;
            }
#if !__has_feature(objc_arc)
            [object release];
#else
            (void) object;
#endif
        }

        MTLPixelFormat pmremPixelFormat(MTLPixelFormat sourcePixelFormat) {
            switch (sourcePixelFormat) {
                case MTLPixelFormatRGBA16Float:
                case MTLPixelFormatRGBA32Float:
                    return sourcePixelFormat;
                default:
                    return MTLPixelFormatRGBA16Float;
            }
        }

        struct PMREMUniforms {
            float maxSourceMip;
            float roughFloorBase;
            float decodeSourceColor;
            float pad1;
        };

        struct CachedPMREM {
            id<MTLTexture> texture = nil;
            unsigned int version = 0;
            NSUInteger sourceWidth = 0;
            NSUInteger sourceHeight = 0;
            MTLPixelFormat sourcePixelFormat = MTLPixelFormatInvalid;
            MTLTextureType sourceTextureType = MTLTextureType2D;
        };

    }// namespace

    struct MetalPMREM::Impl {
        struct OnTextureDispose: EventListener {
            explicit OnTextureDispose(Impl& scope)
                : scope(scope) {}

            void onEvent(Event& event) override {
                auto** texturePtr = std::any_cast<Texture*>(&event.target);
                if (!texturePtr || !*texturePtr) return;

                auto* texture = *texturePtr;
                texture->removeEventListener("dispose", *this);
                scope.deallocateTexture(texture);
            }

            Impl& scope;
        };

        id<MTLDevice> device = nil;
        id<MTLCommandQueue> commandQueue = nil;
        id<MTLComputePipelineState> equirectPipeline = nil;
        id<MTLComputePipelineState> cubePipeline = nil;
        id<MTLSamplerState> sampler = nil;
        OnTextureDispose onTextureDispose{*this};
        std::unordered_map<Texture*, CachedPMREM> cache;

        Impl(id<MTLDevice> dev, id<MTLCommandQueue> queue)
            : device(dev), commandQueue(queue) {}

        ~Impl() {
            for (auto& [texture, cached] : cache) {
                if (texture && texture->hasEventListener("dispose", onTextureDispose)) {
                    texture->removeEventListener("dispose", onTextureDispose);
                }
                releaseOwnedMetalObject(cached.texture);
                cached.texture = nil;
            }
            cache.clear();
            releaseOwnedMetalObject(equirectPipeline);
            releaseOwnedMetalObject(cubePipeline);
            releaseOwnedMetalObject(sampler);
            equirectPipeline = nil;
            cubePipeline = nil;
            sampler = nil;
        }

        id<MTLComputePipelineState> getOrCreatePipeline(MTLTextureType sourceTextureType) {
            const bool cubeSource = sourceTextureType == MTLTextureTypeCube;
            auto& pipeline = cubeSource ? cubePipeline : equirectPipeline;
            if (pipeline) return pipeline;

            NSError* error = nil;
            NSString* source = [NSString stringWithUTF8String:pmremComputeSource];
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                NSString* msg = [NSString stringWithFormat:@"Failed to create Metal PMREM compute library: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            id<MTLFunction> function = [library newFunctionWithName:cubeSource ? @"pmremPrefilterCube" : @"pmremPrefilter"];
            if (!function) {
                releaseOwnedMetalObject(library);
                throw std::runtime_error("Failed to create Metal PMREM compute function");
            }

            pipeline = [device newComputePipelineStateWithFunction:function error:&error];
            releaseOwnedMetalObject(function);
            releaseOwnedMetalObject(library);
            if (!pipeline) {
                NSString* msg = [NSString stringWithFormat:@"Failed to create Metal PMREM compute pipeline: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            return pipeline;
        }

        id<MTLSamplerState> getOrCreateSampler() {
            if (sampler) return sampler;

            MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
            desc.sAddressMode = MTLSamplerAddressModeRepeat;
            desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
            desc.minFilter = MTLSamplerMinMagFilterLinear;
            desc.magFilter = MTLSamplerMinMagFilterLinear;
            desc.mipFilter = MTLSamplerMipFilterLinear;
            desc.lodMinClamp = 0.f;
            desc.lodMaxClamp = 32.f;
            sampler = [device newSamplerStateWithDescriptor:desc];
            releaseOwnedMetalObject(desc);
            if (!sampler) {
                throw std::runtime_error("Failed to create Metal PMREM sampler");
            }
            return sampler;
        }

        id<MTLTexture> createOutputTexture(id<MTLTexture> sourceTexture) {
            constexpr NSUInteger stripWidth = 512;
            constexpr NSUInteger stripHeight = 256;
            constexpr NSUInteger lodCount = 7;
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pmremPixelFormat(sourceTexture.pixelFormat)
                                                                                            width:stripWidth
                                                                                           height:stripHeight * lodCount
                                                                                        mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

            id<MTLTexture> result = [device newTextureWithDescriptor:desc];
            if (!result) {
                throw std::runtime_error("Failed to create Metal PMREM texture");
            }
            return result;
        }

        id<MTLTexture> build(Texture& texture, id<MTLTexture> sourceTexture) {
            if (!sourceTexture) {
                throw std::runtime_error("MetalPMREM requires a source texture");
            }
            if (sourceTexture.textureType != MTLTextureType2D &&
                sourceTexture.textureType != MTLTextureTypeCube) {
                throw std::runtime_error("MetalPMREM only supports 2D equirectangular and cube textures");
            }

            id<MTLTexture> output = createOutputTexture(sourceTexture);
            auto pso = getOrCreatePipeline(sourceTexture.textureType);
            auto sourceSampler = getOrCreateSampler();

            id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
            if (!commandBuffer) {
                releaseOwnedMetalObject(output);
                throw std::runtime_error("MetalPMREM could not create command buffer");
            }

            const auto sourceMaxMip = static_cast<float>(std::max<NSUInteger>(sourceTexture.mipmapLevelCount, 1u) - 1u);
            const auto roughFloorBase = std::max(std::log2(static_cast<float>(std::max<NSUInteger>(sourceTexture.width, 1u))) - 4.f, 0.f);
            const PMREMUniforms uniforms{sourceMaxMip, roughFloorBase, envmap::textureUsesManualCubeDecode(texture) ? 1.f : 0.f, 0.f};

            id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
            [encoder setComputePipelineState:pso];
            [encoder setTexture:sourceTexture atIndex:0];
            [encoder setTexture:output atIndex:1];
            [encoder setSamplerState:sourceSampler atIndex:0];
            [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:0];

            const auto threadWidth = std::max<NSUInteger>(pso.threadExecutionWidth, 1u);
            const auto maxThreads = std::max<NSUInteger>(pso.maxTotalThreadsPerThreadgroup, threadWidth);
            const auto threadHeight = std::max<NSUInteger>(maxThreads / threadWidth, 1u);
            const MTLSize threadsPerThreadgroup = MTLSizeMake(threadWidth, threadHeight, 1);
            const MTLSize threadsPerGrid = MTLSizeMake(output.width, output.height, 1);
            [encoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];

            [encoder endEncoding];
            [commandBuffer commit];

            auto it = cache.find(&texture);
            if (it != cache.end()) {
                releaseOwnedMetalObject(it->second.texture);
                it->second = CachedPMREM{output, texture.version(), sourceTexture.width, sourceTexture.height, sourceTexture.pixelFormat, sourceTexture.textureType};
            } else {
                cache.emplace(&texture, CachedPMREM{output, texture.version(), sourceTexture.width, sourceTexture.height, sourceTexture.pixelFormat, sourceTexture.textureType});
            }

            return output;
        }

        void deallocateTexture(Texture* texture) {
            if (!texture) return;

            auto it = cache.find(texture);
            if (it == cache.end()) return;

            releaseOwnedMetalObject(it->second.texture);
            cache.erase(it);
        }
    };

    MetalPMREM::MetalPMREM(void* device, void* commandQueue)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device,
                                        (__bridge id<MTLCommandQueue>) commandQueue)) {}

    MetalPMREM::~MetalPMREM() = default;

    void* MetalPMREM::getOrCreate(Texture& texture, void* sourceTexture) {
        id<MTLTexture> source = (__bridge id<MTLTexture>) sourceTexture;
        if (!source) {
            throw std::runtime_error("MetalPMREM requires a non-null source texture");
        }

        auto it = pimpl_->cache.find(&texture);
        if (it != pimpl_->cache.end() &&
            it->second.version == texture.version() &&
            it->second.sourceWidth == source.width &&
            it->second.sourceHeight == source.height &&
            it->second.sourcePixelFormat == source.pixelFormat &&
            it->second.sourceTextureType == source.textureType) {
            if (!texture.hasEventListener("dispose", pimpl_->onTextureDispose)) {
                texture.addEventListener("dispose", pimpl_->onTextureDispose);
            }
            return (__bridge void*) it->second.texture;
        }

        auto* result = pimpl_->build(texture, source);
        if (!texture.hasEventListener("dispose", pimpl_->onTextureDispose)) {
            texture.addEventListener("dispose", pimpl_->onTextureDispose);
        }
        return (__bridge void*) result;
    }

    void MetalPMREM::deallocateTexture(Texture* texture) {
        if (texture && texture->hasEventListener("dispose", pimpl_->onTextureDispose)) {
            texture->removeEventListener("dispose", pimpl_->onTextureDispose);
        }
        pimpl_->deallocateTexture(texture);
    }

}// namespace threepp::metal
