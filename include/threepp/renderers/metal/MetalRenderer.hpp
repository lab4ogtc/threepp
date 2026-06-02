
#ifndef THREEPP_METAL_RENDERER_HPP
#define THREEPP_METAL_RENDERER_HPP

#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/Renderer.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace threepp {

    class Window;

    struct MetalShadowMap: public RendererShadowMap {};

    class MetalRenderer: public Renderer {

    public:
        explicit MetalRenderer(Window& window);

        void render(Scene& scene, Camera& camera) override;

        void setSize(std::pair<int, int> size) override;

        [[nodiscard]] WindowSize size() const override;

        void setClearColor(const Color& color, float alpha = 1) override;

        void clear(bool color = true, bool depth = true, bool stencil = true) override;

        void setViewport(const Vector4& v);

        void setViewport(int x, int y, int width, int height) override;

        void setViewport(const std::pair<int, int>& pos, const std::pair<int, int>& size);

        void setScissor(const Vector4& v);

        void setScissor(int x, int y, int width, int height) override;

        void setScissor(const std::pair<int, int>& pos, const std::pair<int, int>& size);

        void setScissorTest(bool boolean) override;

        void setRenderTarget(RenderTarget* renderTarget) override;

        [[nodiscard]] RenderTarget* getRenderTarget() override;

        void addPreRenderJob(const RenderJob& job) override;

        [[nodiscard]] void* device() const;

        [[nodiscard]] void* currentCommandBuffer() const;

        [[nodiscard]] void* currentDrawableTexture() const;

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
