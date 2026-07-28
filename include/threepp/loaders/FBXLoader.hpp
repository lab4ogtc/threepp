#pragma once

#include <filesystem>
#include <memory>

namespace threepp {

    class Group;

    class FBXLoader {
    public:
        // Assimp 后端保留这些枚举用于源码兼容；当前不会读取这些设置。
        enum class MaterialMode {
            Auto, ///< Guess per-material from the SPECULAR texture filename (default).
            Phong,///< Always treat the SPECULAR slot as a traditional specular map (MeshPhongMaterial).
            PBR,  ///< Always treat the SPECULAR slot as ORM-packed roughness/metalness (MeshPhysicalMaterial).
        };

        MaterialMode materialMode = MaterialMode::Auto;

        // Assimp 后端保留该字段用于源码兼容；当前不会缩放 emissive 强度。
        float emissiveScale = 1.0f;

        FBXLoader();
        ~FBXLoader();

        std::shared_ptr<Group> load(const std::filesystem::path& path);

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp
