#ifndef THREEPP_RENDERERS_ENVMAPUTILS_HPP
#define THREEPP_RENDERERS_ENVMAPUTILS_HPP

#include "threepp/materials/interfaces.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/Texture.hpp"

#include <cstdint>
#include <memory>

namespace threepp::envmap {

    inline constexpr float kPMREMMaxMipLevel = 6.0f;

    enum class EnvMapKind: std::uint32_t {
        None = 0,
        Cube = 1,
        Equirectangular = 2,
        PMREM = 3
    };

    struct ResolvedEnvMap {
        std::shared_ptr<Texture> texture;
        float intensity{1.0f};
        EnvMapKind kind{EnvMapKind::None};
        float flipEnvMap{1.0f};
        float maxMipLevel{0.0f};
        float decodeColor{0.0f};
        float usePMREM{0.0f};
    };

    inline bool hasTexture(const std::shared_ptr<Texture>& texture) {
        return texture != nullptr && !texture->images().empty();
    }

    inline bool hasTexture(const Texture* texture) {
        return texture != nullptr && !texture->images().empty();
    }

    inline bool hasCubeTexture(const std::shared_ptr<Texture>& texture) {
        return hasTexture(texture) && dynamic_cast<CubeTexture*>(texture.get()) != nullptr;
    }

    inline bool textureUsesSRGBColorSpace(const Texture& texture) {
        return texture.colorSpace == ColorSpace::sRGB ||
               texture.colorSpace == ColorSpace::Gamma;
    }

    inline bool textureUsesManualCubeDecode(const Texture& texture) {
        return dynamic_cast<const CubeTexture*>(&texture) != nullptr &&
               textureUsesSRGBColorSpace(texture);
    }

    inline bool isCubeEnvMap(const std::shared_ptr<Texture>& texture) {
        return hasCubeTexture(texture) &&
               (texture->mapping == Mapping::CubeReflection ||
                texture->mapping == Mapping::CubeRefraction);
    }

    inline bool isEquirectangularEnvMap(const std::shared_ptr<Texture>& texture) {
        return hasTexture(texture) &&
               (texture->mapping == Mapping::EquirectangularReflection ||
                texture->mapping == Mapping::EquirectangularRefraction);
    }

    inline bool isPMREMEnvMap(const std::shared_ptr<Texture>& texture) {
        return hasTexture(texture) &&
               (texture->mapping == Mapping::CubeUVReflection ||
                texture->mapping == Mapping::CubeUVRefraction);
    }

    inline ResolvedEnvMap makeResolvedEnvMap(
        const std::shared_ptr<Texture>& texture,
        float intensity,
        EnvMapKind kind)
    {
        if (!texture) return {};

        ResolvedEnvMap result{texture, intensity, kind};
        if (kind == EnvMapKind::Cube) {
            if (auto* cubeTexture = dynamic_cast<CubeTexture*>(texture.get())) {
                result.flipEnvMap = cubeTexture->_needsFlipEnvMap ? 1.0f : 0.0f;
            }
            result.maxMipLevel = kPMREMMaxMipLevel;
            result.decodeColor = textureUsesManualCubeDecode(*texture) ? 1.0f : 0.0f;
            result.usePMREM = 1.0f;
        } else if (kind == EnvMapKind::Equirectangular || kind == EnvMapKind::PMREM) {
            result.maxMipLevel = kPMREMMaxMipLevel;
            result.usePMREM = 1.0f;
        }
        return result;
    }

    inline ResolvedEnvMap resolveTextureEnvMap(
        const std::shared_ptr<Texture>& texture,
        float intensity)
    {
        if (isCubeEnvMap(texture)) {
            return makeResolvedEnvMap(texture, intensity, EnvMapKind::Cube);
        }
        if (isEquirectangularEnvMap(texture)) {
            return makeResolvedEnvMap(texture, intensity, EnvMapKind::Equirectangular);
        }
        if (isPMREMEnvMap(texture)) {
            return makeResolvedEnvMap(texture, intensity, EnvMapKind::PMREM);
        }
        return {};
    }

    inline ResolvedEnvMap resolveEnvMap(const Scene& scene, Material& material) {
        auto* env = dynamic_cast<MaterialWithEnvMap*>(&material);
        if (!env) return {};

        if (env->envMap) {
            return resolveTextureEnvMap(env->envMap, env->envMapIntensity);
        }

        return resolveTextureEnvMap(scene.environment, 1.0f);
    }

} // namespace threepp::envmap

#endif // THREEPP_RENDERERS_ENVMAPUTILS_HPP
