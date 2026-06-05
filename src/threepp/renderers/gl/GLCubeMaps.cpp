
#include "GLCubeMaps.hpp"

#include "threepp/renderers/GLCubeRenderTarget.hpp"

using namespace threepp;
using namespace threepp::gl;

namespace {

    void mapTextureMapping(Texture& texture, Mapping mapping) {

        if (mapping == Mapping::EquirectangularReflection) {

            texture.mapping = Mapping::CubeReflection;

        } else if (mapping == Mapping::EquirectangularRefraction) {

            texture.mapping = Mapping::CubeRefraction;
        }
    }

}// namespace

GLCubeMaps::GLCubeMaps(GLRenderer& renderer)
    : renderer(renderer) {}

void GLCubeMaps::get(Texture* texture) {

    if (texture) {

        const auto mapping = texture->mapping;

        if (mapping == Mapping::EquirectangularReflection || mapping == Mapping::EquirectangularRefraction) {

            if (cubemaps.count(texture)) {

                const auto cubemap = cubemaps.at(texture)->texture;
                mapTextureMapping(*cubemap, texture->mapping);

            } else {

                const auto& image = texture->image();

                if (image.height > 0) {

                    auto* currentRenderTarget = renderer.getRenderTarget();
                    const auto currentActiveCubeFace = renderer.getActiveCubeFace();
                    const auto currentActiveMipmapLevel = renderer.getActiveMipmapLevel();
                    const auto currentActiveLayer = renderer.getActiveLayer();

                    auto renderTarget = std::make_unique<GLCubeRenderTarget>(image.height / 2);
                    renderTarget->fromEquirectangularTexture(renderer, *texture);
                    cubemaps[texture] = std::move(renderTarget);

                    renderer.setRenderTarget(currentRenderTarget, currentActiveCubeFace, currentActiveMipmapLevel, currentActiveLayer);

                    //TODO

                    mapTextureMapping(*renderTarget->texture, texture->mapping);
                }
            }
        }
    }
}

void GLCubeMaps::dispose() {

    cubemaps.clear();
}
