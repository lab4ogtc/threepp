// Backend-neutral cube render target.

#ifndef THREEPP_CUBERENDERTARGET_HPP
#define THREEPP_CUBERENDERTARGET_HPP

#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/CubeTexture.hpp"

#include <algorithm>
#include <cstddef>

namespace threepp {

    class CubeRenderTarget: public RenderTarget {

    public:
        explicit CubeRenderTarget(int size, const Options& options = {})
            : RenderTarget(static_cast<unsigned int>(size), static_cast<unsigned int>(size), options) {

            auto createCubeTexture = [&] {
                auto cubeTexture = CubeTexture::create();
                if (options.mapping) cubeTexture->mapping = *options.mapping;
                if (options.wrapS) cubeTexture->wrapS = *options.wrapS;
                if (options.wrapT) cubeTexture->wrapT = *options.wrapT;
                if (options.magFilter) cubeTexture->magFilter = *options.magFilter;
                if (options.format) cubeTexture->format = *options.format;
                if (options.type) cubeTexture->type = *options.type;
                if (options.anisotropy) cubeTexture->anisotropy = *options.anisotropy;
                if (auto colorSpace = options.effectiveColorSpace()) {
                    cubeTexture->colorSpace = *colorSpace;
                }

                cubeTexture->generateMipmaps = options.generateMipmaps;
                cubeTexture->minFilter = options.minFilter.value_or(Filter::Linear);
                return cubeTexture;
            };

            const auto textureCount = std::max(1, options.count);
            textures.clear();
            textures.reserve(static_cast<std::size_t>(textureCount));
            for (int i = 0; i < textureCount; ++i) {
                textures.push_back(createCubeTexture());
            }
            texture = textures.front();
        }

        static std::unique_ptr<CubeRenderTarget> create(int size, const Options& options = {}) {
            return std::make_unique<CubeRenderTarget>(size, options);
        }
    };

}// namespace threepp

#endif// THREEPP_CUBERENDERTARGET_HPP
