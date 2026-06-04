
#ifndef THREEPP_RENDERER_HPP
#define THREEPP_RENDERER_HPP

#include <memory>
#include <utility>
#include <vector>

#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/math/Plane.hpp"

namespace threepp {

    class Window;
    class Scene;
    class Camera;
    class RenderTarget;
    class Texture;
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

        virtual void endFrame() {}

        virtual void setSize(std::pair<int, int> size) = 0;

        [[nodiscard]] virtual WindowSize size() const = 0;

        virtual void setClearColor(const Color& color, float alpha = 1) = 0;

        virtual void clear(bool color = true, bool depth = true, bool stencil = true) = 0;

        virtual void setViewport(int x, int y, int width, int height) = 0;

        virtual void setScissor(int x, int y, int width, int height) = 0;

        virtual void setScissorTest(bool enable) = 0;

        virtual void setRenderTarget(RenderTarget* renderTarget) = 0;

        [[nodiscard]] virtual RenderTarget* getRenderTarget() = 0;

        virtual void copyTextureToImage(Texture& texture) = 0;

        /**
         * 批量同步纹理数据到各自的 CPU Image。
         *
         * 默认实现逐个调用 copyTextureToImage()；支持批量 GPU 读回的后端可覆写以减少同步等待。
         *
         * @param textures 需要读回的纹理指针列表；空指针会被忽略。
         */
        virtual void copyTexturesToImages(const std::vector<Texture*>& textures) {
            for (auto* texture : textures) {
                if (texture) copyTextureToImage(*texture);
            }
        }

        virtual void addPreRenderJob(const RenderJob& job) = 0;

        virtual RendererShadowMap& shadowMap() = 0;

        [[nodiscard]] virtual const RendererShadowMap& shadowMap() const = 0;

        bool autoClear = true;

        ToneMapping toneMapping{ToneMapping::None};
        float toneMappingExposure = 1.0f;
        Encoding outputEncoding{Encoding::Linear};

        std::vector<Plane> clippingPlanes;
        bool localClippingEnabled = false;

        virtual ~Renderer() = default;
    };

}// namespace threepp

#endif//THREEPP_RENDERER_HPP
