#include "threepp/renderers/vulkan/VulkanRenderTargets.hpp"

#include "threepp/renderers/vulkan/VulkanContext.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/Texture.hpp"

#include <algorithm>
#include <any>
#include <stdexcept>

namespace threepp::vulkan {

    namespace {

        VkSamplerAddressMode wrapToVk(TextureWrapping w) {
            switch (w) {
                case TextureWrapping::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                case TextureWrapping::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                default: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            }
        }

        VkFilter filterToVk(Filter f) {
            switch (f) {
                case Filter::Nearest:
                case Filter::NearestMipMapNearest:
                case Filter::NearestMipMapLinear:
                    return VK_FILTER_NEAREST;
                default:
                    return VK_FILTER_LINEAR;
            }
        }

        VkSamplerMipmapMode mipmapModeToVk(Filter f) {
            switch (f) {
                case Filter::NearestMipMapNearest:
                case Filter::LinearMipMapNearest:
                    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
                default:
                    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
            }
        }

        uint32_t calcMipLevels(uint32_t width, uint32_t height) {
            uint32_t maxDim = std::max(width, height);
            uint32_t levels = 1;
            while (maxDim > 1) {
                maxDim /= 2;
                ++levels;
            }
            return levels;
        }

        Image2D createImage(VulkanContext& ctx,
                            uint32_t width,
                            uint32_t height,
                            VkFormat format,
                            VkImageUsageFlags usage,
                            VkImageAspectFlags aspect,
                            uint32_t mipLevels,
                            uint32_t arrayLayers,
                            const char* label,
                            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                            VkImageCreateFlags flags = 0) {
            Image2D out{};
            out.width = width;
            out.height = height;
            out.format = format;
            out.mipLevels = mipLevels;
            out.arrayLayers = arrayLayers;

            VkImageCreateInfo ici{};
            ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.flags = flags;
            ici.imageType = VK_IMAGE_TYPE_2D;
            ici.format = format;
            ici.extent = {width, height, 1};
            ici.mipLevels = mipLevels;
            ici.arrayLayers = arrayLayers;
            ici.samples = samples;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.usage = usage;
            ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo aci{};
            aci.usage = VMA_MEMORY_USAGE_AUTO;
            check(vmaCreateImage(ctx.allocator(), &ici, &aci, &out.image, &out.alloc, nullptr),
                  label);

            VkImageViewCreateInfo vci{};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = out.image;
            vci.viewType = (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0
                    ? VK_IMAGE_VIEW_TYPE_CUBE
                    : (arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
            vci.format = format;
            vci.subresourceRange.aspectMask = aspect;
            vci.subresourceRange.levelCount = mipLevels;
            vci.subresourceRange.layerCount = arrayLayers;
            check(vkCreateImageView(ctx.device(), &vci, nullptr, &out.view),
                  "vkCreateImageView(render target)");

            ctx.setObjectName(out.image, label);
            ctx.setObjectName(out.view, label);
            return out;
        }

        bool isDepthOnlyTarget(const RenderTarget& target) {
            return target.texture && target.texture->format == Format::Depth;
        }

        bool isCubeTarget(const RenderTarget& target) {
            return dynamic_cast<const CubeTexture*>(target.texture.get()) != nullptr;
        }

        bool isSupportedDepthTexture(const DepthTexture& texture) {
            return (texture.format == Format::Depth && texture.type == Type::Float) ||
                   (texture.format == Format::DepthStencil && texture.type == Type::UnsignedInt248);
        }

        VkFormat depthImageFormat(const RenderTargetKey& key) {
            // 需要从默认帧缓冲复制的深度目标必须保持相同格式，尺寸不同时才能合法 blit。
            const bool copiedDepth = key.hasDepthTexture ||
                                     key.format == Format::Depth ||
                                     key.format == Format::DepthStencil;
            return key.stencilBuffer || copiedDepth
                    ? VK_FORMAT_D32_SFLOAT_S8_UINT
                    : VK_FORMAT_D32_SFLOAT;
        }

        VkImageAspectFlags depthViewAspect(const RenderTargetKey& key) {
            return key.stencilBuffer && !key.hasDepthTexture
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                    : VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        VkSampler createSampler(VulkanContext& ctx, const Texture& texture, uint32_t mipLevels) {
            VkSamplerCreateInfo sci{};
            sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sci.magFilter = filterToVk(texture.magFilter);
            sci.minFilter = filterToVk(texture.minFilter);
            sci.mipmapMode = mipmapModeToVk(texture.minFilter);
            sci.addressModeU = wrapToVk(texture.wrapS);
            sci.addressModeV = wrapToVk(texture.wrapT);
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.maxAnisotropy = std::max(1, texture.anisotropy);
            sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            sci.maxLod = static_cast<float>(mipLevels > 0 ? mipLevels - 1 : 0);
            VkSampler sampler = VK_NULL_HANDLE;
            check(vkCreateSampler(ctx.device(), &sci, nullptr, &sampler),
                  "vkCreateSampler(render target)");
            return sampler;
        }

    }// namespace

    VulkanRenderTargets::VulkanRenderTargets(VulkanContext& ctx)
        : ctx_(ctx), disposeListener_(*this) {}

    VulkanRenderTargets::~VulkanRenderTargets() {
        for (auto& [_, record] : records_) destroy(record);
    }

    RenderTargetKey VulkanRenderTargets::makeKey(const RenderTarget& target,
                                                 int activeCubeFace,
                                                 int activeMipmapLevel,
                                                 VkSampleCountFlagBits samples) {
        RenderTargetKey key;
        key.uuid = target.uuid;
        key.width = target.width;
        key.height = target.height;
        key.depth = target.depth;
        key.colorCount = static_cast<uint32_t>(std::max<std::size_t>(1, target.textures.size()));
        key.depthBuffer = target.depthBuffer;
        key.stencilBuffer = target.stencilBuffer;
        key.samples = samples;
        key.cube = isCubeTarget(target);
        key.activeCubeFace = 0;
        key.activeMipmapLevel = 0;
        if (target.texture) {
            key.format = target.texture->format;
            key.type = target.texture->type;
            key.generateMipmaps = key.cube || target.texture->generateMipmaps;
        }
        key.hasDepthTexture = static_cast<bool>(target.depthTexture);
        if (target.depthTexture) {
            key.depthFormat = target.depthTexture->format;
            key.depthType = target.depthTexture->type;
        }
        return key;
    }

    uint32_t VulkanRenderTargets::channelCount(Format format) {
        switch (format) {
            case Format::Red:
            case Format::RedInteger:
            case Format::Alpha:
            case Format::Luminance:
            case Format::Depth:
                return 1;
            case Format::RG:
            case Format::RGInteger:
            case Format::LuminanceAlpha:
            case Format::DepthStencil:
                return 2;
            case Format::RGB:
            case Format::BGR:
            case Format::RGBInteger:
                return 3;
            default:
                return 4;
        }
    }

    VulkanRenderTargets::Record& VulkanRenderTargets::getOrCreate(RenderTarget& target,
                                                                  int activeCubeFace,
                                                                  int activeMipmapLevel,
                                                                  VkSampleCountFlagBits samples,
                                                                  int activeLayer) {
        const bool cube = isCubeTarget(target);
        if (activeCubeFace < 0) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: active cube face must be non-negative");
        }
        if (activeLayer < 0) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: active layer must be non-negative");
        }
        if (cube) {
            if (activeCubeFace >= 6) {
                throw std::runtime_error("VulkanRenderer::setRenderTarget: active cube face is out of range");
            }
            if (activeLayer != 0) {
                throw std::runtime_error("VulkanRenderer::setRenderTarget: cube render targets require activeLayer == 0");
            }
        } else if (activeCubeFace != 0) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: non-cube render targets require activeCubeFace == 0");
        }
        const auto layerCount = std::max(1u, target.depth);
        if (!cube && static_cast<uint32_t>(activeLayer) >= layerCount) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: active layer is out of range");
        }
        if (!cube && layerCount == 1 && activeLayer != 0) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: 2D render targets require activeLayer == 0");
        }
        if (target.depthTexture && !isSupportedDepthTexture(*target.depthTexture)) {
            throw std::runtime_error(
                    "VulkanRenderer::setRenderTarget: depthTexture supports only Format::Depth with Type::Float or Format::DepthStencil with Type::UnsignedInt248");
        }
        if (!target.texture) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: RenderTarget has no color texture");
        }
        if (target.texture->format == Format::DepthStencil) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: depth-stencil color targets are not implemented yet");
        }
        if (target.texture->format == Format::Depth && target.texture->type != Type::Float) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: depth-only targets support only Format::Depth with Type::Float");
        }
        const auto mipLevels = (cube || target.texture->generateMipmaps) ? calcMipLevels(target.width, target.height) : 1u;
        if (activeMipmapLevel < 0 || static_cast<uint32_t>(activeMipmapLevel) >= mipLevels) {
            throw std::runtime_error("VulkanRenderer::setRenderTarget: active mip level is out of range");
        }

        if (listening_.insert(&target).second) {
            target.addEventListener("dispose", disposeListener_);
        }

        if (target.depthTexture || isDepthOnlyTarget(target)) {
            samples = VK_SAMPLE_COUNT_1_BIT;
        }
        if (cube) {
            samples = VK_SAMPLE_COUNT_1_BIT;
        }

        const auto key = makeKey(target, activeCubeFace, activeMipmapLevel, samples);
        auto it = records_.find(&target);
        if (it != records_.end() && it->second.key == key) return it->second;

        if (it != records_.end()) {
            destroy(it->second);
            records_.erase(it);
        }

        auto [inserted, _] = records_.emplace(&target, create(target, key));
        auto& record = inserted->second;
        textureImages_[target.texture.get()] = isDepthOnlyTarget(target)
                ? TextureImage{&record.depth, &record.depthLayout, VK_IMAGE_ASPECT_DEPTH_BIT, &target, 0}
                : TextureImage{&record.color, &record.colorLayout, VK_IMAGE_ASPECT_COLOR_BIT, &target, 0};
        for (std::size_t i = 1; i < target.textures.size() && i - 1 < record.extraColors.size(); ++i) {
            if (target.textures[i]) {
                textureImages_[target.textures[i].get()] = {
                        &record.extraColors[i - 1],
                        &record.extraColorLayouts[i - 1],
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        &target,
                        i};
            }
        }
        if (target.depthTexture) {
            textureImages_[target.depthTexture.get()] = {
                    &record.depth,
                    &record.depthLayout,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    &target,
                    0};
        }
        return record;
    }

    VulkanRenderTargets::TextureImage* VulkanRenderTargets::findByTexture(const Texture& texture) {
        auto it = textureImages_.find(&texture);
        return it == textureImages_.end() ? nullptr : &it->second;
    }

    const VulkanRenderTargets::TextureImage* VulkanRenderTargets::findByTexture(const Texture& texture) const {
        auto it = textureImages_.find(&texture);
        return it == textureImages_.end() ? nullptr : &it->second;
    }

    void VulkanRenderTargets::release(RenderTarget* target) {
        if (!target) return;
        auto it = records_.find(target);
        if (it == records_.end()) return;
        destroy(it->second);
        records_.erase(it);
        listening_.erase(target);
    }

    void VulkanRenderTargets::DisposeListener::onEvent(Event& event) {
        if (event.target.has_value()) {
            owner.release(std::any_cast<RenderTarget*>(event.target));
        }
    }

    void VulkanRenderTargets::destroy(Record& record) {
        for (auto it = textureImages_.begin(); it != textureImages_.end();) {
            const auto ownsImage = [&] {
                if (it->second.image == &record.color || it->second.image == &record.depth) return true;
                for (auto& color : record.extraColors) {
                    if (it->second.image == &color) return true;
                }
                return false;
            }();
            if (ownsImage) {
                it = textureImages_.erase(it);
            } else {
                ++it;
            }
        }
        destroyImage2D(ctx_.allocator(), ctx_.device(), record.color);
        for (auto& color : record.extraColors) {
            destroyImage2D(ctx_.allocator(), ctx_.device(), color);
        }
        destroyImage2D(ctx_.allocator(), ctx_.device(), record.msaaColor);
        for (auto& color : record.extraMsaaColors) {
            destroyImage2D(ctx_.allocator(), ctx_.device(), color);
        }
        destroyImage2D(ctx_.allocator(), ctx_.device(), record.depth);
        destroyImage2D(ctx_.allocator(), ctx_.device(), record.msaaDepth);
        destroyBuffer(ctx_.allocator(), record.depthStencilCopyBuffer);
        record = {};
    }

    VulkanRenderTargets::Record VulkanRenderTargets::create(RenderTarget& target,
                                                            const RenderTargetKey& key) {
        Record record;
        record.key = key;
        const auto colorMipLevels = key.generateMipmaps ? calcMipLevels(key.width, key.height) : 1u;
        const bool depthOnly = key.format == Format::Depth;
        const uint32_t colorLayers = key.cube ? 6u : std::max(1u, key.depth);

        if (!depthOnly) {
            record.color = createImage(
                    ctx_, key.width, key.height, ctx_.swapchainFormat(),
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    colorMipLevels,
                    colorLayers,
                    "renderTarget.color",
                    VK_SAMPLE_COUNT_1_BIT,
                    key.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);
            record.color.sampler = createSampler(ctx_, *target.texture, colorMipLevels);

            const auto colorCount = target.textures.size();
            if (colorCount > 1) {
                record.extraColors.reserve(colorCount - 1);
                record.extraColorLayouts.resize(colorCount - 1, VK_IMAGE_LAYOUT_UNDEFINED);
                for (std::size_t i = 1; i < colorCount; ++i) {
                    const auto& texture = target.textures[i] ? *target.textures[i] : *target.texture;
                    auto extra = createImage(
                            ctx_, key.width, key.height, ctx_.swapchainFormat(),
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            colorMipLevels,
                            colorLayers,
                            "renderTarget.extraColor",
                            VK_SAMPLE_COUNT_1_BIT,
                            key.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0);
                    extra.sampler = createSampler(ctx_, texture, colorMipLevels);
                    record.extraColors.push_back(std::move(extra));
                }
            }
        }

        if (!depthOnly && key.samples != VK_SAMPLE_COUNT_1_BIT) {
            record.msaaColor = createImage(
                    ctx_, key.width, key.height, ctx_.swapchainFormat(),
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    1,
                    1,
                    "renderTarget.msaaColor",
                    key.samples);
            if (key.colorCount > 1) {
                record.extraMsaaColors.reserve(key.colorCount - 1);
                record.extraMsaaColorLayouts.resize(key.colorCount - 1, VK_IMAGE_LAYOUT_UNDEFINED);
                for (uint32_t i = 1; i < key.colorCount; ++i) {
                    record.extraMsaaColors.push_back(createImage(
                            ctx_, key.width, key.height, ctx_.swapchainFormat(),
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            1,
                            1,
                            "renderTarget.extraMsaaColor",
                            key.samples));
                }
            }
        }

        if (target.depthBuffer || target.stencilBuffer || target.depthTexture || depthOnly) {
            const auto depthFormat = depthImageFormat(key);
            const auto depthAspect = depthViewAspect(key);
            record.depth = createImage(
                    ctx_, key.width, key.height, depthFormat,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    depthAspect,
                    1,
                    colorLayers,
                    "renderTarget.depth");
            record.depth.sampler = createSampler(ctx_, target.depthTexture ? *target.depthTexture : *target.texture, 1);

            if (key.samples != VK_SAMPLE_COUNT_1_BIT) {
                record.msaaDepth = createImage(
                        ctx_, key.width, key.height, depthFormat,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                        depthAspect,
                        1,
                        1,
                        "renderTarget.msaaDepth",
                        key.samples);
            }
        }

        return record;
    }

}// namespace threepp::vulkan
