#ifndef THREEPP_METAL_SHADER_MANAGER_HPP
#define THREEPP_METAL_SHADER_MANAGER_HPP

#include <cstddef>
#include <memory>

namespace threepp::metal {

    struct ShaderProgramKey {
        bool useMap = false;
        bool useVertexColors = false;
        bool useNormal = false;
        bool useSkinning = false;
        bool useLights = false;
        bool useInstancing = false;
        bool useInstanceColor = false;
        bool doubleSided = false;
        bool flipSided = false;

        bool operator==(const ShaderProgramKey& other) const {
            return useMap == other.useMap &&
                   useVertexColors == other.useVertexColors &&
                   useNormal == other.useNormal &&
                   useSkinning == other.useSkinning &&
                   useLights == other.useLights &&
                   useInstancing == other.useInstancing &&
                   useInstanceColor == other.useInstanceColor &&
                   doubleSided == other.doubleSided &&
                   flipSided == other.flipSided;
        }
    };

    struct ShaderProgramKeyHash {
        std::size_t operator()(const ShaderProgramKey& key) const {
            return (key.useMap ? 1u : 0u) |
                   ((key.useVertexColors ? 1u : 0u) << 1u) |
                   ((key.useNormal ? 1u : 0u) << 2u) |
                   ((key.useSkinning ? 1u : 0u) << 3u) |
                   ((key.useLights ? 1u : 0u) << 4u) |
                   ((key.useInstancing ? 1u : 0u) << 5u) |
                   ((key.useInstanceColor ? 1u : 0u) << 6u) |
                   ((key.doubleSided ? 1u : 0u) << 7u) |
                   ((key.flipSided ? 1u : 0u) << 8u);
        }
    };

    struct DepthShaderKey {
        bool useSkinning = false;
        bool useInstancing = false;

        bool operator==(const DepthShaderKey& other) const {
            return useSkinning == other.useSkinning && useInstancing == other.useInstancing;
        }
    };

    struct DepthShaderKeyHash {
        std::size_t operator()(const DepthShaderKey& key) const {
            return (key.useSkinning ? 1u : 0u) |
                   ((key.useInstancing ? 1u : 0u) << 1u);
        }
    };

    class MetalShaderManager {

    public:
        explicit MetalShaderManager(void* device);

        ~MetalShaderManager();

        void* getOrCreateVertexFunction(const ShaderProgramKey& key);

        void* getOrCreateFragmentFunction(const ShaderProgramKey& key);

        void* getOrCreateDepthVertexFunction(bool useSkinning, bool useInstancing = false);

        void* getOrCreatePointDepthVertexFunction(bool useSkinning, bool useInstancing = false);

        void* getOrCreatePointDepthFragmentFunction(bool useSkinning, bool useInstancing = false);

        void* getOrCreateSpriteVertexFunction();

        void* getOrCreateSpriteFragmentFunction();

        void* getOrCreateLineVertexFunction(bool useVertexColors = false);

        void* getOrCreateLineFragmentFunction(bool useVertexColors = false);

        void* getOrCreatePointsVertexFunction(bool useVertexColors = false);

        void* getOrCreatePointsFragmentFunction(bool useVertexColors = false);

        void* getOrCreateRawShaderVertexFunction();

        void* getOrCreateRawShaderFragmentFunction();

        void* getOrCreateSkyVertexFunction();

        void* getOrCreateSkyFragmentFunction();

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
