#import "threepp/renderers/metal/MetalRenderer.hpp"

#import "MetalBufferManager.hpp"
#import "MetalCameraUtils.hpp"
#import "MetalPipelineCache.hpp"
#import "MetalRenderList.hpp"
#import "MetalRenderStateUtils.hpp"
#import "MetalShaderManager.hpp"
#import "MetalTextureManager.hpp"

#import "threepp/cameras/Camera.hpp"
#import "threepp/cameras/PerspectiveCamera.hpp"
#import "threepp/canvas/GlfwWindow.hpp"
#import "threepp/canvas/Window.hpp"
#import "threepp/core/BufferAttribute.hpp"
#import "threepp/core/BufferGeometry.hpp"
#import "threepp/core/EventDispatcher.hpp"
#import "threepp/lights/AmbientLight.hpp"
#import "threepp/lights/DirectionalLight.hpp"
#import "threepp/lights/HemisphereLight.hpp"
#import "threepp/lights/LightProbe.hpp"
#import "threepp/lights/PointLight.hpp"
#import "threepp/lights/PointLightShadow.hpp"
#import "threepp/lights/SpotLight.hpp"
#import "threepp/materials/LineBasicMaterial.hpp"
#import "threepp/materials/Material.hpp"
#import "threepp/materials/MeshBasicMaterial.hpp"
#import "threepp/materials/MeshLambertMaterial.hpp"
#import "threepp/materials/MeshNormalMaterial.hpp"
#import "threepp/materials/MeshPhongMaterial.hpp"
#import "threepp/materials/MeshStandardMaterial.hpp"
#import "threepp/materials/PointsMaterial.hpp"
#import "threepp/materials/RawShaderMaterial.hpp"
#import "threepp/materials/ShaderMaterial.hpp"
#import "threepp/materials/SpriteMaterial.hpp"
#import "threepp/materials/interfaces.hpp"
#import "threepp/math/Matrix3.hpp"
#import "threepp/math/Matrix4.hpp"
#import "threepp/objects/InstancedMesh.hpp"
#import "threepp/objects/LOD.hpp"
#import "threepp/objects/Line.hpp"
#import "threepp/objects/LineLoop.hpp"
#import "threepp/objects/LineSegments.hpp"
#import "threepp/objects/Mesh.hpp"
#import "threepp/objects/Points.hpp"
#import "threepp/objects/Reflector.hpp"
#import "threepp/objects/Sky.hpp"
#import "threepp/objects/SkinnedMesh.hpp"
#import "threepp/objects/Sprite.hpp"
#import "threepp/objects/Water.hpp"
#import "threepp/renderers/RenderJob.hpp"
#import "threepp/renderers/RenderTarget.hpp"
#import "threepp/scenes/Scene.hpp"
#import "threepp/textures/CubeTexture.hpp"
#import "threepp/textures/Texture.hpp"

#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace threepp;

namespace {

    constexpr std::size_t maxDirectionalLights = 4;
    constexpr std::size_t maxPointLights = 4;
    constexpr std::size_t maxSpotLights = 4;
    constexpr std::size_t maxHemisphereLights = 4;
    constexpr std::size_t maxShadowMapsPerLightType = 4;

    constexpr std::uint8_t vertexLayoutPosition = 1u << 0u;
    constexpr std::uint8_t vertexLayoutNormal = 1u << 1u;
    constexpr std::uint8_t vertexLayoutUv = 1u << 2u;
    constexpr std::uint8_t vertexLayoutColor = 1u << 3u;
    constexpr std::uint8_t vertexLayoutTangent = 1u << 4u;
    constexpr std::uint8_t vertexLayoutSkinning = 1u << 5u;
    constexpr std::uint8_t vertexLayoutColor4 = 1u << 6u;

    int requestedAntialiasingSamples(Window& window) {
        if (auto* glfwWindow = dynamic_cast<GlfwWindow*>(&window)) {
            return glfwWindow->antialiasing();
        }
        return 1;
    }

    NSUInteger selectSupportedSampleCount(id<MTLDevice> device, int requestedSamples) {
        if (requestedSamples <= 1) return 1;

        const std::array<NSUInteger, 4> candidates{
                static_cast<NSUInteger>(requestedSamples),
                8u,
                4u,
                2u};
        for (auto sampleCount : candidates) {
            if (sampleCount > 1u && sampleCount <= static_cast<NSUInteger>(requestedSamples) && [device supportsTextureSampleCount:sampleCount]) {
                return sampleCount;
            }
        }

        return 1;
    }

    struct alignas(16) TransformUniforms {
        float mvp[16];
        float modelMatrix[16];
        float modelViewMatrix[16];
        float normalMatrix[16];
        float bindMatrix[16];
        float bindMatrixInverse[16];
    };

    struct alignas(16) DepthTransformUniforms {
        float shadowMatrix[16];
        float bindMatrix[16];
        float bindMatrixInverse[16];
    };

    struct alignas(16) PointDepthTransformUniforms {
        float shadowMatrix[16];
        float modelMatrix[16];
        float bindMatrix[16];
        float bindMatrixInverse[16];
        float lightPosition[4];
        float params[4];
    };

    struct alignas(16) ShadingParams {
        float baseColor[4];
        float emissiveColor[4];
        float pbrParams[4];
        std::uint32_t textureFlags0[4];
        std::uint32_t textureFlags1[4];
        float cameraPosition[4];
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        std::uint32_t materialType;
        float specularColor[4];
        float fogColor[4];
        float fogParams[4];
    };

    struct alignas(16) SpriteUniforms {
        float projectionMatrix[16];
        float modelViewMatrix[16];
        float modelMatrix[16];
        float color[4];
        float center[2];
        float rotation;
        float scaleAttenuation;
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding;
    };

    struct alignas(16) LineUniforms {
        float mvp[16];
        float color[4];
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding;
    };

    struct alignas(16) PointUniforms {
        float mvp[16];
        float color[4];
        float pointSize;
        float scale;
        std::uint32_t sizeAttenuation;
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding[2];
    };

    struct alignas(16) RawShaderUniforms {
        float mvp[16];
        float time;
        float padding[3];
    };

    struct alignas(16) SkyUniforms {
        float mvp[16];
        float modelMatrix[16];
        float sunPosition[4];
        float up[4];
        float params[4];
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding;
    };

    struct alignas(16) WaterUniforms {
        float mvp[16];
        float modelMatrix[16];
        float modelViewMatrix[16];
        float textureMatrix[16];
        float sunDirection[4];
        float sunColor[4];
        float eye[4];
        float waterColor[4];
        float params[4];
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding;
        float fogColor[4];
        float fogParams[4];
    };

    struct alignas(16) ReflectorUniforms {
        float mvp[16];
        float modelMatrix[16];
        float textureMatrix[16];
        float color[4];
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding;
    };

    struct alignas(16) DirectionalLightUniform {
        float direction[4];
        float color[4];
        float shadowParams[4];
        float shadowMapSize[4];
        float shadowMatrix[16];
    };

    struct alignas(16) PointLightUniform {
        float position[4];
        float color[4];
        float params[4];
        float shadowParams[4];
        float shadowMapSize[4];
    };

    struct alignas(16) SpotLightUniform {
        float position[4];
        float direction[4];
        float color[4];
        float params[4];
        float shadowParams[4];
        float shadowMapSize[4];
        float shadowMatrix[16];
    };

    struct alignas(16) HemisphereLightUniform {
        float direction[4];
        float skyColor[4];
        float groundColor[4];
    };

    struct alignas(16) LightUniforms {
        float ambientColor[4];
        std::uint32_t counts[4];
        DirectionalLightUniform directionalLights[maxDirectionalLights];
        PointLightUniform pointLights[maxPointLights];
        SpotLightUniform spotLights[maxSpotLights];
        HemisphereLightUniform hemiLights[maxHemisphereLights];
        float shCoefficients[9][4];
    };

    struct SceneLightSet {
        Color ambient{0x000000};
        std::vector<DirectionalLight*> directional;
        std::vector<PointLight*> point;
        std::vector<SpotLight*> spot;
        std::vector<HemisphereLight*> hemisphere;
        std::vector<LightProbe*> probes;
    };

    struct ShadowResources {
        std::unordered_map<unsigned int, std::uint32_t> directionalShadowIndices;
        std::unordered_map<unsigned int, std::uint32_t> pointShadowIndices;
        std::unordered_map<unsigned int, std::uint32_t> spotShadowIndices;
        std::array<id<MTLTexture>, maxShadowMapsPerLightType> directionalTextures{};
        std::array<id<MTLTexture>, maxShadowMapsPerLightType> pointTextures{};
        std::array<id<MTLTexture>, maxShadowMapsPerLightType> spotTextures{};
    };

    void copyMatrix(const Matrix4& source, float* target) {
        std::copy(source.elements.begin(), source.elements.end(), target);
    }

    void copyIdentityMatrix(float* target) {
        std::fill(target, target + 16, 0.f);
        target[0] = 1.f;
        target[5] = 1.f;
        target[10] = 1.f;
        target[15] = 1.f;
    }

    void computeMVP(const Camera& camera, const Object3D& object, Matrix4& out, bool includeObjectMatrix = true) {
        out.copy(metal::convertProjectionToMetalClipSpace(camera.projectionMatrix));
        out.multiply(camera.matrixWorldInverse);
        if (includeObjectMatrix) {
            out.multiply(*object.matrixWorld);
        }
    }

    void computeTransformUniforms(const Camera& camera, const Object3D& object, TransformUniforms& out, bool isInstanced = false) {
        Matrix4 mvp;
        computeMVP(camera, object, mvp, !isInstanced);
        copyMatrix(mvp, out.mvp);
        copyMatrix(*object.matrixWorld, out.modelMatrix);

        Matrix4 modelViewMatrix;
        modelViewMatrix.multiplyMatrices(camera.matrixWorldInverse, *object.matrixWorld);
        copyMatrix(modelViewMatrix, out.modelViewMatrix);

        Matrix3 normalMatrix;
        normalMatrix.getNormalMatrix(*object.matrixWorld);
        copyIdentityMatrix(out.normalMatrix);
        out.normalMatrix[0] = normalMatrix.elements[0];
        out.normalMatrix[1] = normalMatrix.elements[1];
        out.normalMatrix[2] = normalMatrix.elements[2];
        out.normalMatrix[4] = normalMatrix.elements[3];
        out.normalMatrix[5] = normalMatrix.elements[4];
        out.normalMatrix[6] = normalMatrix.elements[5];
        out.normalMatrix[8] = normalMatrix.elements[6];
        out.normalMatrix[9] = normalMatrix.elements[7];
        out.normalMatrix[10] = normalMatrix.elements[8];

        if (const auto* skinnedMesh = dynamic_cast<const SkinnedMesh*>(&object)) {
            copyMatrix(skinnedMesh->bindMatrix, out.bindMatrix);
            copyMatrix(skinnedMesh->bindMatrixInverse, out.bindMatrixInverse);
        } else {
            copyIdentityMatrix(out.bindMatrix);
            copyIdentityMatrix(out.bindMatrixInverse);
        }
    }

    void computeSpriteUniforms(const Camera& camera, const Sprite& sprite, const SpriteMaterial& material, SpriteUniforms& out) {
        const auto projection = metal::convertProjectionToMetalClipSpace(camera.projectionMatrix);
        copyMatrix(projection, out.projectionMatrix);

        Matrix4 modelViewMatrix;
        modelViewMatrix.multiplyMatrices(camera.matrixWorldInverse, *sprite.matrixWorld);
        copyMatrix(modelViewMatrix, out.modelViewMatrix);
        copyMatrix(*sprite.matrixWorld, out.modelMatrix);

        out.color[0] = material.color.r;
        out.color[1] = material.color.g;
        out.color[2] = material.color.b;
        out.color[3] = material.opacity;
        out.center[0] = sprite.center.x;
        out.center[1] = sprite.center.y;
        out.rotation = material.rotation;
        out.scaleAttenuation = material.sizeAttenuation ? 1.f : 0.f;
    }

    void computeLineUniforms(const Camera& camera, const Line& line, const LineBasicMaterial& material, LineUniforms& out) {
        Matrix4 mvp;
        computeMVP(camera, line, mvp);
        copyMatrix(mvp, out.mvp);
        out.color[0] = material.color.r;
        out.color[1] = material.color.g;
        out.color[2] = material.color.b;
        out.color[3] = material.opacity;
    }

    void computePointUniforms(const Camera& camera, const Points& points, const PointsMaterial& material, float scale, bool sizeAttenuation, PointUniforms& out) {
        Matrix4 mvp;
        computeMVP(camera, points, mvp);
        copyMatrix(mvp, out.mvp);
        out.color[0] = material.color.r;
        out.color[1] = material.color.g;
        out.color[2] = material.color.b;
        out.color[3] = material.opacity;
        out.pointSize = material.size;
        out.scale = scale;
        out.sizeAttenuation = sizeAttenuation ? 1u : 0u;
    }

    void computeRawShaderUniforms(const Camera& camera, const Mesh& mesh, float time, RawShaderUniforms& out) {
        Matrix4 mvp;
        computeMVP(camera, mesh, mvp);
        copyMatrix(mvp, out.mvp);
        out.time = time;
        out.padding[0] = 0.f;
        out.padding[1] = 0.f;
        out.padding[2] = 0.f;
    }

    void computeShadowMVP(const Camera& shadowCamera, const Object3D& object, Matrix4& out) {
        out.copy(metal::convertProjectionToMetalClipSpace(shadowCamera.projectionMatrix));
        out.multiply(shadowCamera.matrixWorldInverse);
        out.multiply(*object.matrixWorld);
    }

    void computeDepthTransformUniforms(const Camera& shadowCamera, const Object3D& object, DepthTransformUniforms& out) {
        Matrix4 shadowMVP;
        computeShadowMVP(shadowCamera, object, shadowMVP);
        copyMatrix(shadowMVP, out.shadowMatrix);

        if (const auto* skinnedMesh = dynamic_cast<const SkinnedMesh*>(&object)) {
            copyMatrix(skinnedMesh->bindMatrix, out.bindMatrix);
            copyMatrix(skinnedMesh->bindMatrixInverse, out.bindMatrixInverse);
        } else {
            copyIdentityMatrix(out.bindMatrix);
            copyIdentityMatrix(out.bindMatrixInverse);
        }
    }

    void computePointDepthTransformUniforms(const Camera& shadowCamera, const Object3D& object, const Vector3& lightPosition, float nearPlane, float farPlane, PointDepthTransformUniforms& out) {
        Matrix4 shadowMVP;
        computeShadowMVP(shadowCamera, object, shadowMVP);
        copyMatrix(shadowMVP, out.shadowMatrix);
        copyMatrix(*object.matrixWorld, out.modelMatrix);

        if (const auto* skinnedMesh = dynamic_cast<const SkinnedMesh*>(&object)) {
            copyMatrix(skinnedMesh->bindMatrix, out.bindMatrix);
            copyMatrix(skinnedMesh->bindMatrixInverse, out.bindMatrixInverse);
        } else {
            copyIdentityMatrix(out.bindMatrix);
            copyIdentityMatrix(out.bindMatrixInverse);
        }

        out.lightPosition[0] = lightPosition.x;
        out.lightPosition[1] = lightPosition.y;
        out.lightPosition[2] = lightPosition.z;
        out.lightPosition[3] = 1.f;
        out.params[0] = nearPlane;
        out.params[1] = farPlane;
    }

    void collectRenderables(Object3D& object, std::vector<Object3D*>& out) {
        if (!object.visible) return;

        if (dynamic_cast<Mesh*>(&object) ||
            dynamic_cast<Line*>(&object) ||
            dynamic_cast<Points*>(&object) ||
            dynamic_cast<Sprite*>(&object)) {
            out.push_back(&object);
        }

        for (const auto& child : object.children) {
            collectRenderables(*child, out);
        }
    }

    Material* materialForRenderOrder(Object3D& object) {
        auto material = object.material();
        return material ? material.get() : nullptr;
    }

    template<typename Callback>
    void forEachMaterialGroup(ObjectWithMaterials& object, BufferGeometry& geometry, Callback&& callback) {
        const auto& materials = object.materials();
        if (materials.empty()) return;

        if (materials.size() > 1) {
            for (const auto& group : geometry.groups) {
                if (group.materialIndex >= materials.size()) continue;

                auto* material = materials[group.materialIndex].get();
                if (!material) continue;

                callback(*material, std::optional<GeometryGroup>{group});
            }
            return;
        }

        if (auto* material = materials.front().get()) {
            callback(*material, std::nullopt);
        }
    }

    void buildRenderList(const std::vector<Object3D*>& renderables, const Camera& camera, metal::MetalRenderList& renderList) {
        Matrix4 projScreenMatrix;
        projScreenMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);

        Vector3 projectedPosition;
        for (auto* object : renderables) {
            projectedPosition
                    .setFromMatrixPosition(*object->matrixWorld)
                    .applyMatrix4(projScreenMatrix);

            if (auto* withMaterials = object->as<ObjectWithMaterials>()) {
                const auto geometry = object->geometry();
                if (geometry && withMaterials->materials().size() > 1) {
                    forEachMaterialGroup(*withMaterials, *geometry, [&](Material& material, std::optional<GeometryGroup> group) {
                        if (material.visible) {
                            renderList.push(*object, material, projectedPosition.z, group);
                        }
                    });
                    continue;
                }
            }

            auto* material = materialForRenderOrder(*object);
            if (!material || !material->visible) continue;
            renderList.push(*object, *material, projectedPosition.z, std::nullopt);
        }

        renderList.sort();
    }

    void updateLODs(Object3D& object, Camera& camera) {
        if (!object.visible) return;

        if (auto* lod = dynamic_cast<LOD*>(&object)) {
            if (lod->autoUpdate) {
                lod->update(camera);
            }
        }

        for (const auto& child : object.children) {
            updateLODs(*child, camera);
        }
    }

    void collectPreRenderables(Object3D& object, Camera& camera, const Frustum& frustum, Renderer& renderer) {
        if (!object.visible) return;

        if (object.layers.test(camera.layers)) {
            if (auto* preRenderable = dynamic_cast<PreRenderable*>(&object)) {
                if (!object.frustumCulled || frustum.intersectsObject(object)) {
                    if (auto job = preRenderable->getPreRenderJob(camera)) {
                        renderer.addPreRenderJob(*job);
                    }
                }
            }
        }

        for (const auto& child : object.children) {
            collectPreRenderables(*child, camera, frustum, renderer);
        }
    }

    FloatBufferAttribute* getFloatAttribute(BufferGeometry& geo, const std::string& name) {
        auto* attr = geo.getAttribute(name);
        if (!attr) return nullptr;
        return attr->typed<float>();
    }

    bool isSupportedSkinIndexAttribute(BufferAttribute* attr) {
        if (!attr || attr->itemSize() != 4 || attr->count() == 0) return false;

        return attr->typed<float>() ||
               attr->typed<unsigned int>() ||
               attr->typed<int>() ||
               attr->typed<std::uint16_t>() ||
               attr->typed<std::int16_t>() ||
               attr->typed<std::uint8_t>() ||
               attr->typed<std::int8_t>();
    }

    bool hasSkinningAttributes(BufferGeometry& geometry) {
        auto* skinIndex = geometry.getAttribute("skinIndex");
        auto* skinWeight = getFloatAttribute(geometry, "skinWeight");
        return isSupportedSkinIndexAttribute(skinIndex) &&
               skinWeight &&
               skinWeight->itemSize() == 4 &&
               skinWeight->count() == skinIndex->count();
    }

    bool hasTexture(const std::shared_ptr<Texture>& texture) {
        return texture != nullptr && !texture->images().empty();
    }

    bool hasTexture(const Texture* texture) {
        return texture != nullptr && !texture->images().empty();
    }

    bool hasCubeTexture(const std::shared_ptr<Texture>& texture) {
        return hasTexture(texture) && dynamic_cast<CubeTexture*>(texture.get()) != nullptr;
    }

    float clamp01(float value) {
        return std::clamp(value, 0.f, 1.f);
    }

    void collectLights(Object3D& object, SceneLightSet& lights) {
        if (!object.visible) return;

        if (auto* ambient = dynamic_cast<AmbientLight*>(&object)) {
            Color contribution = ambient->color;
            contribution.multiplyScalar(ambient->intensity);
            lights.ambient.add(contribution);
        } else if (auto* directional = dynamic_cast<DirectionalLight*>(&object)) {
            lights.directional.push_back(directional);
        } else if (auto* point = dynamic_cast<PointLight*>(&object)) {
            lights.point.push_back(point);
        } else if (auto* spot = dynamic_cast<SpotLight*>(&object)) {
            lights.spot.push_back(spot);
        } else if (auto* hemi = dynamic_cast<HemisphereLight*>(&object)) {
            lights.hemisphere.push_back(hemi);
        } else if (auto* probe = dynamic_cast<LightProbe*>(&object)) {
            lights.probes.push_back(probe);
        }

        for (const auto& child : object.children) {
            collectLights(*child, lights);
        }
    }

    void copyColorWithIntensity(const Color& color, float intensity, float* target) {
        target[0] = color.r * intensity;
        target[1] = color.g * intensity;
        target[2] = color.b * intensity;
        target[3] = 1.f;
    }

    void copyVector3(const Vector3& vector, float* target, float w = 0.f) {
        target[0] = vector.x;
        target[1] = vector.y;
        target[2] = vector.z;
        target[3] = w;
    }

    UniformValue* findUniformValue(UniformMap& uniforms, const std::string& name) {
        auto it = uniforms.find(name);
        if (it == uniforms.end() || !it->second.hasValue()) return nullptr;
        return &it->second.value();
    }

    float uniformFloat(UniformMap& uniforms, const std::string& name, float fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<float>(value)) return *v;
        if (const auto* v = std::get_if<int>(value)) return static_cast<float>(*v);
        return fallback;
    }

    Vector3 uniformVector3(UniformMap& uniforms, const std::string& name, const Vector3& fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<Vector3>(value)) return *v;
        if (const auto* v = std::get_if<Vector3*>(value); v && *v) return **v;
        return fallback;
    }

    Color uniformColor(UniformMap& uniforms, const std::string& name, const Color& fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<Color>(value)) return *v;
        return fallback;
    }

    Matrix4 uniformMatrix4(UniformMap& uniforms, const std::string& name, const Matrix4& fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<Matrix4>(value)) return *v;
        if (const auto* v = std::get_if<Matrix4*>(value); v && *v) return **v;
        return fallback;
    }

    Texture* uniformTexture(UniformMap& uniforms, const std::string& name) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return nullptr;
        if (const auto* v = std::get_if<Texture*>(value)) return *v;
        return nullptr;
    }

    void getLightDirection(const LightWithTarget& light, const Object3D& lightObject, Vector3& target) {
        Vector3 lightPosition;
        Vector3 targetPosition;
        lightPosition.setFromMatrixPosition(*lightObject.matrixWorld);
        targetPosition.setFromMatrixPosition(*light.target().matrixWorld);
        target.subVectors(targetPosition, lightPosition).normalize();
    }

    bool isLightingMaterial(const Material& material) {
        return dynamic_cast<const MeshStandardMaterial*>(&material) != nullptr || dynamic_cast<const MeshPhongMaterial*>(&material) != nullptr || dynamic_cast<const MeshLambertMaterial*>(&material) != nullptr;
    }

    template<class Uniforms>
    void fillToneMappingUniforms(const Renderer& renderer, const Material& material, Uniforms& uniforms) {
        uniforms.toneMappingType = static_cast<std::uint32_t>(material.toneMapped ? renderer.toneMapping : ToneMapping::None);
        uniforms.toneMappingExposure = renderer.toneMappingExposure;
        uniforms.toneMapped = material.toneMapped ? 1u : 0u;
    }

    template<class Uniforms>
    void fillFogUniforms(const Scene& scene, const Material& material, Uniforms& uniforms) {
        uniforms.fogColor[0] = 1.f;
        uniforms.fogColor[1] = 1.f;
        uniforms.fogColor[2] = 1.f;
        uniforms.fogColor[3] = 1.f;
        uniforms.fogParams[0] = 1.f;
        uniforms.fogParams[1] = 2000.f;
        uniforms.fogParams[2] = 0.00025f;
        uniforms.fogParams[3] = 0.f;

        if (!material.fog || !scene.fog.has_value()) return;

        if (const auto* fog = std::get_if<Fog>(&*scene.fog)) {
            uniforms.fogColor[0] = fog->color.r;
            uniforms.fogColor[1] = fog->color.g;
            uniforms.fogColor[2] = fog->color.b;
            uniforms.fogParams[0] = fog->nearPlane;
            uniforms.fogParams[1] = fog->farPlane;
            uniforms.fogParams[3] = 1.f;
        } else if (const auto* fog = std::get_if<FogExp2>(&*scene.fog)) {
            uniforms.fogColor[0] = fog->color.r;
            uniforms.fogColor[1] = fog->color.g;
            uniforms.fogColor[2] = fog->color.b;
            uniforms.fogParams[2] = fog->density;
            uniforms.fogParams[3] = 2.f;
        }
    }

    ShadingParams extractShadingParams(const Renderer& renderer, const Scene& scene, Material& material, const Camera& camera, bool receiveShadow) {
        ShadingParams params{};
        params.baseColor[0] = 1.f;
        params.baseColor[1] = 1.f;
        params.baseColor[2] = 1.f;
        params.baseColor[3] = material.opacity;
        params.pbrParams[0] = 1.f;
        params.pbrParams[1] = 0.f;
        params.pbrParams[2] = 1.f;
        params.pbrParams[3] = 1.f;
        params.specularColor[0] = 0.04f;
        params.specularColor[1] = 0.04f;
        params.specularColor[2] = 0.04f;
        params.specularColor[3] = 30.f;

        if (auto* colorMaterial = dynamic_cast<MaterialWithColor*>(&material)) {
            params.baseColor[0] = colorMaterial->color.r;
            params.baseColor[1] = colorMaterial->color.g;
            params.baseColor[2] = colorMaterial->color.b;
        }

        if (auto* standard = dynamic_cast<MeshStandardMaterial*>(&material)) {
            params.pbrParams[0] = clamp01(standard->roughness);
            params.pbrParams[1] = clamp01(standard->metalness);
        } else if (auto* phong = dynamic_cast<MeshPhongMaterial*>(&material)) {
            params.pbrParams[0] = clamp01(1.f - std::min(phong->shininess, 100.f) / 100.f);
            params.pbrParams[1] = 0.f;
        } else if (dynamic_cast<MeshLambertMaterial*>(&material)) {
            params.pbrParams[0] = 1.f;
            params.pbrParams[1] = 0.f;
        }

        if (auto* specular = dynamic_cast<MaterialWithSpecular*>(&material)) {
            params.specularColor[0] = specular->specular.r;
            params.specularColor[1] = specular->specular.g;
            params.specularColor[2] = specular->specular.b;
            params.specularColor[3] = specular->shininess;
        }

        if (auto* roughness = dynamic_cast<MaterialWithRoughness*>(&material)) {
            params.textureFlags0[2] = hasTexture(roughness->roughnessMap) ? 1u : 0u;
        }
        if (auto* metalness = dynamic_cast<MaterialWithMetalness*>(&material)) {
            params.textureFlags0[3] = hasTexture(metalness->metalnessMap) ? 1u : 0u;
        }
        if (auto* map = dynamic_cast<MaterialWithMap*>(&material)) {
            params.textureFlags0[0] = hasTexture(map->map) ? 1u : 0u;
        }
        if (auto* normal = dynamic_cast<MaterialWithNormalMap*>(&material)) {
            params.textureFlags0[1] = hasTexture(normal->normalMap) ? 1u : 0u;
        }
        if (auto* ao = dynamic_cast<MaterialWithAoMap*>(&material)) {
            params.textureFlags1[0] = hasTexture(ao->aoMap) ? 1u : 0u;
            params.pbrParams[2] = ao->aoMapIntensity;
        }
        if (auto* emissive = dynamic_cast<MaterialWithEmissive*>(&material)) {
            params.emissiveColor[0] = emissive->emissive.r;
            params.emissiveColor[1] = emissive->emissive.g;
            params.emissiveColor[2] = emissive->emissive.b;
            params.emissiveColor[3] = emissive->emissiveIntensity;
            params.textureFlags1[1] = hasTexture(emissive->emissiveMap) ? 1u : 0u;
        }
        if (auto* env = dynamic_cast<MaterialWithEnvMap*>(&material)) {
            params.textureFlags1[2] = hasCubeTexture(env->envMap) ? 1u : 0u;
            params.pbrParams[3] = env->envMapIntensity;
        }
        params.textureFlags1[3] = receiveShadow ? 1u : 0u;

        Vector3 cameraPosition;
        cameraPosition.setFromMatrixPosition(*camera.matrixWorld);
        params.cameraPosition[0] = cameraPosition.x;
        params.cameraPosition[1] = cameraPosition.y;
        params.cameraPosition[2] = cameraPosition.z;
        params.cameraPosition[3] = 1.f;
        fillToneMappingUniforms(renderer, material, params);
        fillFogUniforms(scene, material, params);
        if (dynamic_cast<MeshNormalMaterial*>(&material)) {
            params.materialType = 1u;
        } else if (dynamic_cast<MeshPhongMaterial*>(&material)) {
            params.materialType = 2u;
        } else if (dynamic_cast<MeshLambertMaterial*>(&material)) {
            params.materialType = 3u;
        } else {
            params.materialType = 0u;
        }
        return params;
    }

    bool needsUv(const ShadingParams& params) {
        return params.textureFlags0[0] != 0u || params.textureFlags0[1] != 0u || params.textureFlags0[2] != 0u || params.textureFlags0[3] != 0u || params.textureFlags1[0] != 0u || params.textureFlags1[1] != 0u;
    }

    NSUInteger clampToSize(float value, NSUInteger maxValue) {
        const auto rounded = static_cast<long>(std::floor(value));
        return static_cast<NSUInteger>(std::clamp<long>(rounded, 0, static_cast<long>(maxValue)));
    }

    MTLPixelFormat toRenderTargetColorPixelFormat(const Texture& texture) {
        switch (texture.format) {
            case Format::RGB:
            case Format::RGBA:
                return MTLPixelFormatRGBA8Unorm;
            case Format::BGRA:
                return MTLPixelFormatBGRA8Unorm;
            default:
                throw std::runtime_error("Metal RenderTarget currently supports only RGB8, RGBA8, and BGRA8 color textures");
        }
    }

}// namespace

struct MetalRenderer::Impl {

    MetalRenderer& renderer;
    Window& window;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer* metalLayer = nil;
    id<MTLDepthStencilState> depthStencilState = nil;
    id<MTLTexture> depthTexture = nil;
    id<MTLTexture> multisampleColorTexture = nil;
    MTLPixelFormat multisampleColorPixelFormat = MTLPixelFormatInvalid;
    id<CAMetalDrawable> currentDrawable = nil;
    id<MTLCommandBuffer> currentCommandBuffer = nil;
    dispatch_semaphore_t inFlightSemaphore = nullptr;
    MTLPixelFormat depthPixelFormat = MTLPixelFormatDepth32Float;

    std::unique_ptr<metal::MetalPipelineCache> pipelineCache;
    std::unique_ptr<metal::MetalBufferManager> bufferManager;
    std::unique_ptr<metal::MetalShaderManager> shaderManager;
    std::unique_ptr<metal::MetalTextureManager> textureManager;

    MetalShadowMap shadowMapState;
    std::unordered_map<unsigned int, id<MTLTexture>> shadowTextures;
    id<MTLTexture> whiteTexture = nil;
    id<MTLTexture> blackTexture = nil;
    id<MTLTexture> normalTexture = nil;
    id<MTLTexture> whiteCubeTexture = nil;
    id<MTLTexture> whiteDepthTexture = nil;
    id<MTLSamplerState> defaultSampler = nil;
    id<MTLSamplerState> shadowSampler = nil;
    id<MTLBuffer> defaultTangentBuffer = nil;
    std::size_t defaultTangentVertexCount = 0;

    struct ConvertedSkinIndexBuffer {
        unsigned int lastVersion = std::numeric_limits<unsigned int>::max();
        std::vector<float> values;
    };
    std::unordered_map<BufferAttribute*, ConvertedSkinIndexBuffer> convertedSkinIndexBuffers;
    std::unordered_map<BufferGeometry*, bool> geometries;
    std::vector<unsigned int> lineLoopIndices;

    struct MetalRenderTargetResources {
        id<MTLTexture> colorTexture = nil;
        id<MTLTexture> depthTexture = nil;
        NSUInteger width = 0;
        NSUInteger height = 0;
        MTLPixelFormat colorPixelFormat = MTLPixelFormatInvalid;
    };

    struct OnRenderTargetDispose: EventListener {
        explicit OnRenderTargetDispose(Impl& scope)
            : scope(scope) {}

        void onEvent(Event& event) override {
            RenderTarget* target = nullptr;
            if (auto** renderTargetPtr = std::any_cast<RenderTarget*>(&event.target)) {
                target = *renderTargetPtr;
            }
            if (!target) return;

            target->removeEventListener("dispose", *this);
            scope.deallocateRenderTarget(target);
        }

        Impl& scope;
    };

    struct OnGeometryDispose: EventListener {
        explicit OnGeometryDispose(Impl& scope)
            : scope(scope) {}

        void onEvent(Event& event) override {
            auto** geometryPtr = std::any_cast<BufferGeometry*>(&event.target);
            if (!geometryPtr || !*geometryPtr) return;

            auto* geometry = *geometryPtr;
            geometry->removeEventListener("dispose", *this);
            scope.deallocateGeometry(*geometry);
        }

        Impl& scope;
    };

    OnRenderTargetDispose onRenderTargetDispose;
    OnGeometryDispose onGeometryDispose;
    std::unordered_map<RenderTarget*, MetalRenderTargetResources> renderTargetResources;

    Color clearColor{0, 0, 0};
    float clearAlpha = 1;
    bool clearColorFlag = true;
    bool clearDepthFlag = true;
    bool clearRequested = false;
    bool explicitFrameInProgress = false;
    bool currentCommandBufferExternallyAccessed = false;
    bool lastFrameWasExternallyAccessed = false;
    bool renderingPrePass = false;
    std::vector<RenderJob> preRenderJobs;
    std::optional<float> currentDepthBiasFactor;
    std::optional<float> currentDepthBiasUnits;

    int fbWidth = 0;
    int fbHeight = 0;
    float pixelRatio = 1;
    NSUInteger drawableSampleCount = 1;
    NSUInteger activeRenderSampleCount = 1;
    Vector4 viewport;
    Vector4 scissor;
    bool scissorTest = false;
    std::chrono::steady_clock::time_point lastRenderTime{};

    RenderTarget* renderTarget = nullptr;

    explicit Impl(MetalRenderer& r, Window& w)
        : renderer(r),
          window(w),
          onRenderTargetDispose(*this),
          onGeometryDispose(*this) {

        GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
        NSWindow* nsWindow = glfwGetCocoaWindow(glfwWin);
        NSView* contentView = [nsWindow contentView];

        device = MTLCreateSystemDefaultDevice();
        if (!device) {
            throw std::runtime_error("Metal is not supported on this device");
        }
        drawableSampleCount = selectSupportedSampleCount(device, requestedAntialiasingSamples(window));

        commandQueue = [device newCommandQueue];
        inFlightSemaphore = dispatch_semaphore_create(3);

        metalLayer = [CAMetalLayer layer];
        metalLayer.device = device;
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.maximumDrawableCount = 3;
        metalLayer.displaySyncEnabled = YES;
        metalLayer.framebufferOnly = NO;
        metalLayer.frame = contentView.bounds;
        metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        metalLayer.opaque = YES;

        [contentView setWantsLayer:YES];
        [contentView setLayer:metalLayer];

        glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
        updatePixelRatio(window.size());
        metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
        metalLayer.contentsScale = pixelRatio;

        createDepthTexture();

        pipelineCache = std::make_unique<metal::MetalPipelineCache>((__bridge void*) device);
        bufferManager = std::make_unique<metal::MetalBufferManager>((__bridge void*) device);
        shaderManager = std::make_unique<metal::MetalShaderManager>((__bridge void*) device);
        textureManager = std::make_unique<metal::MetalTextureManager>((__bridge void*) device, (__bridge void*) commandQueue);

        depthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState();
        createPlaceholderResources();

        setViewport(0, 0, window.size().width(), window.size().height());
        setScissor(0, 0, window.size().width(), window.size().height());
    }

    ~Impl() {
        commitPendingFrame();
        // 提交空命令缓冲区并等待，借助 Metal FIFO 保证前序 GPU 工作完成后再释放资源。
        id<MTLCommandBuffer> syncBuffer = [commandQueue commandBuffer];
        [syncBuffer commit];
        [syncBuffer waitUntilCompleted];

        for (auto& [target, _] : renderTargetResources) {
            target->removeEventListener("dispose", onRenderTargetDispose);
        }
        for (auto& [geometry, _] : geometries) {
            geometry->removeEventListener("dispose", onGeometryDispose);
        }
    }

    void removeAttribute(BufferAttribute* attribute) {
        if (!attribute) return;

        bufferManager->remove(*attribute);
        convertedSkinIndexBuffers.erase(attribute);
    }

    void deallocateGeometry(BufferGeometry& geometry) {
        removeAttribute(geometry.getIndex());

        for (const auto& [_, attribute] : geometry.getAttributes()) {
            removeAttribute(attribute.get());
        }
        for (const auto& [_, attributes] : geometry.getMorphAttributes()) {
            for (const auto& attribute : attributes) {
                removeAttribute(attribute.get());
            }
        }

        geometries.erase(&geometry);
    }

    void trackGeometry(BufferGeometry& geometry) {
        if (geometries.contains(&geometry)) return;

        geometry.addEventListener("dispose", onGeometryDispose);
        geometries[&geometry] = true;
    }

    void commitPendingFrame() {
        if (!currentCommandBuffer) return;

        if (currentDrawable) {
            [currentCommandBuffer presentDrawable:currentDrawable];
        }
        [currentCommandBuffer commit];
        currentCommandBuffer = nil;
        currentDrawable = nil;
        explicitFrameInProgress = false;
        lastFrameWasExternallyAccessed = currentCommandBufferExternallyAccessed;
        currentCommandBufferExternallyAccessed = false;
    }

    void ensureFrameStarted() {
        if (currentCommandBuffer) return;

        dispatch_semaphore_wait(inFlightSemaphore, DISPATCH_TIME_FOREVER);
        bufferManager->beginFrame();

        currentCommandBuffer = [commandQueue commandBuffer];
        auto semaphore = inFlightSemaphore;
        [currentCommandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> commandBuffer) {
          dispatch_semaphore_signal(semaphore);
        }];
    }

    bool ensureDrawable() {
        if (currentDrawable) return true;

        currentDrawable = [metalLayer nextDrawable];
        return currentDrawable != nil;
    }

    void updatePixelRatio(const WindowSize& size) {
        if (size.width() > 0) {
            pixelRatio = static_cast<float>(fbWidth) / static_cast<float>(size.width());
        } else {
            pixelRatio = 1;
        }
    }

    void createDepthTexture() {
        if (depthTexture) {
            depthTexture = nil;
        }

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depthPixelFormat
                                                                                        width:std::max(fbWidth, 1)
                                                                                       height:std::max(fbHeight, 1)
                                                                                    mipmapped:NO];
        desc.textureType = drawableSampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
        desc.sampleCount = drawableSampleCount;
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        depthTexture = [device newTextureWithDescriptor:desc];
    }

    id<MTLTexture> getOrCreateMultisampleColorTexture(MTLPixelFormat pixelFormat) {
        if (drawableSampleCount <= 1) return nil;
        if (multisampleColorTexture &&
            multisampleColorTexture.width == static_cast<NSUInteger>(std::max(fbWidth, 1)) &&
            multisampleColorTexture.height == static_cast<NSUInteger>(std::max(fbHeight, 1)) &&
            multisampleColorPixelFormat == pixelFormat) {
            return multisampleColorTexture;
        }

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                        width:std::max(fbWidth, 1)
                                                                                       height:std::max(fbHeight, 1)
                                                                                    mipmapped:NO];
        desc.textureType = MTLTextureType2DMultisample;
        desc.sampleCount = drawableSampleCount;
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        multisampleColorTexture = [device newTextureWithDescriptor:desc];
        multisampleColorPixelFormat = pixelFormat;
        return multisampleColorTexture;
    }

    id<MTLTexture> createSolidTexture2D(std::array<unsigned char, 4> rgba) const {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                        width:1
                                                                                       height:1
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                   mipmapLevel:0
                     withBytes:rgba.data()
                   bytesPerRow:4];
        return texture;
    }

    id<MTLTexture> createSolidCubeTexture(std::array<unsigned char, 4> rgba) const {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                           size:1
                                                                                      mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        for (NSUInteger face = 0; face < 6; ++face) {
            [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                       mipmapLevel:0
                             slice:face
                         withBytes:rgba.data()
                       bytesPerRow:4
                     bytesPerImage:4];
        }
        return texture;
    }

    id<MTLTexture> createDepthTexture(NSUInteger width, NSUInteger height) const {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                        width:std::max<NSUInteger>(width, 1)
                                                                                       height:std::max<NSUInteger>(height, 1)
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        return [device newTextureWithDescriptor:desc];
    }

    id<MTLTexture> createRenderTargetColorTexture(RenderTarget& target, MTLPixelFormat pixelFormat) const {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                        width:std::max<NSUInteger>(target.width, 1)
                                                                                       height:std::max<NSUInteger>(target.height, 1)
                                                                                    mipmapped:target.texture->generateMipmaps ? YES : NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        return [device newTextureWithDescriptor:desc];
    }

    id<MTLTexture> createRenderTargetDepthTexture(RenderTarget& target) const {
        if (target.depthTexture &&
            (target.depthTexture->format != Format::Depth || target.depthTexture->type != Type::Float)) {
            throw std::runtime_error("Metal RenderTarget depthTexture requires Format::Depth and Type::Float");
        }
        return createDepthTexture(target.width, target.height);
    }

    MetalRenderTargetResources& getOrCreateRenderTargetResources(RenderTarget& target) {
        if (!target.texture) {
            throw std::runtime_error("Metal RenderTarget requires a color texture");
        }
        if (target.depth != 1) {
            throw std::runtime_error("Metal RenderTarget currently supports only standard 2D targets");
        }

        const auto width = static_cast<NSUInteger>(std::max(target.width, 1u));
        const auto height = static_cast<NSUInteger>(std::max(target.height, 1u));
        const auto colorPixelFormat = toRenderTargetColorPixelFormat(*target.texture);

        auto it = renderTargetResources.find(&target);
        if (it != renderTargetResources.end() &&
            it->second.width == width &&
            it->second.height == height &&
            it->second.colorPixelFormat == colorPixelFormat &&
            it->second.colorTexture &&
            it->second.depthTexture) {
            return it->second;
        }

        auto colorTexture = createRenderTargetColorTexture(target, colorPixelFormat);
        auto depthTexture = createRenderTargetDepthTexture(target);
        if (!colorTexture || !depthTexture) {
            throw std::runtime_error("Failed to create Metal RenderTarget resources");
        }

        target.texture->image().width = target.width;
        target.texture->image().height = target.height;
        target.texture->image().depth = target.depth;
        textureManager->registerExternalTexture(*target.texture, (__bridge void*) colorTexture);

        if (target.depthTexture) {
            target.depthTexture->image().width = target.width;
            target.depthTexture->image().height = target.height;
            target.depthTexture->image().depth = target.depth;
            textureManager->registerExternalTexture(*target.depthTexture, (__bridge void*) depthTexture);
        }

        if (!target.hasEventListener("dispose", onRenderTargetDispose)) {
            target.addEventListener("dispose", onRenderTargetDispose);
        }

        auto& resources = renderTargetResources[&target];
        resources.colorTexture = colorTexture;
        resources.depthTexture = depthTexture;
        resources.width = width;
        resources.height = height;
        resources.colorPixelFormat = colorPixelFormat;
        return resources;
    }

    void deallocateRenderTarget(RenderTarget* target) {
        if (!target) return;

        if (target->texture) {
            textureManager->deallocateTexture(target->texture.get());
        }
        if (target->depthTexture) {
            textureManager->deallocateTexture(target->depthTexture.get());
        }
        renderTargetResources.erase(target);
    }

    void clearDepthTextureToOne(id<MTLTexture> texture) const {
        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.depthAttachment.texture = texture;
        passDesc.depthAttachment.loadAction = MTLLoadActionClear;
        passDesc.depthAttachment.clearDepth = 1.0;
        passDesc.depthAttachment.storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
    }

    void createPlaceholderResources() {
        whiteTexture = createSolidTexture2D({255, 255, 255, 255});
        blackTexture = createSolidTexture2D({0, 0, 0, 255});
        normalTexture = createSolidTexture2D({128, 128, 255, 255});
        whiteCubeTexture = createSolidCubeTexture({255, 255, 255, 255});
        whiteDepthTexture = createDepthTexture(1, 1);
        clearDepthTextureToOne(whiteDepthTexture);

        MTLSamplerDescriptor* defaultSamplerDesc = [[MTLSamplerDescriptor alloc] init];
        defaultSamplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
        defaultSamplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
        defaultSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
        defaultSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
        defaultSampler = [device newSamplerStateWithDescriptor:defaultSamplerDesc];

        MTLSamplerDescriptor* shadowSamplerDesc = [[MTLSamplerDescriptor alloc] init];
        shadowSamplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        shadowSamplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        shadowSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
        shadowSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
        shadowSamplerDesc.compareFunction = MTLCompareFunctionLessEqual;
        shadowSampler = [device newSamplerStateWithDescriptor:shadowSamplerDesc];
    }

    id<MTLTexture> getOrCreateShadowTexture(Light& light, LightShadow& shadow) {
        const auto width = static_cast<NSUInteger>(std::max(1.f, std::ceil(shadow.mapSize.x)));
        const auto height = static_cast<NSUInteger>(std::max(1.f, std::ceil(shadow.mapSize.y)));
        auto it = shadowTextures.find(light.id);
        if (it != shadowTextures.end() && it->second.width == width && it->second.height == height) {
            return it->second;
        }

        id<MTLTexture> texture = createDepthTexture(width, height);
        shadowTextures[light.id] = texture;
        return texture;
    }

    id<MTLTexture> getOrCreatePointShadowTexture(PointLight& light, PointLightShadow& shadow) {
        const auto frameExtents = shadow.getFrameExtents();
        const auto width = static_cast<NSUInteger>(std::max(1.f, std::ceil(shadow.mapSize.x * frameExtents.x)));
        const auto height = static_cast<NSUInteger>(std::max(1.f, std::ceil(shadow.mapSize.y * frameExtents.y)));
        auto it = shadowTextures.find(light.id);
        if (it != shadowTextures.end() && it->second.width == width && it->second.height == height) {
            return it->second;
        }

        id<MTLTexture> texture = createDepthTexture(width, height);
        shadowTextures[light.id] = texture;
        return texture;
    }

    id<MTLBuffer> getDefaultTangentBuffer(std::size_t vertexCount) {
        if (defaultTangentBuffer && defaultTangentVertexCount >= vertexCount) {
            return defaultTangentBuffer;
        }

        std::vector<float> tangents(vertexCount * 4, 0.f);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            tangents[i * 4] = 1.f;
            tangents[i * 4 + 3] = 1.f;
        }

        defaultTangentBuffer = [device newBufferWithBytes:tangents.data()
                                                   length:tangents.size() * sizeof(float)
                                                  options:MTLResourceStorageModeShared];
        defaultTangentVertexCount = vertexCount;
        return defaultTangentBuffer;
    }

    void setSize(std::pair<int, int> size) {
        commitPendingFrame();

        GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
        glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
        updatePixelRatio(WindowSize{size});
        metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
        metalLayer.contentsScale = pixelRatio;
        multisampleColorTexture = nil;
        multisampleColorPixelFormat = MTLPixelFormatInvalid;
        createDepthTexture();
        setViewport(0, 0, size.first, size.second);
        setScissor(0, 0, size.first, size.second);
    }

    void setClearColor(const Color& color, float alpha) {
        clearColor.copy(color);
        clearAlpha = alpha;
    }

    void clear(bool color, bool depth, bool /*stencil*/) {
        if (currentCommandBuffer && color) {
            commitPendingFrame();
        }

        clearColorFlag = color;
        clearDepthFlag = depth;
        clearRequested = true;
        explicitFrameInProgress = true;
    }

    std::vector<unsigned char> readRGBPixels() {
        if (!currentCommandBuffer || !currentDrawable) {
            throw std::runtime_error("MetalRenderer::readRGBPixels requires an uncommitted frame; set autoClear=false, clear, render, then read");
        }

        id<MTLTexture> sourceTexture = currentDrawable.texture;
        const auto width = static_cast<NSUInteger>(sourceTexture.width);
        const auto height = static_cast<NSUInteger>(sourceTexture.height);
        constexpr NSUInteger bytesPerPixel = 4;
        const auto bytesPerRow = ((width * bytesPerPixel) + 255u) & ~255u;
        const auto byteLength = bytesPerRow * height;

        id<MTLBuffer> readbackBuffer = [device newBufferWithLength:byteLength options:MTLResourceStorageModeShared];
        if (!readbackBuffer) {
            throw std::runtime_error("Failed to allocate Metal readback buffer");
        }

        id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
        [blitEncoder copyFromTexture:sourceTexture
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(width, height, 1)
                                toBuffer:readbackBuffer
                       destinationOffset:0
                  destinationBytesPerRow:bytesPerRow
                destinationBytesPerImage:byteLength];
        [blitEncoder endEncoding];

        [currentCommandBuffer presentDrawable:currentDrawable];
        [currentCommandBuffer commit];
        [currentCommandBuffer waitUntilCompleted];

        const auto* bgra = static_cast<const unsigned char*>([readbackBuffer contents]);
        std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
        for (NSUInteger y = 0; y < height; ++y) {
            const auto* srcRow = bgra + y * bytesPerRow;
            auto* dstRow = rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3u;
            for (NSUInteger x = 0; x < width; ++x) {
                dstRow[x * 3u + 0u] = srcRow[x * bytesPerPixel + 2u];
                dstRow[x * 3u + 1u] = srcRow[x * bytesPerPixel + 1u];
                dstRow[x * 3u + 2u] = srcRow[x * bytesPerPixel + 0u];
            }
        }

        currentCommandBuffer = nil;
        currentDrawable = nil;
        explicitFrameInProgress = false;
        return rgb;
    }

    void setViewport(int x, int y, int width, int height) {
        viewport.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    }

    void setScissor(int x, int y, int width, int height) {
        scissor.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    }

    void applyViewport(id<MTLRenderCommandEncoder> encoder) const {
        const MTLViewport mtlViewport{
                viewport.x * pixelRatio,
                viewport.y * pixelRatio,
                viewport.z * pixelRatio,
                viewport.w * pixelRatio,
                0.0,
                1.0};
        [encoder setViewport:mtlViewport];
    }

    void applyScissor(id<MTLRenderCommandEncoder> encoder) const {
        if (!scissorTest) return;

        const auto maxWidth = static_cast<NSUInteger>(std::max(fbWidth, 0));
        const auto maxHeight = static_cast<NSUInteger>(std::max(fbHeight, 0));
        const auto x = clampToSize(scissor.x * pixelRatio, maxWidth);
        const auto y = clampToSize(scissor.y * pixelRatio, maxHeight);
        const auto maxX = clampToSize((scissor.x + scissor.z) * pixelRatio, maxWidth);
        const auto maxY = clampToSize((scissor.y + scissor.w) * pixelRatio, maxHeight);

        const MTLScissorRect rect{x, y, maxX > x ? maxX - x : 0, maxY > y ? maxY - y : 0};
        [encoder setScissorRect:rect];
    }

    void resetDepthBiasCache() {
        currentDepthBiasFactor.reset();
        currentDepthBiasUnits.reset();
    }

    void applyDepthBias(id<MTLRenderCommandEncoder> encoder, const Material& material) {
        const auto factor = material.polygonOffset ? material.polygonOffsetFactor : 0.f;
        const auto units = material.polygonOffset ? material.polygonOffsetUnits : 0.f;

        if (currentDepthBiasFactor && currentDepthBiasUnits &&
            *currentDepthBiasFactor == factor &&
            *currentDepthBiasUnits == units) {
            return;
        }

        [encoder setDepthBias:units slopeScale:factor clamp:0.f];
        currentDepthBiasFactor = factor;
        currentDepthBiasUnits = units;
    }

    void bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder = false) {
        id<MTLTexture> metalTexture = placeholder;
        id<MTLSamplerState> sampler = defaultSampler;
        if (texture) {
            id<MTLTexture> tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, allowPlaceholder);
            if (tex) {
                metalTexture = tex;
                sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
            }
        }
        [encoder setFragmentTexture:metalTexture atIndex:index];
        if (index == 0) {
            [encoder setFragmentSamplerState:sampler atIndex:0];
        }
    }

    void bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, Texture* texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder = false) {
        id<MTLTexture> metalTexture = placeholder;
        id<MTLSamplerState> sampler = defaultSampler;
        if (texture) {
            id<MTLTexture> tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, allowPlaceholder);
            if (tex) {
                metalTexture = tex;
                sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
            }
        }
        [encoder setFragmentTexture:metalTexture atIndex:index];
        if (index == 0) {
            [encoder setFragmentSamplerState:sampler atIndex:0];
        }
    }

    id<MTLSamplerState> samplerForTexture(Texture* texture) {
        if (!texture) return defaultSampler;
        return (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
    }

    void bindCubeTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, NSUInteger index) {
        id<MTLTexture> metalTexture = whiteCubeTexture;
        if (texture && dynamic_cast<CubeTexture*>(texture.get()) != nullptr) {
            metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture);
        }
        [encoder setFragmentTexture:metalTexture atIndex:index];
    }

    void bindPassLightResources(id<MTLRenderCommandEncoder> encoder, const LightUniforms& lightUniforms, const ShadowResources& shadowResources) {
        [encoder setFragmentBytes:&lightUniforms length:sizeof(lightUniforms) atIndex:1];
        for (std::size_t i = 0; i < maxShadowMapsPerLightType; ++i) {
            id<MTLTexture> directionalTexture = shadowResources.directionalTextures[i] ? shadowResources.directionalTextures[i] : whiteDepthTexture;
            id<MTLTexture> spotTexture = shadowResources.spotTextures[i] ? shadowResources.spotTextures[i] : whiteDepthTexture;
            id<MTLTexture> pointTexture = shadowResources.pointTextures[i] ? shadowResources.pointTextures[i] : whiteDepthTexture;
            [encoder setFragmentTexture:directionalTexture atIndex:7 + i];
            [encoder setFragmentTexture:spotTexture atIndex:11 + i];
            [encoder setFragmentTexture:pointTexture atIndex:15 + i];
        }
        [encoder setFragmentSamplerState:shadowSampler atIndex:1];
    }

    template<class T>
    id<MTLBuffer> getConvertedSkinIndexBuffer(TypedBufferAttribute<T>& attribute) {
        const auto& source = attribute.array();
        if (source.empty()) return nil;

        auto& cache = convertedSkinIndexBuffers[&attribute];
        if (cache.lastVersion != attribute.version || cache.values.size() != source.size()) {
            cache.values.resize(source.size());
            std::transform(source.begin(), source.end(), cache.values.begin(), [](auto value) {
                return static_cast<float>(value);
            });
            cache.lastVersion = attribute.version;
        }

        return (__bridge id<MTLBuffer>) bufferManager->getDynamicBuffer(
                &cache,
                cache.values.size() * sizeof(float),
                cache.values.data());
    }

    id<MTLBuffer> getSkinIndexBuffer(BufferAttribute& attribute) {
        if (auto* floatSkinIndex = attribute.typed<float>()) {
            return (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *floatSkinIndex,
                    floatSkinIndex->count() * floatSkinIndex->itemSize() * sizeof(float),
                    floatSkinIndex->array().data());
        }
        if (auto* skinIndex = attribute.typed<unsigned int>()) return getConvertedSkinIndexBuffer(*skinIndex);
        if (auto* skinIndex = attribute.typed<int>()) return getConvertedSkinIndexBuffer(*skinIndex);
        if (auto* skinIndex = attribute.typed<std::uint16_t>()) return getConvertedSkinIndexBuffer(*skinIndex);
        if (auto* skinIndex = attribute.typed<std::int16_t>()) return getConvertedSkinIndexBuffer(*skinIndex);
        if (auto* skinIndex = attribute.typed<std::uint8_t>()) return getConvertedSkinIndexBuffer(*skinIndex);
        if (auto* skinIndex = attribute.typed<std::int8_t>()) return getConvertedSkinIndexBuffer(*skinIndex);
        return nil;
    }

    bool bindSkinning(id<MTLRenderCommandEncoder> encoder, BufferGeometry& geometry, SkinnedMesh* skinnedMesh) {
        if (!skinnedMesh || !skinnedMesh->skeleton) return false;

        auto* skinIndex = geometry.getAttribute("skinIndex");
        auto* skinWeight = getFloatAttribute(geometry, "skinWeight");
        if (!isSupportedSkinIndexAttribute(skinIndex) || !skinWeight || skinWeight->itemSize() != 4 || skinWeight->count() != skinIndex->count()) return false;

        auto* skinIndexBuffer = getSkinIndexBuffer(*skinIndex);
        if (!skinIndexBuffer) return false;
        auto* skinWeightBuffer = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                *skinWeight,
                skinWeight->count() * skinWeight->itemSize() * sizeof(float),
                skinWeight->array().data());
        [encoder setVertexBuffer:skinIndexBuffer offset:0 atIndex:6];
        [encoder setVertexBuffer:skinWeightBuffer offset:0 atIndex:7];

        auto& skeleton = *skinnedMesh->skeleton;
        skeleton.update();
        const auto byteSize = skeleton.boneMatrices.size() * sizeof(float);
        if (byteSize == 0) return false;

        if (byteSize <= 4096) {
            [encoder setVertexBytes:skeleton.boneMatrices.data() length:byteSize atIndex:5];
        } else {
            id<MTLBuffer> boneBuffer = (__bridge id<MTLBuffer>) bufferManager->getDynamicBuffer(
                    &skeleton,
                    byteSize,
                    skeleton.boneMatrices.data());
            [encoder setVertexBuffer:boneBuffer offset:0 atIndex:5];
        }
        return true;
    }

    void bindDrawAttributes(id<MTLRenderCommandEncoder> encoder,
                            BufferGeometry& geometry,
                            FloatBufferAttribute& position,
                            FloatBufferAttribute* normal,
                            FloatBufferAttribute* uv,
                            FloatBufferAttribute* color,
                            bool useNormal,
                            bool useUv,
                            bool useVertexColors,
                            bool useTangent) {
        auto* posBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                position,
                position.count() * position.itemSize() * sizeof(float),
                position.array().data());
        [encoder setVertexBuffer:posBuf offset:0 atIndex:0];

        if (useNormal && normal) {
            auto* buf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *normal,
                    normal->count() * normal->itemSize() * sizeof(float),
                    normal->array().data());
            [encoder setVertexBuffer:buf offset:0 atIndex:1];
        }

        if (useUv && uv) {
            auto* buf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *uv,
                    uv->count() * uv->itemSize() * sizeof(float),
                    uv->array().data());
            [encoder setVertexBuffer:buf offset:0 atIndex:2];
        }

        if (useVertexColors && color) {
            auto* buf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *color,
                    color->count() * color->itemSize() * sizeof(float),
                    color->array().data());
            [encoder setVertexBuffer:buf offset:0 atIndex:3];
        }

        if (!useTangent) return;

        auto* tangent = getFloatAttribute(geometry, "tangent");
        if (tangent) {
            auto* buf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *tangent,
                    tangent->count() * tangent->itemSize() * sizeof(float),
                    tangent->array().data());
            [encoder setVertexBuffer:buf offset:0 atIndex:8];
        } else {
            [encoder setVertexBuffer:getDefaultTangentBuffer(position.count()) offset:0 atIndex:8];
        }
    }

    void bindInstancing(id<MTLRenderCommandEncoder> encoder, InstancedMesh& instancedMesh, bool useInstanceColor) {
        auto* instanceMatrix = instancedMesh.instanceMatrix();
        if (!instanceMatrix) {
            throw std::runtime_error("InstancedMesh is missing instanceMatrix");
        }

        auto* matrixBuffer = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                *instanceMatrix,
                instanceMatrix->count() * instanceMatrix->itemSize() * sizeof(float),
                instanceMatrix->array().data());
        [encoder setVertexBuffer:matrixBuffer offset:0 atIndex:9];

        if (!useInstanceColor) return;

        auto* instanceColor = instancedMesh.instanceColor();
        if (!instanceColor) return;

        auto* colorBuffer = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                *instanceColor,
                instanceColor->count() * instanceColor->itemSize() * sizeof(float),
                instanceColor->array().data());
        [encoder setVertexBuffer:colorBuffer offset:0 atIndex:10];
    }

    struct DrawSpan {
        NSUInteger start;
        NSUInteger count;
    };

    std::optional<DrawSpan> computeDrawSpan(int dataCount, const DrawRange& drawRange, std::optional<GeometryGroup> group) {
        if (dataCount <= 0) return std::nullopt;

        const auto rangeStart = std::max(0, drawRange.start);
        const auto rangeCount = drawRange.count == std::numeric_limits<int>::max() / 2
                                        ? dataCount
                                        : std::max(0, drawRange.count);
        const auto groupStart = group ? std::max(0, group->start) : 0;
        const auto groupCount = group ? std::max(0, group->count) : dataCount;

        const auto drawStart = std::max(rangeStart, groupStart);
        const auto drawEndExclusive = std::min(dataCount, std::min(rangeStart + rangeCount, groupStart + groupCount));
        const auto drawCount = std::max(0, drawEndExclusive - drawStart);

        if (drawCount == 0) return std::nullopt;

        return DrawSpan{
                static_cast<NSUInteger>(drawStart),
                static_cast<NSUInteger>(drawCount)};
    }

    void drawGeometry(id<MTLRenderCommandEncoder> encoder,
                      BufferGeometry& geometry,
                      FloatBufferAttribute& position,
                      MTLPrimitiveType primitiveType,
                      NSUInteger instanceCount = 1,
                      std::optional<GeometryGroup> group = std::nullopt) {
        if (geometry.hasIndex()) {
            auto* indexAttr = geometry.getIndex();
            const auto drawSpan = computeDrawSpan(indexAttr->count(), geometry.drawRange, group);
            if (!drawSpan) return;

            auto* indexBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *indexAttr,
                    indexAttr->count() * indexAttr->itemSize() * sizeof(unsigned int),
                    indexAttr->array().data());

            [encoder drawIndexedPrimitives:primitiveType
                                indexCount:drawSpan->count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:indexBuf
                         indexBufferOffset:drawSpan->start * sizeof(unsigned int)
                             instanceCount:instanceCount];
        } else {
            const auto drawSpan = computeDrawSpan(position.count(), geometry.drawRange, group);
            if (!drawSpan) return;

            [encoder drawPrimitives:primitiveType
                        vertexStart:drawSpan->start
                        vertexCount:drawSpan->count
                      instanceCount:instanceCount];
        }
    }

    void drawLineLoopGeometry(id<MTLRenderCommandEncoder> encoder,
                              BufferGeometry& geometry,
                              FloatBufferAttribute& position,
                              std::optional<GeometryGroup> group = std::nullopt) {
        lineLoopIndices.clear();

        if (geometry.hasIndex()) {
            auto* indexAttr = geometry.getIndex();
            const auto& source = indexAttr->array();
            const auto drawSpan = computeDrawSpan(indexAttr->count(), geometry.drawRange, group);
            if (!drawSpan) return;

            lineLoopIndices.reserve(static_cast<std::size_t>(drawSpan->count) + 1u);
            for (NSUInteger i = 0; i < drawSpan->count; ++i) {
                lineLoopIndices.push_back(source[static_cast<std::size_t>(drawSpan->start + i)]);
            }
        } else {
            const auto drawSpan = computeDrawSpan(position.count(), geometry.drawRange, group);
            if (!drawSpan) return;

            lineLoopIndices.reserve(static_cast<std::size_t>(drawSpan->count) + 1u);
            for (NSUInteger i = 0; i < drawSpan->count; ++i) {
                lineLoopIndices.push_back(static_cast<unsigned int>(drawSpan->start + i));
            }
        }

        lineLoopIndices.push_back(lineLoopIndices.front());
        id<MTLBuffer> indexBuffer = (__bridge id<MTLBuffer>) bufferManager->getTransientBuffer(
                lineLoopIndices.size() * sizeof(unsigned int),
                lineLoopIndices.data());
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeLineStrip
                            indexCount:static_cast<NSUInteger>(lineLoopIndices.size())
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:indexBuffer
                     indexBufferOffset:0];
    }

    void renderLine(id<MTLRenderCommandEncoder> encoder,
                    Line& line,
                    Material& material,
                    Camera& camera,
                    MTLPixelFormat colorPixelFormat,
                    std::optional<GeometryGroup> group = std::nullopt) {
        auto* lineMaterial = material.as<LineBasicMaterial>();
        auto geometry = line.geometry();
        if (!lineMaterial || !geometry || !lineMaterial->visible) return;
        trackGeometry(*geometry);

        auto* posAttr = getFloatAttribute(*geometry, "position");
        if (!posAttr) return;

        auto* colorAttr = getFloatAttribute(*geometry, "color");
        const bool useVertexColors = lineMaterial->vertexColors && colorAttr && colorAttr->itemSize() == 3;

        static bool linewidthWarningPrinted = false;
        if (lineMaterial->linewidth > 1.f && !linewidthWarningPrinted) {
            std::cerr << "MetalRenderer: LineBasicMaterial linewidth > 1 is not supported by Metal and will be ignored.\n";
            linewidthWarningPrinted = true;
        }

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateLineVertexFunction(useVertexColors);
        pipelineKey.fragmentFunction = shaderManager->getOrCreateLineFragmentFunction(useVertexColors);
        pipelineKey.alphaBlending = lineMaterial->transparent || lineMaterial->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
        if (useVertexColors) pipelineKey.vertexLayoutBitmask |= vertexLayoutColor;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                lineMaterial->depthTest,
                lineMaterial->depthWrite,
                lineMaterial->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *lineMaterial);

        bindDrawAttributes(encoder, *geometry, *posAttr, nullptr, nullptr, colorAttr, false, false, useVertexColors, false);

        LineUniforms uniforms{};
        computeLineUniforms(camera, line, *lineMaterial, uniforms);
        fillToneMappingUniforms(renderer, *lineMaterial, uniforms);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

        if (dynamic_cast<LineLoop*>(&line)) {
            drawLineLoopGeometry(encoder, *geometry, *posAttr, group);
        } else {
            const auto primitiveType = dynamic_cast<LineSegments*>(&line) ? MTLPrimitiveTypeLine : MTLPrimitiveTypeLineStrip;
            drawGeometry(encoder, *geometry, *posAttr, primitiveType, 1, group);
        }
    }

    float pointScale() const {
        const auto viewportHeight = renderTarget ? renderTarget->viewport.w : viewport.w * pixelRatio;
        return std::max(viewportHeight, 1.f) * 0.5f;
    }

    void renderPoints(id<MTLRenderCommandEncoder> encoder,
                      Points& points,
                      Material& material,
                      Camera& camera,
                      MTLPixelFormat colorPixelFormat,
                      std::optional<GeometryGroup> group = std::nullopt) {
        auto* pointsMaterial = material.as<PointsMaterial>();
        auto geometry = points.geometry();
        if (!pointsMaterial || !geometry || !pointsMaterial->visible) return;
        trackGeometry(*geometry);

        auto* posAttr = getFloatAttribute(*geometry, "position");
        if (!posAttr) return;

        auto* colorAttr = getFloatAttribute(*geometry, "color");
        const bool useVertexColors = pointsMaterial->vertexColors && colorAttr && colorAttr->itemSize() == 3;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreatePointsVertexFunction(useVertexColors);
        pipelineKey.fragmentFunction = shaderManager->getOrCreatePointsFragmentFunction(useVertexColors);
        pipelineKey.alphaBlending = pointsMaterial->transparent || pointsMaterial->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
        if (useVertexColors) pipelineKey.vertexLayoutBitmask |= vertexLayoutColor;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                pointsMaterial->depthTest,
                pointsMaterial->depthWrite,
                pointsMaterial->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *pointsMaterial);

        bindDrawAttributes(encoder, *geometry, *posAttr, nullptr, nullptr, colorAttr, false, false, useVertexColors, false);

        PointUniforms uniforms{};
        const bool useSizeAttenuation = pointsMaterial->sizeAttenuation && dynamic_cast<PerspectiveCamera*>(&camera) != nullptr;
        computePointUniforms(camera, points, *pointsMaterial, pointScale(), useSizeAttenuation, uniforms);
        fillToneMappingUniforms(renderer, *pointsMaterial, uniforms);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

        drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypePoint, 1, group);
    }

    void renderRawShader(id<MTLRenderCommandEncoder> encoder,
                         Mesh& mesh,
                         Material& material,
                         Camera& camera,
                         MTLPixelFormat colorPixelFormat,
                         std::optional<GeometryGroup> group = std::nullopt) {
        auto* rawMaterial = material.as<RawShaderMaterial>();
        auto geometry = mesh.geometry();
        if (!rawMaterial || !geometry || !rawMaterial->visible) return;
        trackGeometry(*geometry);

        auto* posAttr = getFloatAttribute(*geometry, "position");
        auto* colorAttr = getFloatAttribute(*geometry, "color");
        if (!posAttr || !colorAttr || colorAttr->itemSize() != 4) return;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateRawShaderVertexFunction();
        pipelineKey.fragmentFunction = shaderManager->getOrCreateRawShaderFragmentFunction();
        pipelineKey.alphaBlending = rawMaterial->transparent || rawMaterial->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutColor4;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                rawMaterial->depthTest,
                rawMaterial->depthWrite,
                rawMaterial->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];

        const auto frontFaceCW = mesh.matrixWorld->determinant() < 0;
        const auto faceCullingState = metal::computeFaceCullingState(rawMaterial->side, frontFaceCW, false);
        [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *rawMaterial);

        bindDrawAttributes(encoder, *geometry, *posAttr, nullptr, nullptr, colorAttr, false, false, true, false);

        RawShaderUniforms uniforms{};
        computeRawShaderUniforms(camera, mesh, uniformFloat(rawMaterial->uniforms, "time", 0.f), uniforms);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

        drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle, 1, group);
    }

    void renderSprite(id<MTLRenderCommandEncoder> encoder, Sprite& sprite, Camera& camera, MTLPixelFormat colorPixelFormat) {
        auto* material = sprite.material()->as<SpriteMaterial>();
        if (!material || !material->visible) return;

        static constexpr float positions[] = {
                -0.5f, -0.5f, 0.f,
                 0.5f, -0.5f, 0.f,
                -0.5f,  0.5f, 0.f,
                 0.5f,  0.5f, 0.f};
        static constexpr float uvs[] = {
                0.f, 0.f,
                1.f, 0.f,
                0.f, 1.f,
                1.f, 1.f};

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateSpriteVertexFunction();
        pipelineKey.fragmentFunction = shaderManager->getOrCreateSpriteFragmentFunction();
        pipelineKey.alphaBlending = material->transparent || material->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutUv;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                material->depthTest,
                material->depthWrite,
                material->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];

        const auto faceCullingState = metal::computeFaceCullingState(material->side, false, false);
        [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *material);

        [encoder setVertexBytes:positions length:sizeof(positions) atIndex:0];
        [encoder setVertexBytes:uvs length:sizeof(uvs) atIndex:2];

        SpriteUniforms uniforms{};
        computeSpriteUniforms(camera, sprite, *material, uniforms);
        fillToneMappingUniforms(renderer, *material, uniforms);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

        bindTextureOrPlaceholder(encoder, material->map, whiteTexture, 0);
        [encoder setFragmentSamplerState:defaultSampler atIndex:0];

        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }

    void renderSky(id<MTLRenderCommandEncoder> encoder, Sky& sky, Camera& camera, MTLPixelFormat colorPixelFormat) {
        auto materialPtr = sky.material();
        auto* material = materialPtr ? materialPtr->as<ShaderMaterial>() : nullptr;
        auto geometry = sky.geometry();
        if (!material || !geometry || !material->visible) return;
        trackGeometry(*geometry);

        auto* posAttr = getFloatAttribute(*geometry, "position");
        if (!posAttr) return;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateSkyVertexFunction();
        pipelineKey.fragmentFunction = shaderManager->getOrCreateSkyFragmentFunction();
        pipelineKey.alphaBlending = material->transparent || material->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                material->depthTest,
                material->depthWrite,
                material->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];

        const auto frontFaceCW = sky.matrixWorld->determinant() < 0;
        const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, false);
        [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *material);

        bindDrawAttributes(encoder, *geometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

        SkyUniforms uniforms{};
        Matrix4 mvp;
        computeMVP(camera, sky, mvp);
        copyMatrix(mvp, uniforms.mvp);
        copyMatrix(*sky.matrixWorld, uniforms.modelMatrix);
        copyVector3(uniformVector3(material->uniforms, "sunPosition", Vector3{}), uniforms.sunPosition);
        copyVector3(uniformVector3(material->uniforms, "up", Vector3{0, 1, 0}), uniforms.up);
        uniforms.params[0] = uniformFloat(material->uniforms, "turbidity", 2.f);
        uniforms.params[1] = uniformFloat(material->uniforms, "rayleigh", 1.f);
        uniforms.params[2] = uniformFloat(material->uniforms, "mieCoefficient", 0.005f);
        uniforms.params[3] = uniformFloat(material->uniforms, "mieDirectionalG", 0.8f);
        fillToneMappingUniforms(renderer, *material, uniforms);

        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle);
    }

    void renderWater(id<MTLRenderCommandEncoder> encoder, Scene& scene, Water& water, Camera& camera, MTLPixelFormat colorPixelFormat) {
        auto materialPtr = water.material();
        auto* material = materialPtr ? materialPtr->as<ShaderMaterial>() : nullptr;
        auto geometry = water.geometry();
        if (!material || !geometry || !material->visible) return;
        trackGeometry(*geometry);

        auto* posAttr = getFloatAttribute(*geometry, "position");
        if (!posAttr) return;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateWaterVertexFunction();
        pipelineKey.fragmentFunction = shaderManager->getOrCreateWaterFragmentFunction();
        pipelineKey.alphaBlending = material->transparent || material->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                material->depthTest,
                material->depthWrite,
                material->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];

        const auto frontFaceCW = water.matrixWorld->determinant() < 0;
        const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, false);
        [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *material);

        bindDrawAttributes(encoder, *geometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

        WaterUniforms uniforms{};
        Matrix4 mvp;
        Matrix4 identity;
        computeMVP(camera, water, mvp);
        copyMatrix(mvp, uniforms.mvp);
        copyMatrix(*water.matrixWorld, uniforms.modelMatrix);
        Matrix4 modelViewMatrix;
        modelViewMatrix.multiplyMatrices(camera.matrixWorldInverse, *water.matrixWorld);
        copyMatrix(modelViewMatrix, uniforms.modelViewMatrix);
        const auto textureMatrix = uniformMatrix4(material->uniforms, "textureMatrix", identity);
        copyMatrix(textureMatrix, uniforms.textureMatrix);
        copyVector3(uniformVector3(material->uniforms, "sunDirection", Vector3{0.70707f, 0.70707f, 0}), uniforms.sunDirection);
        const auto sunColor = uniformColor(material->uniforms, "sunColor", Color{0x7f7f7f});
        uniforms.sunColor[0] = sunColor.r;
        uniforms.sunColor[1] = sunColor.g;
        uniforms.sunColor[2] = sunColor.b;
        uniforms.sunColor[3] = 1.f;
        copyVector3(uniformVector3(material->uniforms, "eye", Vector3{}), uniforms.eye);
        const auto waterColor = uniformColor(material->uniforms, "waterColor", Color{0x555555});
        uniforms.waterColor[0] = waterColor.r;
        uniforms.waterColor[1] = waterColor.g;
        uniforms.waterColor[2] = waterColor.b;
        uniforms.waterColor[3] = 1.f;
        uniforms.params[0] = uniformFloat(material->uniforms, "alpha", material->opacity);
        uniforms.params[1] = uniformFloat(material->uniforms, "time", 0.f);
        uniforms.params[2] = uniformFloat(material->uniforms, "size", 1.f);
        uniforms.params[3] = uniformFloat(material->uniforms, "distortionScale", 20.f);
        fillToneMappingUniforms(renderer, *material, uniforms);
        fillFogUniforms(scene, *material, uniforms);

        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

        auto normalSamplerTexture = uniformTexture(material->uniforms, "normalSampler");
        auto mirrorSamplerTexture = uniformTexture(material->uniforms, "mirrorSampler");

        bindTextureOrPlaceholder(encoder, normalSamplerTexture, normalTexture, 0);
        bindTextureOrPlaceholder(encoder, mirrorSamplerTexture, whiteTexture, 1, true);
        [encoder setFragmentSamplerState:samplerForTexture(normalSamplerTexture) atIndex:0];
        [encoder setFragmentSamplerState:samplerForTexture(mirrorSamplerTexture) atIndex:1];

        drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle);
    }

    void renderReflector(id<MTLRenderCommandEncoder> encoder, Scene&, Reflector& reflector, Camera& camera, MTLPixelFormat colorPixelFormat) {
        auto materialPtr = reflector.material();
        auto* material = materialPtr ? materialPtr->as<ShaderMaterial>() : nullptr;
        auto geometry = reflector.geometry();
        if (!material || !geometry || !material->visible) return;
        trackGeometry(*geometry);

        auto* posAttr = getFloatAttribute(*geometry, "position");
        if (!posAttr) return;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateReflectorVertexFunction();
        pipelineKey.fragmentFunction = shaderManager->getOrCreateReflectorFragmentFunction();
        pipelineKey.alphaBlending = material->transparent || material->opacity < 1.f;
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                material->depthTest,
                material->depthWrite,
                material->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];

        const auto frontFaceCW = reflector.matrixWorld->determinant() < 0;
        const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, false);
        [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
        [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *material);

        bindDrawAttributes(encoder, *geometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

        ReflectorUniforms uniforms{};
        Matrix4 mvp;
        Matrix4 identity;
        computeMVP(camera, reflector, mvp);
        copyMatrix(mvp, uniforms.mvp);
        copyMatrix(*reflector.matrixWorld, uniforms.modelMatrix);

        const auto textureMatrix = uniformMatrix4(material->uniforms, "textureMatrix", identity);
        copyMatrix(textureMatrix, uniforms.textureMatrix);

        const auto color = uniformColor(material->uniforms, "color", Color{0x7f7f7f});
        uniforms.color[0] = color.r;
        uniforms.color[1] = color.g;
        uniforms.color[2] = color.b;
        uniforms.color[3] = 1.f;

        fillToneMappingUniforms(renderer, *material, uniforms);

        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

        auto mirrorSamplerTexture = uniformTexture(material->uniforms, "tDiffuse");
        bindTextureOrPlaceholder(encoder, mirrorSamplerTexture, whiteTexture, 0, true);

        drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle);
    }

    bool shouldUpdateShadow(LightShadow& shadow) const {
        return shadowMapState.autoUpdate || shadowMapState.needsUpdate || shadow.autoUpdate || shadow.needsUpdate;
    }

    void renderDepthObject(id<MTLRenderCommandEncoder> encoder, Object3D& object, Camera& shadowCamera, const Frustum& frustum) {
        if (!object.visible) return;

        if (auto* mesh = dynamic_cast<Mesh*>(&object)) {
            auto* instancedMesh = dynamic_cast<InstancedMesh*>(mesh);
            const bool hasRenderableInstances = !instancedMesh || instancedMesh->count() > 0;

            if (hasRenderableInstances && object.castShadow && (!object.frustumCulled || frustum.intersectsObject(object))) {
                auto* geometry = mesh->geometry().get();
                auto* materials = object.as<ObjectWithMaterials>();
                if (geometry && materials) {
                    trackGeometry(*geometry);
                    auto* posAttr = getFloatAttribute(*geometry, "position");
                    if (posAttr) {
                        const bool useInstancing = instancedMesh && instancedMesh->count() > 0;
                        auto* skinnedMesh = dynamic_cast<SkinnedMesh*>(mesh);
                        const bool useSkinning = bindSkinning(encoder, *geometry, skinnedMesh);
                        if (useInstancing && useSkinning) {
                            std::cerr << "MetalRenderer: skipping unsupported instanced skinned shadow caster " << object.id << "\n";
                        } else {
                            std::uint8_t vertexLayoutBitmask = vertexLayoutPosition;
                            if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;

                            auto* vertexFunction = shaderManager->getOrCreateDepthVertexFunction(useSkinning, useInstancing);
                            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreateDepthOnlyPipelineState(vertexFunction, vertexLayoutBitmask);
                            [encoder setRenderPipelineState:pso];

                            auto* posBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                                    *posAttr,
                                    posAttr->count() * posAttr->itemSize() * sizeof(float),
                                    posAttr->array().data());
                            [encoder setVertexBuffer:posBuf offset:0 atIndex:0];

                            DepthTransformUniforms depthTransforms;
                            computeDepthTransformUniforms(shadowCamera, object, depthTransforms);
                            [encoder setVertexBytes:&depthTransforms length:sizeof(depthTransforms) atIndex:4];

                            NSUInteger instanceCount = 1;
                            if (useInstancing) {
                                bindInstancing(encoder, *instancedMesh, false);
                                instanceCount = static_cast<NSUInteger>(instancedMesh->count());
                            }
                            const auto frontFaceCW = object.matrixWorld->determinant() < 0;
                            forEachMaterialGroup(*materials, *geometry, [&](Material& material, std::optional<GeometryGroup> group) {
                                if (!material.visible) return;

                                const auto* wf = dynamic_cast<MaterialWithWireframe*>(&material);
                                const bool isWireframe = wf && wf->wireframe;
                                const auto faceCullingState = metal::computeShadowFaceCullingState(
                                        material.side,
                                        material.shadowSide,
                                        frontFaceCW,
                                        isWireframe,
                                        shadowMapState.type == ShadowMap::VSM);
                                [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
                                [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
                                [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

                                drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle, instanceCount, group);
                            });
                        }
                    }
                }
            }
        }

        for (const auto& child : object.children) {
            renderDepthObject(encoder, *child, shadowCamera, frustum);
        }
    }

    void renderPointDepthObject(id<MTLRenderCommandEncoder> encoder, Object3D& object, Camera& shadowCamera, const Frustum& frustum, const Vector3& lightPosition, float nearPlane, float farPlane) {
        if (!object.visible) return;

        if (auto* mesh = dynamic_cast<Mesh*>(&object)) {
            auto* instancedMesh = dynamic_cast<InstancedMesh*>(mesh);
            const bool hasRenderableInstances = !instancedMesh || instancedMesh->count() > 0;

            if (hasRenderableInstances && object.castShadow && (!object.frustumCulled || frustum.intersectsObject(object))) {
                auto* geometry = mesh->geometry().get();
                auto* materials = object.as<ObjectWithMaterials>();
                if (geometry && materials) {
                    trackGeometry(*geometry);
                    auto* posAttr = getFloatAttribute(*geometry, "position");
                    if (posAttr) {
                        const bool useInstancing = instancedMesh && instancedMesh->count() > 0;
                        auto* skinnedMesh = dynamic_cast<SkinnedMesh*>(mesh);
                        const bool useSkinning = bindSkinning(encoder, *geometry, skinnedMesh);
                        if (useInstancing && useSkinning) {
                            std::cerr << "MetalRenderer: skipping unsupported instanced skinned point shadow caster " << object.id << "\n";
                        } else {
                            std::uint8_t vertexLayoutBitmask = vertexLayoutPosition;
                            if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;

                            auto* vertexFunction = shaderManager->getOrCreatePointDepthVertexFunction(useSkinning, useInstancing);
                            auto* fragmentFunction = shaderManager->getOrCreatePointDepthFragmentFunction(useSkinning, useInstancing);
                            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreateDepthOnlyPipelineState(vertexFunction, fragmentFunction, vertexLayoutBitmask);
                            [encoder setRenderPipelineState:pso];

                            auto* posBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                                    *posAttr,
                                    posAttr->count() * posAttr->itemSize() * sizeof(float),
                                    posAttr->array().data());
                            [encoder setVertexBuffer:posBuf offset:0 atIndex:0];

                            PointDepthTransformUniforms depthTransforms;
                            computePointDepthTransformUniforms(shadowCamera, object, lightPosition, nearPlane, farPlane, depthTransforms);
                            [encoder setVertexBytes:&depthTransforms length:sizeof(depthTransforms) atIndex:4];
                            [encoder setFragmentBytes:&depthTransforms length:sizeof(depthTransforms) atIndex:4];

                            NSUInteger instanceCount = 1;
                            if (useInstancing) {
                                bindInstancing(encoder, *instancedMesh, false);
                                instanceCount = static_cast<NSUInteger>(instancedMesh->count());
                            }
                            const auto frontFaceCW = object.matrixWorld->determinant() < 0;
                            forEachMaterialGroup(*materials, *geometry, [&](Material& material, std::optional<GeometryGroup> group) {
                                if (!material.visible) return;

                                const auto* wf = dynamic_cast<MaterialWithWireframe*>(&material);
                                const bool isWireframe = wf && wf->wireframe;
                                const auto faceCullingState = metal::computeShadowFaceCullingState(
                                        material.side,
                                        material.shadowSide,
                                        frontFaceCW,
                                        isWireframe,
                                        shadowMapState.type == ShadowMap::VSM);
                                [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
                                [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
                                [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

                                drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle, instanceCount, group);
                            });
                        }
                    }
                }
            }
        }

        for (const auto& child : object.children) {
            renderPointDepthObject(encoder, *child, shadowCamera, frustum, lightPosition, nearPlane, farPlane);
        }
    }

    void renderShadowForLight(Scene& scene, Light& light, LightShadow& shadow, id<MTLTexture> shadowTexture) {
        if (!shouldUpdateShadow(shadow)) return;

        shadow.updateMatrices(light);

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.depthAttachment.texture = shadowTexture;
        passDesc.depthAttachment.loadAction = MTLLoadActionClear;
        passDesc.depthAttachment.clearDepth = 1.0;
        passDesc.depthAttachment.storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc];
        resetDepthBiasCache();
        [encoder setDepthStencilState:depthStencilState];
        renderDepthObject(encoder, scene, *shadow.camera, shadow.getFrustum());
        [encoder endEncoding];

        shadow.needsUpdate = false;
    }

    void renderPointLightShadow(Scene& scene, PointLight& light, PointLightShadow& shadow, id<MTLTexture> shadowTexture) {
        if (!shouldUpdateShadow(shadow)) return;

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.depthAttachment.texture = shadowTexture;
        passDesc.depthAttachment.loadAction = MTLLoadActionClear;
        passDesc.depthAttachment.clearDepth = 1.0;
        passDesc.depthAttachment.storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc];
        resetDepthBiasCache();
        [encoder setDepthStencilState:depthStencilState];

        const auto frameExtents = shadow.getFrameExtents();
        const auto faceWidth = static_cast<double>(std::max<NSUInteger>(shadowTexture.width, 1)) / static_cast<double>(std::max(1.f, frameExtents.x));
        const auto faceHeight = static_cast<double>(std::max<NSUInteger>(shadowTexture.height, 1)) / static_cast<double>(std::max(1.f, frameExtents.y));
        Vector3 lightPosition;
        lightPosition.setFromMatrixPosition(*light.matrixWorld);
        for (std::size_t i = 0; i < shadow.getViewportCount(); ++i) {
            shadow.updateMatrices(light, i);
            const auto& viewport = shadow.getViewport(i);
            const MTLViewport metalViewport{
                    static_cast<double>(viewport.x) * faceWidth,
                    static_cast<double>(frameExtents.y - viewport.y - viewport.w) * faceHeight,
                    static_cast<double>(viewport.z) * faceWidth,
                    static_cast<double>(viewport.w) * faceHeight,
                    0.0,
                    1.0};
            [encoder setViewport:metalViewport];

            const MTLScissorRect metalScissor{
                    static_cast<NSUInteger>(std::floor(metalViewport.originX)),
                    static_cast<NSUInteger>(std::floor(metalViewport.originY)),
                    static_cast<NSUInteger>(std::ceil(metalViewport.width)),
                    static_cast<NSUInteger>(std::ceil(metalViewport.height))};
            [encoder setScissorRect:metalScissor];

            renderPointDepthObject(encoder, scene, *shadow.camera, shadow.getFrustum(), lightPosition, shadow.camera->nearPlane, shadow.camera->farPlane);
        }

        [encoder endEncoding];

        shadow.needsUpdate = false;
    }

    ShadowResources renderShadowPasses(Scene& scene, const SceneLightSet& sceneLights) {
        ShadowResources resources;
        resources.directionalTextures.fill(whiteDepthTexture);
        resources.pointTextures.fill(whiteDepthTexture);
        resources.spotTextures.fill(whiteDepthTexture);

        if (!shadowMapState.enabled) return resources;

        std::uint32_t directionalIndex = 0;
        for (auto* light : sceneLights.directional) {
            if (directionalIndex >= maxShadowMapsPerLightType) break;
            if (!light->castShadow || !light->shadow) continue;

            auto* texture = getOrCreateShadowTexture(*light, *light->shadow);
            resources.directionalShadowIndices[light->id] = directionalIndex;
            resources.directionalTextures[directionalIndex] = texture;
            renderShadowForLight(scene, *light, *light->shadow, texture);
            ++directionalIndex;
        }

        std::uint32_t pointIndex = 0;
        for (auto* light : sceneLights.point) {
            if (pointIndex >= maxShadowMapsPerLightType) break;
            if (!light->castShadow || !light->shadow) continue;

            auto* pointShadow = dynamic_cast<PointLightShadow*>(light->shadow.get());
            if (!pointShadow) continue;

            auto* texture = getOrCreatePointShadowTexture(*light, *pointShadow);
            resources.pointShadowIndices[light->id] = pointIndex;
            resources.pointTextures[pointIndex] = texture;
            renderPointLightShadow(scene, *light, *pointShadow, texture);
            ++pointIndex;
        }

        std::uint32_t spotIndex = 0;
        for (auto* light : sceneLights.spot) {
            if (spotIndex >= maxShadowMapsPerLightType) break;
            if (!light->castShadow || !light->shadow) continue;

            auto* texture = getOrCreateShadowTexture(*light, *light->shadow);
            resources.spotShadowIndices[light->id] = spotIndex;
            resources.spotTextures[spotIndex] = texture;
            renderShadowForLight(scene, *light, *light->shadow, texture);
            ++spotIndex;
        }

        shadowMapState.needsUpdate = false;
        return resources;
    }

    LightUniforms buildLightUniforms(const SceneLightSet& sceneLights, const ShadowResources& shadows) const {
        LightUniforms uniforms{};
        uniforms.ambientColor[0] = sceneLights.ambient.r;
        uniforms.ambientColor[1] = sceneLights.ambient.g;
        uniforms.ambientColor[2] = sceneLights.ambient.b;
        uniforms.ambientColor[3] = 1.f;

        for (std::size_t i = 0; i < std::min(sceneLights.directional.size(), maxDirectionalLights); ++i) {
            auto* light = sceneLights.directional[i];
            auto& dst = uniforms.directionalLights[i];
            Vector3 direction;
            getLightDirection(*light, *light, direction);
            copyVector3(direction, dst.direction);
            copyColorWithIntensity(light->color, light->intensity, dst.color);
            dst.shadowParams[1] = -1.f;
            auto shadowIt = shadows.directionalShadowIndices.find(light->id);
            if (shadowIt != shadows.directionalShadowIndices.end() && light->shadow) {
                dst.shadowParams[0] = 1.f;
                dst.shadowParams[1] = static_cast<float>(shadowIt->second);
                dst.shadowParams[2] = light->shadow->bias;
                dst.shadowParams[3] = light->shadow->radius;
                dst.shadowMapSize[0] = light->shadow->mapSize.x;
                dst.shadowMapSize[1] = light->shadow->mapSize.y;
                dst.shadowMapSize[2] = light->shadow->normalBias;
                copyMatrix(light->shadow->matrix, dst.shadowMatrix);
            } else {
                copyIdentityMatrix(dst.shadowMatrix);
            }
            uniforms.counts[0]++;
        }

        for (std::size_t i = 0; i < std::min(sceneLights.point.size(), maxPointLights); ++i) {
            auto* light = sceneLights.point[i];
            auto& dst = uniforms.pointLights[i];
            Vector3 position;
            position.setFromMatrixPosition(*light->matrixWorld);
            copyVector3(position, dst.position, 1.f);
            copyColorWithIntensity(light->color, light->intensity, dst.color);
            dst.params[0] = light->distance;
            dst.params[1] = light->decay;
            dst.params[2] = light->shadow ? light->shadow->normalBias : 0.f;
            dst.shadowParams[1] = -1.f;
            auto shadowIt = shadows.pointShadowIndices.find(light->id);
            if (shadowIt != shadows.pointShadowIndices.end() && light->shadow) {
                dst.shadowParams[0] = 1.f;
                dst.shadowParams[1] = static_cast<float>(shadowIt->second);
                dst.shadowParams[2] = light->shadow->bias;
                dst.shadowParams[3] = light->shadow->radius;
                dst.shadowMapSize[0] = light->shadow->mapSize.x;
                dst.shadowMapSize[1] = light->shadow->mapSize.y;
                dst.shadowMapSize[2] = light->shadow->camera ? light->shadow->camera->nearPlane : 0.5f;
                dst.shadowMapSize[3] = light->shadow->camera ? light->shadow->camera->farPlane : 500.f;
            }
            uniforms.counts[1]++;
        }

        for (std::size_t i = 0; i < std::min(sceneLights.spot.size(), maxSpotLights); ++i) {
            auto* light = sceneLights.spot[i];
            auto& dst = uniforms.spotLights[i];
            Vector3 position;
            Vector3 direction;
            position.setFromMatrixPosition(*light->matrixWorld);
            getLightDirection(*light, *light, direction);
            copyVector3(position, dst.position, 1.f);
            copyVector3(direction, dst.direction);
            copyColorWithIntensity(light->color, light->intensity, dst.color);
            dst.params[0] = light->distance;
            dst.params[1] = light->decay;
            dst.params[2] = std::cos(light->angle);
            dst.params[3] = std::cos(light->angle * (1.f - light->penumbra));
            dst.shadowParams[1] = -1.f;
            auto shadowIt = shadows.spotShadowIndices.find(light->id);
            if (shadowIt != shadows.spotShadowIndices.end() && light->shadow) {
                dst.shadowParams[0] = 1.f;
                dst.shadowParams[1] = static_cast<float>(shadowIt->second);
                dst.shadowParams[2] = light->shadow->bias;
                dst.shadowParams[3] = light->shadow->radius;
                dst.shadowMapSize[0] = light->shadow->mapSize.x;
                dst.shadowMapSize[1] = light->shadow->mapSize.y;
                dst.shadowMapSize[2] = light->shadow->normalBias;
                copyMatrix(light->shadow->matrix, dst.shadowMatrix);
            } else {
                copyIdentityMatrix(dst.shadowMatrix);
            }
            uniforms.counts[2]++;
        }

        for (std::size_t i = 0; i < std::min(sceneLights.hemisphere.size(), maxHemisphereLights); ++i) {
            auto* light = sceneLights.hemisphere[i];
            auto& dst = uniforms.hemiLights[i];
            Vector3 direction;
            direction.setFromMatrixPosition(*light->matrixWorld).normalize();
            copyVector3(direction, dst.direction);
            copyColorWithIntensity(light->color, light->intensity, dst.skyColor);
            copyColorWithIntensity(light->groundColor, light->intensity, dst.groundColor);
            uniforms.counts[3]++;
        }

        if (!sceneLights.probes.empty()) {
            const auto& coefficients = sceneLights.probes.front()->sh.getCoefficients();
            for (std::size_t i = 0; i < std::min<std::size_t>(coefficients.size(), 9); ++i) {
                uniforms.shCoefficients[i][0] = coefficients[i].x * sceneLights.probes.front()->intensity;
                uniforms.shCoefficients[i][1] = coefficients[i].y * sceneLights.probes.front()->intensity;
                uniforms.shCoefficients[i][2] = coefficients[i].z * sceneLights.probes.front()->intensity;
            }
        }

        return uniforms;
    }

    void generateRenderTargetMipmapsIfNeeded(RenderTarget& target, id<MTLTexture> colorTexture) {
        if (!target.texture || !target.texture->generateMipmaps || !colorTexture || colorTexture.mipmapLevelCount <= 1) return;

        id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
        [blitEncoder generateMipmapsForTexture:colorTexture];
        [blitEncoder endEncoding];
    }

    void addPreRenderJob(const RenderJob& job) {
        if (!job.initiator || !job.camera || !job.renderTarget) return;

        preRenderJobs.emplace_back(job);
    }

    void collectPreRenderJobs(Scene& scene, Camera& camera) {
        if (renderingPrePass) return;

        preRenderJobs.clear();
        Matrix4 projScreenMatrix;
        projScreenMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
        Frustum frustum;
        frustum.setFromProjectionMatrix(projScreenMatrix);
        collectPreRenderables(scene, camera, frustum, renderer);
    }

    void renderPreRenderJobs(Scene& scene) {
        if (renderingPrePass || preRenderJobs.empty()) return;

        const auto jobs = std::move(preRenderJobs);
        preRenderJobs.clear();

        const auto previousRenderTarget = renderTarget;
        const auto previousClearRequested = clearRequested;
        const auto previousClearColorFlag = clearColorFlag;
        const auto previousClearDepthFlag = clearDepthFlag;
        const auto previousExplicitFrameInProgress = explicitFrameInProgress;
        const auto previousRenderingPrePass = renderingPrePass;

        renderingPrePass = true;

        for (const auto& job : jobs) {
            if (!job.initiator || !job.camera || !job.renderTarget || renderTarget == job.renderTarget) continue;

            const auto previousVisible = job.initiator->visible;

            const auto restore = [&] {
                job.initiator->visible = previousVisible;
                renderTarget = previousRenderTarget;
                clearRequested = previousClearRequested;
                clearColorFlag = previousClearColorFlag;
                clearDepthFlag = previousClearDepthFlag;
                explicitFrameInProgress = previousExplicitFrameInProgress;
                renderingPrePass = previousRenderingPrePass;
            };

            job.initiator->visible = false;
            renderTarget = job.renderTarget;
            clearRequested = true;
            clearColorFlag = true;
            clearDepthFlag = true;
            explicitFrameInProgress = false;

            try {
                render(scene, *job.camera, true);
            } catch (...) {
                restore();
                throw;
            }

            restore();
            renderingPrePass = true;
        }

        renderingPrePass = previousRenderingPrePass;
    }

    void render(Scene& scene, Camera& camera, bool autoClear) {
        if (currentCommandBuffer && !explicitFrameInProgress) {
            commitPendingFrame();
        }
        lastRenderTime = std::chrono::steady_clock::now();

        scene.updateMatrixWorld(false);
        metal::prepareCameraForRender(camera);
        updateLODs(scene, camera);

        SceneLightSet sceneLights;
        collectLights(scene, sceneLights);

        Color effectiveClearColor = clearColor;
        float effectiveClearAlpha = clearAlpha;
        if (!scene.background.empty() && scene.background.isColor()) {
            effectiveClearColor.copy(scene.background.color());
        }
        if (!renderingPrePass) {
            collectPreRenderJobs(scene, camera);
            renderPreRenderJobs(scene);
        }

        if (!currentCommandBuffer) {
            ensureFrameStarted();
        }

        const auto shadowResources = renderShadowPasses(scene, sceneLights);
        const auto lightUniforms = buildLightUniforms(sceneLights, shadowResources);

        id<MTLTexture> colorTexture = nil;
        id<MTLTexture> passDepthTexture = nil;
        MTLPixelFormat colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        MetalRenderTargetResources* activeRenderTargetResources = nullptr;

        if (renderTarget) {
            auto& resources = getOrCreateRenderTargetResources(*renderTarget);
            activeRenderTargetResources = &resources;
            colorTexture = resources.colorTexture;
            passDepthTexture = resources.depthTexture;
            colorPixelFormat = resources.colorPixelFormat;
            activeRenderSampleCount = 1;
        } else {
            if (!ensureDrawable()) {
                commitPendingFrame();
                return;
            }
            colorTexture = currentDrawable.texture;
            passDepthTexture = depthTexture;
            colorPixelFormat = colorTexture.pixelFormat;
            activeRenderSampleCount = drawableSampleCount;
            if (activeRenderSampleCount > 1) {
                colorTexture = getOrCreateMultisampleColorTexture(colorPixelFormat);
            }
        }

        const auto shouldClear = autoClear || clearRequested;

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = colorTexture;
        passDesc.colorAttachments[0].loadAction = shouldClear && clearColorFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(effectiveClearColor.r, effectiveClearColor.g, effectiveClearColor.b, effectiveClearAlpha);
        if (!renderTarget && activeRenderSampleCount > 1) {
            passDesc.colorAttachments[0].resolveTexture = currentDrawable.texture;
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
        } else {
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        }

        passDesc.depthAttachment.texture = passDepthTexture;
        passDesc.depthAttachment.loadAction = shouldClear && clearDepthFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.depthAttachment.clearDepth = 1.0;
        passDesc.depthAttachment.storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc];
        resetDepthBiasCache();
        [encoder setDepthStencilState:depthStencilState];
        if (renderTarget) {
            const MTLViewport targetViewport{
                    renderTarget->viewport.x,
                    renderTarget->viewport.y,
                    renderTarget->viewport.z,
                    renderTarget->viewport.w,
                    0.0,
                    1.0};
            [encoder setViewport:targetViewport];
            if (renderTarget->scissorTest) {
                const auto x = clampToSize(renderTarget->scissor.x, activeRenderTargetResources->width);
                const auto y = clampToSize(renderTarget->scissor.y, activeRenderTargetResources->height);
                const auto maxX = clampToSize(renderTarget->scissor.x + renderTarget->scissor.z, activeRenderTargetResources->width);
                const auto maxY = clampToSize(renderTarget->scissor.y + renderTarget->scissor.w, activeRenderTargetResources->height);
                const MTLScissorRect rect{x, y, maxX > x ? maxX - x : 0, maxY > y ? maxY - y : 0};
                [encoder setScissorRect:rect];
            }
        } else {
            applyViewport(encoder);
            applyScissor(encoder);
        }

        std::vector<Object3D*> collectedRenderables;
        collectRenderables(scene, collectedRenderables);
        metal::MetalRenderList renderList;
        buildRenderList(collectedRenderables, camera, renderList);
        bindPassLightResources(encoder, lightUniforms, shadowResources);

        auto renderItems = [&](const std::vector<metal::MetalRenderItem>& items) {
            for (const auto& item : items) {
                auto* obj = item.object;
                auto* material = item.material;
                if (!obj || !material || !material->visible) continue;

                if (auto* sky = dynamic_cast<Sky*>(obj)) {
                    renderSky(encoder, *sky, camera, colorPixelFormat);
                    continue;
                }

                if (auto* water = dynamic_cast<Water*>(obj)) {
                    renderWater(encoder, scene, *water, camera, colorPixelFormat);
                    continue;
                }

                if (auto* reflector = dynamic_cast<Reflector*>(obj)) {
                    renderReflector(encoder, scene, *reflector, camera, colorPixelFormat);
                    continue;
                }

                if (auto* sprite = dynamic_cast<Sprite*>(obj)) {
                    renderSprite(encoder, *sprite, camera, colorPixelFormat);
                    continue;
                }

                if (auto* points = dynamic_cast<Points*>(obj)) {
                    renderPoints(encoder, *points, *material, camera, colorPixelFormat, item.group);
                    continue;
                }

                if (auto* line = dynamic_cast<Line*>(obj)) {
                    renderLine(encoder, *line, *material, camera, colorPixelFormat, item.group);
                    continue;
                }

                if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                    if (material->is<RawShaderMaterial>()) {
                        renderRawShader(encoder, *mesh, *material, camera, colorPixelFormat, item.group);
                        continue;
                    }
                }

                BufferGeometry* geometry = nullptr;
                bool isWireframe = false;
                bool transparent = false;

                if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                    geometry = mesh->geometry().get();

                    if (auto* wf = dynamic_cast<MaterialWithWireframe*>(material)) {
                        isWireframe = wf->wireframe;
                    }
                    transparent = material->transparent;
                }

                if (!geometry || !material || !material->visible) continue;
                trackGeometry(*geometry);

                auto* posAttr = getFloatAttribute(*geometry, "position");
                if (!posAttr) continue;
                auto* normAttr = getFloatAttribute(*geometry, "normal");
                auto* uvAttr = getFloatAttribute(*geometry, "uv");
                auto* colorAttr = getFloatAttribute(*geometry, "color");
                auto* instancedMesh = dynamic_cast<InstancedMesh*>(obj);
                if (instancedMesh && instancedMesh->count() == 0) continue;
                auto* skinnedMesh = dynamic_cast<SkinnedMesh*>(obj);

                const auto shadingParams = extractShadingParams(renderer, scene, *material, camera, obj->receiveShadow);
                const bool useUv = uvAttr && needsUv(shadingParams);
                const bool useVertexColors = material->vertexColors && colorAttr;
                const bool useNormal = normAttr != nullptr;
                const bool useLights = useNormal && isLightingMaterial(*material);
                const bool useSkinning = skinnedMesh && skinnedMesh->skeleton && hasSkinningAttributes(*geometry);
                const bool useInstancing = instancedMesh && instancedMesh->count() > 0;
                const bool useInstanceColor = useInstancing && instancedMesh->instanceColor() != nullptr;
                const bool useTangent = useNormal && useUv;
                if (useInstancing && useSkinning) {
                    std::cerr << "MetalRenderer: skipping unsupported instanced skinned renderable " << obj->id << "\n";
                    continue;
                }

                metal::ShaderProgramKey shaderKey;
                shaderKey.useMap = useUv;
                shaderKey.useVertexColors = useVertexColors;
                shaderKey.useNormal = useNormal;
                shaderKey.useSkinning = useSkinning;
                shaderKey.useLights = useLights;
                shaderKey.useInstancing = useInstancing;
                shaderKey.useInstanceColor = useInstanceColor;
                shaderKey.doubleSided = material->side == Side::Double;
                shaderKey.flipSided = material->side == Side::Back;

                std::uint8_t vertexLayoutBitmask = vertexLayoutPosition;
                if (useNormal) vertexLayoutBitmask |= vertexLayoutNormal;
                if (useUv) vertexLayoutBitmask |= vertexLayoutUv;
                if (useVertexColors) vertexLayoutBitmask |= vertexLayoutColor;
                if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;
                if (useTangent) vertexLayoutBitmask |= vertexLayoutTangent;

                metal::PipelineKey pipelineKey;
                pipelineKey.vertexFunction = shaderManager->getOrCreateVertexFunction(shaderKey);
                pipelineKey.fragmentFunction = shaderManager->getOrCreateFragmentFunction(shaderKey);
                pipelineKey.alphaBlending = transparent;
                pipelineKey.vertexLayoutBitmask = vertexLayoutBitmask;
                pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
                pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

                id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
                [encoder setRenderPipelineState:pso];
                id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                        material->depthTest,
                        material->depthWrite,
                        material->depthFunc);
                [encoder setDepthStencilState:materialDepthStencilState];
                const auto frontFaceCW = obj->matrixWorld->determinant() < 0;
                const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, isWireframe);
                [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
                [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
                [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
                applyDepthBias(encoder, *material);

                bindDrawAttributes(encoder, *geometry, *posAttr, normAttr, uvAttr, colorAttr, useNormal, useUv, useVertexColors, useTangent);
                if (useSkinning) {
                    bindSkinning(encoder, *geometry, skinnedMesh);
                }
                NSUInteger instanceCount = 1;
                if (useInstancing) {
                    bindInstancing(encoder, *instancedMesh, useInstanceColor);
                    instanceCount = static_cast<NSUInteger>(instancedMesh->count());
                }

                TransformUniforms transformUniforms;
                computeTransformUniforms(camera, *obj, transformUniforms, useInstancing);
                [encoder setVertexBytes:&transformUniforms length:sizeof(transformUniforms) atIndex:4];

                [encoder setFragmentBytes:&shadingParams length:sizeof(shadingParams) atIndex:0];

                auto* envMaterial = dynamic_cast<MaterialWithEnvMap*>(material);
                if (useUv) {
                    auto* mapMaterial = dynamic_cast<MaterialWithMap*>(material);
                    auto* normalMaterial = dynamic_cast<MaterialWithNormalMap*>(material);
                    auto* roughnessMaterial = dynamic_cast<MaterialWithRoughness*>(material);
                    auto* metalnessMaterial = dynamic_cast<MaterialWithMetalness*>(material);
                    auto* aoMaterial = dynamic_cast<MaterialWithAoMap*>(material);
                    auto* emissiveMaterial = dynamic_cast<MaterialWithEmissive*>(material);
                    bindTextureOrPlaceholder(encoder, mapMaterial ? mapMaterial->map : nullptr, whiteTexture, 0);
                    bindTextureOrPlaceholder(encoder, normalMaterial ? normalMaterial->normalMap : nullptr, normalTexture, 1);
                    bindTextureOrPlaceholder(encoder, roughnessMaterial ? roughnessMaterial->roughnessMap : nullptr, whiteTexture, 2);
                    bindTextureOrPlaceholder(encoder, metalnessMaterial ? metalnessMaterial->metalnessMap : nullptr, blackTexture, 3);
                    bindTextureOrPlaceholder(encoder, aoMaterial ? aoMaterial->aoMap : nullptr, whiteTexture, 4);
                    bindTextureOrPlaceholder(encoder, emissiveMaterial ? emissiveMaterial->emissiveMap : nullptr, whiteTexture, 5);
                }
                if (useLights) {
                    bindCubeTextureOrPlaceholder(encoder, envMaterial ? envMaterial->envMap : nullptr, 6);
                }
                if (!useUv && useLights) {
                    [encoder setFragmentSamplerState:defaultSampler atIndex:0];
                }

                drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle, instanceCount, item.group);
            }
        };

        renderItems(renderList.opaque);
        renderItems(renderList.transparent);

        [encoder endEncoding];
        if (activeRenderTargetResources) {
            generateRenderTargetMipmapsIfNeeded(*renderTarget, activeRenderTargetResources->colorTexture);
        }

        clearRequested = false;
        clearColorFlag = true;
        clearDepthFlag = true;

        if (autoClear) {
            if (!lastFrameWasExternallyAccessed) {
                commitPendingFrame();
            }
        }
    }
};

MetalRenderer::MetalRenderer(Window& window)
    : pimpl_(std::make_unique<Impl>(*this, window)) {}

void MetalRenderer::render(Scene& scene, Camera& camera) {
    pimpl_->render(scene, camera, autoClear);
}

void MetalRenderer::setSize(std::pair<int, int> size) {
    pimpl_->setSize(size);
}

WindowSize MetalRenderer::size() const {
    return pimpl_->window.size();
}

void* MetalRenderer::device() const {
    return (__bridge void*) pimpl_->device;
}

void* MetalRenderer::currentCommandBuffer() const {
    pimpl_->ensureFrameStarted();
    pimpl_->currentCommandBufferExternallyAccessed = true;
    return (__bridge void*) pimpl_->currentCommandBuffer;
}

void* MetalRenderer::currentDrawableTexture() const {
    pimpl_->ensureFrameStarted();
    pimpl_->currentCommandBufferExternallyAccessed = true;
    if (!pimpl_->ensureDrawable()) return nullptr;
    return (__bridge void*) pimpl_->currentDrawable.texture;
}

void MetalRenderer::setClearColor(const Color& color, float alpha) {
    pimpl_->setClearColor(color, alpha);
}

void MetalRenderer::clear(bool color, bool depth, bool stencil) {
    pimpl_->clear(color, depth, stencil);
}

void MetalRenderer::setViewport(const Vector4& v) {
    pimpl_->setViewport(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z), static_cast<int>(v.w));
}

void MetalRenderer::setViewport(int x, int y, int width, int height) {
    pimpl_->setViewport(x, y, width, height);
}

void MetalRenderer::setViewport(const std::pair<int, int>& pos, const std::pair<int, int>& size) {
    pimpl_->setViewport(pos.first, pos.second, size.first, size.second);
}

void MetalRenderer::setScissor(const Vector4& v) {
    pimpl_->setScissor(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z), static_cast<int>(v.w));
}

void MetalRenderer::setScissor(int x, int y, int width, int height) {
    pimpl_->setScissor(x, y, width, height);
}

void MetalRenderer::setScissor(const std::pair<int, int>& pos, const std::pair<int, int>& size) {
    pimpl_->setScissor(pos.first, pos.second, size.first, size.second);
}

void MetalRenderer::setScissorTest(bool boolean) {
    pimpl_->scissorTest = boolean;
}

void MetalRenderer::setRenderTarget(RenderTarget* renderTarget) {
    pimpl_->renderTarget = renderTarget;
}

RenderTarget* MetalRenderer::getRenderTarget() {
    return pimpl_->renderTarget;
}

void MetalRenderer::addPreRenderJob(const RenderJob& job) {
    pimpl_->addPreRenderJob(job);
}

std::vector<unsigned char> MetalRenderer::readRGBPixels() {
    return pimpl_->readRGBPixels();
}

MetalShadowMap& MetalRenderer::shadowMap() {
    return pimpl_->shadowMapState;
}

const MetalShadowMap& MetalRenderer::shadowMap() const {
    return pimpl_->shadowMapState;
}

MetalRenderer::~MetalRenderer() = default;
