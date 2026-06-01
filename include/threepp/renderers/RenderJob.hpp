#ifndef THREEPP_RENDERJOB_HPP
#define THREEPP_RENDERJOB_HPP

#include <optional>

namespace threepp {

    class Camera;
    class Object3D;
    class RenderTarget;

    /**
     * @brief Renderer-owned offscreen pre-render task.
     *
     * The renderer injects the active scene when processing the job. The
     * pointers are non-owning and must remain valid for the current render call.
     */
    struct RenderJob {
        Object3D* initiator = nullptr;
        Camera* camera = nullptr;
        RenderTarget* renderTarget = nullptr;
    };

    /**
     * @brief Interface for objects that need an offscreen render pass before
     * the main scene pass.
     */
    class PreRenderable {

    public:
        /**
         * @brief Builds the pre-render job for the current main camera.
         *
         * @param mainCamera Camera used by the main pass.
         * @return A render job, or std::nullopt when this object should skip the
         * current pre-pass.
         */
        virtual std::optional<RenderJob> getPreRenderJob(Camera& mainCamera) = 0;

        virtual ~PreRenderable() = default;
    };

}// namespace threepp

#endif//THREEPP_RENDERJOB_HPP
