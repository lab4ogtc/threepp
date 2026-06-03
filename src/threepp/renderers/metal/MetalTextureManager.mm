#import "MetalTextureManager.hpp"

#import "threepp/constants.hpp"
#import "threepp/core/EventDispatcher.hpp"
#import "threepp/textures/CubeTexture.hpp"
#import "threepp/textures/DataTexture3D.hpp"
#import "threepp/textures/Texture.hpp"

#import <Metal/Metal.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
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
            TextureWrapping wrapR;
            Filter magFilter;
            Filter minFilter;
            bool mipmapped;

            bool operator==(const SamplerKey& other) const {
                return wrapS == other.wrapS && wrapT == other.wrapT && wrapR == other.wrapR && magFilter == other.magFilter && minFilter == other.minFilter && mipmapped == other.mipmapped;
            }
        };

        struct SamplerKeyHash {
            std::size_t operator()(const SamplerKey& key) const {
                auto h1 = std::hash<int>{}(as_integer(key.wrapS));
                auto h2 = std::hash<int>{}(as_integer(key.wrapT));
                auto h3 = std::hash<int>{}(as_integer(key.wrapR));
                auto h4 = std::hash<int>{}(as_integer(key.magFilter));
                auto h5 = std::hash<int>{}(as_integer(key.minFilter));
                auto h6 = std::hash<bool>{}(key.mipmapped);
                return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5);
            }
        };

        TextureWrapping wrapR(const Texture& texture) {
            if (auto* dataTexture3D = dynamic_cast<const DataTexture3D*>(&texture)) {
                return dataTexture3D->wrapR;
            }

            return TextureWrapping::ClampToEdge;
        }

        bool wantsMipmaps(const Texture& texture) {
            return texture.generateMipmaps || !texture.mipmaps().empty();
        }

        NSUInteger mipLevelCount(NSUInteger width, NSUInteger height, NSUInteger depth) {
            const auto maxDimension = std::max({width, height, depth});
            return static_cast<NSUInteger>(std::floor(std::log2(static_cast<double>(maxDimension)))) + 1u;
        }

        unsigned int sourceChannelCount(Format format) {
            switch (format) {
                case Format::Red:
                    return 1u;
                case Format::RG:
                    return 2u;
                case Format::RGB:
                    return 3u;
                case Format::RGBA:
                    return 4u;
                default:
                    throw std::runtime_error("MetalTextureManager supports only Red, RG, RGB, and RGBA texture formats");
            }
        }

        unsigned int uploadChannelCount(Format format) {
            return format == Format::RGB ? 4u : sourceChannelCount(format);
        }

        bool usesSRGBTextureEncoding(const Texture& texture) {
            if (texture.type != Type::UnsignedByte) return false;
            if (texture.format != Format::RGB && texture.format != Format::RGBA) return false;

            switch (texture.encoding) {
                case Encoding::sRGB:
                case Encoding::Gamma:
                    return true;
                default:
                    return false;
            }
        }

        MTLPixelFormat toColorPixelFormat(const Texture& texture) {
            const auto srgb = usesSRGBTextureEncoding(texture);

            switch (texture.type) {
                case Type::UnsignedByte:
                    switch (texture.format) {
                        case Format::Red:
                            return MTLPixelFormatR8Unorm;
                        case Format::RG:
                            return MTLPixelFormatRG8Unorm;
                        case Format::RGB:
                        case Format::RGBA:
                            return srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
                        default:
                            break;
                    }
                    break;
                case Type::Float:
                    switch (texture.format) {
                        case Format::Red:
                            return MTLPixelFormatR32Float;
                        case Format::RG:
                            return MTLPixelFormatRG32Float;
                        case Format::RGB:
                        case Format::RGBA:
                            return MTLPixelFormatRGBA32Float;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }

            throw std::runtime_error("MetalTextureManager supports only unsigned byte and float Red, RG, RGB, and RGBA textures");
        }

        template<class T>
        T opaqueAlpha() {
            if constexpr (std::is_same_v<T, unsigned char>) {
                return 255;
            } else {
                return 1.f;
            }
        }

        template<class T>
        struct UploadableData {
            std::vector<T> converted;
            const T* bytes = nullptr;
            NSUInteger bytesPerRow = 0;
            NSUInteger bytesPerImage = 0;
        };

        template<class T>
        UploadableData<T> getUploadableDataImpl(const Image& image, Texture& texture) {
            const auto& source = image.data<T>();
            const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * static_cast<std::size_t>(std::max(1u, image.depth));
            const auto sourceChannels = sourceChannelCount(texture.format);
            const auto uploadChannels = uploadChannelCount(texture.format);
            const auto expectedSize = pixelCount * static_cast<std::size_t>(sourceChannels);
            if (source.size() < expectedSize) {
                std::cerr << "[MetalTextureManager] Error: source.size()=" << source.size()
                          << ", pixelCount=" << pixelCount
                          << ", width=" << image.width << ", height=" << image.height
                          << ", sourceChannels=" << sourceChannels
                          << ", texture.format=" << static_cast<int>(texture.format)
                          << ", texture.type=" << static_cast<int>(texture.type) << "\n";
                throw std::runtime_error("Texture image data is smaller than expected");
            }

            UploadableData<T> result;
            result.bytesPerRow = static_cast<NSUInteger>(image.width) * static_cast<NSUInteger>(uploadChannels) * sizeof(T);
            result.bytesPerImage = result.bytesPerRow * static_cast<NSUInteger>(image.height);

            if (texture.format != Format::RGB) {
                result.bytes = source.data();
                return result;
            }

            result.converted.resize(pixelCount * 4u);
            for (std::size_t i = 0; i < pixelCount; ++i) {
                result.converted[i * 4u + 0u] = source[i * 3u + 0u];
                result.converted[i * 4u + 1u] = source[i * 3u + 1u];
                result.converted[i * 4u + 2u] = source[i * 3u + 2u];
                result.converted[i * 4u + 3u] = opaqueAlpha<T>();
            }
            result.bytes = result.converted.data();
            return result;
        }

        bool hasUploadableImageData(const Texture& texture, const Image& image) {
            try {
                const auto pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * static_cast<std::size_t>(std::max(1u, image.depth));
                const auto sourceChannels = sourceChannelCount(texture.format);
                const auto expectedSize = pixelCount * static_cast<std::size_t>(sourceChannels);

                switch (texture.type) {
                    case Type::UnsignedByte:
                        return image.data<unsigned char>().size() >= expectedSize;
                    case Type::Float:
                        return image.data<float>().size() >= expectedSize;
                    default:
                        return false;
                }
            } catch (...) {
                return false;
            }
        }

        void replace2DRegion(id<MTLTexture> mtlTexture, Texture& texture, const Image& image, NSUInteger level, NSUInteger slice = 0) {
            switch (texture.type) {
                case Type::UnsignedByte: {
                    const auto upload = getUploadableDataImpl<unsigned char>(image, texture);
                    [mtlTexture replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
                                  mipmapLevel:level
                                        slice:slice
                                    withBytes:upload.bytes
                                  bytesPerRow:upload.bytesPerRow
                                bytesPerImage:upload.bytesPerImage];
                    return;
                }
                case Type::Float: {
                    const auto upload = getUploadableDataImpl<float>(image, texture);
                    [mtlTexture replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
                                  mipmapLevel:level
                                        slice:slice
                                    withBytes:upload.bytes
                                  bytesPerRow:upload.bytesPerRow
                                bytesPerImage:upload.bytesPerImage];
                    return;
                }
                default:
                    throw std::runtime_error("MetalTextureManager supports only unsigned byte and float texture uploads");
            }
        }

        void replace3DRegion(id<MTLTexture> mtlTexture, Texture& texture, const Image& image, NSUInteger level) {
            switch (texture.type) {
                case Type::UnsignedByte: {
                    const auto upload = getUploadableDataImpl<unsigned char>(image, texture);
                    [mtlTexture replaceRegion:MTLRegionMake3D(0, 0, 0, image.width, image.height, std::max(1u, image.depth))
                                  mipmapLevel:level
                                        slice:0
                                    withBytes:upload.bytes
                                  bytesPerRow:upload.bytesPerRow
                                bytesPerImage:upload.bytesPerImage];
                    return;
                }
                case Type::Float: {
                    const auto upload = getUploadableDataImpl<float>(image, texture);
                    [mtlTexture replaceRegion:MTLRegionMake3D(0, 0, 0, image.width, image.height, std::max(1u, image.depth))
                                  mipmapLevel:level
                                        slice:0
                                    withBytes:upload.bytes
                                  bytesPerRow:upload.bytesPerRow
                                bytesPerImage:upload.bytesPerImage];
                    return;
                }
                default:
                    throw std::runtime_error("MetalTextureManager supports only unsigned byte and float texture uploads");
            }
        }

        void uploadMipmaps(id<MTLTexture> mtlTexture, Texture& texture, NSUInteger slice = 0) {
            const auto& mipmaps = texture.mipmaps();
            const auto maxManualLevels = mtlTexture.mipmapLevelCount > 0 ? mtlTexture.mipmapLevelCount - 1u : 0u;
            const auto manualLevelCount = std::min<NSUInteger>(static_cast<NSUInteger>(mipmaps.size()), maxManualLevels);

            for (NSUInteger i = 0; i < manualLevelCount; ++i) {
                replace2DRegion(mtlTexture, texture, mipmaps[i], i + 1u, slice);
            }
        }

        void upload3DMipmaps(id<MTLTexture> mtlTexture, Texture& texture) {
            const auto& mipmaps = texture.mipmaps();
            const auto maxManualLevels = mtlTexture.mipmapLevelCount > 0 ? mtlTexture.mipmapLevelCount - 1u : 0u;
            const auto manualLevelCount = std::min<NSUInteger>(static_cast<NSUInteger>(mipmaps.size()), maxManualLevels);

            for (NSUInteger i = 0; i < manualLevelCount; ++i) {
                replace3DRegion(mtlTexture, texture, mipmaps[i], i + 1u);
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

            if (texture.images().empty()) {
                if (!allowPlaceholder) {
                    throw std::runtime_error("Cannot create Metal texture without image data");
                }
                warnPlaceholderFallback(texture, "missing image data");
                return nil;
            }

            const auto& image = texture.image();
            if (!hasUploadableImageData(texture, image)) {
                if (!allowPlaceholder) {
                    throw std::runtime_error("Texture image data is incompatible with texture format/type or smaller than expected");
                }
                warnPlaceholderFallback(texture, "image data is incompatible with texture format/type or smaller than expected");
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
                MTLTextureDescriptor* desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:toColorPixelFormat(texture)
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

            if (dynamic_cast<DataTexture3D*>(&texture)) {
                const auto mipmapped = wantsMipmaps(texture);
                MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
                desc.textureType = MTLTextureType3D;
                desc.pixelFormat = toColorPixelFormat(texture);
                desc.width = image.width;
                desc.height = image.height;
                desc.depth = std::max(1u, image.depth);
                desc.mipmapLevelCount = mipmapped ? mipLevelCount(desc.width, desc.height, desc.depth) : 1u;
                desc.usage = MTLTextureUsageShaderRead;

                id<MTLTexture> mtlTexture = [device newTextureWithDescriptor:desc];
                if (!mtlTexture) {
                    throw std::runtime_error("Failed to create Metal 3D texture");
                }

                replace3DRegion(mtlTexture, texture, image, 0);
                upload3DMipmaps(mtlTexture, texture);

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
            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:toColorPixelFormat(texture)
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
            const SamplerKey key{texture.wrapS, texture.wrapT, wrapR(texture), texture.magFilter, texture.minFilter, mipmapped};
            auto it = samplers.find(key);
            if (it != samplers.end()) {
                return it->second;
            }

            MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
            desc.sAddressMode = toAddressMode(texture.wrapS);
            desc.tAddressMode = toAddressMode(texture.wrapT);
            desc.rAddressMode = toAddressMode(wrapR(texture));
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

        void updateCachedTexture(Texture& texture, id<MTLTexture> mtlTexture) {
            if (!mtlTexture) {
                throw std::runtime_error("Cannot cache a null Metal texture");
            }

            if (!texture.hasEventListener("dispose", onTextureDispose)) {
                texture.addEventListener("dispose", onTextureDispose);
            }

            textures[&texture] = CachedTexture{mtlTexture, texture.version(), false};
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

    void MetalTextureManager::updateCachedTexture(Texture& texture, void* mtlTexture) {
        pimpl_->updateCachedTexture(texture, (__bridge id<MTLTexture>) mtlTexture);
    }

    void MetalTextureManager::deallocateTexture(Texture* texture) {
        pimpl_->deallocateTexture(texture);
    }

}// namespace threepp::metal
