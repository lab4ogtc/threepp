#ifndef THREEPP_METAL_SCREEN_SPACE_HPP
#define THREEPP_METAL_SCREEN_SPACE_HPP

#include <algorithm>

namespace threepp::metal {

    struct ScreenSpaceSpriteLayout {
        float logicalWidth;
        float logicalHeight;
        float viewportWidth;
        float viewportHeight;
    };

    inline ScreenSpaceSpriteLayout computeScreenSpaceSpriteLayout(float attachmentWidth,
                                                                  float attachmentHeight,
                                                                  float pixelRatio,
                                                                  bool renderTarget) {

        const auto coordinateRatio = renderTarget || pixelRatio <= 0.f ? 1.f : pixelRatio;
        return {
                attachmentWidth / coordinateRatio,
                attachmentHeight / coordinateRatio,
                attachmentWidth,
                attachmentHeight};
    }

}// namespace threepp::metal

#endif//THREEPP_METAL_SCREEN_SPACE_HPP
