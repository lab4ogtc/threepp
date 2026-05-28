
#ifndef THREEPP_RENDERTARGET_HPP
#define THREEPP_RENDERTARGET_HPP

#include "threepp/constants.hpp"
#include "threepp/core/EventDispatcher.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "threepp/textures/Texture.hpp"

#include <memory>
#include <optional>

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

            std::shared_ptr<DepthTexture> depthTexture;

            TextureType type_{TextureType::Texture2D};
            int depth{1};

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

        bool depthBuffer;
        bool stencilBuffer;

        std::shared_ptr<DepthTexture> depthTexture;

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
