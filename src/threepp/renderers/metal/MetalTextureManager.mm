#import "MetalTextureManager.hpp"

#import "threepp/constants.hpp"
#import "threepp/core/EventDispatcher.hpp"
#import "threepp/textures/Texture.hpp"

#import <Metal/Metal.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace threepp::metal {

    namespace {

        bool isMipmapFilter(Filter filter) {
            return filter == Filter::NearestMipmapNearest
                || filter == Filter::NearestMipmapLinear
                || filter == Filter::LinearMipmapNearest
                || filter == Filter::LinearMipmapLinear;
        }

        MTLSamplerAddressMode toAddressMode(TextureWrapping wrapping) {
            switch (wrapping) {
                case TextureWrapping::Repeat:
                    return MTLSamplerAddressModeRepeat;
                case TextureWrapping::MirroredRepeat:
                    return MTLSamplerAddressModeMirrorRepeat;
                case TextureWrapping::ClampToEdge:
                default:
                    return MTLSamplerAddressModeClampToEdge;
            }
        }

        MTLSamplerMinMagFilter toMinMagFilter(Filter filter) {
            switch (filter) {
                case Filter::Nearest:
                case Filter::NearestMipmapNearest:
                case Filter::NearestMipmapLinear:
                    return MTLSamplerMinMagFilterNearest;
                default:
                    return MTLSamplerMinMagFilterLinear;
            }
        }

        MTLSamplerMipFilter toMipFilter(Filter filter) {
            switch (filter) {
                case Filter::NearestMipmapNearest:
                case Filter::LinearMipmapNearest:
                    return MTLSamplerMipFilterNearest;
                case Filter::NearestMipmapLinear:
                case Filter::LinearMipmapLinear:
                    return MTLSamplerMipFilterLinear;
                default:
                    return MTLSamplerMipFilterNotMipmapped;
            }
        }

        struct SamplerKey {
            TextureWrapping wrapS;
            TextureWrapping wrapT;
            Filter magFilter;
            Filter minFilter;

            bool operator==(const SamplerKey& other) const {
                return wrapS == other.wrapS
                    && wrapT == other.wrapT
                    && magFilter == other.magFilter
                    && minFilter == other.minFilter;
            }
        };

        struct SamplerKeyHash {
            std::size_t operator()(const SamplerKey& key) const {
                auto h1 = std::hash<int>{}(as_integer(key.wrapS));
                auto h2 = std::hash<int>{}(as_integer(key.wrapT));
                auto h3 = std::hash<int>{}(as_integer(key.magFilter));
                auto h4 = std::hash<int>{}(as_integer(key.minFilter));
                return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
            }
        };

    }// namespace

    struct MetalTextureManager::Impl {

        struct CachedTexture {
            id<MTLTexture> texture = nil;
            unsigned int version = 0;
        };

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

        id<MTLDevice> device;
        id<MTLCommandQueue> commandQueue;
        OnTextureDispose onTextureDispose;
        std::unordered_map<Texture*, CachedTexture> textures;
        std::unordered_map<SamplerKey, id<MTLSamplerState>, SamplerKeyHash> samplers;

        Impl(id<MTLDevice> dev, id<MTLCommandQueue> queue)
            : device(dev),
              commandQueue(queue),
              onTextureDispose(*this) {}

        id<MTLTexture> getOrCreateTexture(Texture& texture) {
            auto it = textures.find(&texture);
            if (it != textures.end() && it->second.version == texture.version()) {
                return it->second.texture;
            }

            if (texture.images().empty()) {
                throw std::runtime_error("Cannot create Metal texture without image data");
            }

            const auto& image = texture.image();
            if (texture.type != Type::UnsignedByte) {
                throw std::runtime_error("MetalTextureManager currently supports unsigned byte textures");
            }

            const auto& source = image.data<unsigned char>();
            const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
            const auto sourceChannels = texture.format == Format::RGB ? 3u : 4u;
            if (source.size() < pixelCount * sourceChannels) {
                throw std::runtime_error("Texture image data is smaller than expected");
            }

            std::vector<unsigned char> rgba;
            const unsigned char* uploadData = source.data();
            if (sourceChannels == 3u) {
                rgba.resize(pixelCount * 4u);
                for (std::size_t i = 0; i < pixelCount; ++i) {
                    rgba[i * 4u + 0u] = source[i * 3u + 0u];
                    rgba[i * 4u + 1u] = source[i * 3u + 1u];
                    rgba[i * 4u + 2u] = source[i * 3u + 2u];
                    rgba[i * 4u + 3u] = 255;
                }
                uploadData = rgba.data();
            }

            const auto mipmapped = texture.generateMipmaps || isMipmapFilter(texture.minFilter) || !texture.mipmaps().empty();
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                            width:image.width
                                                                                           height:image.height
                                                                                        mipmapped:mipmapped ? YES : NO];
            desc.usage = MTLTextureUsageShaderRead;

            id<MTLTexture> mtlTexture = [device newTextureWithDescriptor:desc];
            if (!mtlTexture) {
                throw std::runtime_error("Failed to create Metal texture");
            }

            const auto bytesPerRow = static_cast<NSUInteger>(image.width) * 4u;
            [mtlTexture replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
                           mipmapLevel:0
                             withBytes:uploadData
                           bytesPerRow:bytesPerRow];

            if (mipmapped && texture.generateMipmaps) {
                id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
                id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
                [blitEncoder generateMipmapsForTexture:mtlTexture];
                [blitEncoder endEncoding];
                [commandBuffer commit];
            }

            if (!texture.hasEventListener("dispose", onTextureDispose)) {
                texture.addEventListener("dispose", onTextureDispose);
            }

            textures[&texture] = CachedTexture{mtlTexture, texture.version()};
            return mtlTexture;
        }

        id<MTLSamplerState> getOrCreateSampler(Texture& texture) {
            const SamplerKey key{texture.wrapS, texture.wrapT, texture.magFilter, texture.minFilter};
            auto it = samplers.find(key);
            if (it != samplers.end()) {
                return it->second;
            }

            MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
            desc.sAddressMode = toAddressMode(texture.wrapS);
            desc.tAddressMode = toAddressMode(texture.wrapT);
            desc.magFilter = toMinMagFilter(texture.magFilter);
            desc.minFilter = toMinMagFilter(texture.minFilter);
            desc.mipFilter = toMipFilter(texture.minFilter);

            id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:desc];
            if (!sampler) {
                throw std::runtime_error("Failed to create Metal sampler");
            }

            samplers.emplace(key, sampler);
            return sampler;
        }

        void deallocateTexture(Texture* texture) {
            textures.erase(texture);
        }

        ~Impl() {
            for (auto& [texture, _] : textures) {
                texture->removeEventListener("dispose", onTextureDispose);
            }
        }
    };

    MetalTextureManager::MetalTextureManager(void* device, void* commandQueue)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>)device, (__bridge id<MTLCommandQueue>)commandQueue)) {}

    MetalTextureManager::~MetalTextureManager() = default;

    void* MetalTextureManager::getOrCreateTexture(Texture& texture) {
        return (__bridge void*)pimpl_->getOrCreateTexture(texture);
    }

    void* MetalTextureManager::getOrCreateSampler(Texture& texture) {
        return (__bridge void*)pimpl_->getOrCreateSampler(texture);
    }

    void MetalTextureManager::deallocateTexture(Texture* texture) {
        pimpl_->deallocateTexture(texture);
    }

}// namespace threepp::metal
