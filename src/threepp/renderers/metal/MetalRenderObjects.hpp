#ifndef THREEPP_METAL_RENDER_OBJECTS_HPP
#define THREEPP_METAL_RENDER_OBJECTS_HPP

#import "MetalCameraUtils.hpp"
#import "MetalRenderList.hpp"

#import "threepp/cameras/Camera.hpp"
#import "threepp/cameras/PerspectiveCamera.hpp"
#import "threepp/canvas/GlfwWindow.hpp"
#import "threepp/canvas/Window.hpp"
#import "threepp/core/BufferAttribute.hpp"
#import "threepp/core/BufferGeometry.hpp"
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
#import "threepp/materials/ShadowMaterial.hpp"
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
#import "threepp/objects/ObjectWithMaterials.hpp"
#import "threepp/objects/Points.hpp"
#import "threepp/objects/Reflector.hpp"
#import "threepp/objects/SkinnedMesh.hpp"
#import "threepp/objects/Sky.hpp"
#import "threepp/objects/Sprite.hpp"
#import "threepp/objects/Water.hpp"
#import "threepp/renderers/RenderJob.hpp"
#import "threepp/renderers/RenderTarget.hpp"
#import "threepp/renderers/Renderer.hpp"
#import "threepp/scenes/Scene.hpp"
#import "threepp/textures/CubeTexture.hpp"
#import "threepp/textures/Texture.hpp"

#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace threepp {


    inline constexpr std::size_t maxDirectionalLights = 4;
    inline constexpr std::size_t maxPointLights = 4;
    inline constexpr std::size_t maxSpotLights = 4;
    inline constexpr std::size_t maxHemisphereLights = 4;
    inline constexpr std::size_t maxShadowMapsPerLightType = 4;

    inline constexpr std::uint8_t vertexLayoutPosition = 1u << 0u;
    inline constexpr std::uint8_t vertexLayoutNormal = 1u << 1u;
    inline constexpr std::uint8_t vertexLayoutUv = 1u << 2u;
    inline constexpr std::uint8_t vertexLayoutColor = 1u << 3u;
    inline constexpr std::uint8_t vertexLayoutTangent = 1u << 4u;
    inline constexpr std::uint8_t vertexLayoutSkinning = 1u << 5u;
    inline constexpr std::uint8_t vertexLayoutColor4 = 1u << 6u;

    inline int requestedAntialiasingSamples(Window& window) {
        if (auto* glfwWindow = dynamic_cast<GlfwWindow*>(&window)) {
            return glfwWindow->antialiasing();
        }
        return 1;
    }

    inline NSUInteger selectSupportedSampleCount(id<MTLDevice> device, int requestedSamples) {
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
        float alphaTest;
        float uvTransform[12];
        float fogColor[4];
        float fogParams[4];
        float padding[4];
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
        float fogColor[4];
        float fogParams[4];
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

    struct alignas(16) BackgroundCubeUniforms {
        float mvp[16];
        float modelMatrix[16];
        float opacity;
        float flipEnvMap;
        std::uint32_t toneMappingType;
        float toneMappingExposure;
        std::uint32_t toneMapped;
        float padding[3];
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

    inline void copyMatrix(const Matrix4& source, float* target) {
        std::copy(source.elements.begin(), source.elements.end(), target);
    }

    inline void copyMatrix3Columns(const Matrix3& source, float* target) {
        for (std::size_t column = 0; column < 3; ++column) {
            target[column * 4 + 0] = source.elements[column * 3 + 0];
            target[column * 4 + 1] = source.elements[column * 3 + 1];
            target[column * 4 + 2] = source.elements[column * 3 + 2];
            target[column * 4 + 3] = 0.f;
        }
    }

    inline void copyIdentityMatrix(float* target) {
        std::fill(target, target + 16, 0.f);
        target[0] = 1.f;
        target[5] = 1.f;
        target[10] = 1.f;
        target[15] = 1.f;
    }

    inline void computeMVP(const Camera& camera, const Object3D& object, Matrix4& out, bool includeObjectMatrix = true) {
        out.copy(metal::convertProjectionToMetalClipSpace(camera.projectionMatrix));
        out.multiply(camera.matrixWorldInverse);
        if (includeObjectMatrix) {
            out.multiply(*object.matrixWorld);
        }
    }

    inline void computeTransformUniforms(const Camera& camera, const Object3D& object, TransformUniforms& out, bool isInstanced = false) {
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

    inline void computeSpriteUniforms(const Camera& camera, const Sprite& sprite, const SpriteMaterial& material, SpriteUniforms& out) {
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
        out.alphaTest = material.alphaTest;

        Matrix3 uvTransform;
        const auto uvScaleMap = material.map ? material.map : material.alphaMap;
        if (uvScaleMap) {
            if (uvScaleMap->matrixAutoUpdate) {
                uvScaleMap->updateMatrix();
            }
            uvTransform.copy(uvScaleMap->matrix);
        }
        copyMatrix3Columns(uvTransform, out.uvTransform);
    }

    inline void computeLineUniforms(const Camera& camera, const Line& line, const LineBasicMaterial& material, LineUniforms& out) {
        Matrix4 mvp;
        computeMVP(camera, line, mvp);
        copyMatrix(mvp, out.mvp);
        out.color[0] = material.color.r;
        out.color[1] = material.color.g;
        out.color[2] = material.color.b;
        out.color[3] = material.opacity;
    }

    inline void computePointUniforms(const Camera& camera, const Points& points, const PointsMaterial& material, float scale, bool sizeAttenuation, float pixelRatio, PointUniforms& out) {
        Matrix4 mvp;
        computeMVP(camera, points, mvp);
        copyMatrix(mvp, out.mvp);
        out.color[0] = material.color.r;
        out.color[1] = material.color.g;
        out.color[2] = material.color.b;
        out.color[3] = material.opacity;
        out.pointSize = material.size * pixelRatio;
        out.scale = scale;
        out.sizeAttenuation = sizeAttenuation ? 1u : 0u;
    }

    inline void computeRawShaderUniforms(const Camera& camera, const Mesh& mesh, float time, RawShaderUniforms& out) {
        Matrix4 mvp;
        computeMVP(camera, mesh, mvp);
        copyMatrix(mvp, out.mvp);
        out.time = time;
        out.padding[0] = 0.f;
        out.padding[1] = 0.f;
        out.padding[2] = 0.f;
    }

    inline void computeShadowMVP(const Camera& shadowCamera, const Object3D& object, Matrix4& out) {
        out.copy(metal::convertProjectionToMetalClipSpace(shadowCamera.projectionMatrix));
        out.multiply(shadowCamera.matrixWorldInverse);
        out.multiply(*object.matrixWorld);
    }

    inline void computeDepthTransformUniforms(const Camera& shadowCamera, const Object3D& object, DepthTransformUniforms& out) {
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

    inline void computePointDepthTransformUniforms(const Camera& shadowCamera, const Object3D& object, const Vector3& lightPosition, float nearPlane, float farPlane, PointDepthTransformUniforms& out) {
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

    inline void collectRenderables(Object3D& object, std::vector<Object3D*>& out) {
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

    inline Material* materialForRenderOrder(Object3D& object) {
        auto material = object.material();
        return material ? material.get() : nullptr;
    }

    template<typename Callback>
    inline void forEachMaterialGroup(ObjectWithMaterials& object, BufferGeometry& geometry, Callback&& callback) {
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

    inline void buildRenderList(const std::vector<Object3D*>& renderables, const Camera& camera, metal::MetalRenderList& renderList) {
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

    inline void updateLODs(Object3D& object, Camera& camera) {
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

    inline void collectPreRenderables(Object3D& object, Camera& camera, const Frustum& frustum, Renderer& renderer) {
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

    inline FloatBufferAttribute* getFloatAttribute(BufferGeometry& geo, const std::string& name) {
        auto* attr = geo.getAttribute(name);
        if (!attr) return nullptr;
        return attr->typed<float>();
    }

    inline bool isSupportedSkinIndexAttribute(BufferAttribute* attr) {
        if (!attr || attr->itemSize() != 4 || attr->count() == 0) return false;

        return attr->typed<float>() ||
               attr->typed<unsigned int>() ||
               attr->typed<int>() ||
               attr->typed<std::uint16_t>() ||
               attr->typed<std::int16_t>() ||
               attr->typed<std::uint8_t>() ||
               attr->typed<std::int8_t>();
    }

    inline bool hasSkinningAttributes(BufferGeometry& geometry) {
        auto* skinIndex = geometry.getAttribute("skinIndex");
        auto* skinWeight = getFloatAttribute(geometry, "skinWeight");
        return isSupportedSkinIndexAttribute(skinIndex) &&
               skinWeight &&
               skinWeight->itemSize() == 4 &&
               skinWeight->count() == skinIndex->count();
    }

    inline bool hasTexture(const std::shared_ptr<Texture>& texture) {
        return texture != nullptr && !texture->images().empty();
    }

    inline bool hasTexture(const Texture* texture) {
        return texture != nullptr && !texture->images().empty();
    }

    inline bool hasCubeTexture(const std::shared_ptr<Texture>& texture) {
        return hasTexture(texture) && dynamic_cast<CubeTexture*>(texture.get()) != nullptr;
    }

    inline float clamp01(float value) {
        return std::clamp(value, 0.f, 1.f);
    }

    inline void collectLights(Object3D& object, SceneLightSet& lights) {
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

    inline void copyColorWithIntensity(const Color& color, float intensity, float* target) {
        target[0] = color.r * intensity;
        target[1] = color.g * intensity;
        target[2] = color.b * intensity;
        target[3] = 1.f;
    }

    inline void copyVector3(const Vector3& vector, float* target, float w = 0.f) {
        target[0] = vector.x;
        target[1] = vector.y;
        target[2] = vector.z;
        target[3] = w;
    }

    inline UniformValue* findUniformValue(UniformMap& uniforms, const std::string& name) {
        auto it = uniforms.find(name);
        if (it == uniforms.end() || !it->second.hasValue()) return nullptr;
        return &it->second.value();
    }

    inline float uniformFloat(UniformMap& uniforms, const std::string& name, float fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<float>(value)) return *v;
        if (const auto* v = std::get_if<int>(value)) return static_cast<float>(*v);
        return fallback;
    }

    inline Vector3 uniformVector3(UniformMap& uniforms, const std::string& name, const Vector3& fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<Vector3>(value)) return *v;
        if (const auto* v = std::get_if<Vector3*>(value); v && *v) return **v;
        return fallback;
    }

    inline Color uniformColor(UniformMap& uniforms, const std::string& name, const Color& fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<Color>(value)) return *v;
        return fallback;
    }

    inline Matrix4 uniformMatrix4(UniformMap& uniforms, const std::string& name, const Matrix4& fallback) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return fallback;
        if (const auto* v = std::get_if<Matrix4>(value)) return *v;
        if (const auto* v = std::get_if<Matrix4*>(value); v && *v) return **v;
        return fallback;
    }

    inline Texture* uniformTexture(UniformMap& uniforms, const std::string& name) {
        auto* value = findUniformValue(uniforms, name);
        if (!value) return nullptr;
        if (const auto* v = std::get_if<Texture*>(value)) return *v;
        return nullptr;
    }

    inline void getLightDirection(const LightWithTarget& light, const Object3D& lightObject, Vector3& target) {
        Vector3 lightPosition;
        Vector3 targetPosition;
        lightPosition.setFromMatrixPosition(*lightObject.matrixWorld);
        targetPosition.setFromMatrixPosition(*light.target().matrixWorld);
        target.subVectors(targetPosition, lightPosition).normalize();
    }

    inline bool isLightingMaterial(const Material& material) {
        return dynamic_cast<const MeshStandardMaterial*>(&material) != nullptr || dynamic_cast<const MeshPhongMaterial*>(&material) != nullptr || dynamic_cast<const MeshLambertMaterial*>(&material) != nullptr;
    }

    inline bool isShadowMaterial(const Material& material) {
        return dynamic_cast<const ShadowMaterial*>(&material) != nullptr;
    }

    template<class Uniforms>
    inline void fillToneMappingUniforms(const Renderer& renderer, const Material& material, Uniforms& uniforms) {
        uniforms.toneMappingType = static_cast<std::uint32_t>(material.toneMapped ? renderer.toneMapping : ToneMapping::None);
        uniforms.toneMappingExposure = renderer.toneMappingExposure;
        uniforms.toneMapped = material.toneMapped ? 1u : 0u;
    }

    template<class Uniforms>
    inline void fillFogUniforms(const Scene& scene, const Material& material, Uniforms& uniforms) {
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

    inline ShadingParams extractShadingParams(const Renderer& renderer, const Scene& scene, Material& material, const Camera& camera, bool receiveShadow) {
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
        } else if (dynamic_cast<ShadowMaterial*>(&material)) {
            params.materialType = 4u;
        } else {
            params.materialType = 0u;
        }
        return params;
    }

    inline bool needsUv(const ShadingParams& params) {
        return params.textureFlags0[0] != 0u || params.textureFlags0[1] != 0u || params.textureFlags0[2] != 0u || params.textureFlags0[3] != 0u || params.textureFlags1[0] != 0u || params.textureFlags1[1] != 0u;
    }

    inline NSUInteger clampToSize(float value, NSUInteger maxValue) {
        const auto rounded = static_cast<long>(std::floor(value));
        return static_cast<NSUInteger>(std::clamp<long>(rounded, 0, static_cast<long>(maxValue)));
    }

    inline bool usesSRGBColorEncoding(Encoding encoding) {
        switch (encoding) {
            case Encoding::sRGB:
            case Encoding::Gamma:
                return true;
            default:
                return false;
        }
    }

    inline MTLPixelFormat toRenderTargetColorPixelFormat(const Texture& texture) {
        const auto srgb = usesSRGBColorEncoding(texture.encoding);
        switch (texture.format) {
            case Format::RGB:
            case Format::RGBA:
                return srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
            case Format::BGRA:
                return srgb ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm;
            default:
                throw std::runtime_error("Metal RenderTarget currently supports only RGB8, RGBA8, and BGRA8 color textures");
        }
    }


}// namespace threepp

#endif
