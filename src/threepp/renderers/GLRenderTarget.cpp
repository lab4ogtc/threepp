
#include "threepp/renderers/GLRenderTarget.hpp"

using namespace threepp;


std::unique_ptr<GLRenderTarget> GLRenderTarget::create(unsigned int width, unsigned int height, const Options& options) {

    return std::make_unique<GLRenderTarget>(width, height, options);
}

GLRenderTarget::GLRenderTarget(unsigned int width, unsigned int height, const Options& options)
    : RenderTarget(width, height, options) {}

void GLRenderTarget::setSize(unsigned int width, unsigned int height, unsigned int depth) {

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

void GLRenderTarget::dispose() {

    if (!disposed) {

        disposed = true;
        this->dispatchEvent("dispose", this);
    }
}

GLRenderTarget::~GLRenderTarget() {

    dispose();
}
