// VulkanRenderTargets：threepp RenderTarget 的最小 Vulkan 后备资源。

#ifndef THREEPP_VULKAN_RENDER_TARGETS_HPP
#define THREEPP_VULKAN_RENDER_TARGETS_HPP

#include "threepp/core/EventDispatcher.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/vulkan/VulkanResources.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace threepp::vulkan {

    class VulkanContext;

    struct RenderTargetKey {
        std::string uuid;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t colorCount = 1;
        Format format = Format::RGBA;
        Type type = Type::UnsignedByte;
        bool generateMipmaps = false;
        bool depthBuffer = true;
        bool stencilBuffer = false;
        bool hasDepthTexture = false;
        Format depthFormat = Format::Depth;
        Type depthType = Type::UnsignedShort;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        bool cube = false;
        int activeCubeFace = 0;
        int activeMipmapLevel = 0;

        bool operator==(const RenderTargetKey&) const = default;
    };

    class VulkanRenderTargets {

    public:
        struct TextureImage {
            Image2D* image = nullptr;
            VkImageLayout* layout = nullptr;
            VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            RenderTarget* target = nullptr;
            std::size_t textureIndex = 0;
        };

        struct Record {
            RenderTargetKey key;
            Image2D color;
            VkImageLayout colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            std::vector<Image2D> extraColors;
            std::vector<VkImageLayout> extraColorLayouts;
            Image2D msaaColor;
            VkImageLayout msaaColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            std::vector<Image2D> extraMsaaColors;
            std::vector<VkImageLayout> extraMsaaColorLayouts;
            Image2D depth;
            VkImageLayout depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Image2D msaaDepth;
            VkImageLayout msaaDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            Buffer depthStencilCopyBuffer;
        };

        explicit VulkanRenderTargets(VulkanContext& ctx);
        ~VulkanRenderTargets();

        VulkanRenderTargets(const VulkanRenderTargets&) = delete;
        VulkanRenderTargets& operator=(const VulkanRenderTargets&) = delete;

        static RenderTargetKey makeKey(const RenderTarget& target, int activeCubeFace, int activeMipmapLevel,
                                       VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
        static uint32_t channelCount(Format format);

        Record& getOrCreate(RenderTarget& target, int activeCubeFace, int activeMipmapLevel,
                            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                            int activeLayer = 0);
        TextureImage* findByTexture(const Texture& texture);
        const TextureImage* findByTexture(const Texture& texture) const;
        void release(RenderTarget* target);

    private:
        struct DisposeListener: EventListener {
            explicit DisposeListener(VulkanRenderTargets& owner): owner(owner) {}
            void onEvent(Event& event) override;
            VulkanRenderTargets& owner;
        };

        VulkanContext& ctx_;
        DisposeListener disposeListener_;
        std::unordered_map<RenderTarget*, Record> records_;
        std::unordered_map<const Texture*, TextureImage> textureImages_;
        std::unordered_set<RenderTarget*> listening_;

        void destroy(Record& record);
        Record create(RenderTarget& target, const RenderTargetKey& key);
    };

}// namespace threepp::vulkan

#endif// THREEPP_VULKAN_RENDER_TARGETS_HPP
