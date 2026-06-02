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
            if (key.doubleSided && key.flipSided) {
                throw std::runtime_error("Metal shader variant cannot enable double-sided and flip-sided normals together");
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
            source += "#define USE_DOUBLE_SIDED ";
            source += key.doubleSided ? "1\n" : "0\n";
            source += "#define USE_FLIP_SIDED ";
            source += key.flipSided ? "1\n" : "0\n";
            source += fog_functions;
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

        std::string buildLineShaderSource(bool useVertexColors) {
            std::string source;
            source += "#define USE_VERTEX_COLORS ";
            source += useVertexColors ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += line_vertex;
            source += line_fragment;
            return source;
        }

        std::string buildPointsShaderSource(bool useVertexColors) {
            std::string source;
            source += "#define USE_VERTEX_COLORS ";
            source += useVertexColors ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += fog_functions;
            source += points_vertex;
            source += points_fragment;
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
        std::unordered_map<DepthShaderKey, id<MTLLibrary>, DepthShaderKeyHash> pointDepthLibraries;
        std::unordered_map<DepthShaderKey, ShaderProgramInstance, DepthShaderKeyHash> pointDepthPrograms;
        std::unordered_map<std::string, id<MTLLibrary>> builtInLibraries;
        std::unordered_map<std::string, id<MTLFunction>> builtInFunctions;

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

        ShaderProgramInstance& getOrCreatePointDepthProgram(const DepthShaderKey& key) {
            validateDepthShaderKey(key);

            auto it = pointDepthPrograms.find(key);
            if (it != pointDepthPrograms.end()) {
                return it->second;
            }

            std::string sourceText;
            sourceText += "#define USE_SKINNING ";
            sourceText += key.useSkinning ? "1\n" : "0\n";
            sourceText += "#define USE_INSTANCING ";
            sourceText += key.useInstancing ? "1\n" : "0\n";
            sourceText += point_depth_vertex;
            sourceText += point_depth_fragment;

            NSString* source = [NSString stringWithUTF8String:sourceText.c_str()];

            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                std::cerr << "=== MSL Point Depth Compilation Failed ===\n"
                          << sourceText
                          << "\n==========================================\n";
                NSString* msg = [NSString stringWithFormat:@"MSL point depth compilation failed: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            ShaderProgramInstance instance;
            instance.library = library;
            instance.vertexFunction = [library newFunctionWithName:@"point_depth_vertex"];
            instance.fragmentFunction = [library newFunctionWithName:@"point_depth_fragment"];
            if (!instance.vertexFunction || !instance.fragmentFunction) {
                throw std::runtime_error("Failed to find MSL point depth shader functions");
            }

            pointDepthLibraries.emplace(key, library);
            auto [inserted, _] = pointDepthPrograms.emplace(key, instance);
            return inserted->second;
        }

        id<MTLFunction> getOrCreateBuiltInFunction(const std::string& cacheKey, const char* sourceText, const char* functionName) {
            auto functionIt = builtInFunctions.find(cacheKey);
            if (functionIt != builtInFunctions.end()) {
                return functionIt->second;
            }

            NSString* source = [NSString stringWithUTF8String:sourceText];
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                std::cerr << "=== MSL Built-in Compilation Failed (" << cacheKey << ") ===\n"
                          << sourceText
                          << "\n================================================\n";
                NSString* msg = [NSString stringWithFormat:@"MSL built-in compilation failed: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:functionName]];
            if (!function) {
                throw std::runtime_error("Failed to find MSL built-in shader function: " + cacheKey);
            }

            builtInLibraries.emplace(cacheKey, library);
            builtInFunctions.emplace(cacheKey, function);
            return function;
        }

        id<MTLFunction> getOrCreateBuiltInFunction(const std::string& cacheKey, const std::string& sourceText, const char* functionName) {
            auto functionIt = builtInFunctions.find(cacheKey);
            if (functionIt != builtInFunctions.end()) {
                return functionIt->second;
            }

            NSString* source = [NSString stringWithUTF8String:sourceText.c_str()];
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                std::cerr << "=== MSL Built-in Compilation Failed (" << cacheKey << ") ===\n"
                          << sourceText
                          << "\n================================================\n";
                NSString* msg = [NSString stringWithFormat:@"MSL built-in compilation failed: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:functionName]];
            if (!function) {
                throw std::runtime_error("Failed to find MSL built-in shader function: " + cacheKey);
            }

            builtInLibraries.emplace(cacheKey, library);
            builtInFunctions.emplace(cacheKey, function);
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

    void* MetalShaderManager::getOrCreatePointDepthVertexFunction(bool useSkinning, bool useInstancing) {
        return (__bridge void*) pimpl_->getOrCreatePointDepthProgram(DepthShaderKey{useSkinning, useInstancing}).vertexFunction;
    }

    void* MetalShaderManager::getOrCreatePointDepthFragmentFunction(bool useSkinning, bool useInstancing) {
        return (__bridge void*) pimpl_->getOrCreatePointDepthProgram(DepthShaderKey{useSkinning, useInstancing}).fragmentFunction;
    }

    void* MetalShaderManager::getOrCreateSpriteVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("sprite_vertex", sprite_vertex, "sprite_vertex");
    }

    void* MetalShaderManager::getOrCreateSpriteFragmentFunction() {
        std::string source;
        source += tone_mapping_functions;
        source += sprite_fragment;
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("sprite_fragment", source, "sprite_fragment");
    }

    void* MetalShaderManager::getOrCreateLineVertexFunction(bool useVertexColors) {
        const auto cacheKey = useVertexColors ? "line_vertex_color" : "line_vertex";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildLineShaderSource(useVertexColors), "line_vertex");
    }

    void* MetalShaderManager::getOrCreateLineFragmentFunction(bool useVertexColors) {
        const auto cacheKey = useVertexColors ? "line_fragment_color" : "line_fragment";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildLineShaderSource(useVertexColors), "line_fragment");
    }

    void* MetalShaderManager::getOrCreatePointsVertexFunction(bool useVertexColors) {
        const auto cacheKey = useVertexColors ? "points_vertex_color" : "points_vertex";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildPointsShaderSource(useVertexColors), "points_vertex");
    }

    void* MetalShaderManager::getOrCreatePointsFragmentFunction(bool useVertexColors) {
        const auto cacheKey = useVertexColors ? "points_fragment_color" : "points_fragment";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildPointsShaderSource(useVertexColors), "points_fragment");
    }

    void* MetalShaderManager::getOrCreateRawShaderVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("raw_shader_vertex", raw_shader_vertex, "raw_shader_vertex");
    }

    void* MetalShaderManager::getOrCreateRawShaderFragmentFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("raw_shader_fragment", raw_shader_fragment, "raw_shader_fragment");
    }

    void* MetalShaderManager::getOrCreateSkyVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("sky_vertex", sky_vertex, "sky_vertex");
    }

    void* MetalShaderManager::getOrCreateSkyFragmentFunction() {
        std::string source;
        source += tone_mapping_functions;
        source += sky_fragment;
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("sky_fragment", source, "sky_fragment");
    }

    void* MetalShaderManager::getOrCreateBackgroundCubeVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("background_cube_vertex", background_cube_vertex, "background_cube_vertex");
    }

    void* MetalShaderManager::getOrCreateBackgroundCubeFragmentFunction() {
        std::string source;
        source += tone_mapping_functions;
        source += background_cube_fragment;
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("background_cube_fragment", source, "background_cube_fragment");
    }

    void* MetalShaderManager::getOrCreateWaterVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("water_vertex", water_vertex, "water_vertex");
    }

    void* MetalShaderManager::getOrCreateWaterFragmentFunction() {
        std::string source;
        source += fog_functions;
        source += water_fragment;
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("water_fragment", source, "water_fragment");
    }

    void* MetalShaderManager::getOrCreateReflectorVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("reflector_vertex", reflector_vertex, "reflector_vertex");
    }

    void* MetalShaderManager::getOrCreateReflectorFragmentFunction() {
        std::string source;
        source += tone_mapping_functions;
        source += reflector_fragment;
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("reflector_fragment", source, "reflector_fragment");
    }

}// namespace threepp::metal
