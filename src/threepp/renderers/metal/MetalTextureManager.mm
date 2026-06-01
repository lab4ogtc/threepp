#import "MetalTextureManager.hpp"

#import "threepp/constants.hpp"
#import "threepp/core/EventDispatcher.hpp"
#import "threepp/textures/CubeTexture.hpp"
#import "threepp/textures/Texture.hpp"

#import <Metal/Metal.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace threepp::metal {

    namespace {

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
            bool mipmapped;

            bool operator==(const SamplerKey& other) const {
                return wrapS == other.wrapS && wrapT == other.wrapT && magFilter == other.magFilter && minFilter == other.minFilter && mipmapped == other.mipmapped;
            }
        };

        struct SamplerKeyHash {
            std::size_t operator()(const SamplerKey& key) const {
                auto h1 = std::hash<int>{}(as_integer(key.wrapS));
                auto h2 = std::hash<int>{}(as_integer(key.wrapT));
                auto h3 = std::hash<int>{}(as_integer(key.magFilter));
                auto h4 = std::hash<int>{}(as_integer(key.minFilter));
                auto h5 = std::hash<bool>{}(key.mipmapped);
                return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
            }
        };

        std::vector<unsigned char> toRGBA(const Image& image, Texture& texture) {
            const auto& source = image.data<unsigned char>();
            const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
            const auto sourceChannels = texture.format == Format::RGB ? 3u : 4u;
            if (source.size() < pixelCount * sourceChannels) {
                std::cerr << "[MetalTextureManager] Error: source.size()=" << source.size()
                          << ", pixelCount=" << pixelCount
                          << ", width=" << image.width << ", height=" << image.height
                          << ", sourceChannels=" << sourceChannels
                          << ", texture.format=" << (int)texture.format << "\n";
                throw std::runtime_error("Texture image data is smaller than expected");
            }

            std::vector<unsigned char> rgba(pixelCount * 4u);
            if (sourceChannels == 4u) {
                std::copy(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(rgba.size()), rgba.begin());
                return rgba;
            }

            for (std::size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4u + 0u] = source[i * 3u + 0u];
                rgba[i * 4u + 1u] = source[i * 3u + 1u];
                rgba[i * 4u + 2u] = source[i * 3u + 2u];
                rgba[i * 4u + 3u] = 255;
            }
            return rgba;
        }

        bool wantsMipmaps(const Texture& texture) {
            return texture.generateMipmaps || !texture.mipmaps().empty();
        }

        void replace2DRegion(id<MTLTexture> mtlTexture, Texture& texture, const Image& image, NSUInteger level, NSUInteger slice = 0) {
            auto rgba = toRGBA(image, texture);
            const auto bytesPerRow = static_cast<NSUInteger>(image.width) * 4u;
            const auto bytesPerImage = bytesPerRow * static_cast<NSUInteger>(image.height);
            [mtlTexture replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
                          mipmapLevel:level
                                slice:slice
                            withBytes:rgba.data()
                          bytesPerRow:bytesPerRow
                        bytesPerImage:bytesPerImage];
        }

        void uploadMipmaps(id<MTLTexture> mtlTexture, Texture& texture, NSUInteger slice = 0) {
            const auto& mipmaps = texture.mipmaps();
            const auto maxManualLevels = mtlTexture.mipmapLevelCount > 0 ? mtlTexture.mipmapLevelCount - 1u : 0u;
            const auto manualLevelCount = std::min<NSUInteger>(static_cast<NSUInteger>(mipmaps.size()), maxManualLevels);

            for (NSUInteger i = 0; i < manualLevelCount; ++i) {
                replace2DRegion(mtlTexture, texture, mipmaps[i], i + 1u, slice);
            }
        }

    }// namespace

    struct MetalTextureManager::Impl {

        struct CachedTexture {
            id<MTLTexture> texture = nil;
            unsigned int version = 0;
            bool external = false;
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
        std::unordered_set<Texture*> placeholderWarnings;

        Impl(id<MTLDevice> dev, id<MTLCommandQueue> queue)
            : device(dev),
              commandQueue(queue),
              onTextureDispose(*this) {}

        void warnPlaceholderFallback(Texture& texture, std::string_view reason) {
            if (!placeholderWarnings.insert(&texture).second) return;

            if (!texture.hasEventListener("dispose", onTextureDispose)) {
                texture.addEventListener("dispose", onTextureDispose);
            }

            std::cerr << "[MetalTextureManager] Warning: using placeholder texture for texture "
                      << texture.id << " (" << reason << ")\n";
        }

        id<MTLTexture> getOrCreateTexture(Texture& texture, bool allowPlaceholder) {
            auto it = textures.find(&texture);
            if (it != textures.end() && (it->second.external || it->second.version == texture.version())) {
                return it->second.texture;
            }

            if (texture.type != Type::UnsignedByte) {
                throw std::runtime_error("MetalTextureManager currently supports unsigned byte textures");
            }

            if (texture.images().empty()) {
                if (!allowPlaceholder) {
                    throw std::runtime_error("Cannot create Metal texture without image data");
                }
                warnPlaceholderFallback(texture, "missing image data");
                return nil;
            }

            const auto& image = texture.image();
            const auto& source = image.data<unsigned char>();
            const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
            const auto sourceChannels = texture.format == Format::RGB ? 3u : 4u;
            if (source.size() < pixelCount * sourceChannels) {
                if (!allowPlaceholder) {
                    std::cerr << "[MetalTextureManager] Error: source.size()=" << source.size()
                              << ", pixelCount=" << pixelCount
                              << ", width=" << image.width << ", height=" << image.height
                              << ", sourceChannels=" << sourceChannels
                              << ", texture.format=" << (int)texture.format << "\n";
                    throw std::runtime_error("Texture image data is smaller than expected");
                }
                warnPlaceholderFallback(texture, "image data is smaller than expected");
                return nil;
            }

            if (dynamic_cast<CubeTexture*>(&texture)) {
                const auto& images = texture.images();
                if (images.size() != 6) {
                    throw std::runtime_error("Metal cube textures require six images");
                }

                const auto width = images.front().width;
                const auto height = images.front().height;
                if (width != height) {
                    throw std::runtime_error("Metal cube texture faces must be square");
                }

                const auto mipmapped = wantsMipmaps(texture);
                MTLTextureDescriptor* desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                   size:width
                                                                                              mipmapped:mipmapped ? YES : NO];
                desc.usage = MTLTextureUsageShaderRead;

                id<MTLTexture> mtlTexture = [device newTextureWithDescriptor:desc];
                if (!mtlTexture) {
                    throw std::runtime_error("Failed to create Metal cube texture");
                }

                for (NSUInteger face = 0; face < 6; ++face) {
                    const auto& faceImage = images[face];
                    if (faceImage.width != width || faceImage.height != height) {
                        throw std::runtime_error("Metal cube texture faces must have identical dimensions");
                    }

                    replace2DRegion(mtlTexture, texture, faceImage, 0, face);
                    uploadMipmaps(mtlTexture, texture, face);
                }

                if (mipmapped && texture.generateMipmaps && texture.mipmaps().empty()) {
                    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
                    id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
                    [blitEncoder generateMipmapsForTexture:mtlTexture];
                    [blitEncoder endEncoding];
                    [commandBuffer commit];
                }

                if (!texture.hasEventListener("dispose", onTextureDispose)) {
                    texture.addEventListener("dispose", onTextureDispose);
                }

                textures[&texture] = CachedTexture{mtlTexture, texture.version(), false};
                return mtlTexture;
            }

            const auto mipmapped = wantsMipmaps(texture);
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                            width:image.width
                                                                                           height:image.height
                                                                                        mipmapped:mipmapped ? YES : NO];
            desc.usage = MTLTextureUsageShaderRead;

            id<MTLTexture> mtlTexture = [device newTextureWithDescriptor:desc];
            if (!mtlTexture) {
                throw std::runtime_error("Failed to create Metal texture");
            }

            replace2DRegion(mtlTexture, texture, image, 0);
            uploadMipmaps(mtlTexture, texture);

            if (mipmapped && texture.generateMipmaps && texture.mipmaps().empty()) {
                id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
                id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
                [blitEncoder generateMipmapsForTexture:mtlTexture];
                [blitEncoder endEncoding];
                [commandBuffer commit];
            }

            if (!texture.hasEventListener("dispose", onTextureDispose)) {
                texture.addEventListener("dispose", onTextureDispose);
            }

            textures[&texture] = CachedTexture{mtlTexture, texture.version(), false};
            return mtlTexture;
        }

        id<MTLSamplerState> getOrCreateSampler(Texture& texture) {
            const auto mipmapped = wantsMipmaps(texture);
            const SamplerKey key{texture.wrapS, texture.wrapT, texture.magFilter, texture.minFilter, mipmapped};
            auto it = samplers.find(key);
            if (it != samplers.end()) {
                return it->second;
            }

            MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
            desc.sAddressMode = toAddressMode(texture.wrapS);
            desc.tAddressMode = toAddressMode(texture.wrapT);
            desc.magFilter = toMinMagFilter(texture.magFilter);
            desc.minFilter = toMinMagFilter(texture.minFilter);
            desc.mipFilter = mipmapped ? toMipFilter(texture.minFilter) : MTLSamplerMipFilterNotMipmapped;

            id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:desc];
            if (!sampler) {
                throw std::runtime_error("Failed to create Metal sampler");
            }

            samplers.emplace(key, sampler);
            return sampler;
        }

        void registerExternalTexture(Texture& texture, id<MTLTexture> mtlTexture) {
            if (!mtlTexture) {
                throw std::runtime_error("Cannot register a null Metal texture");
            }

            if (!texture.hasEventListener("dispose", onTextureDispose)) {
                texture.addEventListener("dispose", onTextureDispose);
            }

            textures[&texture] = CachedTexture{mtlTexture, texture.version(), true};
        }

        void deallocateTexture(Texture* texture) {
            if (texture && texture->hasEventListener("dispose", onTextureDispose)) {
                texture->removeEventListener("dispose", onTextureDispose);
            }
            placeholderWarnings.erase(texture);
            textures.erase(texture);
        }

        ~Impl() {
            for (auto& [texture, _] : textures) {
                texture->removeEventListener("dispose", onTextureDispose);
            }
        }
    };

    MetalTextureManager::MetalTextureManager(void* device, void* commandQueue)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device, (__bridge id<MTLCommandQueue>) commandQueue)) {}

    MetalTextureManager::~MetalTextureManager() = default;

    void* MetalTextureManager::getOrCreateTexture(Texture& texture, bool allowPlaceholder) {
        return (__bridge void*) pimpl_->getOrCreateTexture(texture, allowPlaceholder);
    }

    void* MetalTextureManager::getOrCreateSampler(Texture& texture) {
        return (__bridge void*) pimpl_->getOrCreateSampler(texture);
    }

    void MetalTextureManager::registerExternalTexture(Texture& texture, void* mtlTexture) {
        pimpl_->registerExternalTexture(texture, (__bridge id<MTLTexture>) mtlTexture);
    }

    void MetalTextureManager::deallocateTexture(Texture* texture) {
        pimpl_->deallocateTexture(texture);
    }

}// namespace threepp::metal
