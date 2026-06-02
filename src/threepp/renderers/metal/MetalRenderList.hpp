#ifndef THREEPP_METAL_RENDER_LIST_HPP
#define THREEPP_METAL_RENDER_LIST_HPP

#include "threepp/core/Object3D.hpp"
#include "threepp/core/misc.hpp"
#include "threepp/materials/Material.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace threepp::metal {

    struct MetalRenderItem {

        Object3D* object = nullptr;
        Material* material = nullptr;
        std::optional<GeometryGroup> group;
        unsigned int renderOrder = 0;
        float z = 0;
    };

    class MetalRenderList {

    public:
        std::vector<MetalRenderItem> opaque;
        std::vector<MetalRenderItem> transparent;

        void clear() {
            opaque.clear();
            transparent.clear();
        }

        void push(Object3D& object, Material& material, float z, std::optional<GeometryGroup> group = std::nullopt) {
            MetalRenderItem item{&object, &material, group, object.renderOrder, z};
            if (material.transparent) {
                transparent.emplace_back(item);
            } else {
                opaque.emplace_back(item);
            }
        }

        void sort() {
            if (opaque.size() > 1) {
                std::stable_sort(opaque.begin(), opaque.end(), [](const MetalRenderItem& a, const MetalRenderItem& b) {
                    if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
                    if (a.material->id != b.material->id) return a.material->id < b.material->id;
                    if (a.z != b.z) return a.z < b.z;
                    return a.object->id < b.object->id;
                });
            }

            if (transparent.size() > 1) {
                std::stable_sort(transparent.begin(), transparent.end(), [](const MetalRenderItem& a, const MetalRenderItem& b) {
                    if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
                    if (a.z != b.z) return a.z > b.z;
                    return a.object->id < b.object->id;
                });
            }
        }
    };

}// namespace threepp::metal

#endif
