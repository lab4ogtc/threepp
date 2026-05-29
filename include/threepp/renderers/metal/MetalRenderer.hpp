
#ifndef THREEPP_METAL_RENDERER_HPP
#define THREEPP_METAL_RENDERER_HPP

#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/Renderer.hpp"

#include <memory>
#include <utility>

namespace threepp {

    class Window;

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

        ~MetalRenderer() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp

#endif
