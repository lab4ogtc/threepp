#import "MetalRendererImpl.hpp"

#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/renderers/shaders/ShaderCompiler.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/DataArrayTexture.hpp"

#include <spark/tracing.hpp>

#ifdef THREEPP_HAS_SLANG
#include "threepp/renderers/shaders/SlangShaderCompiler.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>

using namespace threepp;

extern "C" void freeMetalEvent(void* event) {
    if (!event) {
        return;
    }
#if __has_feature(objc_arc)
    id<MTLEvent> mtlEvent = (__bridge_transfer id<MTLEvent>) event;
    (void) mtlEvent;
#else
    [(id<MTLEvent>) event release];
#endif
}

namespace {

    constexpr float frameBoundaryThresholdMs = 1.5f;

    constexpr const char* scissorClearShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
};

struct ClearUniforms {
    float4 color;
};

vertex VertexOut scissorClearVertex(uint vertexID [[vertex_id]]) {
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    VertexOut out;
    out.position = float4(positions[vertexID], 1.0, 1.0);
    return out;
}

struct FragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

fragment FragmentOut scissorClearFragment(constant ClearUniforms& uniforms [[buffer(0)]]) {
    FragmentOut out;
    out.color = uniforms.color;
    out.depth = 1.0;
    return out;
}
)metal";

    constexpr const char* lidarUnprojectShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct LidarUnprojectUniforms {
    float matrixWorld[16];
    float farPlane;
    uint width;
    uint height;
};

struct MetalLidarBeamSample {
    uint face;
    uint pixelX;
    uint pixelY;
    uint reserved0;
    float u;
    float v;
    float reserved1;
    float reserved2;
};

struct LidarUnprojectBeamsUniforms {
    float matrixWorld[96];
    float farPlane;
    uint beamCount;
    uint2 padding;
};

kernel void lidarUnprojectDense(texture2d<float, access::read> packedDepth [[texture(0)]],
                                device float4* points [[buffer(0)]],
                                constant LidarUnprojectUniforms& uniforms [[buffer(1)]],
                                uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= uniforms.width || gid.y >= uniforms.height) return;

    const uint index = gid.y * uniforms.width + gid.x;
    const float2 rg = packedDepth.read(gid).rg;
    const float normalizedDepth = rg.x + rg.y * (1.0 / 255.0);
    if (normalizedDepth >= 0.9999) {
        points[index] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float depth = normalizedDepth * uniforms.farPlane;
    const float xd = (float(gid.x) + 0.5) / float(uniforms.width) * 2.0 - 1.0;
    const float yd = (float(gid.y) + 0.5) / float(uniforms.height) * 2.0 - 1.0;
    constant float* m = uniforms.matrixWorld;

    points[index] = float4(
        (m[0] * xd + m[4] * yd - m[8]) * depth + m[12],
        (m[1] * xd + m[5] * yd - m[9]) * depth + m[13],
        (m[2] * xd + m[6] * yd - m[10]) * depth + m[14],
        1.0);
}

kernel void lidarUnprojectBeams(texture2d<float, access::read> face0 [[texture(0)]],
                                texture2d<float, access::read> face1 [[texture(1)]],
                                texture2d<float, access::read> face2 [[texture(2)]],
                                texture2d<float, access::read> face3 [[texture(3)]],
                                texture2d<float, access::read> face4 [[texture(4)]],
                                texture2d<float, access::read> face5 [[texture(5)]],
                                device const MetalLidarBeamSample* beams [[buffer(0)]],
                                device float4* points [[buffer(1)]],
                                constant LidarUnprojectBeamsUniforms& uniforms [[buffer(2)]],
                                uint gid [[thread_position_in_grid]]) {
    if (gid >= uniforms.beamCount) return;

    const MetalLidarBeamSample sample = beams[gid];
    const uint2 pixel = uint2(sample.pixelX, sample.pixelY);
    float2 rg;
    switch (sample.face) {
        case 0:
            if (pixel.x >= face0.get_width() || pixel.y >= face0.get_height()) {
                points[gid] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }
            rg = face0.read(pixel).rg;
            break;
        case 1:
            if (pixel.x >= face1.get_width() || pixel.y >= face1.get_height()) {
                points[gid] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }
            rg = face1.read(pixel).rg;
            break;
        case 2:
            if (pixel.x >= face2.get_width() || pixel.y >= face2.get_height()) {
                points[gid] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }
            rg = face2.read(pixel).rg;
            break;
        case 3:
            if (pixel.x >= face3.get_width() || pixel.y >= face3.get_height()) {
                points[gid] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }
            rg = face3.read(pixel).rg;
            break;
        case 4:
            if (pixel.x >= face4.get_width() || pixel.y >= face4.get_height()) {
                points[gid] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }
            rg = face4.read(pixel).rg;
            break;
        case 5:
            if (pixel.x >= face5.get_width() || pixel.y >= face5.get_height()) {
                points[gid] = float4(0.0, 0.0, 0.0, 0.0);
                return;
            }
            rg = face5.read(pixel).rg;
            break;
        default:
            points[gid] = float4(0.0, 0.0, 0.0, 0.0);
            return;
    }

    const float normalizedDepth = rg.x + rg.y * (1.0 / 255.0);
    if (normalizedDepth >= 0.9999) {
        points[gid] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float depth = normalizedDepth * uniforms.farPlane;
    constant float* m = uniforms.matrixWorld + sample.face * 16;
    points[gid] = float4(
        (m[0] * sample.u + m[4] * sample.v - m[8]) * depth + m[12],
        (m[1] * sample.u + m[5] * sample.v - m[9]) * depth + m[13],
        (m[2] * sample.u + m[6] * sample.v - m[10]) * depth + m[14],
        1.0);
}
)metal";

    struct alignas(16) ScissorClearUniforms {
        float color[4];
    };

    struct alignas(16) LidarUnprojectUniforms {
        float matrixWorld[16];
        float farPlane;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t padding;
    };

    struct alignas(16) LidarUnprojectBeamsUniforms {
        float matrixWorld[6][16];
        float farPlane;
        std::uint32_t beamCount;
        std::uint32_t padding[2];
    };

    static_assert(sizeof(LidarUnprojectBeamsUniforms) == 400);

    unsigned int textureFormatChannelCount(Format format) {
        switch (format) {
            case Format::Red:
            case Format::RedInteger:
                return 1u;
            case Format::RG:
            case Format::RGInteger:
                return 2u;
            case Format::RGB:
            case Format::RGBInteger:
                return 3u;
            case Format::RGBA:
            case Format::RGBAInteger:
            case Format::BGRA:
                return 4u;
            default:
                throw std::runtime_error("MetalRenderer::copyTextureToImage supports only Red, RG, RGB, RGBA, BGRA, and integer variants");
        }
    }

    NSUInteger pixelFormatBytesPerPixel(MTLPixelFormat pixelFormat) {
        switch (pixelFormat) {
            case MTLPixelFormatR8Unorm:
                return 1u;
            case MTLPixelFormatR16Float:
                return 2u;
            case MTLPixelFormatRG8Unorm:
                return 2u;
            case MTLPixelFormatRG16Float:
                return 4u;
            case MTLPixelFormatRGBA8Unorm:
            case MTLPixelFormatRGBA8Unorm_sRGB:
            case MTLPixelFormatBGRA8Unorm:
            case MTLPixelFormatBGRA8Unorm_sRGB:
            case MTLPixelFormatR32Float:
            case MTLPixelFormatR32Uint:
                return 4u;
            case MTLPixelFormatRGBA16Float:
                return 8u;
            case MTLPixelFormatRG32Float:
            case MTLPixelFormatRG32Uint:
                return 8u;
            case MTLPixelFormatRGBA32Float:
            case MTLPixelFormatRGBA32Uint:
                return 16u;
            default:
                throw std::runtime_error("MetalRenderer::copyTextureToImage encountered an unsupported Metal pixel format");
        }
    }

    bool pixelFormatIsFloat(MTLPixelFormat pixelFormat) {
        switch (pixelFormat) {
            case MTLPixelFormatR32Float:
            case MTLPixelFormatRG32Float:
            case MTLPixelFormatRGBA32Float:
                return true;
            default:
                return false;
        }
    }

    bool isZeroCopyCompatiblePixelFormat(MTLPixelFormat pixelFormat) {
        switch (pixelFormat) {
            case MTLPixelFormatR8Unorm:
            case MTLPixelFormatRG8Unorm:
            case MTLPixelFormatRGBA8Unorm:
            case MTLPixelFormatBGRA8Unorm:
            case MTLPixelFormatR16Float:
            case MTLPixelFormatRG16Float:
            case MTLPixelFormatRGBA16Float:
            case MTLPixelFormatR32Float:
            case MTLPixelFormatRG32Float:
            case MTLPixelFormatRGBA32Float:
                return true;
            default:
                return false;
        }
    }

    bool pixelFormatIsInteger(MTLPixelFormat pixelFormat) {
        switch (pixelFormat) {
            case MTLPixelFormatR32Uint:
            case MTLPixelFormatRG32Uint:
            case MTLPixelFormatRGBA32Uint:
                return true;
            default:
                return false;
        }
    }

    NSUInteger alignTo(NSUInteger value, NSUInteger alignment) {
        if (alignment <= 1u) return value;
        const auto remainder = value % alignment;
        return remainder == 0u ? value : value + alignment - remainder;
    }

    bool canUseFastReadbackPath(const Texture& texture, MTLPixelFormat pixelFormat) {
        const auto textureFormatIsFastPathEligible = texture.format != Format::BGRA;
        if (!textureFormatIsFastPathEligible) return false;

        if (texture.type == Type::UnsignedByte) {
            switch (texture.format) {
                case Format::Red:
                    return pixelFormat == MTLPixelFormatR8Unorm;
                case Format::RG:
                    return pixelFormat == MTLPixelFormatRG8Unorm;
                case Format::RGBA:
                    return pixelFormat == MTLPixelFormatRGBA8Unorm ||
                           pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB;
                default:
                    return false;
            }
        }

        if (texture.type == Type::Float) {
            switch (texture.format) {
                case Format::Red:
                    return pixelFormat == MTLPixelFormatR32Float;
                case Format::RG:
                    return pixelFormat == MTLPixelFormatRG32Float;
                case Format::RGBA:
                    return pixelFormat == MTLPixelFormatRGBA32Float;
                default:
                    return false;
            }
        }

        if (texture.type == Type::UnsignedInt) {
            switch (texture.format) {
                case Format::Red:
                case Format::RedInteger:
                    return pixelFormat == MTLPixelFormatR32Uint;
                case Format::RG:
                case Format::RGInteger:
                    return pixelFormat == MTLPixelFormatRG32Uint;
                case Format::RGBA:
                case Format::RGBAInteger:
                    return pixelFormat == MTLPixelFormatRGBA32Uint;
                default:
                    return false;
            }
        }

        return false;
    }

    bool canExposeRawReadbackLayout(const Texture& texture, MTLPixelFormat pixelFormat) {
        switch (texture.type) {
            case Type::UnsignedByte:
                switch (texture.format) {
                    case Format::Red:
                        return pixelFormat == MTLPixelFormatR8Unorm;
                    case Format::RG:
                        return pixelFormat == MTLPixelFormatRG8Unorm;
                    case Format::RGBA:
                        return pixelFormat == MTLPixelFormatRGBA8Unorm ||
                               pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB;
                    case Format::BGRA:
                        return pixelFormat == MTLPixelFormatBGRA8Unorm ||
                               pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB;
                    default:
                        return false;
                }
            case Type::HalfFloat:
                switch (texture.format) {
                    case Format::Red:
                        return pixelFormat == MTLPixelFormatR16Float;
                    case Format::RG:
                        return pixelFormat == MTLPixelFormatRG16Float;
                    case Format::RGBA:
                        return pixelFormat == MTLPixelFormatRGBA16Float;
                    default:
                        return false;
                }
            case Type::Float:
                switch (texture.format) {
                    case Format::Red:
                        return pixelFormat == MTLPixelFormatR32Float;
                    case Format::RG:
                        return pixelFormat == MTLPixelFormatRG32Float;
                    case Format::RGBA:
                        return pixelFormat == MTLPixelFormatRGBA32Float;
                    default:
                        return false;
                }
            default:
                return false;
        }
    }

    unsigned char readByteComponent(const unsigned char* pixel, MTLPixelFormat pixelFormat, unsigned int channel) {
        switch (pixelFormat) {
            case MTLPixelFormatR8Unorm:
                return channel == 0u ? pixel[0] : (channel == 3u ? 255u : 0u);
            case MTLPixelFormatRG8Unorm:
                return channel < 2u ? pixel[channel] : (channel == 3u ? 255u : 0u);
            case MTLPixelFormatRGBA8Unorm:
            case MTLPixelFormatRGBA8Unorm_sRGB:
                return channel < 4u ? pixel[channel] : 0u;
            case MTLPixelFormatBGRA8Unorm:
            case MTLPixelFormatBGRA8Unorm_sRGB:
                if (channel == 0u) return pixel[2];
                if (channel == 1u) return pixel[1];
                if (channel == 2u) return pixel[0];
                return pixel[3];
            default:
                return channel == 3u ? 255u : 0u;
        }
    }

    float readFloatComponent(const float* pixel, MTLPixelFormat pixelFormat, unsigned int channel) {
        switch (pixelFormat) {
            case MTLPixelFormatR32Float:
                return channel == 0u ? pixel[0] : (channel == 3u ? 1.f : 0.f);
            case MTLPixelFormatRG32Float:
                return channel < 2u ? pixel[channel] : (channel == 3u ? 1.f : 0.f);
            case MTLPixelFormatRGBA32Float:
                return channel < 4u ? pixel[channel] : 0.f;
            default:
                return channel == 3u ? 1.f : 0.f;
        }
    }

    std::uint32_t readUintComponent(const std::uint32_t* pixel, MTLPixelFormat pixelFormat, unsigned int channel) {
        switch (pixelFormat) {
            case MTLPixelFormatR32Uint:
                return channel == 0u ? pixel[0] : (channel == 3u ? 1u : 0u);
            case MTLPixelFormatRG32Uint:
                return channel < 2u ? pixel[channel] : (channel == 3u ? 1u : 0u);
            case MTLPixelFormatRGBA32Uint:
                return channel < 4u ? pixel[channel] : 0u;
            default:
                return channel == 3u ? 1u : 0u;
        }
    }

    unsigned int destinationCanonicalChannel(Format format, unsigned int destinationChannel) {
        if (format == Format::BGRA) {
            static constexpr unsigned int bgraToRgba[]{2u, 1u, 0u, 3u};
            return bgraToRgba[destinationChannel];
        }
        return destinationChannel;
    }

    id<MTLDepthStencilState> createScissorClearDepthStencilState(id<MTLDevice> device, bool clearDepth) {
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = clearDepth ? YES : NO;
        return [device newDepthStencilStateWithDescriptor:desc];
    }

    void configurePipelineBlending(metal::PipelineKey& key, const Material& material) {
        key.alphaBlending = material.blending != Blending::None &&
                            (material.blending != Blending::Normal || material.transparent || material.opacity < 1.f);
        key.blending = key.alphaBlending ? material.blending : Blending::Normal;
        key.blendEquation = BlendEquation::Add;
        key.blendEquationAlpha = BlendEquation::Add;
        key.blendSrc = BlendFactor::SrcAlpha;
        key.blendDst = BlendFactor::OneMinusSrcAlpha;
        key.blendSrcAlpha = BlendFactor::One;
        key.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;

        if (!key.alphaBlending) return;

        if (material.blending == Blending::Custom) {
            key.blendEquation = material.blendEquation;
            key.blendEquationAlpha = material.blendEquationAlpha.value_or(material.blendEquation);
            key.blendSrc = material.blendSrc;
            key.blendDst = material.blendDst;
            key.blendSrcAlpha = material.blendSrcAlpha.value_or(material.blendSrc);
            key.blendDstAlpha = material.blendDstAlpha.value_or(material.blendDst);
            return;
        }

        if (material.premultipliedAlpha) {
            switch (material.blending) {
                case Blending::Normal:
                    key.blendSrc = BlendFactor::One;
                    key.blendDst = BlendFactor::OneMinusSrcAlpha;
                    key.blendSrcAlpha = BlendFactor::One;
                    key.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
                    break;
                case Blending::Additive:
                    key.blendSrc = BlendFactor::One;
                    key.blendDst = BlendFactor::One;
                    key.blendSrcAlpha = BlendFactor::One;
                    key.blendDstAlpha = BlendFactor::One;
                    break;
                case Blending::Subtractive:
                    key.blendSrc = BlendFactor::Zero;
                    key.blendDst = BlendFactor::OneMinusSrcColor;
                    key.blendSrcAlpha = BlendFactor::Zero;
                    key.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
                    break;
                case Blending::Multiply:
                    key.blendSrc = BlendFactor::Zero;
                    key.blendDst = BlendFactor::SrcColor;
                    key.blendSrcAlpha = BlendFactor::Zero;
                    key.blendDstAlpha = BlendFactor::SrcAlpha;
                    break;
                case Blending::None:
                case Blending::Custom:
                    break;
            }
            return;
        }

        switch (material.blending) {
            case Blending::Normal:
                break;
            case Blending::Additive:
                key.blendSrc = BlendFactor::SrcAlpha;
                key.blendDst = BlendFactor::One;
                key.blendSrcAlpha = BlendFactor::SrcAlpha;
                key.blendDstAlpha = BlendFactor::One;
                break;
            case Blending::Subtractive:
                key.blendSrc = BlendFactor::Zero;
                key.blendDst = BlendFactor::OneMinusSrcColor;
                key.blendSrcAlpha = BlendFactor::Zero;
                key.blendDstAlpha = BlendFactor::OneMinusSrcColor;
                break;
            case Blending::Multiply:
                key.blendSrc = BlendFactor::Zero;
                key.blendDst = BlendFactor::SrcColor;
                key.blendSrcAlpha = BlendFactor::Zero;
                key.blendDstAlpha = BlendFactor::SrcColor;
                break;
            case Blending::None:
            case Blending::Custom:
                break;
        }
    }

    constexpr const char* recursiveRenderCallbackMessage = "MetalRenderer: Recursive render calls inside onBeforeRender or onAfterRender callbacks are not supported.";
    constexpr const char* rendererCallbackOperationMessage = "MetalRenderer: Renderer command buffer operations inside onBeforeRender or onAfterRender callbacks are not supported.";

    thread_local bool inRendererCallback = false;

    void throwIfRendererCallbackOperation(const char* message) {
        if (inRendererCallback) {
            throw std::runtime_error(message);
        }
    }

    class RenderCallbackScope {

    public:
        RenderCallbackScope()
            : previous_(inRendererCallback) {
            inRendererCallback = true;
        }

        ~RenderCallbackScope() {
            inRendererCallback = previous_;
        }

    private:
        bool previous_;
    };

    void invokeRenderCallback(const RenderCallback& callback, MetalRenderer& renderer, Scene& scene, Camera& camera, BufferGeometry* geometry, Material* material, std::optional<GeometryGroup> group) {
        RenderCallbackScope scope;
        callback(static_cast<Renderer*>(&renderer), &scene, &camera, geometry, material, group);
    }

    void invokeOnBeforeRender(Object3D& obj, MetalRenderer& renderer, Scene& scene, Camera& camera, BufferGeometry* geometry, Material* material, std::optional<GeometryGroup> group) {
        if (obj.onBeforeRender) {
            invokeRenderCallback(obj.onBeforeRender.value(), renderer, scene, camera, geometry, material, group);
        }
    }

    void invokeOnAfterRender(Object3D& obj, MetalRenderer& renderer, Scene& scene, Camera& camera, BufferGeometry* geometry, Material* material, std::optional<GeometryGroup> group) {
        if (obj.onAfterRender) {
            invokeRenderCallback(obj.onAfterRender.value(), renderer, scene, camera, geometry, material, group);
        }
    }

    int maxMipmapLevel(unsigned int width, unsigned int height) {
        auto size = std::max(width, height);
        int level = 0;
        while (size > 1) {
            size /= 2;
            ++level;
        }
        return level;
    }

    void validateRenderTargetSelection(RenderTarget* renderTarget, int activeCubeFace, int activeMipmapLevel, int activeLayer) {
        if (activeCubeFace < 0 || activeMipmapLevel < 0 || activeLayer < 0) {
            throw std::invalid_argument("MetalRenderer::setRenderTarget requires non-negative face, mip level, and layer");
        }

        if (!renderTarget) {
            if (activeCubeFace != 0 || activeMipmapLevel != 0 || activeLayer != 0) {
                throw std::invalid_argument("MetalRenderer::setRenderTarget(nullptr) requires zero face, mip level, and layer");
            }
            return;
        }

        const auto isCubeRenderTarget = dynamic_cast<CubeTexture*>(renderTarget->texture.get()) != nullptr;
        if (isCubeRenderTarget) {
            if (activeCubeFace >= 6) {
                throw std::out_of_range("MetalRenderer::setRenderTarget cube face must be in [0, 5]");
            }
            if (activeLayer != 0) {
                throw std::invalid_argument("MetalRenderer::setRenderTarget cube targets require activeLayer == 0");
            }
        } else {
            if (activeCubeFace != 0) {
                throw std::invalid_argument("MetalRenderer::setRenderTarget non-cube targets require activeCubeFace == 0");
            }
            if (renderTarget->depth > 1) {
                if (activeLayer >= static_cast<int>(renderTarget->depth)) {
                    throw std::out_of_range("MetalRenderer::setRenderTarget activeLayer exceeds render target depth");
                }
            } else if (activeLayer != 0) {
                throw std::invalid_argument("MetalRenderer::setRenderTarget 2D targets require activeLayer == 0");
            }
        }

        const auto maxLevel = renderTarget->texture->generateMipmaps ? maxMipmapLevel(renderTarget->width, renderTarget->height) : 0;
        if (activeMipmapLevel > maxLevel) {
            throw std::out_of_range("MetalRenderer::setRenderTarget activeMipmapLevel exceeds allocated render target mip levels");
        }
    }

    void updateTextureImageMetadata(Texture& texture, unsigned int width, unsigned int height, unsigned int depth, bool cubeTexture) {
        auto& images = texture.images();
        if (cubeTexture) {
            while (images.size() < 6) {
                images.emplace_back(Image({}, width, height));
            }
            for (auto& image : images) {
                image.width = width;
                image.height = height;
                image.depth = 0;
            }
            return;
        }

        if (images.empty()) {
            images.emplace_back(Image({}, width, height, depth));
        } else {
            images.front().width = width;
            images.front().height = height;
            images.front().depth = depth;
        }
    }

    struct RenderCommandEncoderScope {
        id<MTLRenderCommandEncoder> encoder = nil;
        bool ended = false;

        explicit RenderCommandEncoderScope(id<MTLRenderCommandEncoder> renderEncoder)
            : encoder(renderEncoder) {}

        ~RenderCommandEncoderScope() {
            end();
        }

        void end() {
            if (encoder && !ended) {
                [encoder endEncoding];
                ended = true;
            }
        }
    };

}// namespace

void MetalRenderer::Impl::OnRenderTargetDispose::onEvent(Event& event) {
    RenderTarget* target = nullptr;
    if (auto** renderTargetPtr = std::any_cast<RenderTarget*>(&event.target)) {
        target = *renderTargetPtr;
    }
    if (!target) return;

    target->removeEventListener("dispose", *this);
    scope.deallocateRenderTarget(target);
}

void MetalRenderer::Impl::OnGeometryDispose::onEvent(Event& event) {
    auto** geometryPtr = std::any_cast<BufferGeometry*>(&event.target);
    if (!geometryPtr || !*geometryPtr) return;

    auto* geometry = *geometryPtr;
    geometry->removeEventListener("dispose", *this);
    scope.deallocateGeometry(*geometry);
}

MetalRenderer::Impl::Impl(MetalRenderer& r, Window& w)
    : renderer(r),
      window(w),
      onRenderTargetDispose(*this),
      onGeometryDispose(*this) {

    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
    NSWindow* nsWindow = glfwGetCocoaWindow(glfwWin);
    NSView* contentView = [nsWindow contentView];

    device = MTLCreateSystemDefaultDevice();
    if (!device) {
        throw std::runtime_error("Metal is not supported on this device");
    }
    drawableSampleCount = selectSupportedSampleCount(device, requestedAntialiasingSamples(window));

    commandQueue = [device newCommandQueue];
    commandQueue.label = @"threepp.main";
    // 公开 Metal SDK 未暴露 GPU queue priority；使用独立队列承载后台工作并通过事件同步。
    lowPriorityCommandQueue = [device newCommandQueue];
    lowPriorityCommandQueue.label = @"threepp.low-priority";
    inFlightSemaphore = dispatch_semaphore_create(3);

    metalLayer = [CAMetalLayer layer];
    metalLayer.device = device;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.maximumDrawableCount = 3;
    metalLayer.displaySyncEnabled = window.vsync() ? YES : NO;
    metalLayer.framebufferOnly = NO;
    metalLayer.frame = contentView.bounds;
    metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    metalLayer.opaque = YES;

    [contentView setWantsLayer:YES];
    [contentView setLayer:metalLayer];

    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
    updatePixelRatio(window.size());
    metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
    metalLayer.contentsScale = pixelRatio;

    createDepthTexture();

    pipelineCache = std::make_unique<metal::MetalPipelineCache>((__bridge void*) device);
    bufferManager = std::make_unique<metal::MetalBufferManager>((__bridge void*) device);
    shaderManager = std::make_unique<metal::MetalShaderManager>((__bridge void*) device);
    textureManager = std::make_unique<metal::MetalTextureManager>((__bridge void*) device, (__bridge void*) commandQueue);
    morphTargets = std::make_unique<metal::MetalMorphTargets>();
#ifdef THREEPP_HAS_SLANG
    try {
        shaderCompiler = std::make_unique<SlangShaderCompiler>();
    } catch (const std::exception& e) {
        std::cerr << "MetalRenderer: SlangShaderCompiler failed to initialize: "
                  << e.what()
                  << ". Slang materials will be disabled.\n";
        shaderCompiler = nullptr;
    }
#endif
    dynamicShaderCache = std::make_unique<metal::MetalDynamicShaderCache>((__bridge void*) device);
    dynamicShaderCache->setEvictFunctionCallback([this](void* function) {
        if (pipelineCache) {
            pipelineCache->removePipelineStatesReferencing(function);
        }
    });

    depthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState();
    createPlaceholderResources();

    setViewport(0, 0, window.size().width(), window.size().height());
    setScissor(0, 0, window.size().width(), window.size().height());
}

MetalRenderer::Impl::~Impl() {
    commitPendingFrame();
    if (lowPriorityCommandQueue) {
        id<MTLCommandBuffer> lowPrioritySyncBuffer = [lowPriorityCommandQueue commandBuffer];
        [lowPrioritySyncBuffer commit];
        [lowPrioritySyncBuffer waitUntilCompleted];
    }
    // 提交空命令缓冲区并等待，借助 Metal FIFO 保证前序 GPU 工作完成后再释放资源。
    id<MTLCommandBuffer> syncBuffer = [commandQueue commandBuffer];
    [syncBuffer commit];
    [syncBuffer waitUntilCompleted];

    for (auto& [target, _] : renderTargetResources) {
        target->removeEventListener("dispose", onRenderTargetDispose);
    }
    for (auto& [geometry, _] : geometries) {
        geometry->removeEventListener("dispose", onGeometryDispose);
    }
    backgroundCubeGeometry.reset();
    for (auto& readbackBuffer : readbackBufferPool) {
        readbackBuffer.buffer = nil;
        readbackBuffer.size = 0;
        readbackBuffer.inUse = false;
    }
    readbackBufferPool.clear();
}

void MetalRenderer::Impl::removeAttribute(BufferAttribute* attribute) {
    if (!attribute) return;

    bufferManager->remove(*attribute);
    convertedSkinIndexBuffers.erase(attribute);
}

void MetalRenderer::Impl::deallocateGeometry(BufferGeometry& geometry) {
    removeAttribute(geometry.getIndex());

    for (const auto& [_, attribute] : geometry.getAttributes()) {
        removeAttribute(attribute.get());
    }
    for (const auto& [_, attributes] : geometry.getMorphAttributes()) {
        for (const auto& attribute : attributes) {
            removeAttribute(attribute.get());
        }
    }

    geometries.erase(&geometry);
    if (morphTargets) {
        morphTargets->removeGeometry(geometry.id);
    }
}

void MetalRenderer::Impl::trackGeometry(BufferGeometry& geometry) {
    if (geometries.contains(&geometry)) return;

    geometry.addEventListener("dispose", onGeometryDispose);
    geometries[&geometry] = true;
}

void MetalRenderer::Impl::commitPendingFrame() {
    SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::commitPendingFrame");
    if (!currentCommandBuffer) return;

    if (currentDrawable) {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::presentDrawable");
        [currentCommandBuffer presentDrawable:currentDrawable];
    }
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::commitCommandBuffer");
        [currentCommandBuffer commit];
    }
    currentCommandBuffer = nil;
    currentDrawable = nil;
    explicitFrameInProgress = false;
    screenCommandsEncoded = false;
    lastFrameWasExternallyAccessed = currentCommandBufferExternallyAccessed;
    currentCommandBufferExternallyAccessed = false;
}

void MetalRenderer::Impl::ensureFrameStarted() {
    SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::ensureFrameStarted");
    if (currentCommandBuffer) return;

    if (useLowPriorityQueue) {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::beginLowPriorityFrame");
        bufferManager->beginFrame();
        currentCommandBuffer = [lowPriorityCommandQueue commandBuffer];
        clearedTargetsInFrame.clear();
        return;
    }

    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::waitInFlightSemaphore");
        dispatch_semaphore_wait(inFlightSemaphore, DISPATCH_TIME_FOREVER);
    }
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::beginFrameBuffers");
        bufferManager->beginFrame();
    }

    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::createCommandBuffer");
        currentCommandBuffer = [commandQueue commandBuffer];
    }
    clearedTargetsInFrame.clear();
    auto semaphore = inFlightSemaphore;
    [currentCommandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> commandBuffer) {
      dispatch_semaphore_signal(semaphore);
    }];
}

void MetalRenderer::Impl::submitLowPriority() {
    if (!currentCommandBuffer) {
        return;
    }
    [currentCommandBuffer commit];
    currentCommandBuffer = nil;
    currentDrawable = nil;
    explicitFrameInProgress = false;
    screenCommandsEncoded = false;
    lastFrameWasExternallyAccessed = currentCommandBufferExternallyAccessed;
    currentCommandBufferExternallyAccessed = false;
}

void* MetalRenderer::Impl::createEvent() {
    id<MTLEvent> event = [device newSharedEvent];
#if __has_feature(objc_arc)
    return (__bridge_retained void*) event;
#else
    return event;
#endif
}

void MetalRenderer::Impl::encodeSignalEvent(void* event, std::uint64_t value) {
    if (!event) {
        return;
    }
    ensureFrameStarted();
    if (!currentCommandBuffer) {
        return;
    }
    id<MTLEvent> mtlEvent = (__bridge id<MTLEvent>) event;
    [currentCommandBuffer encodeSignalEvent:mtlEvent value:value];
}

void MetalRenderer::Impl::encodeWaitEventOnCurrentFrame(void* event, std::uint64_t value) {
    if (!event) {
        return;
    }
    ensureFrameStarted();
    if (!currentCommandBuffer) {
        return;
    }
    id<MTLEvent> mtlEvent = (__bridge id<MTLEvent>) event;
    [currentCommandBuffer encodeWaitForEvent:mtlEvent value:value];
}

bool MetalRenderer::Impl::ensureDrawable() {
    SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::ensureDrawable");
    if (currentDrawable) {
        syncDrawableSize(currentDrawable.texture.width, currentDrawable.texture.height);
        return true;
    }

    updateMetalLayerPixelFormat();
    const auto drawableWaitStart = std::chrono::steady_clock::now();
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::nextDrawable");
        currentDrawable = [metalLayer nextDrawable];
    }
    const auto drawableWaitUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - drawableWaitStart).count();
    SPARK_TRACE_COUNTER("threepp.metal", "MetalRenderer.nextDrawableWaitUs",
                        static_cast<std::int64_t>(drawableWaitUs));
    if (currentDrawable) {
        syncDrawableSize(currentDrawable.texture.width, currentDrawable.texture.height);
    }
    return currentDrawable != nil;
}

void MetalRenderer::Impl::updateMetalLayerPixelFormat() {
    if (renderTarget) return;

    const auto targetPixelFormat = usesSRGBColorEncoding(renderer.outputEncoding)
                                           ? MTLPixelFormatBGRA8Unorm_sRGB
                                           : MTLPixelFormatBGRA8Unorm;
    if (metalLayer.pixelFormat == targetPixelFormat) return;

    metalLayer.pixelFormat = targetPixelFormat;
    multisampleColorPixelFormat = MTLPixelFormatInvalid;
    multisampleColorTexture = nil;
}

void MetalRenderer::Impl::syncDrawableSize(NSUInteger width, NSUInteger height) {
    if (renderTarget || width == 0 || height == 0) return;

    const auto clampedWidth = std::min<NSUInteger>(width, static_cast<NSUInteger>(std::numeric_limits<int>::max()));
    const auto clampedHeight = std::min<NSUInteger>(height, static_cast<NSUInteger>(std::numeric_limits<int>::max()));
    const auto nextFbWidth = static_cast<int>(clampedWidth);
    const auto nextFbHeight = static_cast<int>(clampedHeight);
    const auto logicalSize = window.size();

    float nextPixelRatio = 1;
    if (logicalSize.width() > 0) {
        nextPixelRatio = static_cast<float>(nextFbWidth) / static_cast<float>(logicalSize.width());
    } else if (logicalSize.height() > 0) {
        nextPixelRatio = static_cast<float>(nextFbHeight) / static_cast<float>(logicalSize.height());
    }

    const auto framebufferChanged = fbWidth != nextFbWidth || fbHeight != nextFbHeight;
    const auto layerChanged = metalLayer.contentsScale != nextPixelRatio ||
                              metalLayer.drawableSize.width != static_cast<CGFloat>(nextFbWidth) ||
                              metalLayer.drawableSize.height != static_cast<CGFloat>(nextFbHeight);
    if (!framebufferChanged && pixelRatio == nextPixelRatio && !layerChanged) return;

    fbWidth = nextFbWidth;
    fbHeight = nextFbHeight;
    pixelRatio = nextPixelRatio;
    metalLayer.contentsScale = pixelRatio;
    metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);

    if (framebufferChanged) {
        multisampleColorTexture = nil;
        multisampleColorPixelFormat = MTLPixelFormatInvalid;
        createDepthTexture();
    }
}

void MetalRenderer::Impl::updatePixelRatio(const WindowSize& size) {
    if (size.width() > 0) {
        pixelRatio = static_cast<float>(fbWidth) / static_cast<float>(size.width());
    } else {
        pixelRatio = 1;
    }
}

void MetalRenderer::Impl::createDepthTexture() {
    if (depthTexture) {
        depthTexture = nil;
    }

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depthPixelFormat
                                                                                    width:std::max(fbWidth, 1)
                                                                                   height:std::max(fbHeight, 1)
                                                                                mipmapped:NO];
    desc.textureType = drawableSampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    desc.sampleCount = drawableSampleCount;
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    depthTexture = [device newTextureWithDescriptor:desc];
}

id<MTLTexture> MetalRenderer::Impl::getOrCreateMultisampleColorTexture(MTLPixelFormat pixelFormat) {
    if (drawableSampleCount <= 1) return nil;
    if (multisampleColorTexture &&
        multisampleColorTexture.width == static_cast<NSUInteger>(std::max(fbWidth, 1)) &&
        multisampleColorTexture.height == static_cast<NSUInteger>(std::max(fbHeight, 1)) &&
        multisampleColorPixelFormat == pixelFormat) {
        return multisampleColorTexture;
    }

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                    width:std::max(fbWidth, 1)
                                                                                   height:std::max(fbHeight, 1)
                                                                                mipmapped:NO];
    desc.textureType = MTLTextureType2DMultisample;
    desc.sampleCount = drawableSampleCount;
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    multisampleColorTexture = [device newTextureWithDescriptor:desc];
    multisampleColorPixelFormat = pixelFormat;
    return multisampleColorTexture;
}

id<MTLTexture> MetalRenderer::Impl::createSolidTexture2D(std::array<unsigned char, 4> rgba) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                    width:1
                                                                                   height:1
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0
                 withBytes:rgba.data()
               bytesPerRow:4];
    return texture;
}

id<MTLTexture> MetalRenderer::Impl::createSolidCubeTexture(std::array<unsigned char, 4> rgba) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                       size:1
                                                                                  mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    for (NSUInteger face = 0; face < 6; ++face) {
        [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                   mipmapLevel:0
                         slice:face
                     withBytes:rgba.data()
                   bytesPerRow:4
                 bytesPerImage:4];
    }
    return texture;
}

id<MTLTexture> MetalRenderer::Impl::createDepthTexture(NSUInteger width, NSUInteger height, bool mipmapped) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                    width:std::max<NSUInteger>(width, 1)
                                                                                   height:std::max<NSUInteger>(height, 1)
                                                                                mipmapped:mipmapped ? YES : NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    return [device newTextureWithDescriptor:desc];
}

bool renderTargetUsesArrayTexture(RenderTarget& target) {
    return target.depth > 1 || dynamic_cast<DataArrayTexture*>(target.texture.get()) != nullptr;
}

MTLTextureType renderTargetTextureType(RenderTarget& target, Texture& texture) {
    if (dynamic_cast<CubeTexture*>(&texture)) {
        return MTLTextureTypeCube;
    }
    if (dynamic_cast<DataArrayTexture*>(&texture) || renderTargetUsesArrayTexture(target)) {
        return MTLTextureType2DArray;
    }
    return MTLTextureType2D;
}

MetalRenderer::Impl::RenderTargetColorTextureAllocation MetalRenderer::Impl::createRenderTargetColorTexture(RenderTarget& target, Texture& texture, MTLPixelFormat pixelFormat) const {
    const auto width = std::max<NSUInteger>(target.width, 1);
    const auto height = std::max<NSUInteger>(target.height, 1);
    const auto depth = std::max<NSUInteger>(target.depth, 1);
    const auto mipmapped = texture.generateMipmaps ? YES : NO;
    const auto textureType = renderTargetTextureType(target, texture);
    MTLTextureDescriptor* desc = nil;
    RenderTargetColorTextureAllocation allocation;

    if (textureType == MTLTextureTypeCube) {
        if (width != height) {
            throw std::runtime_error("Metal cube RenderTarget requires square dimensions");
        }
        desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:pixelFormat
                                                                     size:width
                                                                mipmapped:mipmapped];
    } else if (textureType == MTLTextureType2DArray) {
        desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                  width:width
                                                                 height:height
                                                              mipmapped:mipmapped];
        desc.textureType = MTLTextureType2DArray;
        desc.arrayLength = depth;
    } else {
        desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                  width:width
                                                                   height:height
                                                                mipmapped:mipmapped];
    }
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;

    const auto canUseZeroCopy = target.zeroCopy &&
                                &texture == target.texture.get() &&
                                desc.textureType == MTLTextureType2D &&
                                !mipmapped &&
                                isZeroCopyCompatiblePixelFormat(pixelFormat);
    if (canUseZeroCopy) {
        const auto bytesPerPixel = pixelFormatBytesPerPixel(pixelFormat);
        const auto rawBytesPerRow = width * bytesPerPixel;
        const auto minimumAlignment = [device minimumLinearTextureAlignmentForPixelFormat:pixelFormat];
        const auto alignment = std::max<NSUInteger>(minimumAlignment, 256u);
        const auto alignedBytesPerRow = alignTo(rawBytesPerRow, alignment);
        const auto bufferLength = alignedBytesPerRow * height;

        desc.storageMode = MTLStorageModeShared;
        id<MTLBuffer> backingBuffer = [device newBufferWithLength:bufferLength options:MTLResourceStorageModeShared];
        if (backingBuffer) {
            id<MTLTexture> texture = [backingBuffer newTextureWithDescriptor:desc
                                                                      offset:0
                                                                 bytesPerRow:alignedBytesPerRow];
            if (texture) {
                allocation.texture = texture;
                allocation.backingBuffer = backingBuffer;
                allocation.alignedBytesPerRow = alignedBytesPerRow;
                allocation.isZeroCopy = true;
                return allocation;
            }
        }

        std::cerr << "Metal RenderTarget zeroCopy requested but buffer-backed texture allocation failed; falling back to private storage\n";
    }

    desc.storageMode = MTLStorageModePrivate;
    allocation.texture = [device newTextureWithDescriptor:desc];
    return allocation;
}

id<MTLTexture> MetalRenderer::Impl::createRenderTargetDepthTexture(RenderTarget& target) const {
    if (target.depthTexture &&
        (target.depthTexture->format != Format::Depth || target.depthTexture->type != Type::Float)) {
        throw std::runtime_error("Metal RenderTarget depthTexture requires Format::Depth and Type::Float");
    }
    return createDepthTexture(target.width, target.height, target.texture && target.texture->generateMipmaps);
}

MetalRenderer::Impl::MetalRenderTargetResources& MetalRenderer::Impl::getOrCreateRenderTargetResources(RenderTarget& target) {
    if (!target.texture) {
        throw std::runtime_error("Metal RenderTarget requires a color texture");
    }
    if (target.textures.empty()) {
        target.textures.push_back(target.texture);
    }

    const auto width = static_cast<NSUInteger>(std::max(target.width, 1u));
    const auto height = static_cast<NSUInteger>(std::max(target.height, 1u));
    const auto depth = static_cast<NSUInteger>(std::max(target.depth, 1u));
    const auto colorTextureType = renderTargetTextureType(target, *target.texture);
    const auto mipmapped = target.texture->generateMipmaps;
    std::vector<MTLPixelFormat> colorPixelFormats;
    colorPixelFormats.reserve(target.textures.size());
    for (const auto& texture : target.textures) {
        if (!texture) {
            throw std::runtime_error("Metal RenderTarget has a null color attachment texture");
        }
        const auto attachmentFormat = toRenderTargetColorPixelFormat(*texture);
        colorPixelFormats.push_back(attachmentFormat);
        if (texture->generateMipmaps != mipmapped) {
            throw std::runtime_error("Metal RenderTarget MRT currently requires all color attachments to share mipmap settings");
        }
        const auto attachmentTextureType = renderTargetTextureType(target, *texture);
        if (attachmentTextureType != colorTextureType) {
            throw std::runtime_error("Metal RenderTarget MRT currently requires all color attachments to share texture type");
        }
    }

    auto it = renderTargetResources.find(&target);
    if (it != renderTargetResources.end() &&
        it->second.width == width &&
        it->second.height == height &&
        it->second.depth == depth &&
        it->second.colorPixelFormats == colorPixelFormats &&
        it->second.colorTextureType == colorTextureType &&
        it->second.mipmapped == mipmapped &&
        it->second.requestedZeroCopy == target.zeroCopy &&
        it->second.colorTextures.size() == target.textures.size() &&
        std::all_of(it->second.colorTextures.begin(), it->second.colorTextures.end(), [](id<MTLTexture> texture) { return texture != nil; }) &&
        it->second.depthTexture) {
        return it->second;
    }

    RenderTargetColorTextureAllocation primaryColorAllocation;
    std::vector<id<MTLTexture>> colorTextures;
    colorTextures.reserve(target.textures.size());
    for (std::size_t i = 0; i < target.textures.size(); ++i) {
        auto allocation = createRenderTargetColorTexture(target, *target.textures[i], colorPixelFormats[i]);
        if (i == 0) {
            primaryColorAllocation = allocation;
        }
        colorTextures.push_back(allocation.texture);
    }
    auto depthTexture = createRenderTargetDepthTexture(target);
    if (colorTextures.empty() || std::any_of(colorTextures.begin(), colorTextures.end(), [](id<MTLTexture> texture) { return texture == nil; }) || !depthTexture) {
        throw std::runtime_error("Failed to create Metal RenderTarget resources");
    }

    for (std::size_t i = 0; i < target.textures.size(); ++i) {
        auto& texture = target.textures[i];
        updateTextureImageMetadata(*texture, target.width, target.height, target.depth, dynamic_cast<CubeTexture*>(texture.get()) != nullptr);
        textureManager->registerExternalTexture(*texture, (__bridge void*) colorTextures[i]);
    }

    if (target.depthTexture) {
        updateTextureImageMetadata(*target.depthTexture, target.width, target.height, 1, false);
        textureManager->registerExternalTexture(*target.depthTexture, (__bridge void*) depthTexture);
    }

    if (!target.hasEventListener("dispose", onRenderTargetDispose)) {
        target.addEventListener("dispose", onRenderTargetDispose);
    }

    auto& resources = renderTargetResources[&target];
    resources.colorTextures = std::move(colorTextures);
    resources.depthTexture = depthTexture;
    resources.backingBuffer = primaryColorAllocation.backingBuffer;
    resources.width = width;
    resources.height = height;
    resources.depth = depth;
    resources.alignedBytesPerRow = primaryColorAllocation.alignedBytesPerRow;
    resources.colorPixelFormats = std::move(colorPixelFormats);
    resources.colorTextureType = colorTextureType;
    resources.mipmapped = mipmapped;
    resources.requestedZeroCopy = target.zeroCopy;
    resources.isZeroCopy = primaryColorAllocation.isZeroCopy;
    return resources;
}

id<MTLBuffer> MetalRenderer::Impl::acquireReadbackBuffer(NSUInteger size) {
    std::lock_guard<std::mutex> lock(readbackPoolMutex);
    const auto requestedSize = std::max<NSUInteger>(size, 1u);
    constexpr NSUInteger maxIdleOversizeRetainedReadbackBuffer = 16u * 1024u * 1024u;

    for (auto& entry : readbackBufferPool) {
        if (entry.inUse || entry.size < requestedSize) continue;

        const auto oversized = requestedSize <= std::numeric_limits<NSUInteger>::max() / 2u &&
                               entry.size > requestedSize * 2u &&
                               entry.size > maxIdleOversizeRetainedReadbackBuffer;
        if (oversized) {
            entry.buffer = [device newBufferWithLength:requestedSize options:MTLResourceStorageModeShared];
            if (!entry.buffer) {
                throw std::runtime_error("Failed to allocate Metal readback buffer");
            }
            entry.size = requestedSize;
        }

        entry.inUse = true;
        return entry.buffer;
    }

    id<MTLBuffer> buffer = [device newBufferWithLength:requestedSize options:MTLResourceStorageModeShared];
    if (!buffer) {
        throw std::runtime_error("Failed to allocate Metal readback buffer");
    }

    readbackBufferPool.push_back({buffer, requestedSize, true});
    return buffer;
}

void MetalRenderer::Impl::releaseReadbackBuffer(id<MTLBuffer> buffer) {
    std::lock_guard<std::mutex> lock(readbackPoolMutex);
    if (!buffer) return;

    for (auto& entry : readbackBufferPool) {
        if (entry.buffer == buffer) {
            entry.inUse = false;
            return;
        }
    }
}

void MetalRenderer::Impl::releaseAllReadbackBuffers() {
    std::lock_guard<std::mutex> lock(readbackPoolMutex);
    for (auto& entry : readbackBufferPool) {
        entry.inUse = false;
    }
}

void MetalRenderer::Impl::releaseReadbackBuffers(const std::vector<TextureReadback>& readbacks) {
    std::lock_guard<std::mutex> lock(readbackPoolMutex);
    for (const auto& readback : readbacks) {
        for (auto& entry : readbackBufferPool) {
            if (entry.buffer == readback.readbackBuffer) {
                entry.inUse = false;
                break;
            }
        }
    }
}

void MetalRenderer::Impl::readRgba8PixelsToBuffer(id<MTLBuffer> readbackBuffer,
                                                  NSUInteger sourceBytesPerRow,
                                                  NSUInteger sourceBytesPerImage,
                                                  const PixelReadbackRequest& request,
                                                  std::vector<std::uint8_t>& out) const {
    const auto width = static_cast<NSUInteger>(request.width);
    const auto height = static_cast<NSUInteger>(request.height);
    const auto depth = static_cast<NSUInteger>(request.depth);
    const auto rowBytes = width * 4u;
    const auto* rawBytes = static_cast<const std::uint8_t*>([readbackBuffer contents]);
    if (!rawBytes) {
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync could not map the readback buffer");
    }

    const auto compactBytesPerImage = rowBytes * height;
    out.resize(static_cast<std::size_t>(compactBytesPerImage * depth));
    for (NSUInteger z = 0; z < depth; ++z) {
        for (NSUInteger y = 0; y < height; ++y) {
            std::memcpy(
                out.data() + static_cast<std::size_t>(z * compactBytesPerImage + y * rowBytes),
                rawBytes + static_cast<std::size_t>(z * sourceBytesPerImage + y * sourceBytesPerRow),
                static_cast<std::size_t>(rowBytes));
        }
    }
}

void MetalRenderer::Impl::deallocateRenderTarget(RenderTarget* target) {
    if (!target) return;

    if (!target->textures.empty()) {
        for (auto& texture : target->textures) {
            if (texture) {
                textureManager->deallocateTexture(texture.get());
            }
        }
    } else if (target->texture) {
        textureManager->deallocateTexture(target->texture.get());
    }
    if (target->depthTexture) {
        textureManager->deallocateTexture(target->depthTexture.get());
    }
    renderTargetResources.erase(target);
    for (auto it = clearedTargetsInFrame.begin(); it != clearedTargetsInFrame.end();) {
        if (it->target == target) {
            it = clearedTargetsInFrame.erase(it);
        } else {
            ++it;
        }
    }
}

void MetalRenderer::Impl::clearDepthTextureToOne(id<MTLTexture> texture) const {
    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.depthAttachment.texture = texture;
    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
    passDesc.depthAttachment.clearDepth = 1.0;
    passDesc.depthAttachment.storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

void MetalRenderer::Impl::createPlaceholderResources() {
    whiteTexture = createSolidTexture2D({255, 255, 255, 255});
    blackTexture = createSolidTexture2D({0, 0, 0, 255});
    normalTexture = createSolidTexture2D({128, 128, 255, 255});
    whiteCubeTexture = createSolidCubeTexture({255, 255, 255, 255});
    whiteDepthTexture = createDepthTexture(1, 1);
    clearDepthTextureToOne(whiteDepthTexture);

    MTLSamplerDescriptor* defaultSamplerDesc = [[MTLSamplerDescriptor alloc] init];
    defaultSamplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
    defaultSamplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    defaultSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    defaultSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    defaultSampler = [device newSamplerStateWithDescriptor:defaultSamplerDesc];

    MTLSamplerDescriptor* shadowSamplerDesc = [[MTLSamplerDescriptor alloc] init];
    shadowSamplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    shadowSamplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    shadowSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    shadowSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    shadowSamplerDesc.compareFunction = MTLCompareFunctionLessEqual;
    shadowSampler = [device newSamplerStateWithDescriptor:shadowSamplerDesc];
}

void MetalRenderer::Impl::setSize(std::pair<int, int> size) {
    commitPendingFrame();

    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
    updatePixelRatio(WindowSize{size});
    metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
    metalLayer.contentsScale = pixelRatio;
    multisampleColorTexture = nil;
    multisampleColorPixelFormat = MTLPixelFormatInvalid;
    createDepthTexture();
    setViewport(0, 0, size.first, size.second);
    setScissor(0, 0, size.first, size.second);
}

void MetalRenderer::Impl::setClearColor(const Color& color, float alpha) {
    clearColor.copy(color);
    clearAlpha = alpha;
}

void MetalRenderer::Impl::clear(bool color, bool depth, bool /*stencil*/) {
    if (currentCommandBuffer && color && screenCommandsEncoded) {
        commitPendingFrame();
    }

    clearColorFlag = color;
    clearDepthFlag = depth;
    clearRequested = true;
    explicitFrameInProgress = true;
}

void MetalRenderer::Impl::copyFramebufferToTexture(const Vector2& position, Texture& texture, int level) {
    if (level < 0) {
        throw std::invalid_argument("MetalRenderer::copyFramebufferToTexture requires a non-negative mip level");
    }

    id<MTLTexture> sourceTexture = nil;
    bool temporaryCommandBuffer = false;

    if (renderTarget) {
        auto& resources = getOrCreateRenderTargetResources(*renderTarget);
        sourceTexture = resources.colorTextures.empty() ? nil : resources.colorTextures.front();
        if (!currentCommandBuffer) {
            ensureFrameStarted();
            temporaryCommandBuffer = true;
        }
    } else {
        if (!currentCommandBuffer) {
            throw std::runtime_error("MetalRenderer::copyFramebufferToTexture requires an active screen frame; set autoClear=false and copy before the frame is committed");
        }
        if (!currentDrawable && !ensureDrawable()) {
            throw std::runtime_error("MetalRenderer::copyFramebufferToTexture requires a current drawable");
        }
        sourceTexture = currentDrawable.texture;
    }

    if (!sourceTexture || !currentCommandBuffer) {
        throw std::runtime_error("MetalRenderer::copyFramebufferToTexture could not acquire a source texture or command buffer");
    }

    id<MTLTexture> targetTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(texture);
    const auto mipmapped = texture.generateMipmaps || !texture.mipmaps().empty() || level > 0;
    const auto baseWidth = std::max<NSUInteger>(static_cast<NSUInteger>(texture.image().width), 1u);
    const auto baseHeight = std::max<NSUInteger>(static_cast<NSUInteger>(texture.image().height), 1u);

    if (!targetTexture ||
        targetTexture.pixelFormat != sourceTexture.pixelFormat ||
        targetTexture.width != baseWidth ||
        targetTexture.height != baseHeight ||
        static_cast<NSUInteger>(level) >= targetTexture.mipmapLevelCount) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:sourceTexture.pixelFormat
                                                                                        width:baseWidth
                                                                                       height:baseHeight
                                                                                    mipmapped:mipmapped ? YES : NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        targetTexture = [device newTextureWithDescriptor:desc];
        if (!targetTexture) {
            throw std::runtime_error("Failed to create Metal framebuffer copy target texture");
        }
        textureManager->updateCachedTexture(texture, (__bridge void*) targetTexture);
    }

    if (static_cast<NSUInteger>(level) >= targetTexture.mipmapLevelCount) {
        throw std::runtime_error("MetalRenderer::copyFramebufferToTexture target texture does not contain the requested mip level");
    }

    const auto levelScale = std::pow(2.0, -static_cast<double>(level));
    const auto copyWidth = std::max<NSInteger>(static_cast<NSInteger>(std::floor(static_cast<double>(texture.image().width) * levelScale)), 1);
    const auto copyHeight = std::max<NSInteger>(static_cast<NSInteger>(std::floor(static_cast<double>(texture.image().height) * levelScale)), 1);
    const auto coordinateRatio = renderTarget ? 1.f : pixelRatio;
    const auto sourceX = static_cast<NSInteger>(std::floor(static_cast<double>(position.x) * coordinateRatio));
    const auto logicalY = static_cast<NSInteger>(std::floor(static_cast<double>(position.y) * coordinateRatio));
    const auto sourceY = static_cast<NSInteger>(sourceTexture.height) - logicalY - copyHeight;

    if (sourceX < 0 ||
        sourceY < 0 ||
        sourceX + copyWidth > static_cast<NSInteger>(sourceTexture.width) ||
        sourceY + copyHeight > static_cast<NSInteger>(sourceTexture.height)) {
        throw std::runtime_error("MetalRenderer::copyFramebufferToTexture source region is outside the framebuffer");
    }

    id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
    [blitEncoder copyFromTexture:sourceTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(sourceX), static_cast<NSUInteger>(sourceY), 0)
                      sourceSize:MTLSizeMake(static_cast<NSUInteger>(copyWidth), static_cast<NSUInteger>(copyHeight), 1)
                       toTexture:targetTexture
                destinationSlice:0
                destinationLevel:static_cast<NSUInteger>(level)
               destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blitEncoder endEncoding];

    if (temporaryCommandBuffer) {
        commitPendingFrame();
    }
}

void MetalRenderer::Impl::copyTextureToImage(Texture& texture) {
    std::vector<Texture*> textures{&texture};
    copyTexturesToImages(textures);
}

void MetalRenderer::Impl::copyTexturesToImages(const std::vector<Texture*>& textures) {
    const auto hasReadableTexture = std::any_of(textures.begin(), textures.end(), [](const auto* texture) {
        return texture != nullptr;
    });
    if (!hasReadableTexture) return;

    auto future = copyTexturesToImagesAsync(textures);
    commitPendingFrame();
    future.get();
}

std::future<void> MetalRenderer::Impl::copyTextureToImageAsync(Texture& texture) {
    std::vector<Texture*> textures{&texture};
    return copyTexturesToImagesAsync(textures);
}

std::future<void> MetalRenderer::Impl::copyTexturesToImagesAsync(const std::vector<Texture*>& textures) {
    std::vector<TextureReadback> readbacks;
    readbacks.reserve(textures.size());

    try {
        for (auto* texture : textures) {
            if (!texture) continue;

            id<MTLTexture> sourceTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture);
            if (!sourceTexture) {
                throw std::runtime_error("MetalRenderer::copyTextureToImage could not acquire the source texture");
            }

            const auto width = static_cast<NSUInteger>(sourceTexture.width);
            const auto height = static_cast<NSUInteger>(sourceTexture.height);
            const auto sourceDepth = sourceTexture.textureType == MTLTextureType2DArray ||
                                             sourceTexture.textureType == MTLTextureTypeCube ||
                                             sourceTexture.textureType == MTLTextureTypeCubeArray
                                         ? static_cast<NSUInteger>(sourceTexture.arrayLength)
                                         : (sourceTexture.textureType == MTLTextureType3D ? static_cast<NSUInteger>(sourceTexture.depth) : 1u);
            const auto sourceBytesPerPixel = pixelFormatBytesPerPixel(sourceTexture.pixelFormat);
            const auto sourceBytesPerRow = ((width * sourceBytesPerPixel) + 255u) & ~255u;
            const auto sourceBytesPerImage = sourceBytesPerRow * height;
            const auto byteLength = sourceBytesPerImage * std::max<NSUInteger>(sourceDepth, 1u);

            id<MTLBuffer> readbackBuffer = acquireReadbackBuffer(byteLength);

            readbacks.push_back({texture, sourceTexture, readbackBuffer, sourceBytesPerRow, sourceBytesPerImage, sourceBytesPerPixel, sourceDepth, byteLength});
        }
    } catch (...) {
        releaseReadbackBuffers(readbacks);
        throw;
    }

    std::promise<void> readyPromise;
    if (readbacks.empty()) {
        readyPromise.set_value();
        return readyPromise.get_future();
    }

    id<MTLCommandBuffer> commandBuffer = currentCommandBuffer;
    const bool temporaryCommandBuffer = commandBuffer == nil;
    if (temporaryCommandBuffer) {
        commandBuffer = [commandQueue commandBuffer];
    }
    if (!commandBuffer) {
        releaseReadbackBuffers(readbacks);
        throw std::runtime_error("MetalRenderer::copyTextureToImage could not create a command buffer");
    }

    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
    for (const auto& readback : readbacks) {
        const auto width = static_cast<NSUInteger>(readback.sourceTexture.width);
        const auto height = static_cast<NSUInteger>(readback.sourceTexture.height);
        if (readback.sourceTexture.textureType == MTLTextureType2DArray ||
            readback.sourceTexture.textureType == MTLTextureTypeCube ||
            readback.sourceTexture.textureType == MTLTextureTypeCubeArray) {
            for (NSUInteger slice = 0; slice < readback.sourceDepth; ++slice) {
                [blitEncoder copyFromTexture:readback.sourceTexture
                                 sourceSlice:slice
                                 sourceLevel:0
                                sourceOrigin:MTLOriginMake(0, 0, 0)
                                  sourceSize:MTLSizeMake(width, height, 1)
                                    toBuffer:readback.readbackBuffer
                           destinationOffset:readback.sourceBytesPerImage * slice
                      destinationBytesPerRow:readback.sourceBytesPerRow
                    destinationBytesPerImage:readback.sourceBytesPerImage];
            }
        } else {
            const auto depth = readback.sourceTexture.textureType == MTLTextureType3D ? readback.sourceDepth : 1u;
            [blitEncoder copyFromTexture:readback.sourceTexture
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(width, height, depth)
                                toBuffer:readback.readbackBuffer
                       destinationOffset:0
                  destinationBytesPerRow:readback.sourceBytesPerRow
                destinationBytesPerImage:readback.sourceBytesPerImage];
        }
    }
    [blitEncoder endEncoding];

    auto completionPromise = std::make_shared<std::promise<void>>();
    auto completionFuture = completionPromise->get_future();
    auto completionReadbacks = std::make_shared<std::vector<TextureReadback>>(std::move(readbacks));
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
        try {
            if (completedCommandBuffer.error) {
                const char* message = [[completedCommandBuffer.error localizedDescription] UTF8String];
                throw std::runtime_error(message ? message : "MetalRenderer::copyTextureToImageAsync command buffer failed");
            }
            for (const auto& readback : *completionReadbacks) {
                readPixelsFromTextureReadback(*readback.texture,
                                              readback.sourceTexture,
                                              readback.readbackBuffer,
                                              readback.sourceBytesPerRow,
                                              readback.sourceBytesPerImage,
                                              readback.sourceDepth,
                                              readback.sourceBytesPerPixel);
            }
            releaseReadbackBuffers(*completionReadbacks);
            completionPromise->set_value();
        } catch (...) {
            releaseReadbackBuffers(*completionReadbacks);
            completionPromise->set_exception(std::current_exception());
        }
    }];

    if (temporaryCommandBuffer) {
        [commandBuffer commit];
    }

    return completionFuture;
}

std::future<PixelReadbackBuffer> MetalRenderer::Impl::readRenderTargetPixelsAsync(
        const PixelReadbackRequest& request) {
    if (!request.renderTarget) {
        throw std::invalid_argument("MetalRenderer::readRenderTargetPixelsAsync requires a render target");
    }
    if (request.width <= 0 || request.height <= 0 || request.depth <= 0) {
        throw std::invalid_argument("MetalRenderer::readRenderTargetPixelsAsync requires a positive readback size");
    }
    if (request.x < 0 || request.y < 0) {
        throw std::invalid_argument("MetalRenderer::readRenderTargetPixelsAsync requires a non-negative origin");
    }
    if (request.format != Format::RGBA || request.type != Type::UnsignedByte) {
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync supports only RGBA8 pixel readback");
    }

    auto& resources = getOrCreateRenderTargetResources(*request.renderTarget);
    if (request.textureIndex >= resources.colorTextures.size()) {
        throw std::out_of_range("MetalRenderer::readRenderTargetPixelsAsync texture index is outside the render target attachments");
    }
    id<MTLTexture> sourceTexture = resources.colorTextures[request.textureIndex];
    if (!sourceTexture) {
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync could not acquire the source texture");
    }
    if (sourceTexture.pixelFormat != MTLPixelFormatRGBA8Unorm &&
        sourceTexture.pixelFormat != MTLPixelFormatRGBA8Unorm_sRGB) {
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync supports only RGBA8 source textures");
    }

    const auto width = static_cast<NSUInteger>(request.width);
    const auto height = static_cast<NSUInteger>(request.height);
    const auto depth = static_cast<NSUInteger>(request.depth);
    const auto sourceX = static_cast<NSUInteger>(request.x);
    const auto sourceY = static_cast<NSUInteger>(request.y);
    if (sourceX + width > sourceTexture.width || sourceY + height > sourceTexture.height) {
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync source region is outside the render target");
    }

    NSUInteger sourceSlice = 0u;
    if (sourceTexture.textureType == MTLTextureType2DArray ||
        sourceTexture.textureType == MTLTextureType2DMultisampleArray) {
        if (request.activeLayer < 0 ||
            static_cast<NSUInteger>(request.activeLayer) + depth > sourceTexture.arrayLength) {
            throw std::out_of_range("MetalRenderer::readRenderTargetPixelsAsync active layer is outside the render target texture");
        }
        sourceSlice = static_cast<NSUInteger>(request.activeLayer);
    } else if (sourceTexture.textureType == MTLTextureTypeCube ||
               sourceTexture.textureType == MTLTextureTypeCubeArray) {
        if (request.activeCubeFace < 0 ||
            static_cast<NSUInteger>(request.activeCubeFace) + depth > sourceTexture.arrayLength) {
            throw std::out_of_range("MetalRenderer::readRenderTargetPixelsAsync active cube face is outside the render target texture");
        }
        sourceSlice = static_cast<NSUInteger>(request.activeCubeFace);
    } else if (depth != 1u) {
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync depth > 1 requires an array or cube source texture");
    }

    const auto sourceBytesPerRow = alignTo(width * 4u, 256u);
    const auto sourceBytesPerImage = sourceBytesPerRow * height;
    id<MTLBuffer> readbackBuffer = acquireReadbackBuffer(sourceBytesPerImage * depth);

    id<MTLCommandBuffer> commandBuffer = currentCommandBuffer;
    const bool temporaryCommandBuffer = commandBuffer == nil;
    if (temporaryCommandBuffer) {
        commandBuffer = [commandQueue commandBuffer];
    }
    if (!commandBuffer) {
        releaseReadbackBuffer(readbackBuffer);
        throw std::runtime_error("MetalRenderer::readRenderTargetPixelsAsync could not create a command buffer");
    }

    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
    for (NSUInteger layer = 0; layer < depth; ++layer) {
        [blitEncoder copyFromTexture:sourceTexture
                         sourceSlice:sourceSlice + layer
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(sourceX, sourceY, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                            toBuffer:readbackBuffer
                   destinationOffset:sourceBytesPerImage * layer
              destinationBytesPerRow:sourceBytesPerRow
            destinationBytesPerImage:sourceBytesPerImage];
    }
    [blitEncoder endEncoding];

    auto completionPromise = std::make_shared<std::promise<PixelReadbackBuffer>>();
    auto completionFuture = completionPromise->get_future();
    PixelReadback readback{sourceTexture, readbackBuffer, sourceBytesPerRow, sourceBytesPerImage, sourceBytesPerImage * depth, request};
    auto completionReadback = std::make_shared<PixelReadback>(std::move(readback));
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
        try {
            if (completedCommandBuffer.error) {
                const char* message = [[completedCommandBuffer.error localizedDescription] UTF8String];
                throw std::runtime_error(message ? message : "MetalRenderer::readRenderTargetPixelsAsync command buffer failed");
            }

            PixelReadbackBuffer buffer;
            buffer.width = static_cast<unsigned int>(completionReadback->request.width);
            buffer.height = static_cast<unsigned int>(completionReadback->request.height);
            buffer.depth = static_cast<unsigned int>(completionReadback->request.depth);
            buffer.bytesPerPixel = 4;
            buffer.format = completionReadback->request.format;
            buffer.type = completionReadback->request.type;
            readRgba8PixelsToBuffer(
                    completionReadback->readbackBuffer,
                    completionReadback->sourceBytesPerRow,
                    completionReadback->sourceBytesPerImage,
                    completionReadback->request,
                    buffer.bytes);
            releaseReadbackBuffer(completionReadback->readbackBuffer);
            completionPromise->set_value(std::move(buffer));
        } catch (...) {
            releaseReadbackBuffer(completionReadback->readbackBuffer);
            completionPromise->set_exception(std::current_exception());
        }
    }];

    if (temporaryCommandBuffer) {
        [commandBuffer commit];
    }

    return completionFuture;
}

void MetalRenderer::Impl::readPixelsFromTextureReadback(Texture& texture,
                                                        id<MTLTexture> sourceTexture,
                                                        id<MTLBuffer> readbackBuffer,
                                                        NSUInteger sourceBytesPerRow,
                                                        NSUInteger sourceBytesPerImage,
                                                        NSUInteger sourceDepth,
                                                        NSUInteger sourceBytesPerPixel) {
    const auto width = static_cast<NSUInteger>(sourceTexture.width);
    const auto height = static_cast<NSUInteger>(sourceTexture.height);
    const auto depth = std::max<NSUInteger>(sourceDepth, 1u);

    auto& image = texture.image();
    image.width = static_cast<unsigned int>(width);
    image.height = static_cast<unsigned int>(height);
    image.depth = depth > 1u ? static_cast<unsigned int>(depth) : 0u;

    const auto destinationChannels = textureFormatChannelCount(texture.format);
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(depth);
    const auto sourceIsFloat = pixelFormatIsFloat(sourceTexture.pixelFormat);
    const auto sourceIsInteger = pixelFormatIsInteger(sourceTexture.pixelFormat);
    const auto* rawBytes = static_cast<const unsigned char*>([readbackBuffer contents]);

    if (texture.format != Format::BGRA && canUseFastReadbackPath(texture, sourceTexture.pixelFormat)) {
        const auto elementSize =
                texture.type == Type::Float ? sizeof(float) :
                texture.type == Type::UnsignedInt ? sizeof(std::uint32_t) :
                sizeof(unsigned char);
        const auto rowBytes = static_cast<NSUInteger>(width * destinationChannels * elementSize);
        const auto imageBytes = rowBytes * height;
        unsigned char* dstBytes = nullptr;

        if (texture.type == Type::Float) {
            image.setData(std::vector<float>(pixelCount * destinationChannels));
            dstBytes = reinterpret_cast<unsigned char*>(image.data<float>().data());
        } else if (texture.type == Type::UnsignedInt) {
            image.setData(std::vector<std::uint32_t>(pixelCount * destinationChannels));
            dstBytes = reinterpret_cast<unsigned char*>(image.data<std::uint32_t>().data());
        } else {
            image.setData(std::vector<unsigned char>(pixelCount * destinationChannels));
            dstBytes = reinterpret_cast<unsigned char*>(image.data<unsigned char>().data());
        }

        if (sourceBytesPerRow == rowBytes && sourceBytesPerImage == imageBytes) {
            std::memcpy(dstBytes, rawBytes, static_cast<std::size_t>(imageBytes * depth));
        } else {
            for (NSUInteger z = 0; z < depth; ++z) {
                for (NSUInteger y = 0; y < height; ++y) {
                    std::memcpy(dstBytes + static_cast<std::size_t>(z * imageBytes + y * rowBytes),
                                rawBytes + static_cast<std::size_t>(z * sourceBytesPerImage + y * sourceBytesPerRow),
                                static_cast<std::size_t>(rowBytes));
                }
            }
        }
        return;
    }

    if (texture.type == Type::UnsignedInt) {
        image.setData(std::vector<std::uint32_t>(pixelCount * destinationChannels));
        auto& out = image.data<std::uint32_t>();
        for (NSUInteger z = 0; z < depth; ++z) {
            for (NSUInteger y = 0; y < height; ++y) {
                const auto* srcRow = rawBytes + z * sourceBytesPerImage + y * sourceBytesPerRow;
                for (NSUInteger x = 0; x < width; ++x) {
                    const auto* srcPixel = srcRow + x * sourceBytesPerPixel;
                    const auto dstBase =
                            ((static_cast<std::size_t>(z) * static_cast<std::size_t>(height) + static_cast<std::size_t>(y)) *
                                 static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            destinationChannels;
                    for (unsigned int c = 0; c < destinationChannels; ++c) {
                        const auto canonical = destinationCanonicalChannel(texture.format, c);
                        std::uint32_t value = 0u;
                        if (sourceIsInteger) {
                            value = readUintComponent(reinterpret_cast<const std::uint32_t*>(srcPixel), sourceTexture.pixelFormat, canonical);
                        } else if (sourceIsFloat) {
                            value = static_cast<std::uint32_t>(std::max(0.0f, readFloatComponent(reinterpret_cast<const float*>(srcPixel), sourceTexture.pixelFormat, canonical)));
                        } else {
                            value = readByteComponent(srcPixel, sourceTexture.pixelFormat, canonical);
                        }
                        out[dstBase + c] = value;
                    }
                }
            }
        }
        return;
    }

    if (texture.type == Type::Float) {
        image.setData(std::vector<float>(pixelCount * destinationChannels));
        auto& out = image.data<float>();
        for (NSUInteger z = 0; z < depth; ++z) {
            for (NSUInteger y = 0; y < height; ++y) {
                const auto* srcRow = rawBytes + z * sourceBytesPerImage + y * sourceBytesPerRow;
                for (NSUInteger x = 0; x < width; ++x) {
                    const auto* srcPixel = srcRow + x * sourceBytesPerPixel;
                    const auto dstBase =
                            ((static_cast<std::size_t>(z) * static_cast<std::size_t>(height) + static_cast<std::size_t>(y)) *
                                 static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            destinationChannels;
                    for (unsigned int c = 0; c < destinationChannels; ++c) {
                        const auto canonical = destinationCanonicalChannel(texture.format, c);
                        float value = 0.f;
                        if (sourceIsFloat) {
                            value = readFloatComponent(reinterpret_cast<const float*>(srcPixel), sourceTexture.pixelFormat, canonical);
                        } else {
                            value = static_cast<float>(readByteComponent(srcPixel, sourceTexture.pixelFormat, canonical)) * (1.f / 255.f);
                        }
                        out[dstBase + c] = value;
                    }
                }
            }
        }
        return;
    }

    if (texture.type != Type::UnsignedByte) {
        throw std::runtime_error("MetalRenderer::copyTextureToImage supports only UnsignedByte, UnsignedInt, and Float texture readback");
    }

    image.setData(std::vector<unsigned char>(pixelCount * destinationChannels));
    auto& out = image.data<unsigned char>();
    for (NSUInteger z = 0; z < depth; ++z) {
        for (NSUInteger y = 0; y < height; ++y) {
            const auto* srcRow = rawBytes + z * sourceBytesPerImage + y * sourceBytesPerRow;
            for (NSUInteger x = 0; x < width; ++x) {
                const auto* srcPixel = srcRow + x * sourceBytesPerPixel;
                const auto dstBase =
                        ((static_cast<std::size_t>(z) * static_cast<std::size_t>(height) + static_cast<std::size_t>(y)) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        destinationChannels;
                for (unsigned int c = 0; c < destinationChannels; ++c) {
                    const auto canonical = destinationCanonicalChannel(texture.format, c);
                    unsigned char value = 0;
                    if (sourceIsFloat) {
                        const auto floatValue = readFloatComponent(reinterpret_cast<const float*>(srcPixel), sourceTexture.pixelFormat, canonical);
                        value = static_cast<unsigned char>(std::clamp(std::lround(floatValue * 255.f), 0l, 255l));
                    } else {
                        value = readByteComponent(srcPixel, sourceTexture.pixelFormat, canonical);
                    }
                    out[dstBase + c] = value;
                }
            }
        }
    }
}

void MetalRenderer::Impl::readbackTextureAsync(Texture& texture,
                                               std::function<void(const ReadbackResult& result)> onComplete,
                                               std::function<void(const std::string& error)> onError) {
    if (!onComplete) return;

    try {
        id<MTLTexture> sourceTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(texture);
        if (!sourceTexture) {
            throw std::runtime_error("MetalRenderer::readbackTextureAsync could not acquire the source texture");
        }
        if (!canExposeRawReadbackLayout(texture, sourceTexture.pixelFormat)) {
            throw std::runtime_error("MetalRenderer::readbackTextureAsync cannot expose this texture's raw Metal layout as the requested Format/Type");
        }

        const auto width = static_cast<NSUInteger>(sourceTexture.width);
        const auto height = static_cast<NSUInteger>(sourceTexture.height);
        const auto sourceBytesPerPixel = pixelFormatBytesPerPixel(sourceTexture.pixelFormat);
        const auto fallbackBytesPerRow = alignTo(width * sourceBytesPerPixel, 256u);
        id<MTLBuffer> zeroCopyBuffer = sourceTexture.buffer;
        const auto isZeroCopy = zeroCopyBuffer != nil;
        const auto bytesPerRow = isZeroCopy && sourceTexture.bufferBytesPerRow > 0u
                                         ? sourceTexture.bufferBytesPerRow
                                         : fallbackBytesPerRow;
        const auto bufferOffset = isZeroCopy ? sourceTexture.bufferOffset : 0u;
        const auto byteLength = bytesPerRow * height;

        id<MTLBuffer> readbackBuffer = nil;
        id<MTLCommandBuffer> commandBuffer = currentCommandBuffer;
        bool temporaryCommandBuffer = false;

        if (!isZeroCopy) {
            readbackBuffer = [device newBufferWithLength:byteLength options:MTLResourceStorageModeShared];
            if (!readbackBuffer) {
                throw std::runtime_error("MetalRenderer::readbackTextureAsync could not allocate a readback buffer");
            }
            if (!commandBuffer) {
                commandBuffer = [commandQueue commandBuffer];
                temporaryCommandBuffer = true;
            }
            if (!commandBuffer) {
                throw std::runtime_error("MetalRenderer::readbackTextureAsync could not create a command buffer");
            }

            id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
            [blitEncoder copyFromTexture:sourceTexture
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(width, height, 1)
                                toBuffer:readbackBuffer
                       destinationOffset:0
                  destinationBytesPerRow:bytesPerRow
                destinationBytesPerImage:byteLength];
            [blitEncoder endEncoding];
        } else if (!commandBuffer) {
            commandBuffer = [commandQueue commandBuffer];
            temporaryCommandBuffer = true;
            if (!commandBuffer) {
                throw std::runtime_error("MetalRenderer::readbackTextureAsync could not create a command buffer");
            }
        }

        auto complete = std::move(onComplete);
        auto error = std::move(onError);
        const auto format = texture.format;
        const auto type = texture.type;
        id<MTLBuffer> resultBuffer = isZeroCopy ? zeroCopyBuffer : readbackBuffer;

        auto invokeOnMain = ^(id<MTLCommandBuffer> completedCommandBuffer) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (completedCommandBuffer &&
                completedCommandBuffer.status == MTLCommandBufferStatusError) {
                if (error) {
                    const char* message = completedCommandBuffer.error.localizedDescription.UTF8String;
                    error(message ? message : "MetalRenderer::readbackTextureAsync command buffer failed");
                }
                return;
            }

            const auto* contents = static_cast<const unsigned char*>([resultBuffer contents]);
            const ReadbackResult result{
                    contents ? contents + static_cast<std::size_t>(bufferOffset) : nullptr,
                    static_cast<unsigned int>(width),
                    static_cast<unsigned int>(height),
                    static_cast<unsigned int>(bytesPerRow),
                    format,
                    type,
                    isZeroCopy};

            try {
                complete(result);
            } catch (const std::exception& e) {
                if (error) error(e.what());
            } catch (...) {
                if (error) error("MetalRenderer::readbackTextureAsync completion callback failed");
            }
          });
        };

        [commandBuffer addCompletedHandler:invokeOnMain];
        if (temporaryCommandBuffer) {
            [commandBuffer commit];
        }
    } catch (const std::exception& e) {
        if (onError) {
            onError(e.what());
            return;
        }
        throw;
    } catch (...) {
        if (onError) {
            onError("MetalRenderer::readbackTextureAsync failed");
            return;
        }
        throw;
    }
}

id<MTLComputePipelineState> MetalRenderer::Impl::getOrCreateUnprojectComputePSO() {
    if (unprojectComputePSO) return unprojectComputePSO;

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:lidarUnprojectShaderSource];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        NSString* msg = [NSString stringWithFormat:@"Failed to create lidar unproject compute library: %@", error.localizedDescription];
        throw std::runtime_error([msg UTF8String]);
    }

    id<MTLFunction> function = [library newFunctionWithName:@"lidarUnprojectDense"];
    if (!function) {
        throw std::runtime_error("Failed to create lidar unproject compute function");
    }

    unprojectComputePSO = [device newComputePipelineStateWithFunction:function error:&error];
    if (!unprojectComputePSO) {
        NSString* msg = [NSString stringWithFormat:@"Failed to create lidar unproject compute pipeline: %@", error.localizedDescription];
        throw std::runtime_error([msg UTF8String]);
    }

    return unprojectComputePSO;
}

id<MTLComputePipelineState> MetalRenderer::Impl::getOrCreateUnprojectBeamsComputePSO() {
    if (unprojectBeamsComputePSO) return unprojectBeamsComputePSO;

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:lidarUnprojectShaderSource];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        NSString* msg = [NSString stringWithFormat:@"Failed to create lidar beam unproject compute library: %@", error.localizedDescription];
        throw std::runtime_error([msg UTF8String]);
    }

    id<MTLFunction> function = [library newFunctionWithName:@"lidarUnprojectBeams"];
    if (!function) {
        throw std::runtime_error("Failed to create lidar beam unproject compute function");
    }

    unprojectBeamsComputePSO = [device newComputePipelineStateWithFunction:function error:&error];
    if (!unprojectBeamsComputePSO) {
        NSString* msg = [NSString stringWithFormat:@"Failed to create lidar beam unproject compute pipeline: %@", error.localizedDescription];
        throw std::runtime_error([msg UTF8String]);
    }

    return unprojectBeamsComputePSO;
}

void MetalRenderer::Impl::readbackLidarDepthAsPointCloudAsync(Texture& packedDepthTexture,
                                                              const std::array<float, 16>& matrixWorld,
                                                              float farPlane,
                                                              std::function<void(const ReadbackResult& result)> onComplete,
                                                              std::function<void(const std::string& error)> onError) {
    if (!onComplete) return;

    try {
        id<MTLTexture> sourceTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(packedDepthTexture);
        if (!sourceTexture) {
            throw std::runtime_error("MetalRenderer::readbackLidarDepthAsPointCloudAsync could not acquire the packed depth texture");
        }
        if (packedDepthTexture.format != Format::RG || packedDepthTexture.type != Type::UnsignedByte) {
            throw std::runtime_error("MetalRenderer::readbackLidarDepthAsPointCloudAsync requires an RG8 packed depth texture");
        }

        auto pso = getOrCreateUnprojectComputePSO();
        const auto width = static_cast<NSUInteger>(sourceTexture.width);
        const auto height = static_cast<NSUInteger>(sourceTexture.height);
        const auto bytesPerRow = width * sizeof(float) * 4u;
        const auto byteLength = bytesPerRow * height;
        id<MTLBuffer> outputBuffer = [device newBufferWithLength:byteLength options:MTLResourceStorageModeShared];
        if (!outputBuffer) {
            throw std::runtime_error("MetalRenderer::readbackLidarDepthAsPointCloudAsync could not allocate an output buffer");
        }

        id<MTLCommandBuffer> commandBuffer = currentCommandBuffer;
        bool temporaryCommandBuffer = false;
        if (!commandBuffer) {
            commandBuffer = [commandQueue commandBuffer];
            temporaryCommandBuffer = true;
        }
        if (!commandBuffer) {
            throw std::runtime_error("MetalRenderer::readbackLidarDepthAsPointCloudAsync could not create a command buffer");
        }

        LidarUnprojectUniforms uniforms{};
        std::copy(matrixWorld.begin(), matrixWorld.end(), uniforms.matrixWorld);
        uniforms.farPlane = farPlane;
        uniforms.width = static_cast<std::uint32_t>(width);
        uniforms.height = static_cast<std::uint32_t>(height);

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pso];
        [encoder setTexture:sourceTexture atIndex:0];
        [encoder setBuffer:outputBuffer offset:0 atIndex:0];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];

        const auto threadWidth = std::max<NSUInteger>(pso.threadExecutionWidth, 1u);
        const auto maxThreads = std::max<NSUInteger>(pso.maxTotalThreadsPerThreadgroup, threadWidth);
        const auto threadHeight = std::max<NSUInteger>(maxThreads / threadWidth, 1u);
        const MTLSize threadsPerGrid = MTLSizeMake(width, height, 1);
        const MTLSize threadsPerThreadgroup = MTLSizeMake(threadWidth, threadHeight, 1);
        [encoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
        [encoder endEncoding];

        auto complete = std::move(onComplete);
        auto error = std::move(onError);
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (completedCommandBuffer.status == MTLCommandBufferStatusError) {
                if (error) {
                    const char* message = completedCommandBuffer.error.localizedDescription.UTF8String;
                    error(message ? message : "MetalRenderer::readbackLidarDepthAsPointCloudAsync command buffer failed");
                }
                return;
            }

            const ReadbackResult result{
                    static_cast<const unsigned char*>([outputBuffer contents]),
                    static_cast<unsigned int>(width),
                    static_cast<unsigned int>(height),
                    static_cast<unsigned int>(bytesPerRow),
                    Format::RGBA,
                    Type::Float,
                    true};

            try {
                complete(result);
            } catch (const std::exception& e) {
                if (error) error(e.what());
            } catch (...) {
                if (error) error("MetalRenderer::readbackLidarDepthAsPointCloudAsync completion callback failed");
            }
          });
        }];

        if (temporaryCommandBuffer) {
            [commandBuffer commit];
        }
    } catch (const std::exception& e) {
        if (onError) {
            onError(e.what());
            return;
        }
        throw;
    } catch (...) {
        if (onError) {
            onError("MetalRenderer::readbackLidarDepthAsPointCloudAsync failed");
            return;
        }
        throw;
    }
}

void MetalRenderer::Impl::readbackLidarBeamsAsPointCloudAsync(const std::array<Texture*, 6>& packedDepthTextures,
                                                              const std::array<std::array<float, 16>, 6>& matrixWorldPerFace,
                                                              std::span<const MetalLidarBeamSample> beams,
                                                              float farPlane,
                                                              std::function<void(const ReadbackResult& result)> onComplete,
                                                              std::function<void(const std::string& error)> onError) {
    if (!onComplete) return;

    try {
        if (beams.size() > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
            throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync beam count exceeds ReadbackResult width");
        }

        if (beams.empty()) {
            const ReadbackResult result{
                    nullptr,
                    0u,
                    1u,
                    0u,
                    Format::RGBA,
                    Type::Float,
                    true};
            onComplete(result);
            return;
        }

        std::array<id<MTLTexture>, 6> sourceTextures{};
        for (std::size_t i = 0; i < packedDepthTextures.size(); ++i) {
            auto* texture = packedDepthTextures[i];
            if (!texture) {
                throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync received a null depth texture");
            }
            if (texture->format != Format::RG || texture->type != Type::UnsignedByte) {
                throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync requires RG8 packed depth textures");
            }

            sourceTextures[i] = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture);
            if (!sourceTextures[i]) {
                throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync could not acquire a packed depth texture");
            }
        }

        auto pso = getOrCreateUnprojectBeamsComputePSO();
        const auto beamCount = static_cast<NSUInteger>(beams.size());
        const auto bytesPerRow = beamCount * sizeof(float) * 4u;
        const auto byteLength = bytesPerRow;

        id<MTLBuffer> beamBuffer = [device newBufferWithLength:beamCount * sizeof(MetalLidarBeamSample)
                                                       options:MTLResourceStorageModeShared];
        if (!beamBuffer) {
            throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync could not allocate a beam buffer");
        }
        std::memcpy([beamBuffer contents], beams.data(), beams.size_bytes());

        id<MTLBuffer> outputBuffer = [device newBufferWithLength:byteLength options:MTLResourceStorageModeShared];
        if (!outputBuffer) {
            throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync could not allocate an output buffer");
        }

        id<MTLCommandBuffer> commandBuffer = currentCommandBuffer;
        bool temporaryCommandBuffer = false;
        if (!commandBuffer) {
            commandBuffer = [commandQueue commandBuffer];
            temporaryCommandBuffer = true;
        }
        if (!commandBuffer) {
            throw std::runtime_error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync could not create a command buffer");
        }

        LidarUnprojectBeamsUniforms uniforms{};
        for (std::size_t face = 0; face < matrixWorldPerFace.size(); ++face) {
            std::copy(matrixWorldPerFace[face].begin(), matrixWorldPerFace[face].end(), uniforms.matrixWorld[face]);
        }
        uniforms.farPlane = farPlane;
        uniforms.beamCount = static_cast<std::uint32_t>(beamCount);

        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        [encoder setComputePipelineState:pso];
        for (NSUInteger face = 0; face < sourceTextures.size(); ++face) {
            [encoder setTexture:sourceTextures[face] atIndex:face];
        }
        [encoder setBuffer:beamBuffer offset:0 atIndex:0];
        [encoder setBuffer:outputBuffer offset:0 atIndex:1];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];

        const auto threadWidth = std::max<NSUInteger>(
                std::min<NSUInteger>(pso.threadExecutionWidth, pso.maxTotalThreadsPerThreadgroup),
                1u);
        const MTLSize threadsPerGrid = MTLSizeMake(beamCount, 1, 1);
        const MTLSize threadsPerThreadgroup = MTLSizeMake(threadWidth, 1, 1);
        [encoder dispatchThreads:threadsPerGrid threadsPerThreadgroup:threadsPerThreadgroup];
        [encoder endEncoding];

        auto complete = std::move(onComplete);
        auto error = std::move(onError);
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
          dispatch_async(dispatch_get_main_queue(), ^{
            (void) beamBuffer;
            if (completedCommandBuffer.status == MTLCommandBufferStatusError) {
                if (error) {
                    const char* message = completedCommandBuffer.error.localizedDescription.UTF8String;
                    error(message ? message : "MetalRenderer::readbackLidarBeamsAsPointCloudAsync command buffer failed");
                }
                return;
            }

            const ReadbackResult result{
                    static_cast<const unsigned char*>([outputBuffer contents]),
                    static_cast<unsigned int>(beamCount),
                    1u,
                    static_cast<unsigned int>(bytesPerRow),
                    Format::RGBA,
                    Type::Float,
                    true};

            try {
                complete(result);
            } catch (const std::exception& e) {
                if (error) error(e.what());
            } catch (...) {
                if (error) error("MetalRenderer::readbackLidarBeamsAsPointCloudAsync completion callback failed");
            }
          });
        }];

        if (temporaryCommandBuffer) {
            [commandBuffer commit];
        }
    } catch (const std::exception& e) {
        if (onError) {
            onError(e.what());
            return;
        }
        throw;
    } catch (...) {
        if (onError) {
            onError("MetalRenderer::readbackLidarBeamsAsPointCloudAsync failed");
            return;
        }
        throw;
    }
}

std::vector<unsigned char> MetalRenderer::Impl::readRGBPixels() {
    if (!currentCommandBuffer || !currentDrawable) {
        throw std::runtime_error("MetalRenderer::readRGBPixels requires an uncommitted frame; set autoClear=false, clear, render, then read");
    }

    // 读回保持 BGRA->RGB 拷贝；Linear/sRGB 的字节含义由当前 drawable 像素格式决定。
    id<MTLTexture> sourceTexture = currentDrawable.texture;
    const auto width = static_cast<NSUInteger>(sourceTexture.width);
    const auto height = static_cast<NSUInteger>(sourceTexture.height);
    constexpr NSUInteger bytesPerPixel = 4;
    const auto bytesPerRow = ((width * bytesPerPixel) + 255u) & ~255u;
    const auto byteLength = bytesPerRow * height;

    id<MTLBuffer> readbackBuffer = acquireReadbackBuffer(byteLength);

    id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
    [blitEncoder copyFromTexture:sourceTexture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                            toBuffer:readbackBuffer
                   destinationOffset:0
              destinationBytesPerRow:bytesPerRow
            destinationBytesPerImage:byteLength];
    [blitEncoder endEncoding];

    [currentCommandBuffer presentDrawable:currentDrawable];
    [currentCommandBuffer commit];
    [currentCommandBuffer waitUntilCompleted];
    releaseAllReadbackBuffers();

    const auto* bgra = static_cast<const unsigned char*>([readbackBuffer contents]);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
    for (NSUInteger y = 0; y < height; ++y) {
        const auto* srcRow = bgra + y * bytesPerRow;
        auto* dstRow = rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3u;
        for (NSUInteger x = 0; x < width; ++x) {
            dstRow[x * 3u + 0u] = srcRow[x * bytesPerPixel + 2u];
            dstRow[x * 3u + 1u] = srcRow[x * bytesPerPixel + 1u];
            dstRow[x * 3u + 2u] = srcRow[x * bytesPerPixel + 0u];
        }
    }

    currentCommandBuffer = nil;
    currentDrawable = nil;
    explicitFrameInProgress = false;
    screenCommandsEncoded = false;
    lastFrameWasExternallyAccessed = currentCommandBufferExternallyAccessed;
    currentCommandBufferExternallyAccessed = false;
    return rgb;
}

void MetalRenderer::Impl::setViewport(int x, int y, int width, int height) {
    viewport.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
}

void MetalRenderer::Impl::setScissor(int x, int y, int width, int height) {
    scissor.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
}

void MetalRenderer::Impl::applyViewport(id<MTLRenderCommandEncoder> encoder) const {
    const MTLViewport mtlViewport{
            viewport.x * pixelRatio,
            viewport.y * pixelRatio,
            viewport.z * pixelRatio,
            viewport.w * pixelRatio,
            0.0,
            1.0};
    [encoder setViewport:mtlViewport];
}

void MetalRenderer::Impl::applyScissor(id<MTLRenderCommandEncoder> encoder) const {
    if (!scissorTest) return;

    const auto maxWidth = static_cast<NSUInteger>(std::max(fbWidth, 0));
    const auto maxHeight = static_cast<NSUInteger>(std::max(fbHeight, 0));
    const auto x = clampToSize(scissor.x * pixelRatio, maxWidth);
    const auto y = clampToSize(scissor.y * pixelRatio, maxHeight);
    const auto maxX = clampToSize((scissor.x + scissor.z) * pixelRatio, maxWidth);
    const auto maxY = clampToSize((scissor.y + scissor.w) * pixelRatio, maxHeight);

    const MTLScissorRect rect{x, y, maxX > x ? maxX - x : 0, maxY > y ? maxY - y : 0};
    [encoder setScissorRect:rect];
}

id<MTLRenderPipelineState> MetalRenderer::Impl::getOrCreateScissorClearPipelineState(MTLPixelFormat format, NSUInteger sampleCount, bool clearColor, bool clearDepth) {
    const auto clampedSampleCount = std::max<NSUInteger>(sampleCount, 1u);
    const auto key = static_cast<std::uint64_t>(format) ^
                     (static_cast<std::uint64_t>(clampedSampleCount) << 16u) ^
                     (clearColor ? (1ull << 32u) : 0ull) ^
                     (clearDepth ? (1ull << 33u) : 0ull);

    auto it = scissorClearPipelineStates.find(key);
    if (it != scissorClearPipelineStates.end()) {
        return it->second;
    }

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:scissorClearShaderSource];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        NSString* msg = [NSString stringWithFormat:@"Failed to create scissor clear library: %@", error.localizedDescription];
        throw std::runtime_error([msg UTF8String]);
    }

    id<MTLFunction> vertexFunction = [library newFunctionWithName:@"scissorClearVertex"];
    id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"scissorClearFragment"];
    if (!vertexFunction || !fragmentFunction) {
        throw std::runtime_error("Failed to create scissor clear shader functions");
    }

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertexFunction;
    desc.fragmentFunction = fragmentFunction;
    desc.colorAttachments[0].pixelFormat = format;
    desc.colorAttachments[0].writeMask = clearColor ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
    desc.depthAttachmentPixelFormat = depthPixelFormat;
    desc.rasterSampleCount = clampedSampleCount;

    id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pso) {
        NSString* msg = [NSString stringWithFormat:@"Failed to create scissor clear PSO: %@", error.localizedDescription];
        throw std::runtime_error([msg UTF8String]);
    }

    scissorClearPipelineStates[key] = pso;
    return pso;
}

void MetalRenderer::Impl::performScissorClear(id<MTLRenderCommandEncoder> encoder, const Color& color, float alpha, MTLPixelFormat colorPixelFormat, bool clearColor, bool clearDepth) {
    if (!clearColor && !clearDepth) return;

    auto pso = getOrCreateScissorClearPipelineState(colorPixelFormat, activeRenderSampleCount, clearColor, clearDepth);
    [encoder setRenderPipelineState:pso];

    if (clearDepth) {
        if (!scissorClearDepthStencilState) {
            scissorClearDepthStencilState = createScissorClearDepthStencilState(device, true);
        }
        [encoder setDepthStencilState:scissorClearDepthStencilState];
    } else {
        if (!scissorClearNoDepthStencilState) {
            scissorClearNoDepthStencilState = createScissorClearDepthStencilState(device, false);
        }
        [encoder setDepthStencilState:scissorClearNoDepthStencilState];
    }

    const ScissorClearUniforms uniforms{{color.r, color.g, color.b, alpha}};
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];
    [encoder setDepthBias:0.f slopeScale:0.f clamp:0.f];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void MetalRenderer::Impl::resetDepthBiasCache() {
    currentDepthBiasFactor.reset();
    currentDepthBiasUnits.reset();
}

void MetalRenderer::Impl::applyDepthBias(id<MTLRenderCommandEncoder> encoder, const Material& material) {
    const auto factor = material.polygonOffset ? material.polygonOffsetFactor : 0.f;
    const auto units = material.polygonOffset ? material.polygonOffsetUnits : 0.f;

    if (currentDepthBiasFactor && currentDepthBiasUnits &&
        *currentDepthBiasFactor == factor &&
        *currentDepthBiasUnits == units) {
        return;
    }

    [encoder setDepthBias:units slopeScale:factor clamp:0.f];
    currentDepthBiasFactor = factor;
    currentDepthBiasUnits = units;
}

void MetalRenderer::Impl::configurePipelineColorFormats(metal::PipelineKey& key, MTLPixelFormat primaryFormat) const {
    key.colorPixelFormat = static_cast<std::uint64_t>(primaryFormat);
    key.colorAttachmentCount = static_cast<std::uint64_t>(activeColorAttachmentCount);
    key.colorPixelFormats.fill(0);

    const auto count = std::min<std::size_t>(
            static_cast<std::size_t>(activeColorAttachmentCount),
            key.colorPixelFormats.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto format = i < activeColorPixelFormats.size() ? activeColorPixelFormats[i] : primaryFormat;
        key.colorPixelFormats[i] = static_cast<std::uint64_t>(format);
    }
}

void MetalRenderer::Impl::bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder) {
    id<MTLTexture> metalTexture = placeholder;
    id<MTLSamplerState> sampler = defaultSampler;
    if (texture) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, allowPlaceholder);
        if (tex) {
            metalTexture = tex;
            sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
        }
    }
    [encoder setFragmentTexture:metalTexture atIndex:index];
    if (index == 0) {
        [encoder setFragmentSamplerState:sampler atIndex:0];
    }
}

void MetalRenderer::Impl::bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, Texture* texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder) {
    id<MTLTexture> metalTexture = placeholder;
    id<MTLSamplerState> sampler = defaultSampler;
    if (texture) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, allowPlaceholder);
        if (tex) {
            metalTexture = tex;
            sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
        }
    }
    [encoder setFragmentTexture:metalTexture atIndex:index];
    if (index == 0) {
        [encoder setFragmentSamplerState:sampler atIndex:0];
    }
}

id<MTLSamplerState> MetalRenderer::Impl::samplerForTexture(Texture* texture) {
    if (!texture) return defaultSampler;
    return (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
}

void MetalRenderer::Impl::bindCubeTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, NSUInteger index) {
    id<MTLTexture> metalTexture = whiteCubeTexture;
    if (texture && dynamic_cast<CubeTexture*>(texture.get()) != nullptr) {
        metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture);
    }
    [encoder setFragmentTexture:metalTexture atIndex:index];
}

void MetalRenderer::Impl::bindPassLightResources(id<MTLRenderCommandEncoder> encoder, const LightUniforms& lightUniforms, const ShadowResources& shadowResources) {
    [encoder setFragmentBytes:&lightUniforms length:sizeof(lightUniforms) atIndex:1];
    for (std::size_t i = 0; i < maxShadowMapsPerLightType; ++i) {
        id<MTLTexture> directionalTexture = shadowResources.directionalTextures[i] ? shadowResources.directionalTextures[i] : whiteDepthTexture;
        id<MTLTexture> spotTexture = shadowResources.spotTextures[i] ? shadowResources.spotTextures[i] : whiteDepthTexture;
        id<MTLTexture> pointTexture = shadowResources.pointTextures[i] ? shadowResources.pointTextures[i] : whiteDepthTexture;
        [encoder setFragmentTexture:directionalTexture atIndex:7 + i];
        [encoder setFragmentTexture:spotTexture atIndex:11 + i];
        [encoder setFragmentTexture:pointTexture atIndex:15 + i];
    }
    [encoder setFragmentSamplerState:shadowSampler atIndex:1];
}

void MetalRenderer::Impl::renderBackgroundCube(id<MTLRenderCommandEncoder> encoder, CubeTexture& cubeTexture, Camera& camera, MTLPixelFormat colorPixelFormat) {
    if (!backgroundCubeGeometry) {
        backgroundCubeGeometry = BoxGeometry::create(1, 1, 1);
        backgroundCubeGeometry->deleteAttribute("normal");
        backgroundCubeGeometry->deleteAttribute("uv");
    }

    trackGeometry(*backgroundCubeGeometry);

    auto* posAttr = getFloatAttribute(*backgroundCubeGeometry, "position");
    if (!posAttr) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateBackgroundCubeVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateBackgroundCubeFragmentFunction();
    pipelineKey.alphaBlending = false;
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
    pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

    id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
    [encoder setRenderPipelineState:pso];

    id<MTLDepthStencilState> backgroundDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
            false,
            false,
            DepthFunc::Always);
    [encoder setDepthStencilState:backgroundDepthStencilState];

    const auto faceCullingState = metal::computeFaceCullingState(Side::Back, false, false);
    [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];

    bindDrawAttributes(encoder, *backgroundCubeGeometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

    Matrix4 modelMatrix;
    modelMatrix.copyPosition(*camera.matrixWorld);

    Matrix4 mvp;
    mvp.copy(metal::convertProjectionToMetalClipSpace(camera.projectionMatrix));
    mvp.multiply(camera.matrixWorldInverse);
    mvp.multiply(modelMatrix);

    BackgroundCubeUniforms uniforms{};
    copyMatrix(mvp, uniforms.mvp);
    copyMatrix(modelMatrix, uniforms.modelMatrix);
    uniforms.opacity = 1.f;
    uniforms.flipEnvMap = cubeTexture._needsFlipEnvMap ? 1.f : -1.f;
    uniforms.toneMappingType = static_cast<std::uint32_t>(renderer.toneMapping);
    uniforms.toneMappingExposure = renderer.toneMappingExposure;
    uniforms.toneMapped = 1u;

    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    id<MTLTexture> metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(cubeTexture);
    [encoder setFragmentTexture:metalTexture atIndex:0];
    [encoder setFragmentSamplerState:samplerForTexture(&cubeTexture) atIndex:0];

    drawGeometry(encoder, *backgroundCubeGeometry, *posAttr, MTLPrimitiveTypeTriangle);
}

void MetalRenderer::Impl::generateRenderTargetMipmapsIfNeeded(RenderTarget& target, id<MTLTexture> colorTexture) {
    if (!target.texture || !target.texture->generateMipmaps || !colorTexture || colorTexture.mipmapLevelCount <= 1) return;

    id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
    [blitEncoder generateMipmapsForTexture:colorTexture];
    [blitEncoder endEncoding];
}

void MetalRenderer::Impl::addPreRenderJob(const RenderJob& job) {
    if (!job.initiator || !job.camera || !job.renderTarget) return;

    preRenderJobs.emplace_back(job);
}

void MetalRenderer::Impl::collectPreRenderJobs(Scene& scene, Camera& camera) {
    if (renderingPrePass) return;

    preRenderJobs.clear();
    Matrix4 projScreenMatrix;
    projScreenMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
    Frustum frustum;
    frustum.setFromProjectionMatrix(projScreenMatrix);
    collectPreRenderables(scene, camera, frustum, renderer);
}

void MetalRenderer::Impl::renderPreRenderJobs(Scene& scene) {
    if (renderingPrePass || preRenderJobs.empty()) return;

    const auto jobs = std::move(preRenderJobs);
    preRenderJobs.clear();

    const auto previousRenderTarget = renderTarget;
    const auto previousActiveCubeFace = activeCubeFace;
    const auto previousActiveMipmapLevel = activeMipmapLevel;
    const auto previousActiveLayer = activeLayer;
    const auto previousClearRequested = clearRequested;
    const auto previousClearColorFlag = clearColorFlag;
    const auto previousClearDepthFlag = clearDepthFlag;
    const auto previousExplicitFrameInProgress = explicitFrameInProgress;
    const auto previousRenderingPrePass = renderingPrePass;

    renderingPrePass = true;

    for (const auto& job : jobs) {
        if (!job.initiator || !job.camera || !job.renderTarget || renderTarget == job.renderTarget) continue;

        const auto previousVisible = job.initiator->visible;

        const auto restore = [&] {
            job.initiator->visible = previousVisible;
            renderTarget = previousRenderTarget;
            activeCubeFace = previousActiveCubeFace;
            activeMipmapLevel = previousActiveMipmapLevel;
            activeLayer = previousActiveLayer;
            clearRequested = previousClearRequested;
            clearColorFlag = previousClearColorFlag;
            clearDepthFlag = previousClearDepthFlag;
            explicitFrameInProgress = previousExplicitFrameInProgress;
            renderingPrePass = previousRenderingPrePass;
        };

        job.initiator->visible = false;
        renderTarget = job.renderTarget;
        activeCubeFace = 0;
        activeMipmapLevel = 0;
        activeLayer = 0;
        clearRequested = true;
        clearColorFlag = true;
        clearDepthFlag = true;
        explicitFrameInProgress = false;

        try {
            render(scene, *job.camera, true);
        } catch (...) {
            restore();
            throw;
        }

        restore();
        renderingPrePass = true;
    }

    renderingPrePass = previousRenderingPrePass;
}

void MetalRenderer::Impl::render(Scene& scene, Camera& camera, bool autoClear) {
    SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::render");
    throwIfRendererCallbackOperation(recursiveRenderCallbackMessage);
    if (currentCommandBuffer) {
        const auto now = std::chrono::steady_clock::now();
        const auto hasPreviousRender = lastRenderTime.time_since_epoch().count() != 0;
        if (hasPreviousRender) {
            const auto elapsed = std::chrono::duration<float, std::milli>(now - lastRenderTime).count();
            const auto isOrderedScissorContinuation = scissorTest && (scissor.x > lastScissor.x || scissor.y > lastScissor.y);
            if (!renderTarget && elapsed > frameBoundaryThresholdMs && !isOrderedScissorContinuation && screenCommandsEncoded) {
                commitPendingFrame();
            }
        }

        if (currentCommandBuffer && !explicitFrameInProgress && screenCommandsEncoded) {
            bool isNewFrame = false;
            if (scissorTest) {
                if (scissor.x < lastScissor.x || scissor.y < lastScissor.y) {
                    isNewFrame = true;
                }
            } else {
                isNewFrame = true;
            }

            if (isNewFrame) {
                commitPendingFrame();
            }
        }
    }
    updateMetalLayerPixelFormat();

    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::prepareScene");
        scene.updateMatrixWorld(false);
        metal::prepareCameraForRender(camera);
        updateLODs(scene, camera);
    }

    SceneLightSet sceneLights;
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::collectLights");
        collectLights(scene, sceneLights);
    }

    Color effectiveClearColor = clearColor;
    float effectiveClearAlpha = clearAlpha;
    if (!scene.background.empty() && scene.background.isColor()) {
        effectiveClearColor.copy(scene.background.color());
    }
    if (!renderingPrePass) {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::preRenderJobs");
        collectPreRenderJobs(scene, camera);
        renderPreRenderJobs(scene);
    }

    if (!currentCommandBuffer) {
        ensureFrameStarted();
    }

    auto shadowResources = [&] {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::renderShadowPasses");
        return renderShadowPasses(scene, sceneLights);
    }();
    auto lightUniforms = [&] {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::buildLightUniforms");
        return buildLightUniforms(sceneLights, shadowResources);
    }();

    id<MTLTexture> colorTexture = nil;
    id<MTLTexture> passDepthTexture = nil;
    MTLPixelFormat colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    MetalRenderTargetResources* activeRenderTargetResources = nullptr;

    if (renderTarget) {
        auto& resources = getOrCreateRenderTargetResources(*renderTarget);
        activeRenderTargetResources = &resources;
        colorTexture = resources.colorTextures.empty() ? nil : resources.colorTextures.front();
        passDepthTexture = resources.depthTexture;
        colorPixelFormat = resources.colorPixelFormats.empty() ? MTLPixelFormatInvalid : resources.colorPixelFormats.front();
        activeRenderSampleCount = 1;
        activeColorAttachmentCount = static_cast<NSUInteger>(std::max<std::size_t>(resources.colorTextures.size(), 1));
        activeColorPixelFormats = resources.colorPixelFormats;
    } else {
        if (!ensureDrawable()) {
            commitPendingFrame();
            return;
        }
        colorTexture = currentDrawable.texture;
        screenCommandsEncoded = true;
        passDepthTexture = depthTexture;
        colorPixelFormat = colorTexture.pixelFormat;
        activeRenderSampleCount = drawableSampleCount;
        activeColorAttachmentCount = 1;
        activeColorPixelFormats = {colorPixelFormat};
        if (activeRenderSampleCount > 1) {
            colorTexture = getOrCreateMultisampleColorTexture(colorPixelFormat);
        }
    }

    const auto shouldClear = autoClear || clearRequested;
    const auto activeScissorTest = renderTarget ? renderTarget->scissorTest : scissorTest;
    auto* cubeRenderTargetTexture = renderTarget ? dynamic_cast<CubeTexture*>(renderTarget->texture.get()) : nullptr;
    const auto isArrayRenderTarget = renderTarget && !cubeRenderTargetTexture && renderTarget->depth > 1;
    const RenderTargetClearKey clearKey{
            renderTarget,
            cubeRenderTargetTexture ? activeCubeFace : 0,
            renderTarget ? activeMipmapLevel : 0,
            isArrayRenderTarget ? activeLayer : 0};
    const auto isFirstRender = !clearedTargetsInFrame.count(clearKey);
    const auto canUseMetalClear = shouldClear && !activeScissorTest && isFirstRender;

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    if (renderTarget && activeRenderTargetResources) {
        for (NSUInteger i = 0; i < activeRenderTargetResources->colorTextures.size(); ++i) {
            auto attachmentTexture = activeRenderTargetResources->colorTextures[i];
            if (static_cast<NSUInteger>(activeMipmapLevel) >= attachmentTexture.mipmapLevelCount ||
                (passDepthTexture && static_cast<NSUInteger>(activeMipmapLevel) >= passDepthTexture.mipmapLevelCount)) {
                throw std::out_of_range("MetalRenderer::render activeMipmapLevel exceeds render target attachment mip levels");
            }
            passDesc.colorAttachments[i].texture = attachmentTexture;
            passDesc.colorAttachments[i].level = static_cast<NSUInteger>(activeMipmapLevel);
            if (cubeRenderTargetTexture) {
                passDesc.colorAttachments[i].slice = static_cast<NSUInteger>(activeCubeFace);
            } else if (isArrayRenderTarget) {
                passDesc.colorAttachments[i].slice = static_cast<NSUInteger>(activeLayer);
            }
            passDesc.colorAttachments[i].loadAction = canUseMetalClear && clearColorFlag ? MTLLoadActionClear : MTLLoadActionLoad;
            passDesc.colorAttachments[i].clearColor = MTLClearColorMake(effectiveClearColor.r, effectiveClearColor.g, effectiveClearColor.b, effectiveClearAlpha);
            passDesc.colorAttachments[i].storeAction = MTLStoreActionStore;
        }
    } else {
        passDesc.colorAttachments[0].texture = colorTexture;
        passDesc.colorAttachments[0].loadAction = canUseMetalClear && clearColorFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(effectiveClearColor.r, effectiveClearColor.g, effectiveClearColor.b, effectiveClearAlpha);
        if (activeRenderSampleCount > 1) {
            passDesc.colorAttachments[0].resolveTexture = currentDrawable.texture;
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
        } else {
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        }
    }

    passDesc.depthAttachment.texture = passDepthTexture;
    if (renderTarget) {
        passDesc.depthAttachment.level = static_cast<NSUInteger>(activeMipmapLevel);
    }
    passDesc.depthAttachment.loadAction = canUseMetalClear && clearDepthFlag ? MTLLoadActionClear : MTLLoadActionLoad;
    passDesc.depthAttachment.clearDepth = 1.0;
    passDesc.depthAttachment.storeAction = MTLStoreActionStore;

    RenderCommandEncoderScope encoderScope{[currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc]};
    id<MTLRenderCommandEncoder> encoder = encoderScope.encoder;

    auto configureActiveEncoder = [&] {
        resetDepthBiasCache();
        [encoder setDepthStencilState:depthStencilState];
        if (renderTarget) {
            const MTLViewport targetViewport{
                    renderTarget->viewport.x,
                    renderTarget->viewport.y,
                    renderTarget->viewport.z,
                    renderTarget->viewport.w,
                    0.0,
                    1.0};
            [encoder setViewport:targetViewport];
            if (renderTarget->scissorTest && activeRenderTargetResources) {
                const auto x = clampToSize(renderTarget->scissor.x, activeRenderTargetResources->width);
                const auto y = clampToSize(renderTarget->scissor.y, activeRenderTargetResources->height);
                const auto maxX = clampToSize(renderTarget->scissor.x + renderTarget->scissor.z, activeRenderTargetResources->width);
                const auto maxY = clampToSize(renderTarget->scissor.y + renderTarget->scissor.w, activeRenderTargetResources->height);
                const MTLScissorRect rect{x, y, maxX > x ? maxX - x : 0, maxY > y ? maxY - y : 0};
                [encoder setScissorRect:rect];
            }
        } else {
            applyViewport(encoder);
            applyScissor(encoder);
        }
    };

    configureActiveEncoder();

    if (shouldClear && !canUseMetalClear && !pixelFormatIsInteger(colorPixelFormat)) {
        const MTLViewport clearViewport{
                0.0,
                0.0,
                static_cast<double>(colorTexture.width),
                static_cast<double>(colorTexture.height),
                0.0,
                1.0};
        [encoder setViewport:clearViewport];
        performScissorClear(encoder, effectiveClearColor, effectiveClearAlpha, colorPixelFormat, clearColorFlag, clearDepthFlag);
        if (renderTarget) {
            const MTLViewport targetViewport{
                    renderTarget->viewport.x,
                    renderTarget->viewport.y,
                    renderTarget->viewport.z,
                    renderTarget->viewport.w,
                    0.0,
                    1.0};
            [encoder setViewport:targetViewport];
        } else {
            applyViewport(encoder);
        }
    }

    if (!scene.background.empty() && scene.background.isTexture()) {
        if (auto cubeTexture = std::dynamic_pointer_cast<CubeTexture>(scene.background.texture())) {
            renderBackgroundCube(encoder, *cubeTexture, camera, colorPixelFormat);
        }
    }

    Matrix4 projScreenMatrix;
    projScreenMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
    Frustum frustum;
    frustum.setFromProjectionMatrix(projScreenMatrix);
    std::vector<Object3D*> collectedRenderables;
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::collectRenderables");
        collectRenderables(scene, camera, frustum, collectedRenderables);
    }
    metal::MetalRenderList renderList;
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::buildRenderList");
        buildRenderList(collectedRenderables, camera, renderList);
    }
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::bindPassLightResources");
        bindPassLightResources(encoder, lightUniforms, shadowResources);
    }

    auto invokeBeforeRenderCallback = [&](Object3D& obj, BufferGeometry* geometry, Material* material, std::optional<GeometryGroup> group) {
        if (!obj.onBeforeRender) return;

        invokeOnBeforeRender(obj, renderer, scene, camera, geometry, material, group);
    };

    auto invokeAfterRenderCallback = [&](Object3D& obj, BufferGeometry* geometry, Material* material, std::optional<GeometryGroup> group) {
        if (!obj.onAfterRender) return;

        invokeOnAfterRender(obj, renderer, scene, camera, geometry, material, group);
    };

    auto renderItems = [&](const std::vector<metal::MetalRenderItem>& items) {
        Material* overrideMaterial = scene.overrideMaterial ? scene.overrideMaterial.get() : nullptr;
        const bool hasOverrideMaterial = overrideMaterial != nullptr;
        for (const auto& item : items) {
            auto* obj = item.object;
            auto* geometry = item.geometry;
            auto* material = overrideMaterial ? overrideMaterial : item.material;
            if (!obj || !geometry || !material || !material->visible) continue;

            invokeBeforeRenderCallback(*obj, geometry, material, item.group);

            if (auto* sky = hasOverrideMaterial ? nullptr : dynamic_cast<Sky*>(obj)) {
                renderSky(encoder, *sky, *geometry, *material, camera, colorPixelFormat);
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }

            if (auto* water = hasOverrideMaterial ? nullptr : dynamic_cast<Water*>(obj)) {
                renderWater(encoder, scene, *water, *geometry, *material, camera, colorPixelFormat);
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }

            if (auto* reflector = hasOverrideMaterial ? nullptr : dynamic_cast<Reflector*>(obj)) {
                renderReflector(encoder, scene, *reflector, *geometry, *material, camera, colorPixelFormat);
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }

            if (auto* sprite = hasOverrideMaterial ? nullptr : dynamic_cast<Sprite*>(obj)) {
                renderSprite(encoder, scene, *sprite, *geometry, *material, camera, colorPixelFormat);
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }

            if (auto* points = dynamic_cast<Points*>(obj)) {
                renderPoints(encoder, scene, *points, *geometry, *material, camera, colorPixelFormat, item.group);
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }

            if (auto* line = dynamic_cast<Line*>(obj)) {
                renderLine(encoder, *line, *geometry, *material, camera, colorPixelFormat, item.group);
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }

            if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                if (material->is<RawShaderMaterial>()) {
                    renderRawShader(encoder, *mesh, *geometry, *material, camera, colorPixelFormat, item.group);
                    invokeAfterRenderCallback(*obj, geometry, material, item.group);
                    continue;
                }
                if (auto* shaderMaterial = material->as<ShaderMaterial>()) {
                    if (shaderMaterial->uniforms.count("tDepth") > 0 &&
                        shaderMaterial->uniforms.count("cameraNear") > 0 &&
                        shaderMaterial->uniforms.count("cameraFar") > 0) {
                        if (shaderMaterial->uniforms.count("tDiffuse") > 0) {
                            renderDepthTexture(encoder, *mesh, *geometry, *shaderMaterial, camera, colorPixelFormat, item.group);
                        } else {
                            renderLinearDepthTexture(encoder, *mesh, *geometry, *shaderMaterial, camera, colorPixelFormat, item.group);
                        }
                        invokeAfterRenderCallback(*obj, geometry, material, item.group);
                        continue;
                    }
                }
            }

            bool isWireframe = false;
            if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                if (auto* wf = dynamic_cast<MaterialWithWireframe*>(material)) {
                    isWireframe = wf->wireframe;
                }
            }

            trackGeometry(*geometry);

            auto* posAttr = getFloatAttribute(*geometry, "position");
            if (!posAttr) {
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }
            auto* normAttr = getFloatAttribute(*geometry, "normal");
            auto* uvAttr = getFloatAttribute(*geometry, "uv");
            auto* colorAttr = getFloatAttribute(*geometry, "color");
            auto* instancedMesh = dynamic_cast<InstancedMesh*>(obj);
            if (instancedMesh && instancedMesh->count() == 0) {
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }
            auto* skinnedMesh = dynamic_cast<SkinnedMesh*>(obj);

            const auto shadingParams = extractShadingParams(renderer, scene, *material, camera, obj->receiveShadow);
            const bool useClipping = shadingParams.numClippingPlanes > 0u;
            const bool useUv = uvAttr && needsUv(shadingParams);
            const bool useVertexColors = material->vertexColors && colorAttr;
            const bool useNormal = normAttr != nullptr;
            const auto* flatMaterial = dynamic_cast<MaterialWithFlatShading*>(material);
            const bool useFlatShading = useNormal && flatMaterial && flatMaterial->flatShading;
            const bool useLights = useNormal && (isLightingMaterial(*material) || isShadowMaterial(*material));
            const bool useSkinning = skinnedMesh && skinnedMesh->skeleton && hasSkinningAttributes(*geometry);
            const bool useInstancing = instancedMesh && instancedMesh->count() > 0;
            const bool useInstanceColor = useInstancing && instancedMesh->instanceColor() != nullptr;
            const bool useTangent = useNormal && useUv;
            const bool useMorphTargets = wantsMorphTargets(*material, *geometry);
            const bool useMorphNormals = wantsMorphNormals(*material, *geometry, useNormal, useMorphTargets);
            if (useInstancing && useSkinning) {
                std::cerr << "MetalRenderer: skipping unsupported instanced skinned renderable " << obj->id << "\n";
                invokeAfterRenderCallback(*obj, geometry, material, item.group);
                continue;
            }
            if (useMorphTargets && morphTargets) {
                morphTargets->update(obj, geometry, material, useMorphNormals);
            }

            metal::ShaderProgramKey shaderKey;
            shaderKey.useMap = useUv;
            shaderKey.useVertexColors = useVertexColors;
            shaderKey.useNormal = useNormal;
            shaderKey.flatShading = useFlatShading;
            shaderKey.useSkinning = useSkinning;
            shaderKey.useLights = useLights;
            shaderKey.useInstancing = useInstancing;
            shaderKey.useInstanceColor = useInstanceColor;
            shaderKey.doubleSided = material->side == Side::Double;
            shaderKey.flipSided = material->side == Side::Back;
            shaderKey.useClipping = useClipping;
            shaderKey.useMorphTargets = useMorphTargets;
            shaderKey.useMorphNormals = useMorphNormals;

            std::uint16_t vertexLayoutBitmask = vertexLayoutPosition;
            if (useNormal) vertexLayoutBitmask |= vertexLayoutNormal;
            if (useUv) vertexLayoutBitmask |= vertexLayoutUv;
            if (useVertexColors) vertexLayoutBitmask |= vertexLayoutColor;
            if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;
            if (useTangent) vertexLayoutBitmask |= vertexLayoutTangent;
            if (useMorphTargets) vertexLayoutBitmask |= vertexLayoutMorphTargets;
            if (useMorphNormals) vertexLayoutBitmask |= vertexLayoutMorphNormals;

            metal::PipelineKey pipelineKey;
            pipelineKey.vertexFunction = shaderManager->getOrCreateVertexFunction(shaderKey);
            pipelineKey.fragmentFunction = shaderManager->getOrCreateFragmentFunction(shaderKey);
            configurePipelineBlending(pipelineKey, *material);
            pipelineKey.vertexLayoutBitmask = vertexLayoutBitmask;
            configurePipelineColorFormats(pipelineKey, colorPixelFormat);
            pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
            [encoder setRenderPipelineState:pso];
            id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                    material->depthTest,
                    material->depthWrite,
                    material->depthFunc);
            [encoder setDepthStencilState:materialDepthStencilState];
            const auto frontFaceCW = obj->matrixWorld->determinant() < 0;
            const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, isWireframe);
            [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
            [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
            [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
            applyDepthBias(encoder, *material);

            bindDrawAttributes(encoder, *geometry, *posAttr, normAttr, uvAttr, colorAttr, useNormal, useUv, useVertexColors, useTangent, useMorphTargets, useMorphNormals);
            if (useSkinning) {
                bindSkinning(encoder, *geometry, skinnedMesh);
            }
            NSUInteger instanceCount = 1;
            if (useInstancing) {
                bindInstancing(encoder, *instancedMesh, useInstanceColor);
                instanceCount = static_cast<NSUInteger>(instancedMesh->count());
            }

            TransformUniforms transformUniforms{};
            computeTransformUniforms(camera, *obj, transformUniforms, useInstancing);
            if (useMorphTargets && morphTargets) {
                writeMorphTargetUniforms(*morphTargets, transformUniforms);
            }
            [encoder setVertexBytes:&transformUniforms length:sizeof(transformUniforms) atIndex:4];

            [encoder setFragmentBytes:&shadingParams length:sizeof(shadingParams) atIndex:0];

            auto* envMaterial = dynamic_cast<MaterialWithEnvMap*>(material);
            if (useUv) {
                auto* mapMaterial = dynamic_cast<MaterialWithMap*>(material);
                auto* normalMaterial = dynamic_cast<MaterialWithNormalMap*>(material);
                auto* roughnessMaterial = dynamic_cast<MaterialWithRoughness*>(material);
                auto* metalnessMaterial = dynamic_cast<MaterialWithMetalness*>(material);
                auto* aoMaterial = dynamic_cast<MaterialWithAoMap*>(material);
                auto* emissiveMaterial = dynamic_cast<MaterialWithEmissive*>(material);
                auto* specularMaterial = dynamic_cast<MaterialWithSpecularMap*>(material);
                bindTextureOrPlaceholder(encoder, mapMaterial ? mapMaterial->map : nullptr, whiteTexture, 0);
                bindTextureOrPlaceholder(encoder, normalMaterial ? normalMaterial->normalMap : nullptr, normalTexture, 1);
                bindTextureOrPlaceholder(encoder, roughnessMaterial ? roughnessMaterial->roughnessMap : nullptr, whiteTexture, 2);
                bindTextureOrPlaceholder(encoder, metalnessMaterial ? metalnessMaterial->metalnessMap : nullptr, blackTexture, 3);
                bindTextureOrPlaceholder(encoder, aoMaterial ? aoMaterial->aoMap : nullptr, whiteTexture, 4);
                bindTextureOrPlaceholder(encoder, emissiveMaterial ? emissiveMaterial->emissiveMap : nullptr, whiteTexture, 5);
                bindTextureOrPlaceholder(encoder, specularMaterial ? specularMaterial->specularMap : nullptr, whiteTexture, 19);
            }
            if (useLights) {
                bindCubeTextureOrPlaceholder(encoder, envMaterial ? envMaterial->envMap : nullptr, 6);
            }
            if (!useUv && useLights) {
                [encoder setFragmentSamplerState:defaultSampler atIndex:0];
            }

            drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle, instanceCount, item.group);
            invokeAfterRenderCallback(*obj, geometry, material, item.group);
        }
    };

    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::renderOpaque");
        renderItems(renderList.opaque);
    }
    {
        SPARK_TRACE_SCOPE("threepp.metal", "MetalRenderer::renderTransparent");
        renderItems(renderList.transparent);
    }

    encoderScope.end();
    if (activeRenderTargetResources) {
        for (auto colorAttachmentTexture : activeRenderTargetResources->colorTextures) {
            generateRenderTargetMipmapsIfNeeded(*renderTarget, colorAttachmentTexture);
        }
    }

    clearRequested = false;
    clearColorFlag = true;
    clearDepthFlag = true;

    lastScissor = scissor;
    lastRenderTime = std::chrono::steady_clock::now();
    clearedTargetsInFrame.insert(clearKey);

    if (autoClear && !renderTarget) {
        if (!lastFrameWasExternallyAccessed) {
            if (!scissorTest) {
                commitPendingFrame();
            }
        }
    }
}
MetalRenderer::MetalRenderer(Window& window)
    : pimpl_(std::make_unique<Impl>(*this, window)) {}

void MetalRenderer::render(Scene& scene, Camera& camera) {
    pimpl_->render(scene, camera, autoClear);
}

void MetalRenderer::endFrame() {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->commitPendingFrame();
}

void MetalRenderer::setSize(std::pair<int, int> size) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->setSize(size);
}

WindowSize MetalRenderer::size() const {
    return pimpl_->window.size();
}

void* MetalRenderer::device() const {
    return (__bridge void*) pimpl_->device;
}

void* MetalRenderer::currentCommandBuffer() const {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->ensureFrameStarted();
    pimpl_->currentCommandBufferExternallyAccessed = true;
    return (__bridge void*) pimpl_->currentCommandBuffer;
}

void* MetalRenderer::currentDrawableTexture() const {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->ensureFrameStarted();
    pimpl_->currentCommandBufferExternallyAccessed = true;
    if (!pimpl_->ensureDrawable()) return nullptr;
    return (__bridge void*) pimpl_->currentDrawable.texture;
}

void MetalRenderer::setClearColor(const Color& color, float alpha) {
    pimpl_->setClearColor(color, alpha);
}

void MetalRenderer::clear(bool color, bool depth, bool stencil) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->clear(color, depth, stencil);
}

void MetalRenderer::clearDepth() {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->clear(false, true, false);
}

void MetalRenderer::setViewport(const Vector4& v) {
    pimpl_->setViewport(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z), static_cast<int>(v.w));
}

void MetalRenderer::setViewport(int x, int y, int width, int height) {
    pimpl_->setViewport(x, y, width, height);
}

void MetalRenderer::setViewport(const std::pair<int, int>& pos, const std::pair<int, int>& size) {
    pimpl_->setViewport(pos.first, pos.second, size.first, size.second);
}

void MetalRenderer::setScissor(const Vector4& v) {
    pimpl_->setScissor(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z), static_cast<int>(v.w));
}

void MetalRenderer::setScissor(int x, int y, int width, int height) {
    pimpl_->setScissor(x, y, width, height);
}

void MetalRenderer::setScissor(const std::pair<int, int>& pos, const std::pair<int, int>& size) {
    pimpl_->setScissor(pos.first, pos.second, size.first, size.second);
}

void MetalRenderer::setScissorTest(bool boolean) {
    pimpl_->scissorTest = boolean;
}

void MetalRenderer::setRenderTarget(RenderTarget* renderTarget, int activeCubeFace, int activeMipmapLevel, int activeLayer) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    validateRenderTargetSelection(renderTarget, activeCubeFace, activeMipmapLevel, activeLayer);
    pimpl_->renderTarget = renderTarget;
    pimpl_->activeCubeFace = activeCubeFace;
    pimpl_->activeMipmapLevel = activeMipmapLevel;
    pimpl_->activeLayer = activeLayer;
}

RenderTarget* MetalRenderer::getRenderTarget() {
    return pimpl_->renderTarget;
}

void MetalRenderer::addPreRenderJob(const RenderJob& job) {
    pimpl_->addPreRenderJob(job);
}

void MetalRenderer::copyFramebufferToTexture(const Vector2& position, Texture& texture, int level) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->copyFramebufferToTexture(position, texture, level);
}

void MetalRenderer::copyTextureToImage(Texture& texture) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->copyTextureToImage(texture);
}

std::future<void> MetalRenderer::copyTextureToImageAsync(Texture& texture) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    return pimpl_->copyTextureToImageAsync(texture);
}

bool MetalRenderer::supportsAsyncPixelReadback() const noexcept {
    return true;
}

void MetalRenderer::setUseLowPriorityQueue(bool useLowPriority) {
    pimpl_->useLowPriorityQueue = useLowPriority;
}

void MetalRenderer::submitLowPriority() {
    pimpl_->submitLowPriority();
}

void* MetalRenderer::createEvent() {
    return pimpl_->createEvent();
}

void MetalRenderer::encodeSignalEvent(void* event, std::uint64_t value) {
    pimpl_->encodeSignalEvent(event, value);
}

void MetalRenderer::encodeWaitEventOnCurrentFrame(void* event, std::uint64_t value) {
    pimpl_->encodeWaitEventOnCurrentFrame(event, value);
}

std::future<PixelReadbackBuffer> MetalRenderer::readRenderTargetPixelsAsync(
        const PixelReadbackRequest& request) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    return pimpl_->readRenderTargetPixelsAsync(request);
}

MaterialPrewarmStatus MetalRenderer::prewarmMaterial(RawShaderMaterial& material) {
    MaterialPrewarmRequest request;
    request.material = &material;
    return prewarmMaterial(request);
}

MaterialPrewarmStatus MetalRenderer::prewarmMaterial(const MaterialPrewarmRequest& request) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    return pimpl_->prewarmMaterial(request);
}

void MetalRenderer::copyTexturesToImages(const std::vector<Texture*>& textures) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->copyTexturesToImages(textures);
}

void MetalRenderer::readbackTextureAsync(Texture& texture,
                                         std::function<void(const ReadbackResult& result)> onComplete,
                                         std::function<void(const std::string& error)> onError) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->readbackTextureAsync(texture, std::move(onComplete), std::move(onError));
}

void MetalRenderer::readbackLidarDepthAsPointCloudAsync(Texture& packedDepthTexture,
                                                        const std::array<float, 16>& matrixWorld,
                                                        float farPlane,
                                                        std::function<void(const ReadbackResult& result)> onComplete,
                                                        std::function<void(const std::string& error)> onError) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->readbackLidarDepthAsPointCloudAsync(packedDepthTexture, matrixWorld, farPlane, std::move(onComplete), std::move(onError));
}

void MetalRenderer::readbackLidarBeamsAsPointCloudAsync(const std::array<Texture*, 6>& packedDepthTextures,
                                                        const std::array<std::array<float, 16>, 6>& matrixWorldPerFace,
                                                        std::span<const MetalLidarBeamSample> beams,
                                                        float farPlane,
                                                        std::function<void(const ReadbackResult& result)> onComplete,
                                                        std::function<void(const std::string& error)> onError) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    pimpl_->readbackLidarBeamsAsPointCloudAsync(packedDepthTextures, matrixWorldPerFace, beams, farPlane, std::move(onComplete), std::move(onError));
}

std::future<void> MetalRenderer::copyTexturesToImagesAsync(const std::vector<Texture*>& textures) {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    return pimpl_->copyTexturesToImagesAsync(textures);
}

std::optional<void*> MetalRenderer::getMetalTexture(Texture& texture) const {
    auto* metalTexture = pimpl_->textureManager->getOrCreateTexture(texture);
    if (!metalTexture) return std::nullopt;
    return metalTexture;
}

std::vector<unsigned char> MetalRenderer::readRGBPixels() {
    throwIfRendererCallbackOperation(rendererCallbackOperationMessage);
    return pimpl_->readRGBPixels();
}

MetalShadowMap& MetalRenderer::shadowMap() {
    return pimpl_->shadowMapState;
}

const MetalShadowMap& MetalRenderer::shadowMap() const {
    return pimpl_->shadowMapState;
}

MetalRenderer::~MetalRenderer() = default;
