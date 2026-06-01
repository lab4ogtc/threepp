// https://github.com/mrdoob/three.js/blob/r129/examples/js/objects/Water.js

#ifndef THREEPP_WATER_HPP
#define THREEPP_WATER_HPP

#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/RenderJob.hpp"

namespace threepp {

    class Camera;
    class PerspectiveCamera;
    class RenderTarget;

    class Water: public Mesh, public PreRenderable {

    public:
        struct Options {

            std::optional<unsigned int> textureWidth;
            std::optional<unsigned int> textureHeight;
            std::optional<float> clipBias;
            std::optional<float> alpha;
            std::optional<float> time;
            std::shared_ptr<Texture> waterNormals;
            std::optional<Vector3> sunDirection;
            std::optional<Color> sunColor;
            std::optional<Color> waterColor;
            std::optional<Vector3> eye;
            std::optional<float> distortionScale;
            std::optional<Side> side;
            std::optional<bool> fog;
        };

        Water(const std::shared_ptr<BufferGeometry>& geometry, const Options& options);

        [[nodiscard]] std::string type() const override;

        /**
         * @brief 更新反射相机、纹理矩阵和 eye uniform。
         *
         * @param camera 当前主相机。
         * @return 当反射面朝向相机时返回 true；背向相机时返回 false。
         */
        bool updateReflection(Camera& camera);

        /**
         * @brief 获取水面反射使用的渲染目标。
         *
         * @return 非拥有指针；生命周期由 Water 管理。
         */
        [[nodiscard]] RenderTarget* reflectionRenderTarget() const;

        /**
         * @brief 获取水面反射使用的镜像相机。
         *
         * @return 镜像相机引用；生命周期由 Water 管理。
         */
        [[nodiscard]] PerspectiveCamera& reflectionCamera() const;

        std::optional<RenderJob> getPreRenderJob(Camera& mainCamera) override;

        static std::shared_ptr<Water> create(
                const std::shared_ptr<BufferGeometry>& geometry,
                const Options& options = Options());

        ~Water() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp

#endif//THREEPP_WATER_HPP
