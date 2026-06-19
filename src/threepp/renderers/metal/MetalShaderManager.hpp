#ifndef THREEPP_METAL_SHADER_MANAGER_HPP
#define THREEPP_METAL_SHADER_MANAGER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace threepp::metal {

    struct ShaderProgramKey {
        bool useMap = false;
        bool useVertexColors = false;
        bool useNormal = false;
        bool flatShading = false;
        bool useSkinning = false;
        bool useLights = false;
        bool useInstancing = false;
        bool useInstanceColor = false;
        bool doubleSided = false;
        bool flipSided = false;
        bool useClipping = false;
        bool useMorphTargets = false;
        bool useMorphNormals = false;
        bool useTransmission = false;
        std::uint32_t rectAreaLightCount = 0;

        bool operator==(const ShaderProgramKey& other) const {
            return useMap == other.useMap &&
                   useVertexColors == other.useVertexColors &&
                   useNormal == other.useNormal &&
                   flatShading == other.flatShading &&
                   useSkinning == other.useSkinning &&
                   useLights == other.useLights &&
                   useInstancing == other.useInstancing &&
                   useInstanceColor == other.useInstanceColor &&
                   doubleSided == other.doubleSided &&
                   flipSided == other.flipSided &&
                   useClipping == other.useClipping &&
                   useMorphTargets == other.useMorphTargets &&
                   useMorphNormals == other.useMorphNormals &&
                   useTransmission == other.useTransmission &&
                   rectAreaLightCount == other.rectAreaLightCount;
        }
    };

    struct ShaderProgramKeyHash {
        std::size_t operator()(const ShaderProgramKey& key) const {
            std::size_t value = (key.useMap ? 1u : 0u) |
                                ((key.useVertexColors ? 1u : 0u) << 1u) |
                                ((key.useNormal ? 1u : 0u) << 2u) |
                                ((key.flatShading ? 1u : 0u) << 3u) |
                                ((key.useSkinning ? 1u : 0u) << 4u) |
                                ((key.useLights ? 1u : 0u) << 5u) |
                                ((key.useInstancing ? 1u : 0u) << 6u) |
                                ((key.useInstanceColor ? 1u : 0u) << 7u) |
                                ((key.doubleSided ? 1u : 0u) << 8u) |
                                ((key.flipSided ? 1u : 0u) << 9u) |
                                ((key.useClipping ? 1u : 0u) << 10u) |
                                ((key.useMorphTargets ? 1u : 0u) << 11u) |
                                ((key.useMorphNormals ? 1u : 0u) << 12u) |
                                ((key.useTransmission ? 1u : 0u) << 13u);
            value ^= std::hash<std::uint32_t>{}(key.rectAreaLightCount) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
            return value;
        }
    };

    struct DepthShaderKey {
        bool useSkinning = false;
        bool useInstancing = false;
        bool useClipping = false;
        bool useMorphTargets = false;

        bool operator==(const DepthShaderKey& other) const {
            return useSkinning == other.useSkinning &&
                   useInstancing == other.useInstancing &&
                   useClipping == other.useClipping &&
                   useMorphTargets == other.useMorphTargets;
        }
    };

    struct DepthShaderKeyHash {
        std::size_t operator()(const DepthShaderKey& key) const {
            return (key.useSkinning ? 1u : 0u) |
                   ((key.useInstancing ? 1u : 0u) << 1u) |
                   ((key.useClipping ? 1u : 0u) << 2u) |
                   ((key.useMorphTargets ? 1u : 0u) << 3u);
        }
    };

    struct SpriteShaderKey {
        bool useSizeAttenuation = false;
        bool useAlphaMap = false;
        bool useAlphaTest = false;
        bool useFog = false;

        bool operator==(const SpriteShaderKey& other) const {
            return useSizeAttenuation == other.useSizeAttenuation &&
                   useAlphaMap == other.useAlphaMap &&
                   useAlphaTest == other.useAlphaTest &&
                   useFog == other.useFog;
        }
    };

    struct SpriteShaderKeyHash {
        std::size_t operator()(const SpriteShaderKey& key) const {
            return (key.useSizeAttenuation ? 1u : 0u) |
                   ((key.useAlphaMap ? 1u : 0u) << 1u) |
                   ((key.useAlphaTest ? 1u : 0u) << 2u) |
                   ((key.useFog ? 1u : 0u) << 3u);
        }
    };

    class MetalShaderManager {

    public:
        explicit MetalShaderManager(void* device);

        ~MetalShaderManager();

        void* getOrCreateVertexFunction(const ShaderProgramKey& key);

        void* getOrCreateFragmentFunction(const ShaderProgramKey& key);

        void* getOrCreateDepthVertexFunction(const DepthShaderKey& key);

        void* getOrCreateDepthFragmentFunction(const DepthShaderKey& key);

        void* getOrCreatePointDepthVertexFunction(const DepthShaderKey& key);

        void* getOrCreatePointDepthFragmentFunction(const DepthShaderKey& key);

        void* getOrCreateSpriteVertexFunction(const SpriteShaderKey& key);

        void* getOrCreateSpriteFragmentFunction(const SpriteShaderKey& key);

        void* getOrCreateLineVertexFunction(bool useVertexColors = false);

        void* getOrCreateLineFragmentFunction(bool useVertexColors = false);

        void* getOrCreatePointsVertexFunction(bool useVertexColors = false, bool useMorphTargets = false);

        void* getOrCreatePointsFragmentFunction(bool useVertexColors = false);

        void* getOrCreateParticleVertexFunction(bool useMap = false);

        void* getOrCreateParticleFragmentFunction(bool useMap = false);

        void* getOrCreateRawShaderVertexFunction();

        void* getOrCreateRawShaderFragmentFunction();

        void* getOrCreateDepthTextureVertexFunction();

        void* getOrCreateDepthTextureFragmentFunction();

        void* getOrCreateDepthTextureLinearReadbackFragmentFunction();

        void* getOrCreateSkyVertexFunction();

        void* getOrCreateSkyFragmentFunction();

        void* getOrCreateBackgroundCubeVertexFunction();

        void* getOrCreateBackgroundCubeFragmentFunction();

        void* getOrCreateBackgroundEquirectFragmentFunction();

        void* getOrCreateEquirectToCubeVertexFunction();

        void* getOrCreateEquirectToCubeFragmentFunction();

        void* getOrCreateWaterVertexFunction();

        void* getOrCreateWaterFragmentFunction();

        void* getOrCreateReflectorVertexFunction();

        void* getOrCreateReflectorFragmentFunction();

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp::metal

#endif
