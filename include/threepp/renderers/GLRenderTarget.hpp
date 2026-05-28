// https://github.com/mrdoob/three.js/blob/r129/src/renderers/WebGLRenderTarget.js

#ifndef THREEPP_GLRENDERTARGET_HPP
#define THREEPP_GLRENDERTARGET_HPP

#include "threepp/renderers/RenderTarget.hpp"

namespace threepp {

    class GLRenderTarget: public RenderTarget {

    public:
        explicit GLRenderTarget(unsigned int width, unsigned int height, const Options& options);

        GLRenderTarget(GLRenderTarget&&) = delete;
        GLRenderTarget(const GLRenderTarget&) = delete;
        GLRenderTarget& operator=(GLRenderTarget&&) = delete;
        GLRenderTarget& operator=(const GLRenderTarget&) = delete;

        void setSize(unsigned int width, unsigned int height, unsigned int depth = 1) override;

        void dispose() override;

        static std::unique_ptr<GLRenderTarget> create(unsigned int width, unsigned int height, const Options& options);

        ~GLRenderTarget() override;
    };

}// namespace threepp

#endif//THREEPP_GLRENDERTARGET_HPP
