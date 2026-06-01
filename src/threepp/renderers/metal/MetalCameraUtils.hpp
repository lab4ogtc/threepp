#ifndef THREEPP_METAL_CAMERA_UTILS_HPP
#define THREEPP_METAL_CAMERA_UTILS_HPP

#include "threepp/cameras/Camera.hpp"
#include "threepp/math/Matrix4.hpp"

namespace threepp::metal {

    inline Matrix4 convertProjectionToMetalClipSpace(const Matrix4& projectionMatrix) {

        static const Matrix4 depthRangeConversion = Matrix4{}.set(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 0.5f, 0.5f,
            0, 0, 0, 1);

        return Matrix4{}.multiplyMatrices(depthRangeConversion, projectionMatrix);
    }

    inline void prepareCameraForRender(Camera& camera) {

        if (camera.parent == nullptr) {
            camera.updateMatrixWorld();
        } else {
            camera.matrixWorldInverse.copy(*camera.matrixWorld).invert();
        }
    }

}// namespace threepp::metal

#endif
