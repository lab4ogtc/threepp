
#ifndef THREEPP_RENDERTARGET_HPP
#define THREEPP_RENDERTARGET_HPP

#include "threepp/constants.hpp"
#include "threepp/core/EventDispatcher.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "threepp/textures/Texture.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace threepp {

    class RenderTarget: public EventDispatcher {

    public:
        struct Options {

            std::optional<Mapping> mapping;
            std::optional<TextureWrapping> wrapS;
            std::optional<TextureWrapping> wrapT;
            std::optional<Filter> magFilter;
            std::optional<Filter> minFilter;
            std::optional<Format> format;
            std::optional<Type> type;
            std::optional<int> anisotropy;
            std::optional<Encoding> encoding;

            bool generateMipmaps{false};
            bool depthBuffer{true};
            bool stencilBuffer{false};
            bool zeroCopy{false};

            std::shared_ptr<DepthTexture> depthTexture;

            TextureType type_{TextureType::Texture2D};
            int depth{1};
            int count{1};

            Options() = default;
        };

        const std::string uuid;

        unsigned int width;
        unsigned int height;
        unsigned int depth = 1;

        Vector4 scissor;
        bool scissorTest = false;

        Vector4 viewport;

        std::shared_ptr<Texture> texture;
        std::vector<std::shared_ptr<Texture>> textures;

        bool depthBuffer;
        bool stencilBuffer;
        bool zeroCopy;
        bool isExternal = false;

        std::shared_ptr<DepthTexture> depthTexture;

        /**
         * @brief Creates a render target suitable for the enabled backend set.
         *
         * With OpenGL support enabled, this returns a GLRenderTarget so existing
         * GL renderer internals keep their strong target type. Without OpenGL
         * support, it returns a generic backend-neutral RenderTarget whose GPU
         * resources are allocated lazily by the active renderer.
         *
         * @param width Target width in pixels.
         * @param height Target height in pixels.
         * @param options Texture/depth/stencil creation options.
         * @return Shared render target instance.
         */
        static std::shared_ptr<RenderTarget> create(unsigned int width, unsigned int height);

        static std::shared_ptr<RenderTarget> create(unsigned int width, unsigned int height, const Options& options);

        virtual void setSize(unsigned int width, unsigned int height, unsigned int depth = 1) = 0;

        RenderTarget& copy(const RenderTarget& source);

        virtual void dispose();

        ~RenderTarget() override;

    protected:
        explicit RenderTarget(unsigned int width, unsigned int height, const Options& options);

        bool disposed = false;
    };

}// namespace threepp

#endif//THREEPP_RENDERTARGET_HPP
