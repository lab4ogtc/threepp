// Backend-neutral cube render target.

#ifndef THREEPP_CUBERENDERTARGET_HPP
#define THREEPP_CUBERENDERTARGET_HPP

#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/CubeTexture.hpp"

namespace threepp {

    class CubeRenderTarget: public RenderTarget {

    public:
        explicit CubeRenderTarget(int size, const Options& options = {})
            : RenderTarget(static_cast<unsigned int>(size), static_cast<unsigned int>(size), options) {

            texture = CubeTexture::create();
            if (options.mapping) texture->mapping = *options.mapping;
            if (options.wrapS) texture->wrapS = *options.wrapS;
            if (options.wrapT) texture->wrapT = *options.wrapT;
            if (options.magFilter) texture->magFilter = *options.magFilter;
            if (options.format) texture->format = *options.format;
            if (options.type) texture->type = *options.type;
            if (options.anisotropy) texture->anisotropy = *options.anisotropy;
            if (options.encoding) texture->colorSpace = *options.encoding;

            texture->generateMipmaps = options.generateMipmaps;
            texture->minFilter = options.minFilter.value_or(Filter::Linear);

            textures.clear();
            textures.push_back(texture);
        }

        static std::unique_ptr<CubeRenderTarget> create(int size, const Options& options = {}) {
            return std::make_unique<CubeRenderTarget>(size, options);
        }
    };

}// namespace threepp

#endif// THREEPP_CUBERENDERTARGET_HPP
