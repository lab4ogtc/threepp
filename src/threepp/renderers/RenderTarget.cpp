
#include "threepp/renderers/RenderTarget.hpp"

#include "threepp/math/MathUtils.hpp"

using namespace threepp;

RenderTarget::RenderTarget(unsigned int width, unsigned int height, const Options& options)
    : uuid(math::generateUUID()),
      width(width), height(height),
      depth(options.depth),
      scissor(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)),
      viewport(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)),
      depthBuffer(options.depthBuffer), stencilBuffer(options.stencilBuffer),
      texture(Texture::create({Image({}, width, height)})) {

    if (options.mapping) texture->mapping = *options.mapping;
    if (options.wrapS) texture->wrapS = *options.wrapS;
    if (options.wrapT) texture->wrapT = *options.wrapT;
    if (options.magFilter) texture->magFilter = *options.magFilter;
    if (options.minFilter) texture->minFilter = *options.minFilter;
    if (options.format) texture->format = *options.format;
    if (options.type) texture->type = *options.type;
    if (options.anisotropy) texture->anisotropy = *options.anisotropy;
    if (options.encoding) texture->encoding = *options.encoding;

    if (options.depthTexture) depthTexture = options.depthTexture;
}

RenderTarget& RenderTarget::copy(const RenderTarget& source) {

    this->width = source.width;
    this->height = source.height;
    this->depth = source.depth;

    this->viewport.copy(source.viewport);

    this->texture = source.texture;

    this->depthBuffer = source.depthBuffer;
    this->stencilBuffer = source.stencilBuffer;
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
