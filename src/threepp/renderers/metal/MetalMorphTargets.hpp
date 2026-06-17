#ifndef THREEPP_METAL_MORPH_TARGETS_HPP
#define THREEPP_METAL_MORPH_TARGETS_HPP

#include "threepp/core/BufferGeometry.hpp"
#include "threepp/materials/Material.hpp"
#include "threepp/materials/interfaces.hpp"
#include "threepp/objects/ObjectWithMorphTargetInfluences.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace threepp::metal {

    using MetalMorphInfluence = std::pair<std::size_t, float>;

    class MetalMorphTargets {

    public:
        std::unordered_map<unsigned int, std::vector<MetalMorphInfluence>> influencesList;

        MetalMorphTargets() {
            for (std::size_t i = 0; i < workInfluences_.size(); ++i) {
                workInfluences_[i] = {i, 0.f};
            }
        }

        void update(Object3D* object, BufferGeometry* geometry, Material* material) {
            update(object, geometry, material, materialWantsMorphNormals(geometry, material));
        }

        void update(Object3D* object, BufferGeometry* geometry, Material* material, bool useMorphNormals) {
            morphInfluences_.fill(0.f);
            morphTargetBaseInfluence_ = 1.f;
            if (!object || !geometry || !material) return;

            std::vector<float> objectInfluences;
            if (auto* objectWithMorphTargetInfluences = dynamic_cast<ObjectWithMorphTargetInfluences*>(object)) {
                objectInfluences = objectWithMorphTargetInfluences->morphTargetInfluences();
            }

            auto* morphMaterial = material->as<MaterialWithMorphTargets>();
            std::vector<std::shared_ptr<BufferAttribute>>* morphTargets = nullptr;
            std::vector<std::shared_ptr<BufferAttribute>>* morphNormals = nullptr;
            if (morphMaterial) {
                if (morphMaterial->morphTargets) {
                    morphTargets = geometry->getMorphAttribute("position");
                }
                if (useMorphNormals && morphMaterial->morphNormals) {
                    morphNormals = geometry->getMorphAttribute("normal");
                }
            }

            std::size_t length = objectInfluences.size();
            if (morphTargets) {
                length = std::min(length, morphTargets->size());
            }
            if (!morphTargets && morphNormals) {
                length = std::min(length, morphNormals->size());
            }
            const auto activeSlotCount = morphNormals ? 4u : 8u;

            auto& influences = influencesList[geometry->id];
            if (influences.size() < length) {
                for (std::size_t i = influences.size(); i < length; ++i) {
                    influences.emplace_back(i, 0.f);
                }
            }
            influences.resize(length);

            for (std::size_t i = 0; i < length; ++i) {
                influences[i].first = i;
                influences[i].second = objectInfluences[i];
            }

            std::stable_sort(influences.begin(), influences.end(), [](const auto& a, const auto& b) {
                return std::abs(b.second) < std::abs(a.second);
            });

            for (std::size_t i = 0; i < workInfluences_.size(); ++i) {
                if (i < length && i < activeSlotCount && influences[i].second > 0.f) {
                    workInfluences_[i] = influences[i];
                } else {
                    workInfluences_[i] = {maxSafeInteger, 0.f};
                }
            }

            std::stable_sort(workInfluences_.begin(), workInfluences_.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });

            float morphInfluencesSum = 0.f;
            for (std::size_t i = 0; i < workInfluences_.size(); ++i) {
                const auto [index, value] = workInfluences_[i];

                const auto morphTargetName = "morphTarget" + std::to_string(i);
                const auto morphNormalName = "morphNormal" + std::to_string(i);

                if (index != maxSafeInteger && value > 0.f) {
                    if (morphTargets && index < morphTargets->size() && geometry->getAttribute(morphTargetName) != morphTargets->at(index).get()) {
                        geometry->setAttribute(morphTargetName, morphTargets->at(index));
                    }

                    if (morphNormals && i < 4 && index < morphNormals->size() && geometry->getAttribute(morphNormalName) != morphNormals->at(index).get()) {
                        geometry->setAttribute(morphNormalName, morphNormals->at(index));
                    }

                    morphInfluences_[i] = value;
                    morphInfluencesSum += value;
                } else {
                    if (morphTargets && geometry->hasAttribute(morphTargetName)) {
                        geometry->deleteAttribute(morphTargetName);
                    }
                }

                if (morphNormals && (i >= 4 || index == maxSafeInteger || value <= 0.f) && geometry->hasAttribute(morphNormalName)) {
                    geometry->deleteAttribute(morphNormalName);
                }
            }

            morphTargetBaseInfluence_ = geometry->morphTargetsRelative ? 1.f : 1.f - morphInfluencesSum;
        }

        [[nodiscard]] float morphTargetBaseInfluence() const {
            return morphTargetBaseInfluence_;
        }

        [[nodiscard]] const std::array<float, 8>& morphTargetInfluences() const {
            return morphInfluences_;
        }

        void removeGeometry(unsigned int geometryId) {
            influencesList.erase(geometryId);
        }

    private:
        static constexpr auto maxSafeInteger = static_cast<std::size_t>(std::numeric_limits<unsigned int>::max());

        static bool materialWantsMorphNormals(BufferGeometry* geometry, Material* material) {
            if (!geometry || !material || geometry->getMorphAttribute("normal") == nullptr) return false;

            auto* morphMaterial = material->as<MaterialWithMorphTargets>();
            if (!morphMaterial || !morphMaterial->morphNormals) return false;

            auto* flatMaterial = dynamic_cast<MaterialWithFlatShading*>(material);
            return !flatMaterial || !flatMaterial->flatShading;
        }

        std::array<MetalMorphInfluence, 8> workInfluences_{};
        std::array<float, 8> morphInfluences_{};
        float morphTargetBaseInfluence_ = 1.f;
    };

}// namespace threepp::metal

#endif// THREEPP_METAL_MORPH_TARGETS_HPP
