// https://github.com/mrdoob/three.js/blob/r129/examples/js/objects/Reflector.js

#ifndef THREEPP_REFLECTOR_HPP
#define THREEPP_REFLECTOR_HPP

#include "threepp/core/Shader.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/RenderJob.hpp"

namespace threepp {

    class Camera;
    class PerspectiveCamera;
    class RenderTarget;

    class Reflector: public Mesh, public PreRenderable {

    public:
        struct Options {

            std::optional<Color> color;
            std::optional<unsigned int> textureWidth;
            std::optional<unsigned int> textureHeight;
            std::optional<float> clipBias;
            std::optional<Shader> shader;
        };

        Reflector(const std::shared_ptr<BufferGeometry>& geometry, Options options);

        [[nodiscard]] std::string type() const override;

        /**
         * @brief 更新反射相机和纹理矩阵。
         *
         * @param camera 当前主相机。
         * @return 当反射面朝向相机时返回 true；背向相机时返回 false。
         */
        bool updateReflection(Camera& camera);

        [[nodiscard]] RenderTarget* reflectionRenderTarget() const;

        [[nodiscard]] PerspectiveCamera& reflectionCamera() const;

        std::optional<RenderJob> getPreRenderJob(Camera& mainCamera) override;

        static std::shared_ptr<Reflector> create(const std::shared_ptr<BufferGeometry>& geometry, Options options = Options());

        ~Reflector() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp

#endif//THREEPP_REFLECTOR_HPP
