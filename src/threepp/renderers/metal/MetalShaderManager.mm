#import "MetalShaderManager.hpp"

#import "MetalShaders.hpp"

#import <Metal/Metal.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace threepp::metal {

    namespace {

        std::string buildShaderSource(const ShaderProgramKey& key) {
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
            source += basic_vertex;
            source += basic_fragment;
            return source;
        }

        std::string buildDepthShaderSource(bool useSkinning) {
            std::string source;
            source += "#define USE_SKINNING ";
            source += useSkinning ? "1\n" : "0\n";
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
        std::unordered_map<bool, id<MTLLibrary>> depthLibraries;
        std::unordered_map<bool, id<MTLFunction>> depthVertexFunctions;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        ShaderProgramInstance& getOrCreateProgram(const ShaderProgramKey& key) {
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

        id<MTLFunction> getOrCreateDepthVertexFunction(bool useSkinning) {
            auto functionIt = depthVertexFunctions.find(useSkinning);
            if (functionIt != depthVertexFunctions.end()) {
                return functionIt->second;
            }

            const auto sourceText = buildDepthShaderSource(useSkinning);
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

            depthLibraries.emplace(useSkinning, library);
            depthVertexFunctions.emplace(useSkinning, function);
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

    void* MetalShaderManager::getOrCreateDepthVertexFunction(bool useSkinning) {
        return (__bridge void*) pimpl_->getOrCreateDepthVertexFunction(useSkinning);
    }

}// namespace threepp::metal
