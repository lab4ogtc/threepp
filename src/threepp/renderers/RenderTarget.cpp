
#include "threepp/renderers/RenderTarget.hpp"

#include "threepp/math/MathUtils.hpp"

#ifdef THREEPP_HAS_GL
#include "threepp/renderers/GLRenderTarget.hpp"
#endif

using namespace threepp;

namespace {

#ifndef THREEPP_HAS_GL
    class GenericRenderTarget: public RenderTarget {

    public:
        GenericRenderTarget(unsigned int width, unsigned int height, const Options& options)
            : RenderTarget(width, height, options) {}

        void setSize(unsigned int width, unsigned int height, unsigned int depth = 1) override {
            if (this->width != width || this->height != height || this->depth != depth) {
                this->width = width;
                this->height = height;
                this->depth = depth;

                this->texture->image().width = width;
                this->texture->image().height = height;
                this->texture->image().depth = depth;

                this->dispose();
                this->disposed = false;
            }

            this->viewport.set(0, 0, static_cast<float>(width), static_cast<float>(height));
            this->scissor.set(0, 0, static_cast<float>(width), static_cast<float>(height));
        }
    };
#endif

}// namespace

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
    texture->generateMipmaps = options.generateMipmaps;

    if (options.depthTexture) depthTexture = options.depthTexture;
}

std::shared_ptr<RenderTarget> RenderTarget::create(unsigned int width, unsigned int height) {

    return create(width, height, Options{});
}

std::shared_ptr<RenderTarget> RenderTarget::create(unsigned int width, unsigned int height, const Options& options) {

#ifdef THREEPP_HAS_GL
    return std::make_shared<GLRenderTarget>(width, height, options);
#else
    return std::make_shared<GenericRenderTarget>(width, height, options);
#endif
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
