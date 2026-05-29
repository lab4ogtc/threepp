#ifndef THREEPP_METAL_SHADER_MANAGER_HPP
#define THREEPP_METAL_SHADER_MANAGER_HPP

#include <cstddef>
#include <memory>

namespace threepp::metal {

    struct ShaderProgramKey {
        bool useMap = false;
        bool useVertexColors = false;
        bool useNormal = false;

        bool operator==(const ShaderProgramKey& other) const {
            return useMap == other.useMap
                && useVertexColors == other.useVertexColors
                && useNormal == other.useNormal;
        }
    };

    struct ShaderProgramKeyHash {
        std::size_t operator()(const ShaderProgramKey& key) const {
            return (key.useMap ? 1u : 0u)
                 | ((key.useVertexColors ? 1u : 0u) << 1u)
                 | ((key.useNormal ? 1u : 0u) << 2u);
        }
    };

    class MetalShaderManager {

    public:
        explicit MetalShaderManager(void* device);

        ~MetalShaderManager();

        void* getOrCreateVertexFunction(const ShaderProgramKey& key);

        void* getOrCreateFragmentFunction(const ShaderProgramKey& key);

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp::metal

#endif
