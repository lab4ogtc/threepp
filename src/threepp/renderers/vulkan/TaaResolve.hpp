// TaaResolve — temporal anti-aliasing resolve pass.
//
// Reads the denoise output (`inputView(frame)` — denoise targets this image
// when TAA is active), reprojects last frame's history via the raster
// G-buffer's motion vector, blends with neighborhood-AABB clamp, writes the
// result to a fresh history slot for next frame, then uses a fullscreen
// color-attachment pass to present it to the swapchain.
//
// Extracted from VulkanRenderer.cpp during the file split. Owns its
// pipeline, descriptor set layout + pool + sets, sampler, input/history
// image ping-pong. External deps (raster G-buffer views, swapchain views)
// are passed in at descriptor-write time.

#ifndef THREEPP_VULKAN_TAA_RESOLVE_HPP
#define THREEPP_VULKAN_TAA_RESOLVE_HPP

#include "threepp/renderers/vulkan/VulkanResources.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace threepp::vulkan {

    class VulkanContext;

    class TaaResolve {

    public:
        // `cmdPool` is used internally for one-shot image layout transitions
        // (UNDEFINED → GENERAL) at image creation time. Pipeline + layout +
        // sampler + descriptor pool + descriptor sets are allocated here;
        // images are deferred to `createImages` so we don't need the surface
        // size yet.
        TaaResolve(VulkanContext& ctx,
                   VkCommandPool cmdPool,
                   uint32_t imageCount,
                   uint32_t framesInFlight);
        ~TaaResolve();
        TaaResolve(const TaaResolve&) = delete;
        TaaResolve& operator=(const TaaResolve&) = delete;

        // Allocate input images at the render extent and history images at
        // the output (swapchain) extent. When the two extents differ the
        // resolve runs as a temporal upsampler; when equal it is a plain
        // 1:1 TAA resolve. Idempotent — frees existing images first. Resets
        // history-valid to false (the freshly-allocated slots are undefined).
        void createImages(uint32_t inWidth, uint32_t inHeight,
                           uint32_t outWidth, uint32_t outHeight);
        void destroyImages();

        // Rewrite all descriptor sets. Caller supplies the external view
        // sources from the raster G-buffer pass. Must be
        // called after createImages + after the raster G-buffer has been
        // allocated (its views must be valid). Both per-frame arrays are
        // indexed by frame-in-flight slot.
        struct DescriptorWriteInputs {
            VkSampler          gbufSampler         = VK_NULL_HANDLE;
            const VkImageView* gbufMotionPerFrame  = nullptr;// [framesInFlight]
            const VkImageView* gbufIdsPerFrame     = nullptr;// [framesInFlight]
        };
        void rewriteDescriptors(const DescriptorWriteInputs& inputs);

        // Per-frame dispatch. Records barrier on input/history images, binds
        // pipeline + descriptor set + push constants, dispatches over the
        // OUTPUT extent in 8×8 groups (each thread reconstructs one full-res
        // pixel; the input may be lower-res). Auto-flips history-valid to
        // true after the first dispatch.
        // When `sharpen` is true a post-resolve RCAS pass
        // (sharpenAmount ~0.2–0.6) reads the resolved frame back from the
        // history slot and writes the sharpened result to an internal present
        // image. A fullscreen graphics pass then writes the linear LDR color
        // to the swapchain color attachment, letting SRGB swapchain formats
        // perform the final encode in fixed-function hardware.
        // `dtFrames` = this frame's duration in reference frames (dt · 90 fps,
        // clamped [1, 6] by the caller; 1 at high fps). The shader scales its
        // per-frame temporal constants (deviation-streak ramp, soft-clip rate)
        // by it so ghost decay is constant in wall-clock time, not frames.
        // Scissored writes: outWidth/outHeight are the dispatch/write region,
        // dstX/dstY are the swapchain/history offset, and physIn/physOut are
        // the full texture sizes used for full-frame UV normalisation.
        void recordResolve(VkCommandBuffer cb,
                           uint32_t frame,
                           uint32_t imageIndex,
                           uint32_t inWidth,
                           uint32_t inHeight,
                           uint32_t outWidth,
                           uint32_t outHeight,
                           float blendAlpha,
                           float dtFrames,
                           bool sharpen,
                           float sharpenAmount,
                           const float* skyReproj,
                           uint32_t dstX = 0,
                           uint32_t dstY = 0,
                           uint32_t physInW = 0,
                           uint32_t physInH = 0,
                           uint32_t physOutW = 0,
                           uint32_t physOutH = 0);

        // Denoise writes its output here when TAA is active (replaces the
        // direct-to-swapchain write of non-TAA mode).
        [[nodiscard]] VkImageView inputView(uint32_t frame) const {
            return inputImagesPP_[frame].view;
        }
        [[nodiscard]] VkImage inputImage(uint32_t frame) const {
            return inputImagesPP_[frame].image;
        }

        // History images — accessed for inter-frame barriers in the caller's
        // pre-RT block (TAA writes them, denoise / next-frame TAA reads).
        [[nodiscard]] VkImage historyImage(uint32_t slot) const {
            return historyImagesPP_[slot].image;
        }

        // First-frame history is undefined. Caller sets this to false on
        // resetAccumulation; the first recordResolve after that uses
        // alpha=1 so we don't bleed garbage into history.
        void invalidateHistory() { historyValid_ = false; }
        [[nodiscard]] bool historyValid() const { return historyValid_; }

    private:
        VulkanContext& ctx_;
        VkCommandPool  cmdPool_;
        uint32_t       imageCount_;
        uint32_t       framesInFlight_;

        // Input image per frame-in-flight (denoise's target) — sized to the
        // path-trace RENDER extent. BGRA8_UNORM to match denoise.comp's
        // rgba8 output and the swapchain channel order.
        std::vector<Image2D> inputImagesPP_;
        // History ping-pong — sized to the OUTPUT (swapchain) extent, so it
        // accumulates the temporal upsampler's reconstructed full-res image.
        // RGBA16F (higher precision than the rgba8 input) so the running
        // mix() doesn't re-quantize to uint8 each frame, which produced
        // visible iso-luminance "lines" on smooth specular surfaces.
        std::array<Image2D, 2> historyImagesPP_{};
        // RCAS 开启时的内部输出。保持 UNORM/storage-capable，避免 compute
        // 直接写 SRGB swapchain。
        std::array<Image2D, 2> presentImagesPP_{};
        VkSampler sampler_ = VK_NULL_HANDLE;

        VkDescriptorSetLayout dsLayout_       = VK_NULL_HANDLE;
        VkPipelineLayout      pipelineLayout_ = VK_NULL_HANDLE;
        VkPipeline            pipeline_       = VK_NULL_HANDLE;
        VkDescriptorPool      descPool_       = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descSets_;

        // resolve 后的 RCAS 锐化。读取刚写入的 history slot，并写入
        // presentImagesPP_。
        VkDescriptorSetLayout rcasDsLayout_   = VK_NULL_HANDLE;
        VkPipelineLayout      rcasPipeLayout_ = VK_NULL_HANDLE;
        VkPipeline            rcasPipe_       = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> rcasSets_;

        // 最终 fullscreen present pass：采样 historyImagesPP_ 或
        // presentImagesPP_，以 color attachment 写入 swapchain。
        VkDescriptorSetLayout presentDsLayout_   = VK_NULL_HANDLE;
        VkPipelineLayout      presentPipeLayout_ = VK_NULL_HANDLE;
        VkPipeline            presentPipe_       = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> presentSets_;
        std::vector<VkDescriptorSet> presentSharpenSets_;

        bool historyValid_ = false;

        // Internal helpers.
        Image2D createStorageSampledImage(uint32_t w, uint32_t h, VkFormat format,
                                          const char* label);
        void    transitionFreshImage(VkImage img);
        void    createPipeline();
        void    createDescriptorPool();
    };

}// namespace threepp::vulkan

#endif//THREEPP_VULKAN_TAA_RESOLVE_HPP
