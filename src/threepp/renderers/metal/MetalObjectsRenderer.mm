#import "MetalRendererImpl.hpp"

using namespace threepp;

id<MTLBuffer> MetalRenderer::Impl::getDefaultTangentBuffer(std::size_t vertexCount) {
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

id<MTLBuffer> MetalRenderer::Impl::getSkinIndexBuffer(BufferAttribute& attribute) {
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

bool MetalRenderer::Impl::bindSkinning(id<MTLRenderCommandEncoder> encoder, BufferGeometry& geometry, SkinnedMesh* skinnedMesh) {
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

void MetalRenderer::Impl::bindDrawAttributes(id<MTLRenderCommandEncoder> encoder,
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

void MetalRenderer::Impl::bindInstancing(id<MTLRenderCommandEncoder> encoder, InstancedMesh& instancedMesh, bool useInstanceColor) {
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


std::optional<MetalRenderer::Impl::DrawSpan> MetalRenderer::Impl::computeDrawSpan(int dataCount, const DrawRange& drawRange, std::optional<GeometryGroup> group) {
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

void MetalRenderer::Impl::drawGeometry(id<MTLRenderCommandEncoder> encoder,
                                       BufferGeometry& geometry,
                                       FloatBufferAttribute& position,
                                       MTLPrimitiveType primitiveType,
                                       NSUInteger instanceCount,
                                       std::optional<GeometryGroup> group) {
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

void MetalRenderer::Impl::drawLineLoopGeometry(id<MTLRenderCommandEncoder> encoder,
                                               BufferGeometry& geometry,
                                               FloatBufferAttribute& position,
                                               std::optional<GeometryGroup> group) {
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

void MetalRenderer::Impl::renderLine(id<MTLRenderCommandEncoder> encoder,
                                     Line& line,
                                     Material& material,
                                     Camera& camera,
                                     MTLPixelFormat colorPixelFormat,
                                     std::optional<GeometryGroup> group) {
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

float MetalRenderer::Impl::pointScale() const {
    const auto viewportHeight = renderTarget ? renderTarget->viewport.w : viewport.w * pixelRatio;
    return std::max(viewportHeight, 1.f) * 0.5f;
}

void MetalRenderer::Impl::renderPoints(id<MTLRenderCommandEncoder> encoder,
                                       Points& points,
                                       Material& material,
                                       Camera& camera,
                                       MTLPixelFormat colorPixelFormat,
                                       std::optional<GeometryGroup> group) {
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

void MetalRenderer::Impl::renderRawShader(id<MTLRenderCommandEncoder> encoder,
                                          Mesh& mesh,
                                          Material& material,
                                          Camera& camera,
                                          MTLPixelFormat colorPixelFormat,
                                          std::optional<GeometryGroup> group) {
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

void MetalRenderer::Impl::renderSprite(id<MTLRenderCommandEncoder> encoder, Sprite& sprite, Camera& camera, MTLPixelFormat colorPixelFormat) {
    auto* material = sprite.material()->as<SpriteMaterial>();
    if (!material || !material->visible) return;

    static constexpr float positions[] = {
            -0.5f, -0.5f, 0.f,
            0.5f, -0.5f, 0.f,
            -0.5f, 0.5f, 0.f,
            0.5f, 0.5f, 0.f};
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

void MetalRenderer::Impl::renderSky(id<MTLRenderCommandEncoder> encoder, Sky& sky, Camera& camera, MTLPixelFormat colorPixelFormat) {
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

void MetalRenderer::Impl::renderWater(id<MTLRenderCommandEncoder> encoder, Scene& scene, Water& water, Camera& camera, MTLPixelFormat colorPixelFormat) {
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

void MetalRenderer::Impl::renderReflector(id<MTLRenderCommandEncoder> encoder, Scene&, Reflector& reflector, Camera& camera, MTLPixelFormat colorPixelFormat) {
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
