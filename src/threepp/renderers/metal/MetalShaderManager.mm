#import "MetalShaderManager.hpp"

#import "MetalShaders.hpp"

#import <Metal/Metal.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace threepp::metal {

    namespace {

        void validateShaderKey(const ShaderProgramKey& key) {
            if (key.useInstancing && key.useSkinning) {
                throw std::runtime_error("Metal shader variant cannot enable instancing and skinning together");
            }
            if (key.useInstanceColor && !key.useInstancing) {
                throw std::runtime_error("Metal shader variant cannot enable instance colors without instancing");
            }
        }

        void validateDepthShaderKey(const DepthShaderKey& key) {
            if (key.useInstancing && key.useSkinning) {
                throw std::runtime_error("Metal depth shader variant cannot enable instancing and skinning together");
            }
        }

        std::string buildShaderSource(const ShaderProgramKey& key) {
            validateShaderKey(key);

            std::string source;
            source += "#define USE_MAP ";
            source += key.useMap ? "1\n" : "0\n";
            source += "#define USE_VERTEX_COLORS ";
            source += key.useVertexColors ? "1\n" : "0\n";
            source += "#define USE_NORMAL ";
            source += key.useNormal ? "1\n" : "0\n";
            source += "#define USE_SKINNING ";
            source += key.useSkinning ? "1\n" : "0\n";
            source += "#define USE_LIGHTS ";
            source += key.useLights ? "1\n" : "0\n";
            source += "#define USE_INSTANCING ";
            source += key.useInstancing ? "1\n" : "0\n";
            source += "#define USE_INSTANCE_COLOR ";
            source += key.useInstanceColor ? "1\n" : "0\n";
            source += basic_vertex;
            source += basic_fragment;
            return source;
        }

        std::string buildDepthShaderSource(const DepthShaderKey& key) {
            validateDepthShaderKey(key);

            std::string source;
            source += "#define USE_SKINNING ";
            source += key.useSkinning ? "1\n" : "0\n";
            source += "#define USE_INSTANCING ";
            source += key.useInstancing ? "1\n" : "0\n";
            source += depth_vertex;
            return source;
        }

    }// namespace

    struct MetalShaderManager::Impl {

        struct ShaderProgramInstance {
            id<MTLLibrary> library = nil;
            id<MTLFunction> vertexFunction = nil;
            id<MTLFunction> fragmentFunction = nil;
        };

        id<MTLDevice> device;
        std::unordered_map<ShaderProgramKey, ShaderProgramInstance, ShaderProgramKeyHash> programs;
        std::unordered_map<DepthShaderKey, id<MTLLibrary>, DepthShaderKeyHash> depthLibraries;
        std::unordered_map<DepthShaderKey, id<MTLFunction>, DepthShaderKeyHash> depthVertexFunctions;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        ShaderProgramInstance& getOrCreateProgram(const ShaderProgramKey& key) {
            validateShaderKey(key);

            auto it = programs.find(key);
            if (it != programs.end()) {
                return it->second;
            }

            const auto sourceText = buildShaderSource(key);
            NSString* source = [NSString stringWithUTF8String:sourceText.c_str()];

            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                std::cerr << "=== MSL Compilation Failed ===\n"
                          << sourceText
                          << "\n==============================\n";
                NSString* msg = [NSString stringWithFormat:@"MSL compilation failed: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            ShaderProgramInstance instance;
            instance.library = library;
            instance.vertexFunction = [library newFunctionWithName:@"basic_vertex"];
            instance.fragmentFunction = [library newFunctionWithName:@"basic_fragment"];
            if (!instance.vertexFunction || !instance.fragmentFunction) {
                throw std::runtime_error("Failed to find MSL shader functions");
            }

            auto [inserted, _] = programs.emplace(key, instance);
            return inserted->second;
        }

        id<MTLFunction> getOrCreateDepthVertexFunction(const DepthShaderKey& key) {
            validateDepthShaderKey(key);

            auto functionIt = depthVertexFunctions.find(key);
            if (functionIt != depthVertexFunctions.end()) {
                return functionIt->second;
            }

            const auto sourceText = buildDepthShaderSource(key);
            NSString* source = [NSString stringWithUTF8String:sourceText.c_str()];

            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                std::cerr << "=== MSL Depth Compilation Failed ===\n"
                          << sourceText
                          << "\n====================================\n";
                NSString* msg = [NSString stringWithFormat:@"MSL depth compilation failed: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            id<MTLFunction> function = [library newFunctionWithName:@"depth_vertex"];
            if (!function) {
                throw std::runtime_error("Failed to find MSL depth vertex function");
            }

            depthLibraries.emplace(key, library);
            depthVertexFunctions.emplace(key, function);
            return function;
        }
    };

    MetalShaderManager::MetalShaderManager(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device)) {}

    MetalShaderManager::~MetalShaderManager() = default;

    void* MetalShaderManager::getOrCreateVertexFunction(const ShaderProgramKey& key) {
        return (__bridge void*) pimpl_->getOrCreateProgram(key).vertexFunction;
    }

    void* MetalShaderManager::getOrCreateFragmentFunction(const ShaderProgramKey& key) {
        return (__bridge void*) pimpl_->getOrCreateProgram(key).fragmentFunction;
    }

    void* MetalShaderManager::getOrCreateDepthVertexFunction(bool useSkinning, bool useInstancing) {
        return (__bridge void*) pimpl_->getOrCreateDepthVertexFunction(DepthShaderKey{useSkinning, useInstancing});
    }

}// namespace threepp::metal
