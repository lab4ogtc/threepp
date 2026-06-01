#ifndef THREEPP_METAL_RENDER_STATE_UTILS_HPP
#define THREEPP_METAL_RENDER_STATE_UTILS_HPP

#include "threepp/constants.hpp"

#include <optional>

namespace threepp::metal {

    enum class FrontFaceWinding {
        Clockwise,
        CounterClockwise
    };

    enum class CullMode {
        None,
        Back
    };

    struct FaceCullingState {
        FrontFaceWinding frontFaceWinding;
        CullMode cullMode;
    };

    inline FaceCullingState computeFaceCullingState(Side side, bool frontFaceCW, bool wireframe = false) {

        auto flipSided = side == Side::Back;
        if (frontFaceCW) {
            flipSided = !flipSided;
        }

        return {
            flipSided ? FrontFaceWinding::Clockwise : FrontFaceWinding::CounterClockwise,
            (wireframe || side == Side::Double) ? CullMode::None : CullMode::Back};
    }

    inline Side defaultShadowSide(Side side) {

        switch (side) {
            case Side::Front:
                return Side::Back;
            case Side::Back:
                return Side::Front;
            case Side::Double:
                return Side::Double;
        }

        return Side::Double;
    }

    inline FaceCullingState computeShadowFaceCullingState(
            Side materialSide,
            std::optional<Side> shadowSide,
            bool frontFaceCW,
            bool wireframe = false,
            bool isVSM = false) {

        const auto effectiveSide = shadowSide.value_or(isVSM ? materialSide : defaultShadowSide(materialSide));
        return computeFaceCullingState(effectiveSide, frontFaceCW, wireframe);
    }

}// namespace threepp::metal

#endif
