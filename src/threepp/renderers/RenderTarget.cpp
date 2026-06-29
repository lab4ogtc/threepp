
#include "threepp/renderers/RenderTarget.hpp"

#include "threepp/math/MathUtils.hpp"

#include <algorithm>

using namespace threepp;

namespace {

    std::shared_ptr<Texture> createRenderTargetTexture(
            unsigned int width,
            unsigned int height,
            unsigned int depth,
            const RenderTarget::Options& options) {

        auto texture = Texture::create({Image({}, width, height, depth)});

        if (options.mapping) texture->mapping = *options.mapping;
        if (options.wrapS) texture->wrapS = *options.wrapS;
        if (options.wrapT) texture->wrapT = *options.wrapT;
        if (options.magFilter) texture->magFilter = *options.magFilter;
        if (options.minFilter) texture->minFilter = *options.minFilter;
        if (options.format) texture->format = *options.format;
        if (options.type) texture->type = *options.type;
        if (options.anisotropy) texture->anisotropy = *options.anisotropy;
        if (auto colorSpace = options.effectiveColorSpace()) {
            texture->colorSpace = *colorSpace;
        }
        texture->generateMipmaps = options.generateMipmaps;

        return texture;
    }

}// namespace


std::unique_ptr<RenderTarget> RenderTarget::create(unsigned int width, unsigned int height, const Options& options) {

    return std::make_unique<RenderTarget>(width, height, options);
}

RenderTarget::RenderTarget(unsigned int width, unsigned int height, const Options& options)
    : uuid(math::generateUUID()),
      width(width), height(height),
      scissor(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)),
      viewport(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)),
      depthBuffer(options.depthBuffer), stencilBuffer(options.stencilBuffer),
      zeroCopy(options.zeroCopy) {

    const auto textureCount = std::max(1, options.count);
    textures.reserve(static_cast<std::size_t>(textureCount));
    for (int i = 0; i < textureCount; ++i) {
        textures.push_back(createRenderTargetTexture(width, height, depth, options));
    }
    texture = textures.front();

    if (options.depthTexture) depthTexture = options.depthTexture;

}

void RenderTarget::setSize(unsigned int width, unsigned int height, unsigned int depth) {

    if (this->width != width || this->height != height || this->depth != depth) {

        this->width = width;
        this->height = height;
        this->depth = depth;

        if (this->textures.empty()) {
            this->texture->image() = Image(std::vector<unsigned char>{}, width, height, depth);
        } else {
            for (auto& targetTexture : this->textures) {
                if (targetTexture) {
                    targetTexture->image() = Image(std::vector<unsigned char>{}, width, height, depth);
                }
            }
            this->texture = this->textures.front();
        }

        this->dispose();
    }

    this->viewport.set(0, 0, static_cast<float>(width), static_cast<float>(height));
    this->scissor.set(0, 0, static_cast<float>(width), static_cast<float>(height));
}

RenderTarget& RenderTarget::copy(const RenderTarget& source) {

    this->width = source.width;
    this->height = source.height;
    this->depth = source.depth;

    this->viewport.copy(source.viewport);

    this->texture = source.texture;
    this->textures = source.textures;
    //                this->texture.image = { ...this->texture.image }; // See #20328.

    this->depthBuffer = source.depthBuffer;
    this->stencilBuffer = source.stencilBuffer;
    this->zeroCopy = source.zeroCopy;
    this->isExternal = source.isExternal;
    this->depthTexture = source.depthTexture;

    return *this;
}

void RenderTarget::dispose() {

    if (!disposed) {

        disposed = true;
        this->dispatchEvent("dispose", this);
    }
}

RenderTarget::~RenderTarget() {

    dispose();
}
