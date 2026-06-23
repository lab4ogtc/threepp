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
            if (key.useMorphNormals && !key.useMorphTargets) {
                throw std::runtime_error("Metal shader variant cannot enable morph normals without morph targets");
            }
            if (key.rectAreaLightCount > 0 && !key.useLights) {
                throw std::runtime_error("Metal shader variant cannot enable RectAreaLights without lights");
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
            source += "#define USE_FLAT_SHADING ";
            source += key.flatShading ? "1\n" : "0\n";
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
            source += "#define USE_CLIPPING ";
            source += key.useClipping ? "1\n" : "0\n";
            source += "#define USE_MORPHTARGETS ";
            source += key.useMorphTargets ? "1\n" : "0\n";
            source += "#define USE_MORPHNORMALS ";
            source += key.useMorphNormals ? "1\n" : "0\n";
            source += "#define USE_TRANSMISSION ";
            source += key.useTransmission ? "1\n" : "0\n";
            source += "#define USE_RECT_AREA_LIGHTS ";
            source += key.rectAreaLightCount > 0 ? "1\n" : "0\n";
            source += "#define RECT_AREA_LIGHT_COUNT ";
            source += std::to_string(key.rectAreaLightCount);
            source += "\n";
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
            source += "#define USE_CLIPPING ";
            source += key.useClipping ? "1\n" : "0\n";
            source += "#define USE_MORPHTARGETS ";
            source += key.useMorphTargets ? "1\n" : "0\n";
            source += depth_vertex;
            if (key.useClipping) {
                source += depth_fragment;
            }
            return source;
        }

        std::string buildLineShaderSource(bool useVertexColors) {
            std::string source;
            source += "#define USE_VERTEX_COLORS ";
            source += useVertexColors ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += fog_functions;
            source += line_vertex;
            source += line_fragment;
            return source;
        }

        std::string buildPointsShaderSource(bool useVertexColors, bool useMorphTargets, bool useMap, bool useAlphaMap) {
            std::string source;
            source += "#define USE_VERTEX_COLORS ";
            source += useVertexColors ? "1\n" : "0\n";
            source += "#define USE_MORPHTARGETS ";
            source += useMorphTargets ? "1\n" : "0\n";
            source += "#define USE_POINT_MAP ";
            source += useMap ? "1\n" : "0\n";
            source += "#define USE_POINT_ALPHAMAP ";
            source += useAlphaMap ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += fog_functions;
            source += points_vertex;
            source += points_fragment;
            return source;
        }

        std::string buildParticleShaderSource(bool useMap) {
            std::string source;
            source += "#define USE_MAP ";
            source += useMap ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += particle_system_vertex;
            source += particle_system_fragment;
            return source;
        }

        std::string buildParticlePointShaderSource(bool useMap) {
            std::string source;
            source += "#define USE_MAP ";
            source += useMap ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += particle_points_vertex;
            source += particle_points_fragment;
            return source;
        }

        std::string buildSpriteShaderSource(const SpriteShaderKey& key) {
            std::string source;
            source += "#define USE_SIZEATTENUATION ";
            source += key.useSizeAttenuation ? "1\n" : "0\n";
            source += "#define USE_ALPHAMAP ";
            source += key.useAlphaMap ? "1\n" : "0\n";
            source += "#define USE_ALPHATEST ";
            source += key.useAlphaTest ? "1\n" : "0\n";
            source += "#define USE_FOG ";
            source += key.useFog ? "1\n" : "0\n";
            source += tone_mapping_functions;
            source += fog_functions;
            source += sprite_vertex;
            source += sprite_fragment;
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
        std::unordered_map<DepthShaderKey, id<MTLFunction>, DepthShaderKeyHash> depthFragmentFunctions;
        std::unordered_map<DepthShaderKey, id<MTLLibrary>, DepthShaderKeyHash> pointDepthLibraries;
        std::unordered_map<DepthShaderKey, ShaderProgramInstance, DepthShaderKeyHash> pointDepthPrograms;
        std::unordered_map<SpriteShaderKey, ShaderProgramInstance, SpriteShaderKeyHash> spritePrograms;
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

        id<MTLFunction> getOrCreateDepthFragmentFunction(const DepthShaderKey& key) {
            validateDepthShaderKey(key);
            if (!key.useClipping) return nil;

            auto functionIt = depthFragmentFunctions.find(key);
            if (functionIt != depthFragmentFunctions.end()) {
                return functionIt->second;
            }

            getOrCreateDepthVertexFunction(key);
            auto libraryIt = depthLibraries.find(key);
            if (libraryIt == depthLibraries.end()) {
                throw std::runtime_error("Failed to find MSL depth library for clipping fragment");
            }

            id<MTLFunction> function = [libraryIt->second newFunctionWithName:@"depth_fragment"];
            if (!function) {
                throw std::runtime_error("Failed to find MSL depth fragment function");
            }

            depthFragmentFunctions.emplace(key, function);
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
            sourceText += "#define USE_CLIPPING ";
            sourceText += key.useClipping ? "1\n" : "0\n";
            sourceText += "#define USE_MORPHTARGETS ";
            sourceText += key.useMorphTargets ? "1\n" : "0\n";
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

        ShaderProgramInstance& getOrCreateSpriteProgram(const SpriteShaderKey& key) {
            auto it = spritePrograms.find(key);
            if (it != spritePrograms.end()) {
                return it->second;
            }

            const auto sourceText = buildSpriteShaderSource(key);
            NSString* source = [NSString stringWithUTF8String:sourceText.c_str()];

            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                std::cerr << "=== MSL Sprite Compilation Failed ===\n"
                          << sourceText
                          << "\n=====================================\n";
                NSString* msg = [NSString stringWithFormat:@"MSL sprite compilation failed: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            ShaderProgramInstance instance;
            instance.library = library;
            instance.vertexFunction = [library newFunctionWithName:@"sprite_vertex"];
            instance.fragmentFunction = [library newFunctionWithName:@"sprite_fragment"];
            if (!instance.vertexFunction || !instance.fragmentFunction) {
                throw std::runtime_error("Failed to find MSL sprite shader functions");
            }

            auto [inserted, _] = spritePrograms.emplace(key, instance);
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

    void* MetalShaderManager::getOrCreateDepthVertexFunction(const DepthShaderKey& key) {
        return (__bridge void*) pimpl_->getOrCreateDepthVertexFunction(key);
    }

    void* MetalShaderManager::getOrCreateDepthFragmentFunction(const DepthShaderKey& key) {
        return (__bridge void*) pimpl_->getOrCreateDepthFragmentFunction(key);
    }

    void* MetalShaderManager::getOrCreatePointDepthVertexFunction(const DepthShaderKey& key) {
        return (__bridge void*) pimpl_->getOrCreatePointDepthProgram(key).vertexFunction;
    }

    void* MetalShaderManager::getOrCreatePointDepthFragmentFunction(const DepthShaderKey& key) {
        return (__bridge void*) pimpl_->getOrCreatePointDepthProgram(key).fragmentFunction;
    }

    void* MetalShaderManager::getOrCreateSpriteVertexFunction(const SpriteShaderKey& key) {
        return (__bridge void*) pimpl_->getOrCreateSpriteProgram(key).vertexFunction;
    }

    void* MetalShaderManager::getOrCreateSpriteFragmentFunction(const SpriteShaderKey& key) {
        return (__bridge void*) pimpl_->getOrCreateSpriteProgram(key).fragmentFunction;
    }

    void* MetalShaderManager::getOrCreateLineVertexFunction(bool useVertexColors) {
        const auto cacheKey = useVertexColors ? "line_vertex_color" : "line_vertex";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildLineShaderSource(useVertexColors), "line_vertex");
    }

    void* MetalShaderManager::getOrCreateLineFragmentFunction(bool useVertexColors) {
        const auto cacheKey = useVertexColors ? "line_fragment_color" : "line_fragment";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildLineShaderSource(useVertexColors), "line_fragment");
    }

    void* MetalShaderManager::getOrCreatePointsVertexFunction(bool useVertexColors, bool useMorphTargets) {
        std::string cacheKey = useVertexColors ? "points_vertex_color" : "points_vertex";
        if (useMorphTargets) {
            cacheKey += "_morph";
        }
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildPointsShaderSource(useVertexColors, useMorphTargets, false, false), "points_vertex");
    }

    void* MetalShaderManager::getOrCreatePointsFragmentFunction(bool useVertexColors, bool useMap, bool useAlphaMap) {
        std::string cacheKey = useVertexColors ? "points_fragment_color" : "points_fragment";
        if (useMap) {
            cacheKey += "_map";
        }
        if (useAlphaMap) {
            cacheKey += "_alphamap";
        }
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildPointsShaderSource(useVertexColors, false, useMap, useAlphaMap), "points_fragment");
    }

    void* MetalShaderManager::getOrCreateParticleVertexFunction(bool useMap) {
        const auto cacheKey = useMap ? "particle_system_vertex_map" : "particle_system_vertex";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildParticleShaderSource(useMap), "particle_system_vertex");
    }

    void* MetalShaderManager::getOrCreateParticleFragmentFunction(bool useMap) {
        const auto cacheKey = useMap ? "particle_system_fragment_map" : "particle_system_fragment";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildParticleShaderSource(useMap), "particle_system_fragment");
    }

    void* MetalShaderManager::getOrCreateParticlePointVertexFunction(bool useMap) {
        const auto cacheKey = useMap ? "particle_points_vertex_map" : "particle_points_vertex";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildParticlePointShaderSource(useMap), "particle_system_vertex");
    }

    void* MetalShaderManager::getOrCreateParticlePointFragmentFunction(bool useMap) {
        const auto cacheKey = useMap ? "particle_points_fragment_map" : "particle_points_fragment";
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction(cacheKey, buildParticlePointShaderSource(useMap), "particle_system_fragment");
    }

    void* MetalShaderManager::getOrCreateRawShaderVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("raw_shader_vertex", raw_shader_vertex, "raw_shader_vertex");
    }

    void* MetalShaderManager::getOrCreateRawShaderFragmentFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("raw_shader_fragment", raw_shader_fragment, "raw_shader_fragment");
    }

    void* MetalShaderManager::getOrCreateDepthTextureVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("depth_texture_vertex", depth_texture_vertex, "depth_texture_vertex");
    }

    void* MetalShaderManager::getOrCreateDepthTextureFragmentFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("depth_texture_fragment", depth_texture_fragment, "depth_texture_fragment");
    }

    void* MetalShaderManager::getOrCreateDepthTextureLinearReadbackFragmentFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("depth_linear_readback_fragment", depth_linear_readback_fragment, "depth_linear_readback_fragment");
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

    void* MetalShaderManager::getOrCreateBackgroundEquirectFragmentFunction() {
        std::string source;
        source += tone_mapping_functions;
        source += background_equirect_fragment;
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("background_equirect_fragment", source, "background_equirect_fragment");
    }

    void* MetalShaderManager::getOrCreateEquirectToCubeVertexFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("equirect_to_cube_vertex", equirect_to_cube_vertex, "equirect_to_cube_vertex");
    }

    void* MetalShaderManager::getOrCreateEquirectToCubeFragmentFunction() {
        return (__bridge void*) pimpl_->getOrCreateBuiltInFunction("equirect_to_cube_fragment", equirect_to_cube_fragment, "equirect_to_cube_fragment");
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
