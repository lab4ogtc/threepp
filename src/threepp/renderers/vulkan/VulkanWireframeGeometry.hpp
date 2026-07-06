#ifndef THREEPP_VULKAN_WIREFRAME_GEOMETRY_HPP
#define THREEPP_VULKAN_WIREFRAME_GEOMETRY_HPP

#include <vector>

namespace threepp {

    class BufferGeometry;

    namespace vulkan {

        std::vector<unsigned int> buildWireframeIndices(const BufferGeometry& geometry);

    }// namespace vulkan
}// namespace threepp

#endif// THREEPP_VULKAN_WIREFRAME_GEOMETRY_HPP
