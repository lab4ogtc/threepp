#pragma once

#include <threepp/constants.hpp>
#include <threepp/math/Color.hpp>
#include <threepp/renderers/Renderer.hpp>
#include <threepp/renderers/RenderTarget.hpp>

#include <cstdint>

namespace threepp {

inline bool usesSRGBColorEncoding(ColorSpace colorSpace) {
    switch (colorSpace) {
        case ColorSpace::sRGB:
        case ColorSpace::Gamma:
            return true;
        default:
            return false;
    }
}

inline std::uint32_t outputColorSpaceSRGBUniformFlag(ColorSpace colorSpace) {
    return usesSRGBColorEncoding(colorSpace) ? 1u : 0u;
}

inline ColorSpace activeOutputColorSpace(const Renderer& renderer, const RenderTarget* renderTarget) {
    return renderTarget && renderTarget->texture
        ? renderTarget->texture->colorSpace
        : renderer.outputColorSpace;
}

inline Color activeOutputClearColor(const Renderer& renderer, const RenderTarget* renderTarget, const Color& workingColor) {
    auto color = workingColor;
    ColorManagement::workingToColorSpace(color, activeOutputColorSpace(renderer, renderTarget));
    return color;
}

} // namespace threepp
