
#ifndef THREEPP_METAL_RENDERER_HPP
#define THREEPP_METAL_RENDERER_HPP

#include "threepp/constants.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/Renderer.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace threepp {

    class Window;

    struct MetalShadowMap {
        bool enabled = false;
        bool autoUpdate = true;
        bool needsUpdate = false;
        ShadowMap type = ShadowMap::PFC;
    };

    class MetalRenderer: public Renderer {

    public:
        explicit MetalRenderer(Window& window);

        void render(Scene& scene, Camera& camera) override;

        void setSize(std::pair<int, int> size) override;

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

        /**
         * Reads the current drawable into an RGB8 pixel buffer.
         *
         * The renderer must have an uncommitted frame when this is called. Use
         * autoClear=false, then call clear(), render(), and readRGBPixels().
         *
         * @return RGB pixels in top-to-bottom row order, sized to the current
         * framebuffer in physical pixels.
         * @throws std::runtime_error if no frame is pending or readback setup fails.
         */
        [[nodiscard]] std::vector<unsigned char> readRGBPixels();

        MetalShadowMap& shadowMap();

        [[nodiscard]] const MetalShadowMap& shadowMap() const;

        ~MetalRenderer() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp

#endif
