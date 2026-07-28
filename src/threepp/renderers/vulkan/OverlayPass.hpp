// OverlayPass — ortho/HUD overlay rendering (Sprites, Lines, Meshes).
//
// Manages the ortho sprite pipeline, the ortho line/mesh pipelines, per-frame
// descriptor pools for per-sprite atlas binding, and the three geometry caches
// (sprite atlas, sprite quad geometry, line/mesh geometry). recordOrthoOverlay
// is the only entry point; it is self-contained and replaces the same-named
// Impl method it was extracted from.
//
// Atlas texture creation is delegated back to the caller via SampledImageCreator
// because createSampledImage2D is shared across 10+ Impl call sites and cannot
// move here alone. The lambda supplied at construction captures Impl's
// beginOneShot/endAndSubmitOneShot transparently.
//
// Extracted from VulkanRenderer.cpp during the file split.

#ifndef THREEPP_VULKAN_OVERLAY_PASS_HPP
#define THREEPP_VULKAN_OVERLAY_PASS_HPP

#include "threepp/renderers/vulkan/VulkanFrameTypes.hpp"
#include "threepp/renderers/vulkan/VulkanResources.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/textures/Texture.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace threepp {
    class Object3D;
    class Camera;
    class InstancedMesh;
}

namespace threepp::vulkan {

    class VulkanContext;

    class OverlayPass {
    public:
        // Callback type that wraps Impl::createSampledImage2D. Called from
        // ensureSpriteAtlasTexture when a new or stale atlas needs to be
        // uploaded; the lambda stored here captures Impl* and forwards to the
        // shared helper so OverlayPass does not need beginOneShot/endOneShot.
        using SampledImageCreator = std::function<Image2D(
                uint32_t w, uint32_t h, VkFormat fmt,
                const void* pixels, VkDeviceSize byteSize,
                VkFilter filter,
                VkSamplerAddressMode addrU,
                VkSamplerAddressMode addrV,
                const char* debugName)>;
        using ExternalImageResolver = std::function<const Image2D*(const Texture*)>;

        OverlayPass(VulkanContext& ctx, uint32_t framesInFlight, SampledImageCreator uploadFn,
                    ExternalImageResolver externalImageFn = {});
        ~OverlayPass();
        OverlayPass(const OverlayPass&)            = delete;
        OverlayPass& operator=(const OverlayPass&) = delete;

        // Record the entire ortho/HUD overlay into `cb`.
        // cb       — the per-frame command buffer (already open).
        // frame    — which frame-in-flight slot to use (descriptor pool, etc.).
        // imageIndex — which swapchain image/view to render into.
        // scene / camera — the ortho HUD scene and its camera.
        // screenSpaceOnly — when true, only sprites with Sprite::screenSpace=true
        //                   are drawn (used for the automatic screen-space sprite
        //                   compositing after the PT body).
        // regionW == 0 表示整帧。否则将 swapchain 写入裁剪到
        // (regionX, regionY, regionW, regionH)。regionAsViewport 为 true 时，
        // Vulkan 动态 viewport 也使用同一区域；否则投影保持整帧，
        // 用于匹配 GL scissor 语义。
        void record(VkCommandBuffer cb, uint32_t frame, uint32_t imageIndex,
                    VkImage outputImage, VkImageView outputView, VkExtent2D outputExtent,
                    Object3D& scene, Camera& camera, bool screenSpaceOnly,
                    uint32_t regionX = 0, uint32_t regionY = 0,
                    uint32_t regionW = 0, uint32_t regionH = 0,
                    bool regionAsViewport = false,
                    VkImageLayout inputLayout = VK_IMAGE_LAYOUT_GENERAL,
                    VkImageLayout outputLayout = VK_IMAGE_LAYOUT_GENERAL,
                    bool linearOutput = false);

    private:
        // Cached uploaded sprite atlas. Keyed on Texture*; liveCheck detects
        // pointer recycle; textureVersion mirrors Texture::version() so
        // setText()-triggered re-rasterisation forces a re-upload.
        struct SpriteAtlasRec {
            Image2D      image{};
            unsigned int textureVersion = ~0u;
            uint32_t     width          = 0;
            uint32_t     height         = 0;
            std::weak_ptr<Texture> liveCheck;
            bool         ownsImage      = true;
        };

        // Per-BufferGeometry vertex/index upload for Sprite quads.
        struct SpriteGeomRec {
            Buffer   vertex;
            Buffer   index;
            uint32_t indexCount = 0;
            std::weak_ptr<BufferGeometry> liveCheck;
        };

        // 正交贴图 Mesh 使用普通 BufferGeometry：position 与 uv 分开上传。
        struct TexturedMeshGeomRec {
            Buffer   position;
            Buffer   uv;
            Buffer   index;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            uint32_t geomId = 0;
            unsigned int positionVersion = 0;
            unsigned int uvVersion = 0;
            unsigned int indexVersion = 0;
        };

        struct InstancedMeshRec {
            Buffer matrix;
            Buffer color;
            unsigned int matrixVersion = ~0u;
            unsigned int colorVersion = ~0u;
            unsigned int meshId = 0;
            uint32_t capacity = 0;
            uint64_t lastTouch = 0;
        };

        struct RetiredInstancedBuffer {
            Buffer buffer;
            uint64_t retireFrame = 0;
        };

        // Lazy pipeline setup — called from record() on first use.
        void createSpriteOverlayPipeline();
        void createOrthoLinePipelines();
        void createOrthoPointPipeline();
        void createTexturedMeshPipeline();
        Image2D& ensureDepthImage(uint32_t frame, uint32_t width, uint32_t height);

        // Cache helpers — called from record() on each draw.
        const SpriteAtlasRec* ensureSpriteAtlasTexture(const std::shared_ptr<Texture>& texSp);
        const SpriteGeomRec*  ensureSpriteGeometryUploaded(const BufferGeometry* geom);
        const TexturedMeshGeomRec* ensureTexturedMeshGeometryUploaded(const BufferGeometry* geom);
        const InstancedMeshRec*    ensureInstancedMeshUploaded(const InstancedMesh* mesh);
        LineRec*              ensureLineGeometryUploaded(const BufferGeometry* geom);
        WireframeRec*         ensureWireframeGeometryUploaded(const BufferGeometry* geom);

        VulkanContext&      ctx_;
        uint32_t            framesInFlight_;
        SampledImageCreator uploadFn_;
        ExternalImageResolver externalImageFn_;

        static constexpr uint32_t kMaxSpritesPerFrame = 64;

        // Sprite pipeline
        VkDescriptorSetLayout spriteDescSetLayout_   = VK_NULL_HANDLE;
        VkPipelineLayout      spritePipelineLayout_  = VK_NULL_HANDLE;
        VkPipeline            overlaySpritePipeline_ = VK_NULL_HANDLE;

        // Ortho line / mesh pipelines (overlay.vert/frag, shared depth attachment)
        VkPipelineLayout orthoLinePipelineLayout_      = VK_NULL_HANDLE;
        VkPipeline       orthoLineListPipeline_        = VK_NULL_HANDLE;
        VkPipeline       orthoLineStripPipeline_       = VK_NULL_HANDLE;
        VkPipeline       orthoLineDashedListPipeline_  = VK_NULL_HANDLE;
        VkPipeline       orthoLineDashedStripPipeline_ = VK_NULL_HANDLE;
        VkPipeline       orthoLineColoredListPipeline_ = VK_NULL_HANDLE;
        VkPipeline       orthoLineColoredStripPipeline_ = VK_NULL_HANDLE;
        VkPipeline       orthoMeshPipeline_            = VK_NULL_HANDLE;
        VkPipeline       orthoMeshColoredPipeline_     = VK_NULL_HANDLE;
        VkPipeline       orthoMeshInstancedPipeline_   = VK_NULL_HANDLE;
        VkPipeline       orthoMeshTransparentPipeline_ = VK_NULL_HANDLE;
        VkPipeline       orthoTexturedMeshPipeline_    = VK_NULL_HANDLE;
        VkPipeline       orthoDepthTextureMeshPipeline_ = VK_NULL_HANDLE;

        // Ortho point pipeline (overlay_point.vert/frag, POINT_LIST, pos+color
        // vertex bindings, depth-tested). Reuses orthoLinePipelineLayout_.
        VkPipeline       orthoPointListPipeline_       = VK_NULL_HANDLE;

        // Per-frame descriptor pools reset at the top of each record() call.
        std::vector<VkDescriptorPool> spriteDescPools_;
        std::vector<Image2D> overlayDepthImages_;

        // Texture + geometry caches
        std::unordered_map<const Texture*,        SpriteAtlasRec> spriteAtlasCache_;
        std::unordered_map<const BufferGeometry*, SpriteGeomRec>  spriteGeomCache_;
        std::unordered_map<const BufferGeometry*, TexturedMeshGeomRec> texturedMeshGeomCache_;
        std::unordered_map<const InstancedMesh*, InstancedMeshRec> instancedMeshCache_;
        std::vector<RetiredInstancedBuffer> retiredInstancedBuffers_;
        std::unordered_map<const BufferGeometry*, LineRec> lineGeomCache_;
        std::unordered_map<const BufferGeometry*, WireframeRec> wireframeGeomCache_;
        uint64_t overlayFrameCounter_ = 0;
    };

}// namespace threepp::vulkan

#endif// THREEPP_VULKAN_OVERLAY_PASS_HPP
