
#ifndef THREEPP_RENDERER_HPP
#define THREEPP_RENDERER_HPP

#include <memory>
#include <utility>

namespace threepp {

    class Window;
    class Scene;
    class Camera;
    class RenderTarget;
    class Color;

    enum class Backend {
        OpenGL,
        Metal,
        Vulkan
    };

    class Renderer {

    public:
        struct Parameters {};

        static std::unique_ptr<Renderer> create(
                Window& window,
                Backend backend = Backend::OpenGL,
                const Parameters& params = Parameters{});

        virtual void render(Scene& scene, Camera& camera) = 0;

        virtual void setSize(std::pair<int, int> size) = 0;

        virtual void setClearColor(const Color& color, float alpha = 1) = 0;

        virtual void clear(bool color = true, bool depth = true, bool stencil = true) = 0;

        virtual void setViewport(int x, int y, int width, int height) = 0;

        virtual void setScissor(int x, int y, int width, int height) = 0;

        virtual void setScissorTest(bool enable) = 0;

        virtual void setRenderTarget(RenderTarget* renderTarget) = 0;

        [[nodiscard]] virtual RenderTarget* getRenderTarget() = 0;

        bool autoClear = true;

        virtual ~Renderer() = default;
    };

}// namespace threepp

#endif//THREEPP_RENDERER_HPP
