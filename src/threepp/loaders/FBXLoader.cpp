#include "threepp/loaders/FBXLoader.hpp"

#include "threepp/loaders/AssimpLoader.hpp"

#include <exception>
#include <iostream>

namespace threepp {

    struct FBXLoader::Impl {};

    FBXLoader::FBXLoader(): pimpl_(std::make_unique<Impl>()) {}
    FBXLoader::~FBXLoader() = default;

    std::shared_ptr<Group> FBXLoader::load(const std::filesystem::path& path) {
        try {
            AssimpLoader loader;
            return loader.load(path);
        } catch (const std::exception& e) {
            std::cerr << "[FBXLoader] " << e.what() << std::endl;
            return nullptr;
        }
    }

}// namespace threepp
