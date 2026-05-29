
#ifndef THREEPP_METAL_RENDERER_HPP
#define THREEPP_METAL_RENDERER_HPP

#include "threepp/renderers/Renderer.hpp"

#include <memory>

namespace threepp {

    class Window;

    class MetalRenderer: public Renderer {

    public:
        explicit MetalRenderer(Window& window);

        void render(Scene& scene, Camera& camera) override;

        void setSize(std::pair<int, int> size) override;

        void setClearColor(const Color& color, float alpha = 1) override;

        void clear(bool color = true, bool depth = true, bool stencil = true) override;

        void setRenderTarget(RenderTarget* renderTarget) override;

        [[nodiscard]] RenderTarget* getRenderTarget() override;

        ~MetalRenderer() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp

#endif
