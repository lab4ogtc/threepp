
#include "GLCubeMaps.hpp"

#include "GLPMREM.hpp"

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

GLCubeMaps::~GLCubeMaps() = default;

Texture* GLCubeMaps::get(Texture* texture) {

    if (texture) {

        const auto mapping = texture->mapping;

        if (mapping == Mapping::EquirectangularReflection || mapping == Mapping::EquirectangularRefraction) {

            if (cubemaps.contains(texture)) {

                const auto cubemap = cubemaps.at(texture)->texture.get();
                mapTextureMapping(*cubemap, texture->mapping);
                return cubemap;

            } else {

                const auto& image = texture->image();

                if (image.height() > 0) {

                    const auto& currentRenderTarget = renderer.getRenderTarget();

                    auto renderTarget = std::make_unique<GLCubeRenderTarget>(image.height() / 2);
                    renderTarget->fromEquirectangularTexture(renderer, *texture);
                    cubemaps[texture] = std::move(renderTarget);

                    renderer.setRenderTarget(currentRenderTarget);

                    auto* cubemap = cubemaps[texture]->texture.get();
                    mapTextureMapping(*cubemap, texture->mapping);
                    return cubemap;

                } else {

                    return nullptr;
                }
            }
        }
    }

    return texture;
}

Texture* GLCubeMaps::getPMREM(Texture* texture) {

    if (!texture) return nullptr;

    const auto mapping = texture->mapping;
    const bool isEquirect = mapping == Mapping::EquirectangularReflection ||
                            mapping == Mapping::EquirectangularRefraction;
    const bool isCube = mapping == Mapping::CubeReflection ||
                        mapping == Mapping::CubeRefraction;
    const bool isPmrem = mapping == Mapping::CubeUVReflection ||
                         mapping == Mapping::CubeUVRefraction;
    if (isPmrem) return texture;
    if (!isEquirect && !isCube) return texture;

    const auto version = texture->version();
    if (pmrems.contains(texture) &&
        pmremVersions.contains(texture) &&
        pmremVersions.at(texture) == version) {
        return pmrems.at(texture)->texture.get();
    }

    if (isEquirect) {
        const auto& image = texture->image();
        if (image.height() == 0) return nullptr;
    } else if (isCube) {
        const auto& images = texture->images();
        if (images.empty() || images.front().height() == 0) return nullptr;
    }

    if (!pmremGenerator) {
        pmremGenerator = std::make_unique<GLPMREM>(renderer);
    }

    auto* currentRenderTarget = renderer.getRenderTarget();
    auto pmrem = isCube
                     ? pmremGenerator->fromCubemap(*texture)
                     : pmremGenerator->fromEquirectangular(*texture);
    renderer.setRenderTarget(currentRenderTarget);

    auto* result = pmrem->texture.get();
    pmrems[texture] = std::move(pmrem);
    pmremVersions[texture] = version;
    return result;
}

void GLCubeMaps::dispose() {

    cubemaps.clear();
    pmrems.clear();
    pmremVersions.clear();
    pmremGenerator.reset();
}
