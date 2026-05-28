
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/GLRenderer.hpp"

#ifdef __APPLE__
//#include "threepp/renderers/metal/MetalRenderer.hpp"
#endif

using namespace threepp;

std::unique_ptr<Renderer> Renderer::create(
        Window& window,
        Backend backend,
        const Parameters& params) {

    switch (backend) {

        case Backend::OpenGL:
            return std::make_unique<GLRenderer>(window);

        case Backend::Metal:
#ifdef __APPLE__
            // return std::make_unique<MetalRenderer>(window);
            throw std::runtime_error("Metal backend not yet implemented in P0");
#else
            throw std::runtime_error("Metal backend not supported on this platform");
#endif

        case Backend::Vulkan:
            throw std::runtime_error("Vulkan backend not yet implemented");
    }

    return nullptr;
}
