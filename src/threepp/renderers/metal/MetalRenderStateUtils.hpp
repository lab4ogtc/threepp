#ifndef THREEPP_METAL_RENDER_STATE_UTILS_HPP
#define THREEPP_METAL_RENDER_STATE_UTILS_HPP

#include "threepp/constants.hpp"

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

}// namespace threepp::metal

#endif
