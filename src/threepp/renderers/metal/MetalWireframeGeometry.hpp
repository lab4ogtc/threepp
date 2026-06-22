#ifndef THREEPP_METAL_WIREFRAME_GEOMETRY_HPP
#define THREEPP_METAL_WIREFRAME_GEOMETRY_HPP

#include "threepp/core/BufferAttribute.hpp"

#include <limits>
#include <memory>
#include <vector>

namespace threepp {

    class BufferGeometry;

    namespace metal {

        struct WireframeIndexAttribute {
            std::unique_ptr<IntBufferAttribute> attribute;
            unsigned int indexVersion = std::numeric_limits<unsigned int>::max();
            unsigned int positionVersion = std::numeric_limits<unsigned int>::max();
            unsigned int attributesVersion = std::numeric_limits<unsigned int>::max();
        };

        std::vector<unsigned int> buildWireframeIndices(const BufferGeometry& geometry);

        IntBufferAttribute& getOrUpdateWireframeAttribute(BufferGeometry& geometry, WireframeIndexAttribute& cache);

    }// namespace metal
}// namespace threepp

#endif//THREEPP_METAL_WIREFRAME_GEOMETRY_HPP
