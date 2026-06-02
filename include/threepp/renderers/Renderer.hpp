
#ifndef THREEPP_RENDERER_HPP
#define THREEPP_RENDERER_HPP

#include <memory>
#include <utility>

#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"

namespace threepp {

    class Window;
    class Scene;
    class Camera;
    class RenderTarget;
    struct RenderJob;
    class Color;

    enum class Backend {
        OpenGL,
        Metal,
        Vulkan
    };

    struct RendererShadowMap {
        bool enabled = false;
        bool autoUpdate = true;
        bool needsUpdate = false;
        ShadowMap type = ShadowMap::PFC;
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

        [[nodiscard]] virtual WindowSize size() const = 0;

        virtual void setClearColor(const Color& color, float alpha = 1) = 0;

        virtual void clear(bool color = true, bool depth = true, bool stencil = true) = 0;

        virtual void setViewport(int x, int y, int width, int height) = 0;

        virtual void setScissor(int x, int y, int width, int height) = 0;

        virtual void setScissorTest(bool enable) = 0;

        virtual void setRenderTarget(RenderTarget* renderTarget) = 0;

        [[nodiscard]] virtual RenderTarget* getRenderTarget() = 0;

        virtual void addPreRenderJob(const RenderJob& job) = 0;

        virtual RendererShadowMap& shadowMap() = 0;

        [[nodiscard]] virtual const RendererShadowMap& shadowMap() const = 0;

        bool autoClear = true;

        ToneMapping toneMapping{ToneMapping::None};
        float toneMappingExposure = 1.0f;
        Encoding outputEncoding{Encoding::Linear};

        virtual ~Renderer() = default;
    };

}// namespace threepp

#endif//THREEPP_RENDERER_HPP
