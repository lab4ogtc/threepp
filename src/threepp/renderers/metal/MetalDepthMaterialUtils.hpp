#ifndef THREEPP_METAL_DEPTH_MATERIAL_UTILS_HPP
#define THREEPP_METAL_DEPTH_MATERIAL_UTILS_HPP

#include "threepp/materials/ShaderMaterial.hpp"

#include <string>

namespace threepp::metal {

    [[nodiscard]] inline bool isPackedLinearDepthMaterial(const ShaderMaterial& material) {
        const auto& fragment = material.fragmentShader;
        return fragment.find("vec4(r, g") != std::string::npos ||
               fragment.find("vec4(r,g") != std::string::npos;
    }

}// namespace threepp::metal

#endif// THREEPP_METAL_DEPTH_MATERIAL_UTILS_HPP
