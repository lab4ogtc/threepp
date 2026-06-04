#ifndef THREEPP_PARTICLEMATERIAL_HPP
#define THREEPP_PARTICLEMATERIAL_HPP

#include "threepp/materials/ShaderMaterial.hpp"

namespace threepp {

    /**
     * @brief ParticleSystem 使用的着色器材质。
     *
     * 该材质继承 ShaderMaterial，以保持 OpenGL 自定义着色器路径不变，
     * 同时允许其他后端显式识别 ParticleSystem 渲染。
     */
    class ParticleMaterial: public ShaderMaterial {

    public:
        [[nodiscard]] std::string type() const override;

        /**
         * @brief 创建带有默认 ParticleSystem 着色器的粒子材质。
         */
        static std::shared_ptr<ParticleMaterial> create();

    protected:
        ParticleMaterial();

        std::shared_ptr<Material> createDefault() const override;
    };

}// namespace threepp

#endif//THREEPP_PARTICLEMATERIAL_HPP
