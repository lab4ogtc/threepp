
#ifndef THREEPP_METAL_RENDERER_HPP
#define THREEPP_METAL_RENDERER_HPP

#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/math/Vector2.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/Renderer.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace threepp {

    class Texture;
    class Window;

    struct MetalShadowMap: public RendererShadowMap {};

    /**
     * @brief Metal Lidar beam compute 输入样本。
     *
     * 该布局与内联 Metal compute shader 保持一致。每个样本描述一个 beam
     * 应从哪个 cube face 的哪个 RG 深度像素读取，并携带该 beam 在对应 face
     * camera 中的精确 NDC 坐标。
     */
    struct alignas(16) MetalLidarBeamSample {
        std::uint32_t face = 0;
        std::uint32_t pixelX = 0;
        std::uint32_t pixelY = 0;
        std::uint32_t reserved0 = 0;
        float u = 0.f;
        float v = 0.f;
        float reserved1 = 0.f;
        float reserved2 = 0.f;
    };

    static_assert(sizeof(MetalLidarBeamSample) == 32);

    class MetalRenderer: public Renderer {

    public:
        explicit MetalRenderer(Window& window);

        void render(Scene& scene, Camera& camera) override;

        void endFrame() override;

        void setSize(std::pair<int, int> size) override;

        [[nodiscard]] WindowSize size() const override;

        void setClearColor(const Color& color, float alpha = 1) override;

        void clear(bool color = true, bool depth = true, bool stencil = true) override;

        void clearDepth();

        void setViewport(const Vector4& v);

        void setViewport(int x, int y, int width, int height) override;

        void setViewport(const std::pair<int, int>& pos, const std::pair<int, int>& size);

        void setScissor(const Vector4& v);

        void setScissor(int x, int y, int width, int height) override;

        void setScissor(const std::pair<int, int>& pos, const std::pair<int, int>& size);

        void setScissorTest(bool boolean) override;

        using Renderer::setRenderTarget;

        void setRenderTarget(RenderTarget* renderTarget, int activeCubeFace, int activeMipmapLevel, int activeLayer) override;

        [[nodiscard]] RenderTarget* getRenderTarget() override;

        void addPreRenderJob(const RenderJob& job) override;

        void copyFramebufferToTexture(const Vector2& position, Texture& texture, int level = 0);

        void copyTextureToImage(Texture& texture) override;

        std::future<void> copyTextureToImageAsync(Texture& texture) override;

        [[nodiscard]] bool supportsAsyncPixelReadback() const noexcept override;

        std::future<PixelReadbackBuffer> readRenderTargetPixelsAsync(
                const PixelReadbackRequest& request) override;

        /**
         * 使用单个 Metal blit command encoder 批量读回纹理，并只等待一次 GPU 完成。
         *
         * @param textures 需要同步到 CPU Image 的纹理指针列表；空指针会被忽略。
         * @throws std::runtime_error 当纹理或读回缓冲创建失败时抛出。
         */
        void copyTexturesToImages(const std::vector<Texture*>& textures) override;

        void readbackTextureAsync(
                Texture& texture,
                std::function<void(const ReadbackResult& result)> onComplete,
                std::function<void(const std::string& error)> onError = nullptr) override;

        void readbackLidarDepthAsPointCloudAsync(
                Texture& packedDepthTexture,
                const std::array<float, 16>& matrixWorld,
                float farPlane,
                std::function<void(const ReadbackResult& result)> onComplete,
                std::function<void(const std::string& error)> onError = nullptr);

        /**
         * @brief 使用 Metal compute 将 model-based Lidar beams 直接反投影为 float4 点云。
         *
         * @param packedDepthTextures 六个 cube face 的 RG8 packed depth 纹理。
         * @param matrixWorldPerFace 六个 face camera 的 world matrix 快照。
         * @param beams 需要采样的 beam 表；输出顺序与输入顺序一致。
         * @param farPlane 深度编码对应的 far plane。
         * @param onComplete GPU 计算完成后的主线程回调，返回 RGBA/Float 数据视图。
         * @param onError 可选错误回调；未提供时异常继续抛出。
         */
        void readbackLidarBeamsAsPointCloudAsync(
                const std::array<Texture*, 6>& packedDepthTextures,
                const std::array<std::array<float, 16>, 6>& matrixWorldPerFace,
                std::span<const MetalLidarBeamSample> beams,
                float farPlane,
                std::function<void(const ReadbackResult& result)> onComplete,
                std::function<void(const std::string& error)> onError = nullptr);

        std::future<void> copyTexturesToImagesAsync(const std::vector<Texture*>& textures) override;

        [[nodiscard]] void* device() const;

        [[nodiscard]] void* currentCommandBuffer() const;

        [[nodiscard]] void* currentDrawableTexture() const;

        /**
         * @brief 获取 threepp 纹理对应的 Metal `id<MTLTexture>` 裸指针。
         *
         * 返回值通过 `__bridge void*` 零开销桥接，调用方可将其还原为 `id<MTLTexture>`。
         * 该指针仅在当前帧且获取后的当前作用域内有效；不得跨帧持有，也不得在同一帧
         * 获取后对同一个 Texture 调用 `needsUpdate()` 或 `dispose()`。
         *
         * @param texture 需要查询的 threepp Texture。
         * @return 存在底层 Metal 纹理时返回 `void*`，创建失败或占位失败时返回 `std::nullopt`。
         * @throws std::runtime_error 当纹理格式/类型不受 Metal 后端支持时抛出。
         */
        [[nodiscard]] std::optional<void*> getMetalTexture(Texture& texture) const;

        /**
         * @brief 将当前 drawable 读取为 RGB8 像素缓冲。
         *
         * 调用时渲染器必须仍持有未提交的当前帧。典型流程是设置 autoClear=false，
         * 依次调用 clear()、render()，再调用 readRGBPixels()。
         *
         * 返回的字节流跟随当前 drawable 的像素格式；该格式在获取 drawable 前由
         * outputEncoding 同步。Linear 输出返回线性附件中的字节，sRGB/Gamma 输出返回
         * Metal 硬件 sRGB 写入后的字节，适合直接写出为常见图片文件。
         *
         * @return 当前物理 framebuffer 尺寸对应的 RGB 像素，按从上到下的行顺序排列。
         * @throws std::runtime_error 当前没有待提交帧或读回资源创建失败时抛出。
         */
        [[nodiscard]] std::vector<unsigned char> readRGBPixels();

        MetalShadowMap& shadowMap() override;

        [[nodiscard]] const MetalShadowMap& shadowMap() const override;

        ~MetalRenderer() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp

#endif
