#import "MetalRendererImpl.hpp"

using namespace threepp;

id<MTLTexture> MetalRenderer::Impl::getOrCreateShadowTexture(Light& light, LightShadow& shadow) {
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

id<MTLTexture> MetalRenderer::Impl::getOrCreatePointShadowTexture(PointLight& light, PointLightShadow& shadow) {
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

bool MetalRenderer::Impl::shouldUpdateShadow(LightShadow& shadow) const {
    return shadowMapState.autoUpdate || shadowMapState.needsUpdate || shadow.autoUpdate || shadow.needsUpdate;
}

void MetalRenderer::Impl::renderDepthObject(id<MTLRenderCommandEncoder> encoder, Scene& scene, Object3D& object, Camera& shadowCamera, const Frustum& frustum) {
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
                        Material* morphMaterial = nullptr;
                        forEachMaterialGroup(*materials, *geometry, [&](Material& material, std::optional<GeometryGroup>) {
                            if (!morphMaterial && material.visible && wantsMorphTargets(material, *geometry)) {
                                morphMaterial = &material;
                            }
                        });
                        const bool useMorphTargets = morphMaterial != nullptr;
                        if (useMorphTargets && morphTargets) {
                            morphTargets->update(&object, geometry, morphMaterial, false);
                        }

                        std::uint16_t vertexLayoutBitmask = vertexLayoutPosition;
                        if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;
                        if (useMorphTargets) vertexLayoutBitmask |= vertexLayoutMorphTargets;

                        auto* posBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                                *posAttr,
                                posAttr->count() * posAttr->itemSize() * sizeof(float),
                                posAttr->array().data());
                        [encoder setVertexBuffer:posBuf offset:0 atIndex:0];
                        bindMorphTargetAttributes(encoder, *geometry, static_cast<std::size_t>(posAttr->count()), useMorphTargets, false);

                        DepthTransformUniforms depthTransforms{};
                        computeDepthTransformUniforms(shadowCamera, object, depthTransforms);
                        if (useMorphTargets && morphTargets) {
                            writeMorphTargetUniforms(*morphTargets, depthTransforms);
                        }
                        [encoder setVertexBytes:&depthTransforms length:sizeof(depthTransforms) atIndex:4];

                        NSUInteger instanceCount = 1;
                        if (useInstancing) {
                            bindInstancing(encoder, *instancedMesh, false);
                            instanceCount = static_cast<NSUInteger>(instancedMesh->count());
                        }
                        const auto frontFaceCW = object.matrixWorld->determinant() < 0;
                        forEachMaterialGroup(*materials, *geometry, [&](Material& material, std::optional<GeometryGroup> group) {
                            if (!material.visible) return;

                            ClippingExtractionOptions clippingOptions;
                            clippingOptions.includeGlobal = false;
                            clippingOptions.includeLocal = material.clipShadows;
                            const auto shadingParams = extractShadingParams(renderer, scene, material, shadowCamera, false, clippingOptions);
                            const bool useClipping = shadingParams.numClippingPlanes > 0u;
                            const metal::DepthShaderKey shaderKey{useSkinning, useInstancing, useClipping, useMorphTargets};
                            auto* vertexFunction = shaderManager->getOrCreateDepthVertexFunction(shaderKey);
                            auto* fragmentFunction = shaderManager->getOrCreateDepthFragmentFunction(shaderKey);
                            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreateDepthOnlyPipelineState(vertexFunction, fragmentFunction, vertexLayoutBitmask);
                            [encoder setRenderPipelineState:pso];
                            if (useClipping) {
                                [encoder setFragmentBytes:&shadingParams length:sizeof(shadingParams) atIndex:0];
                            }

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
        renderDepthObject(encoder, scene, *child, shadowCamera, frustum);
    }
}

void MetalRenderer::Impl::renderPointDepthObject(id<MTLRenderCommandEncoder> encoder, Scene& scene, Object3D& object, Camera& shadowCamera, const Frustum& frustum, const Vector3& lightPosition, float nearPlane, float farPlane) {
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
                        Material* morphMaterial = nullptr;
                        forEachMaterialGroup(*materials, *geometry, [&](Material& material, std::optional<GeometryGroup>) {
                            if (!morphMaterial && material.visible && wantsMorphTargets(material, *geometry)) {
                                morphMaterial = &material;
                            }
                        });
                        const bool useMorphTargets = morphMaterial != nullptr;
                        if (useMorphTargets && morphTargets) {
                            morphTargets->update(&object, geometry, morphMaterial, false);
                        }

                        std::uint16_t vertexLayoutBitmask = vertexLayoutPosition;
                        if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;
                        if (useMorphTargets) vertexLayoutBitmask |= vertexLayoutMorphTargets;

                        auto* posBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                                *posAttr,
                                posAttr->count() * posAttr->itemSize() * sizeof(float),
                                posAttr->array().data());
                        [encoder setVertexBuffer:posBuf offset:0 atIndex:0];
                        bindMorphTargetAttributes(encoder, *geometry, static_cast<std::size_t>(posAttr->count()), useMorphTargets, false);

                        PointDepthTransformUniforms depthTransforms{};
                        computePointDepthTransformUniforms(shadowCamera, object, lightPosition, nearPlane, farPlane, depthTransforms);
                        if (useMorphTargets && morphTargets) {
                            writeMorphTargetUniforms(*morphTargets, depthTransforms);
                        }
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

                            ClippingExtractionOptions clippingOptions;
                            clippingOptions.includeGlobal = false;
                            clippingOptions.includeLocal = material.clipShadows;
                            const auto shadingParams = extractShadingParams(renderer, scene, material, shadowCamera, false, clippingOptions);
                            const bool useClipping = shadingParams.numClippingPlanes > 0u;
                            const metal::DepthShaderKey shaderKey{useSkinning, useInstancing, useClipping, useMorphTargets};
                            auto* vertexFunction = shaderManager->getOrCreatePointDepthVertexFunction(shaderKey);
                            auto* fragmentFunction = shaderManager->getOrCreatePointDepthFragmentFunction(shaderKey);
                            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreateDepthOnlyPipelineState(vertexFunction, fragmentFunction, vertexLayoutBitmask);
                            [encoder setRenderPipelineState:pso];
                            if (useClipping) {
                                [encoder setFragmentBytes:&shadingParams length:sizeof(shadingParams) atIndex:0];
                            }

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
        renderPointDepthObject(encoder, scene, *child, shadowCamera, frustum, lightPosition, nearPlane, farPlane);
    }
}

void MetalRenderer::Impl::renderShadowForLight(Scene& scene, Light& light, LightShadow& shadow, id<MTLTexture> shadowTexture) {
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
    renderDepthObject(encoder, scene, scene, *shadow.camera, shadow.getFrustum());
    [encoder endEncoding];

    shadow.needsUpdate = false;
}

void MetalRenderer::Impl::renderPointLightShadow(Scene& scene, PointLight& light, PointLightShadow& shadow, id<MTLTexture> shadowTexture) {
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

        renderPointDepthObject(encoder, scene, scene, *shadow.camera, shadow.getFrustum(), lightPosition, shadow.camera->nearPlane, shadow.camera->farPlane);
    }

    [encoder endEncoding];

    shadow.needsUpdate = false;
}

ShadowResources MetalRenderer::Impl::renderShadowPasses(Scene& scene, const SceneLightSet& sceneLights) {
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

LightUniforms MetalRenderer::Impl::buildLightUniforms(const SceneLightSet& sceneLights, const ShadowResources& shadows) const {
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
