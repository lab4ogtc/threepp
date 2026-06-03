// https://github.com/mrdoob/three.js/blob/r129/src/materials/ShaderMaterial.js

#ifndef THREEPP_SHADERMATERIAL_HPP
#define THREEPP_SHADERMATERIAL_HPP

#include "interfaces.hpp"
#include "threepp/materials/Material.hpp"

#include <vector>

namespace threepp {

    /**
     * @brief Source language used by a shader material.
     */
    enum class ShaderLanguage {
        GLSL,
        SLANG,
    };

    class ShaderMaterial: public virtual Material,
                          public MaterialWithClipping,
                          public MaterialWithLights,
                          public MaterialWithWireframe,
                          public MaterialWithLineWidth,
                          public MaterialWithEnvMap,
                          public MaterialWithDefines {

    public:
        std::string vertexShader;
        std::string fragmentShader;
        UniformMap uniforms;
        ShaderLanguage shaderLanguage = ShaderLanguage::GLSL;
        std::vector<std::string> uniformLayout;

        std::optional<std::string> index0AttributeName;
        bool uniformsNeedUpdate = false;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<ShaderMaterial> create();

    protected:
        ShaderMaterial();

        std::shared_ptr<Material> createDefault() const override;

        void copyInto(Material& material) const override;
    };

}// namespace threepp

#endif//THREEPP_SHADERMATERIAL_HPP
