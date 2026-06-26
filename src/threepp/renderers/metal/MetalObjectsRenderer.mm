#import "MetalRendererImpl.hpp"

#include "threepp/renderers/shaders/ShaderCompiler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace threepp;

namespace {

    bool isTextureUniform(Uniform& uniform) {
        if (!uniform.hasValue()) return false;

        auto& value = uniform.value();
        return std::holds_alternative<Texture*>(value) || std::holds_alternative<std::vector<Texture*>>(value);
    }

    std::vector<std::string> uniformOrder(const RawShaderMaterial& material) {
        if (!material.uniformLayout.empty()) return material.uniformLayout;

        std::vector<std::string> keys;
        keys.reserve(material.uniforms.size());
        for (const auto& [key, uniform] : material.uniforms) {
            if (uniform.hasValue()) {
                keys.push_back(key);
            }
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    void alignBytes(std::vector<std::uint8_t>& bytes, std::size_t alignment) {
        const auto padding = (alignment - (bytes.size() % alignment)) % alignment;
        bytes.insert(bytes.end(), padding, 0u);
    }

    template<class T>
    void appendValue(std::vector<std::uint8_t>& bytes, const T& value, std::size_t alignment = alignof(T)) {
        alignBytes(bytes, alignment);
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }

    void appendFloat4(std::vector<std::uint8_t>& bytes, float x, float y, float z, float w) {
        alignBytes(bytes, 16);
        const float values[4]{x, y, z, w};
        const auto* raw = reinterpret_cast<const std::uint8_t*>(values);
        bytes.insert(bytes.end(), raw, raw + sizeof(values));
    }

    void appendMatrix3(std::vector<std::uint8_t>& bytes, const Matrix3& value) {
        alignBytes(bytes, 16);
        for (int column = 0; column < 3; ++column) {
            const float values[4]{
                    value.elements[column * 3 + 0],
                    value.elements[column * 3 + 1],
                    value.elements[column * 3 + 2],
                    0.f};
            const auto* raw = reinterpret_cast<const std::uint8_t*>(values);
            bytes.insert(bytes.end(), raw, raw + sizeof(values));
        }
    }

    void appendMatrix4(std::vector<std::uint8_t>& bytes, const Matrix4& value) {
        alignBytes(bytes, 16);
        const auto* raw = reinterpret_cast<const std::uint8_t*>(value.elements.data());
        bytes.insert(bytes.end(), raw, raw + value.elements.size() * sizeof(float));
    }

    bool appendUint4Array(std::vector<std::uint8_t>& bytes, const std::vector<std::uint32_t>& value) {
        if (value.empty() || value.size() % 4u != 0u) {
            return false;
        }
        alignBytes(bytes, 16);
        const auto* raw = reinterpret_cast<const std::uint8_t*>(value.data());
        bytes.insert(bytes.end(), raw, raw + value.size() * sizeof(std::uint32_t));
        return true;
    }

    std::string uniformValueTypeName(const UniformValue& value) {
        if (std::holds_alternative<bool>(value)) return "bool";
        if (std::holds_alternative<int>(value)) return "int";
        if (std::holds_alternative<float>(value)) return "float";
        if (std::holds_alternative<Color>(value)) return "Color";
        if (std::holds_alternative<Vector2>(value)) return "Vector2";
        if (std::holds_alternative<Vector3>(value)) return "Vector3";
        if (std::holds_alternative<Vector3*>(value)) return "Vector3*";
        if (std::holds_alternative<Vector4>(value)) return "Vector4";
        if (std::holds_alternative<Matrix3>(value)) return "Matrix3";
        if (std::holds_alternative<Matrix4>(value)) return "Matrix4";
        if (std::holds_alternative<Matrix4*>(value)) return "Matrix4*";
        if (std::holds_alternative<Texture*>(value)) return "Texture*";
        if (std::holds_alternative<std::vector<float>>(value)) return "std::vector<float>";
        if (std::holds_alternative<std::vector<std::uint32_t>>(value)) return "std::vector<std::uint32_t>";
        if (std::holds_alternative<std::vector<Vector2>>(value)) return "std::vector<Vector2>";
        if (std::holds_alternative<std::vector<Vector3>>(value)) return "std::vector<Vector3>";
        if (std::holds_alternative<std::vector<Matrix3>>(value)) return "std::vector<Matrix3>";
        if (std::holds_alternative<std::vector<Matrix4>>(value)) return "std::vector<Matrix4>";
        if (std::holds_alternative<std::vector<Matrix4*>>(value)) return "std::vector<Matrix4*>";
        if (std::holds_alternative<std::vector<Texture*>>(value)) return "std::vector<Texture*>";
        if (std::holds_alternative<std::unordered_map<std::string, NestedUniformValue>>(value)) return "nested uniform object";
        if (std::holds_alternative<std::vector<std::unordered_map<std::string, NestedUniformValue>*>>(value)) return "nested uniform object array";
        return "unknown";
    }

    void appendDiagnostic(std::string& diagnostics, const std::string& message) {
        if (!diagnostics.empty() && diagnostics.back() != '\n') {
            diagnostics.push_back('\n');
        }
        diagnostics += message;
    }

    bool appendUniformValue(std::vector<std::uint8_t>& bytes, UniformValue& value) {
        if (auto* v = std::get_if<bool>(&value)) {
            const auto asInt = *v ? 1 : 0;
            appendValue(bytes, asInt, 4);
        } else if (auto* v = std::get_if<int>(&value)) {
            appendValue(bytes, *v, 4);
        } else if (auto* v = std::get_if<float>(&value)) {
            appendValue(bytes, *v, 4);
        } else if (auto* v = std::get_if<Color>(&value)) {
            appendFloat4(bytes, v->r, v->g, v->b, 1.f);
        } else if (auto* v = std::get_if<Vector2>(&value)) {
            alignBytes(bytes, 8);
            const float values[2]{v->x, v->y};
            const auto* raw = reinterpret_cast<const std::uint8_t*>(values);
            bytes.insert(bytes.end(), raw, raw + sizeof(values));
        } else if (auto* v = std::get_if<Vector3>(&value)) {
            appendFloat4(bytes, v->x, v->y, v->z, 0.f);
        } else if (auto* v = std::get_if<Vector3*>(&value); v && *v) {
            appendFloat4(bytes, (*v)->x, (*v)->y, (*v)->z, 0.f);
        } else if (auto* v = std::get_if<Vector4>(&value)) {
            appendFloat4(bytes, v->x, v->y, v->z, v->w);
        } else if (auto* v = std::get_if<Matrix3>(&value)) {
            appendMatrix3(bytes, *v);
        } else if (auto* v = std::get_if<Matrix4>(&value)) {
            appendMatrix4(bytes, *v);
        } else if (auto* v = std::get_if<Matrix4*>(&value); v && *v) {
            appendMatrix4(bytes, **v);
        } else if (auto* v = std::get_if<std::vector<std::uint32_t>>(&value)) {
            return appendUint4Array(bytes, *v);
        } else {
            return false;
        }

        return true;
    }

    struct UniformPackResult {
        std::vector<std::uint8_t> bytes;
        std::string diagnostics;
        bool success = true;
    };

    UniformPackResult packCustomUniforms(RawShaderMaterial& material, const std::vector<std::string>& order) {
        UniformPackResult result;
        const auto explicitLayout = !material.uniformLayout.empty();

        for (const auto& key : order) {
            auto it = material.uniforms.find(key);
            if (it == material.uniforms.end()) {
                if (explicitLayout) {
                    result.success = false;
                    appendDiagnostic(result.diagnostics, "uniformLayout references missing uniform '" + key + "'");
                }
                continue;
            }

            if (!it->second.hasValue()) {
                if (explicitLayout) {
                    result.success = false;
                    appendDiagnostic(result.diagnostics, "uniform '" + key + "' has no value");
                }
                continue;
            }

            if (isTextureUniform(it->second)) continue;

            auto& value = it->second.value();
            if (!appendUniformValue(result.bytes, value)) {
                result.success = false;
                appendDiagnostic(result.diagnostics, "uniform '" + key + "' has unsupported type " + uniformValueTypeName(value));
            }
        }

        alignBytes(result.bytes, 16);
        return result;
    }

    std::vector<Texture*> collectUniformTextures(RawShaderMaterial& material, const std::vector<std::string>& order) {
        std::vector<Texture*> textures;

        for (const auto& key : order) {
            auto it = material.uniforms.find(key);
            if (it == material.uniforms.end() || !it->second.hasValue()) continue;

            auto& value = it->second.value();
            if (auto* texture = std::get_if<Texture*>(&value)) {
                textures.push_back(*texture);
            } else if (auto* textureVector = std::get_if<std::vector<Texture*>>(&value)) {
                for (auto* texture : *textureVector) {
                    textures.push_back(texture);
                }
            }
        }

        return textures;
    }

    std::uint16_t rawShaderVertexLayout(BufferGeometry& geometry) {
        auto bitmask = vertexLayoutPosition;

        if (auto* normal = getFloatAttribute(geometry, "normal"); normal && normal->itemSize() == 3) {
            bitmask |= vertexLayoutNormal;
        }
        if (auto* uv = getFloatAttribute(geometry, "uv"); uv && uv->itemSize() == 2) {
            bitmask |= vertexLayoutUv;
        }
        if (auto* color = getFloatAttribute(geometry, "color")) {
            if (color->itemSize() == 4) {
                bitmask |= vertexLayoutColor4;
            } else if (color->itemSize() == 3) {
                bitmask |= vertexLayoutColor;
            }
        }
        if (auto* tangent = getFloatAttribute(geometry, "tangent"); tangent && tangent->itemSize() == 4) {
            bitmask |= vertexLayoutTangent;
        }

        return bitmask;
    }

    struct RawShaderProfileScope {
        std::string_view label;
        bool enabled;
        std::chrono::steady_clock::time_point start;

        RawShaderProfileScope(std::string_view label, bool enabled)
            : label(label),
              enabled(enabled),
              start(std::chrono::steady_clock::now()) {}

        ~RawShaderProfileScope() {
            if (!enabled) return;

            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start);
            std::cerr << "MetalRenderer raw shader " << label << ": "
                      << elapsed.count() << "us\n";
        }
    };

    void warnSlangCompilerUnavailableOnce() {
        static std::once_flag flag;
        std::call_once(flag, [] {
            std::cerr << "MetalRenderer: Slang shader compiler is unavailable; "
                      << "Slang RawShaderMaterial draw calls will be skipped.\n";
        });
    }

    void configurePipelineBlending(metal::PipelineKey& key, const Material& material) {
        key.alphaBlending = material.blending != Blending::None &&
                            (material.blending != Blending::Normal || material.transparent || material.opacity < 1.f);
        key.blending = key.alphaBlending ? material.blending : Blending::Normal;
        key.blendEquation = BlendEquation::Add;
        key.blendEquationAlpha = BlendEquation::Add;
        key.blendSrc = BlendFactor::SrcAlpha;
        key.blendDst = BlendFactor::OneMinusSrcAlpha;
        key.blendSrcAlpha = BlendFactor::One;
        key.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;

        if (!key.alphaBlending) return;

        if (material.blending == Blending::Custom) {
            key.blendEquation = material.blendEquation;
            key.blendEquationAlpha = material.blendEquationAlpha.value_or(material.blendEquation);
            key.blendSrc = material.blendSrc;
            key.blendDst = material.blendDst;
            key.blendSrcAlpha = material.blendSrcAlpha.value_or(material.blendSrc);
            key.blendDstAlpha = material.blendDstAlpha.value_or(material.blendDst);
            return;
        }

        if (material.premultipliedAlpha) {
            switch (material.blending) {
                case Blending::Normal:
                    key.blendSrc = BlendFactor::One;
                    key.blendDst = BlendFactor::OneMinusSrcAlpha;
                    key.blendSrcAlpha = BlendFactor::One;
                    key.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
                    break;
                case Blending::Additive:
                    key.blendSrc = BlendFactor::One;
                    key.blendDst = BlendFactor::One;
                    key.blendSrcAlpha = BlendFactor::One;
                    key.blendDstAlpha = BlendFactor::One;
                    break;
                case Blending::Subtractive:
                    key.blendSrc = BlendFactor::Zero;
                    key.blendDst = BlendFactor::OneMinusSrcColor;
                    key.blendSrcAlpha = BlendFactor::Zero;
                    key.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
                    break;
                case Blending::Multiply:
                    key.blendSrc = BlendFactor::Zero;
                    key.blendDst = BlendFactor::SrcColor;
                    key.blendSrcAlpha = BlendFactor::Zero;
                    key.blendDstAlpha = BlendFactor::SrcAlpha;
                    break;
                case Blending::None:
                case Blending::Custom:
                    break;
            }
            return;
        }

        switch (material.blending) {
            case Blending::Normal:
                break;
            case Blending::Additive:
                key.blendSrc = BlendFactor::SrcAlpha;
                key.blendDst = BlendFactor::One;
                key.blendSrcAlpha = BlendFactor::SrcAlpha;
                key.blendDstAlpha = BlendFactor::One;
                break;
            case Blending::Subtractive:
                key.blendSrc = BlendFactor::Zero;
                key.blendDst = BlendFactor::OneMinusSrcColor;
                key.blendSrcAlpha = BlendFactor::Zero;
                key.blendDstAlpha = BlendFactor::OneMinusSrcColor;
                break;
            case Blending::Multiply:
                key.blendSrc = BlendFactor::Zero;
                key.blendDst = BlendFactor::SrcColor;
                key.blendSrcAlpha = BlendFactor::Zero;
                key.blendDstAlpha = BlendFactor::SrcColor;
                break;
            case Blending::None:
            case Blending::Custom:
                break;
        }
    }

    template<class SystemUniforms>
    void fillSystemUniforms(const Camera& camera, const Mesh& mesh, RawShaderMaterial& material, SystemUniforms& out) {
        copyMatrix(*mesh.matrixWorld, out.modelMatrix);
        Matrix4 modelMatrixInverse;
        modelMatrixInverse.copy(*mesh.matrixWorld).invert();
        copyMatrix(modelMatrixInverse, out.modelMatrixInverse);

        Matrix4 modelViewMatrix;
        modelViewMatrix.multiplyMatrices(camera.matrixWorldInverse, *mesh.matrixWorld);
        copyMatrix(modelViewMatrix, out.modelViewMatrix);

        const auto projection = metal::convertProjectionToMetalClipSpace(camera.projectionMatrix);
        copyMatrix(projection, out.projectionMatrix);

        Vector3 cameraPosition;
        cameraPosition.setFromMatrixPosition(*camera.matrixWorld);
        out.cameraPos[0] = cameraPosition.x;
        out.cameraPos[1] = cameraPosition.y;
        out.cameraPos[2] = cameraPosition.z;
        out.cameraPos[3] = 1.f;
        out.time = uniformFloat(material.uniforms, "time", 0.f);
        out.padding[0] = 0.f;
        out.padding[1] = 0.f;
        out.padding[2] = 0.f;
    }

}// namespace

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

id<MTLBuffer> MetalRenderer::Impl::getDefaultMorphTargetBuffer(std::size_t vertexCount) {
    if (defaultMorphTargetBuffer && defaultMorphTargetVertexCount >= vertexCount) {
        return defaultMorphTargetBuffer;
    }

    std::vector<float> values(vertexCount * 3, 0.f);
    defaultMorphTargetBuffer = [device newBufferWithBytes:values.data()
                                                   length:values.size() * sizeof(float)
                                                  options:MTLResourceStorageModeShared];
    defaultMorphTargetVertexCount = vertexCount;
    return defaultMorphTargetBuffer;
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

void MetalRenderer::Impl::bindMorphTargetAttributes(id<MTLRenderCommandEncoder> encoder,
                                                    BufferGeometry& geometry,
                                                    std::size_t vertexCount,
                                                    bool useMorphTargets,
                                                    bool useMorphNormals) {
    if (!useMorphTargets) return;

    auto bindMorphBuffer = [&](const std::string& name, NSUInteger bufferIndex) {
        auto* attr = getFloatAttribute(geometry, name);
        if (attr && attr->itemSize() == 3) {
            auto* buf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *attr,
                    attr->count() * attr->itemSize() * sizeof(float),
                    attr->array().data());
            [encoder setVertexBuffer:buf offset:0 atIndex:bufferIndex];
        } else {
            [encoder setVertexBuffer:getDefaultMorphTargetBuffer(vertexCount) offset:0 atIndex:bufferIndex];
        }
    };

    for (int i = 0; i < 4; ++i) {
        bindMorphBuffer("morphTarget" + std::to_string(i), static_cast<NSUInteger>(11 + i));
    }

    if (useMorphNormals) {
        for (int i = 0; i < 4; ++i) {
            bindMorphBuffer("morphNormal" + std::to_string(i), static_cast<NSUInteger>(15 + i));
        }
    } else {
        for (int i = 4; i < 8; ++i) {
            bindMorphBuffer("morphTarget" + std::to_string(i), static_cast<NSUInteger>(11 + i));
        }
    }
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
                                             bool useTangent,
                                             bool useMorphTargets,
                                             bool useMorphNormals) {
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

    if (useTangent) {
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

    bindMorphTargetAttributes(encoder, geometry, static_cast<std::size_t>(position.count()), useMorphTargets, useMorphNormals);
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

void MetalRenderer::Impl::drawWireframeGeometry(id<MTLRenderCommandEncoder> encoder,
                                                BufferGeometry& geometry,
                                                NSUInteger instanceCount,
                                                std::optional<GeometryGroup> group) {

    auto& wireframeAttribute = metal::getOrUpdateWireframeAttribute(geometry, wireframeAttributes[&geometry]);

    const DrawRange wireframeRange{
            geometry.drawRange.start * 2,
            geometry.drawRange.count * 2};
    std::optional<GeometryGroup> wireframeGroup;
    if (group) {
        wireframeGroup = GeometryGroup{
                group->start * 2,
                group->count * 2,
                group->materialIndex};
    }

    const auto drawSpan = computeDrawSpan(wireframeAttribute.count(), wireframeRange, wireframeGroup);
    if (!drawSpan) return;

    auto* indexBuffer = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
            wireframeAttribute,
            wireframeAttribute.count() * wireframeAttribute.itemSize() * sizeof(unsigned int),
            wireframeAttribute.array().data());

    [encoder drawIndexedPrimitives:MTLPrimitiveTypeLine
                        indexCount:drawSpan->count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:indexBuffer
                 indexBufferOffset:drawSpan->start * sizeof(unsigned int)
                     instanceCount:instanceCount];
}

void MetalRenderer::Impl::renderLine(id<MTLRenderCommandEncoder> encoder,
                                     Scene& scene,
                                     Line& line,
                                     BufferGeometry& geometry,
                                     Material& material,
                                     Camera& camera,
                                     MTLPixelFormat colorPixelFormat,
                                     std::optional<GeometryGroup> group) {
    auto* lineMaterial = material.as<LineBasicMaterial>();
    if (!lineMaterial || !lineMaterial->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    if (!posAttr) return;

    auto* colorAttr = getFloatAttribute(geometry, "color");
    const bool useVertexColors = lineMaterial->vertexColors && colorAttr && colorAttr->itemSize() == 3;

    static bool linewidthWarningPrinted = false;
    if (lineMaterial->linewidth > 1.f && !linewidthWarningPrinted) {
        std::cerr << "MetalRenderer: LineBasicMaterial linewidth > 1 is not supported by Metal and will be ignored.\n";
        linewidthWarningPrinted = true;
    }

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateLineVertexFunction(useVertexColors);
    pipelineKey.fragmentFunction = shaderManager->getOrCreateLineFragmentFunction(useVertexColors);
    configurePipelineBlending(pipelineKey, *lineMaterial);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    if (useVertexColors) pipelineKey.vertexLayoutBitmask |= vertexLayoutColor;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, nullptr, colorAttr, false, false, useVertexColors, false);

    LineUniforms uniforms{};
    computeLineUniforms(camera, line, *lineMaterial, uniforms);
    fillToneMappingUniforms(renderer, *lineMaterial, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));
    uniforms.outputColorSpaceSRGB = renderer.outputColorSpace == ColorSpace::sRGB || renderer.outputColorSpace == ColorSpace::Gamma ? 1u : 0u;
    fillFogUniforms(scene, *lineMaterial, uniforms);
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    if (dynamic_cast<LineLoop*>(&line)) {
        drawLineLoopGeometry(encoder, geometry, *posAttr, group);
    } else {
        const auto primitiveType = dynamic_cast<LineSegments*>(&line) ? MTLPrimitiveTypeLine : MTLPrimitiveTypeLineStrip;
        drawGeometry(encoder, geometry, *posAttr, primitiveType, 1, group);
    }
}

float MetalRenderer::Impl::pointScale() const {
    const auto viewportHeight = renderTarget ? renderTarget->viewport.w : viewport.w;
    return std::max(viewportHeight, 1.f) * 0.5f;
}

void MetalRenderer::Impl::renderPoints(id<MTLRenderCommandEncoder> encoder,
                                       Scene& scene,
                                       Points& points,
                                       BufferGeometry& geometry,
                                       Material& material,
                                       Camera& camera,
                                       MTLPixelFormat colorPixelFormat,
                                       std::optional<GeometryGroup> group) {
    if (auto* particleMaterial = material.as<ParticleMaterial>()) {
        if (!particleMaterial->visible) return;
        trackGeometry(geometry);

        auto* posAttr = getFloatAttribute(geometry, "position");
        if (!posAttr || posAttr->itemSize() != 3) return;

        auto bindParticleAttribute = [&](const std::string& name, NSUInteger index, int itemSize) {
            auto* attr = getFloatAttribute(geometry, name);
            if (!attr || attr->itemSize() != itemSize) return false;

            auto* buffer = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                    *attr,
                    attr->count() * attr->itemSize() * sizeof(float),
                    attr->array().data());
            [encoder setVertexBuffer:buffer offset:0 atIndex:index];
            return true;
        };

        auto* posBuf = (__bridge id<MTLBuffer>) bufferManager->getBuffer(
                *posAttr,
                posAttr->count() * posAttr->itemSize() * sizeof(float),
                posAttr->array().data());
        [encoder setVertexBuffer:posBuf offset:0 atIndex:0];

        if (!bindParticleAttribute("customVisible", 1, 1) ||
            !bindParticleAttribute("customAngle", 2, 1) ||
            !bindParticleAttribute("customSize", 3, 1) ||
            !bindParticleAttribute("customColor", 4, 3) ||
            !bindParticleAttribute("customOpacity", 5, 1)) {
            return;
        }

        auto* map = uniformTexture(particleMaterial->uniforms, "tex");
        const bool useMap = map != nullptr;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = shaderManager->getOrCreateParticlePointVertexFunction(useMap);
        pipelineKey.fragmentFunction = shaderManager->getOrCreateParticlePointFragmentFunction(useMap);
        configurePipelineBlending(pipelineKey, *particleMaterial);
        pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutParticleSystem;
        configurePipelineColorFormats(pipelineKey, colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        [encoder setRenderPipelineState:pso];
        id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                particleMaterial->depthTest,
                particleMaterial->depthWrite,
                particleMaterial->depthFunc);
        [encoder setDepthStencilState:materialDepthStencilState];
        [encoder setCullMode:MTLCullModeNone];
        [encoder setTriangleFillMode:MTLTriangleFillModeFill];
        applyDepthBias(encoder, *particleMaterial);

        ParticleUniforms uniforms{};
        computeParticleUniforms(camera, points, uniforms);
        fillToneMappingUniforms(renderer, *particleMaterial, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:6];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:6];

        bindTextureOrPlaceholder(encoder, map, whiteTexture, 0);

        drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypePoint, 1, group);
        return;
    }

    auto* pointsMaterial = material.as<PointsMaterial>();
    if (!pointsMaterial || !pointsMaterial->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    if (!posAttr) return;

    auto* colorAttr = getFloatAttribute(geometry, "color");
    const bool useVertexColors = pointsMaterial->vertexColors && colorAttr && colorAttr->itemSize() == 3;
    const bool useMorphTargets = wantsMorphTargets(*pointsMaterial, geometry);
    const bool useMap = static_cast<bool>(pointsMaterial->map);
    const bool useAlphaMap = static_cast<bool>(pointsMaterial->alphaMap);
    if (useMorphTargets && morphTargets) {
        morphTargets->update(&points, &geometry, pointsMaterial, false);
    }

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreatePointsVertexFunction(useVertexColors, useMorphTargets);
    pipelineKey.fragmentFunction = shaderManager->getOrCreatePointsFragmentFunction(useVertexColors, useMap, useAlphaMap);
    configurePipelineBlending(pipelineKey, *pointsMaterial);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    if (useVertexColors) pipelineKey.vertexLayoutBitmask |= vertexLayoutColor;
    if (useMorphTargets) pipelineKey.vertexLayoutBitmask |= vertexLayoutMorphTargets;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, nullptr, colorAttr, false, false, useVertexColors, false, useMorphTargets, false);

    PointUniforms uniforms{};
    const bool useSizeAttenuation = pointsMaterial->sizeAttenuation && dynamic_cast<PerspectiveCamera*>(&camera) != nullptr;
    computePointUniforms(camera, points, *pointsMaterial, pointScale(), useSizeAttenuation, pixelRatio, uniforms);
    if (useMorphTargets && morphTargets) {
        writeMorphTargetUniforms(*morphTargets, uniforms);
    }
    fillToneMappingUniforms(renderer, *pointsMaterial, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));
    uniforms.outputColorSpaceSRGB = renderer.outputColorSpace == ColorSpace::sRGB || renderer.outputColorSpace == ColorSpace::Gamma ? 1u : 0u;
    fillFogUniforms(scene, *pointsMaterial, uniforms);
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    if (useMap) {
        bindTextureOrPlaceholder(encoder, pointsMaterial->map, whiteTexture, 0);
    }
    if (useAlphaMap) {
        bindTextureOrPlaceholder(encoder, pointsMaterial->alphaMap, whiteTexture, 1);
        [encoder setFragmentSamplerState:samplerForTexture(pointsMaterial->alphaMap.get()) atIndex:1];
    }

    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypePoint, 1, group);
}

void MetalRenderer::Impl::renderParticleSystem(id<MTLRenderCommandEncoder> encoder,
                                               Mesh& mesh,
                                               BufferGeometry& geometry,
                                               Material& material,
                                               Camera& camera,
                                               MTLPixelFormat colorPixelFormat,
                                               std::optional<GeometryGroup> group) {
    auto* shaderMaterial = material.as<ShaderMaterial>();
    if (!shaderMaterial || !shaderMaterial->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    auto* normalAttr = getFloatAttribute(geometry, "normal");
    auto* uvAttr = getFloatAttribute(geometry, "uv");
    auto* colorAttr = getFloatAttribute(geometry, "color");
    if (!posAttr || posAttr->itemSize() != 3 ||
        !normalAttr || normalAttr->itemSize() != 3 ||
        !uvAttr || uvAttr->itemSize() != 2 ||
        !colorAttr || colorAttr->itemSize() != 3) {
        return;
    }

    auto* map = uniformTexture(shaderMaterial->uniforms, "tex");
    const bool useMap = map != nullptr;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateParticleVertexFunction(useMap);
    pipelineKey.fragmentFunction = shaderManager->getOrCreateParticleFragmentFunction(useMap);
    configurePipelineBlending(pipelineKey, *shaderMaterial);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutNormal | vertexLayoutUv | vertexLayoutColor;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
    pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

    id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
    [encoder setRenderPipelineState:pso];
    id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
            shaderMaterial->depthTest,
            shaderMaterial->depthWrite,
            shaderMaterial->depthFunc);
    [encoder setDepthStencilState:materialDepthStencilState];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];
    applyDepthBias(encoder, *shaderMaterial);

    bindDrawAttributes(encoder, geometry, *posAttr, normalAttr, uvAttr, colorAttr, true, true, true, false);

    ParticleUniforms uniforms{};
    computeParticleUniforms(camera, mesh, uniforms);
    fillToneMappingUniforms(renderer, *shaderMaterial, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:6];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:6];

    bindTextureOrPlaceholder(encoder, map, whiteTexture, 0);
    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle, 1, group);
}

MaterialPrewarmStatus MetalRenderer::Impl::prewarmMaterial(const MaterialPrewarmRequest& request) {
    auto* rawMaterial = request.material;
    if (!rawMaterial || !rawMaterial->visible) {
        return MaterialPrewarmStatus::Ready;
    }
    if (rawMaterial->shaderLanguage != ShaderLanguage::SLANG) {
        return MaterialPrewarmStatus::Ready;
    }
    if (!shaderCompiler) {
        warnSlangCompilerUnavailableOnce();
        return MaterialPrewarmStatus::Failed;
    }
    if (!dynamicShaderCache || !pipelineCache) {
        return MaterialPrewarmStatus::Failed;
    }

    CompileResult vertexCompile;
    CompileResult fragmentCompile;
    {
        RawShaderProfileScope profileCompile{"prewarm slang compile", profileRawShader};
        vertexCompile = dynamicShaderCache->compile(*shaderCompiler, rawMaterial->vertexShader, ShaderStage::Vertex, TargetLanguage::MSL);
        fragmentCompile = dynamicShaderCache->compile(*shaderCompiler, rawMaterial->fragmentShader, ShaderStage::Fragment, TargetLanguage::MSL);
    }
    if (!vertexCompile.success) {
        std::cerr << "MetalRenderer: Slang vertex shader prewarm failed:\n"
                  << vertexCompile.diagnostics << "\n";
        return MaterialPrewarmStatus::Failed;
    }
    if (!fragmentCompile.success) {
        std::cerr << "MetalRenderer: Slang fragment shader prewarm failed:\n"
                  << fragmentCompile.diagnostics << "\n";
        return MaterialPrewarmStatus::Failed;
    }

    id<MTLFunction> vertexFunction = dynamicShaderCache->getFunction(vertexCompile.code, @"vertexMain");
    id<MTLFunction> fragmentFunction = dynamicShaderCache->getFunction(fragmentCompile.code, @"fragmentMain");
    if (!vertexFunction || !fragmentFunction) {
        return MaterialPrewarmStatus::Failed;
    }

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = (__bridge void*) vertexFunction;
    pipelineKey.fragmentFunction = (__bridge void*) fragmentFunction;
    configurePipelineBlending(pipelineKey, *rawMaterial);
    pipelineKey.vertexLayoutBitmask = request.vertexLayoutBitmask != 0u
        ? request.vertexLayoutBitmask
        : vertexLayoutPosition;

    if (request.renderTarget) {
        auto& resources = getOrCreateRenderTargetResources(*request.renderTarget);
        const auto primaryFormat = resources.colorPixelFormats.empty()
            ? MTLPixelFormatInvalid
            : resources.colorPixelFormats.front();
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(primaryFormat);
        pipelineKey.colorAttachmentCount = static_cast<std::uint64_t>(std::max<std::size_t>(resources.colorPixelFormats.size(), 1u));
        pipelineKey.colorPixelFormats.fill(0);
        const auto count = std::min<std::size_t>(resources.colorPixelFormats.size(), pipelineKey.colorPixelFormats.size());
        for (std::size_t i = 0; i < count; ++i) {
            pipelineKey.colorPixelFormats[i] = static_cast<std::uint64_t>(resources.colorPixelFormats[i]);
        }
        pipelineKey.rasterSampleCount = 1;
    } else {
        pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(MTLPixelFormatBGRA8Unorm);
        pipelineKey.colorAttachmentCount = 1;
        pipelineKey.colorPixelFormats.fill(0);
        pipelineKey.colorPixelFormats[0] = static_cast<std::uint64_t>(MTLPixelFormatBGRA8Unorm);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(std::max<NSUInteger>(drawableSampleCount, 1));
    }

    const auto status = pipelineCache->prewarmPipelineState(pipelineKey);
    switch (status) {
        case metal::PipelinePrewarmStatus::Ready:
            return MaterialPrewarmStatus::Ready;
        case metal::PipelinePrewarmStatus::Compiling:
            return MaterialPrewarmStatus::Compiling;
        case metal::PipelinePrewarmStatus::Failed:
            return MaterialPrewarmStatus::Failed;
    }
    return MaterialPrewarmStatus::Failed;
}

void MetalRenderer::Impl::renderRawShader(id<MTLRenderCommandEncoder> encoder,
                                          Mesh& mesh,
                                          BufferGeometry& geometry,
                                          Material& material,
                                          Camera& camera,
                                          MTLPixelFormat colorPixelFormat,
                                          std::optional<GeometryGroup> group) {
    auto* rawMaterial = material.as<RawShaderMaterial>();
    if (!rawMaterial || !rawMaterial->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    if (!posAttr) return;

    if (rawMaterial->shaderLanguage == ShaderLanguage::SLANG) {
        if (!rawShaderProfileEnvChecked) {
            rawShaderProfileEnvChecked = true;
            profileRawShader = std::getenv("THREEPP_METAL_PROFILE_RAW_SHADER") != nullptr;
        }

        if (!shaderCompiler) {
            warnSlangCompilerUnavailableOnce();
            return;
        }
        if (!dynamicShaderCache) return;

        CompileResult vertexCompile;
        CompileResult fragmentCompile;
        {
            RawShaderProfileScope profileCompile{"slang compile", profileRawShader};
            vertexCompile = dynamicShaderCache->compile(*shaderCompiler, rawMaterial->vertexShader, ShaderStage::Vertex, TargetLanguage::MSL);
            fragmentCompile = dynamicShaderCache->compile(*shaderCompiler, rawMaterial->fragmentShader, ShaderStage::Fragment, TargetLanguage::MSL);
        }
        if (!vertexCompile.success) {
            std::cerr << "MetalRenderer: Slang vertex shader compilation failed:\n"
                      << vertexCompile.diagnostics << "\n";
            return;
        }

        if (!fragmentCompile.success) {
            std::cerr << "MetalRenderer: Slang fragment shader compilation failed:\n"
                      << fragmentCompile.diagnostics << "\n";
            return;
        }

        id<MTLFunction> vertexFunction = nil;
        id<MTLFunction> fragmentFunction = nil;
        {
            RawShaderProfileScope profileFunction{"dynamic function", profileRawShader};
            vertexFunction = dynamicShaderCache->getFunction(vertexCompile.code, @"vertexMain");
            fragmentFunction = dynamicShaderCache->getFunction(fragmentCompile.code, @"fragmentMain");
        }
        if (!vertexFunction || !fragmentFunction) return;

        metal::PipelineKey pipelineKey;
        pipelineKey.vertexFunction = (__bridge void*) vertexFunction;
        pipelineKey.fragmentFunction = (__bridge void*) fragmentFunction;
        configurePipelineBlending(pipelineKey, *rawMaterial);
        pipelineKey.vertexLayoutBitmask = rawShaderVertexLayout(geometry);
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
        pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

        id<MTLRenderPipelineState> pso = nil;
        try {
            RawShaderProfileScope profilePso{"pipeline state", profileRawShader};
            pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
        } catch (const std::exception& e) {
            std::cerr << "MetalRenderer: failed to create dynamic Slang PSO: " << e.what() << "\n";
            return;
        }
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

        auto* normalAttr = getFloatAttribute(geometry, "normal");
        auto* uvAttr = getFloatAttribute(geometry, "uv");
        auto* colorAttr = getFloatAttribute(geometry, "color");
        const auto useNormal = normalAttr && normalAttr->itemSize() == 3;
        const auto useUv = uvAttr && uvAttr->itemSize() == 2;
        const auto useVertexColors = colorAttr && (colorAttr->itemSize() == 3 || colorAttr->itemSize() == 4);
        auto* tangentAttr = getFloatAttribute(geometry, "tangent");
        const auto useTangent = tangentAttr && tangentAttr->itemSize() == 4;
        bindDrawAttributes(encoder, geometry, *posAttr, normalAttr, uvAttr, colorAttr, useNormal, useUv, useVertexColors, useTangent);
        auto* instancedMesh = dynamic_cast<InstancedMesh*>(&mesh);
        const auto resolvedInstanceCount = geometry.instanceCount.value_or(instancedMesh ? instancedMesh->count() : 1);
        if (resolvedInstanceCount == 0) return;
        if (instancedMesh) {
            bindInstancing(encoder, *instancedMesh, instancedMesh->instanceColor() != nullptr);
        }

        SystemUniforms systemUniforms{};
        fillSystemUniforms(camera, mesh, *rawMaterial, systemUniforms);
        [encoder setVertexBytes:&systemUniforms length:sizeof(systemUniforms) atIndex:4];
        [encoder setFragmentBytes:&systemUniforms length:sizeof(systemUniforms) atIndex:4];

        const auto order = uniformOrder(*rawMaterial);
        auto customUniforms = packCustomUniforms(*rawMaterial, order);
        if (!customUniforms.success) {
            std::cerr << "MetalRenderer: failed to pack dynamic Slang uniforms:\n"
                      << customUniforms.diagnostics << "\n";
            return;
        }
        if (!customUniforms.bytes.empty()) {
            [encoder setVertexBytes:customUniforms.bytes.data() length:customUniforms.bytes.size() atIndex:11];
            [encoder setFragmentBytes:customUniforms.bytes.data() length:customUniforms.bytes.size() atIndex:11];
        }

        auto textures = collectUniformTextures(*rawMaterial, order);
        for (NSUInteger i = 0; i < textures.size(); ++i) {
            auto* texture = textures[i];
            id<MTLTexture> metalTexture = whiteTexture;
            id<MTLSamplerState> sampler = defaultSampler;
            if (texture) {
                try {
                    if (auto tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, true)) {
                        metalTexture = tex;
                        sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "MetalRenderer: failed to bind dynamic Slang texture '" << texture->id << "': " << e.what() << "\n";
                    return;
                }
            }
            [encoder setVertexTexture:metalTexture atIndex:i];
            [encoder setVertexSamplerState:sampler atIndex:i];
            [encoder setFragmentTexture:metalTexture atIndex:i];
            [encoder setFragmentSamplerState:sampler atIndex:i];
        }

        drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle, static_cast<NSUInteger>(resolvedInstanceCount), group);
        return;
    }

    auto* colorAttr = getFloatAttribute(geometry, "color");
    if (!colorAttr || colorAttr->itemSize() != 4) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateRawShaderVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateRawShaderFragmentFunction();
    configurePipelineBlending(pipelineKey, *rawMaterial);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutColor4;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, nullptr, colorAttr, false, false, true, false);

    RawShaderUniforms uniforms{};
    computeRawShaderUniforms(camera, mesh, uniformFloat(rawMaterial->uniforms, "time", 0.f), uniforms);
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle, 1, group);
}

void MetalRenderer::Impl::renderDepthTexture(id<MTLRenderCommandEncoder> encoder,
                                             Mesh& mesh,
                                             BufferGeometry& geometry,
                                             ShaderMaterial& material,
                                             Camera& camera,
                                             MTLPixelFormat colorPixelFormat,
                                             std::optional<GeometryGroup> group) {
    auto* depthMaterial = &material;
    if (!depthMaterial->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    auto* uvAttr = getFloatAttribute(geometry, "uv");
    if (!posAttr || posAttr->itemSize() != 3 || !uvAttr || uvAttr->itemSize() != 2) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateDepthTextureVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateDepthTextureFragmentFunction();
    configurePipelineBlending(pipelineKey, *depthMaterial);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutUv;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
    pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

    id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
    [encoder setRenderPipelineState:pso];
    id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(false, false, DepthFunc::LessEqual);
    [encoder setDepthStencilState:materialDepthStencilState];

    const auto frontFaceCW = mesh.matrixWorld->determinant() < 0;
    const auto faceCullingState = metal::computeFaceCullingState(depthMaterial->side, frontFaceCW, false);
    [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];
    applyDepthBias(encoder, *depthMaterial);

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, uvAttr, nullptr, false, true, false, false);

    DepthTextureUniforms uniforms{};
    Matrix4 mvp;
    computeMVP(camera, mesh, mvp);
    copyMatrix(mvp, uniforms.mvp);
    uniforms.cameraNear = uniformFloat(depthMaterial->uniforms, "cameraNear", camera.nearPlane);
    uniforms.cameraFar = uniformFloat(depthMaterial->uniforms, "cameraFar", camera.farPlane);
    uniforms.flipUv = uniformFloat(depthMaterial->uniforms, "flipUv", 0.f);
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    struct ResolvedTexture {
        id<MTLTexture> texture = nil;
        id<MTLSamplerState> sampler = nil;
    };

    auto resolveUniformTexture = [&](Texture* texture, id<MTLTexture> fallback, const char* uniformName) {
        ResolvedTexture resolved{fallback, defaultSampler};
        if (!texture) return resolved;

        try {
            if (auto metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, true)) {
                resolved.texture = metalTexture;
                resolved.sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
            }
        } catch (const std::exception& e) {
            std::cerr << "MetalRenderer: failed to bind depth texture uniform '" << uniformName << "': " << e.what() << "\n";
        }
        return resolved;
    };

    auto* diffuseUniform = uniformTexture(depthMaterial->uniforms, "tDiffuse");
    const auto diffuse = resolveUniformTexture(diffuseUniform, whiteTexture, "tDiffuse");
    [encoder setFragmentTexture:diffuse.texture atIndex:0];
    [encoder setFragmentSamplerState:diffuse.sampler atIndex:0];

    auto* depthUniform = uniformTexture(depthMaterial->uniforms, "tDepth");
    const auto depth = resolveUniformTexture(depthUniform, whiteDepthTexture, "tDepth");
    id<MTLTexture> depthTexture = depth.texture;
    id<MTLSamplerState> depthSampler = depth.sampler;
    [encoder setFragmentTexture:depthTexture atIndex:1];
    [encoder setFragmentSamplerState:depthSampler atIndex:1];

    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle, 1, group);
}

void MetalRenderer::Impl::renderLinearDepthTexture(id<MTLRenderCommandEncoder> encoder,
                                                   Mesh& mesh,
                                                   BufferGeometry& geometry,
                                                   ShaderMaterial& material,
                                                   Camera& camera,
                                                   MTLPixelFormat colorPixelFormat,
                                                   std::optional<GeometryGroup> group) {
    auto* depthMaterial = &material;
    if (!depthMaterial->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    auto* uvAttr = getFloatAttribute(geometry, "uv");
    if (!posAttr || posAttr->itemSize() != 3 || !uvAttr || uvAttr->itemSize() != 2) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateDepthTextureVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateDepthTextureLinearReadbackFragmentFunction();
    configurePipelineBlending(pipelineKey, *depthMaterial);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutUv;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
    pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

    id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
    [encoder setRenderPipelineState:pso];
    id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(false, false, DepthFunc::LessEqual);
    [encoder setDepthStencilState:materialDepthStencilState];

    const auto frontFaceCW = mesh.matrixWorld->determinant() < 0;
    const auto faceCullingState = metal::computeFaceCullingState(depthMaterial->side, frontFaceCW, false);
    [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];
    applyDepthBias(encoder, *depthMaterial);

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, uvAttr, nullptr, false, true, false, false);

    DepthTextureUniforms uniforms{};
    Matrix4 mvp;
    computeMVP(camera, mesh, mvp);
    copyMatrix(mvp, uniforms.mvp);
    uniforms.cameraNear = uniformFloat(depthMaterial->uniforms, "cameraNear", camera.nearPlane);
    uniforms.cameraFar = uniformFloat(depthMaterial->uniforms, "cameraFar", camera.farPlane);
    uniforms.flipUv = uniformFloat(depthMaterial->uniforms, "flipUv", 0.f);
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    id<MTLTexture> depthTexture = whiteDepthTexture;
    id<MTLSamplerState> depthSampler = defaultSampler;
    if (auto* depthUniform = uniformTexture(depthMaterial->uniforms, "tDepth")) {
        try {
            if (auto metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*depthUniform, true)) {
                depthTexture = metalTexture;
                depthSampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*depthUniform);
            }
        } catch (const std::exception& e) {
            std::cerr << "MetalRenderer: failed to bind depth texture uniform 'tDepth': " << e.what() << "\n";
        }
    }
    [encoder setFragmentTexture:depthTexture atIndex:1];
    [encoder setFragmentSamplerState:depthSampler atIndex:1];

    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle, 1, group);
}

void MetalRenderer::Impl::renderSprite(id<MTLRenderCommandEncoder> encoder, Scene& scene, Sprite& sprite, BufferGeometry& geometry, Material& itemMaterial, Camera& camera, MTLPixelFormat colorPixelFormat) {
    auto* material = itemMaterial.as<SpriteMaterial>();
    if (!material || !material->visible) return;
    trackGeometry(geometry);

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

    metal::SpriteShaderKey shaderKey;
    shaderKey.useSizeAttenuation = material->sizeAttenuation;
    shaderKey.useAlphaMap = static_cast<bool>(material->alphaMap);
    shaderKey.useAlphaTest = material->alphaTest > 0.f;
    shaderKey.useFog = material->fog && scene.fog.has_value();

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateSpriteVertexFunction(shaderKey);
    pipelineKey.fragmentFunction = shaderManager->getOrCreateSpriteFragmentFunction(shaderKey);
    configurePipelineBlending(pipelineKey, *material);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition | vertexLayoutUv;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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
    fillToneMappingUniforms(renderer, *material, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));
    uniforms.outputColorSpaceSRGB = renderer.outputColorSpace == ColorSpace::sRGB || renderer.outputColorSpace == ColorSpace::Gamma ? 1u : 0u;
    fillFogUniforms(scene, *material, uniforms);
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    bindTextureOrPlaceholder(encoder, material->map, whiteTexture, 0);
    if (material->alphaMap) {
        bindTextureOrPlaceholder(encoder, material->alphaMap, whiteTexture, 1);
        [encoder setFragmentSamplerState:samplerForTexture(material->alphaMap.get()) atIndex:1];
    }

    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

void MetalRenderer::Impl::renderSky(id<MTLRenderCommandEncoder> encoder, Sky& sky, BufferGeometry& geometry, Material& itemMaterial, Camera& camera, MTLPixelFormat colorPixelFormat) {
    auto* material = itemMaterial.as<ShaderMaterial>();
    if (!material || !material->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    if (!posAttr) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateSkyVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateSkyFragmentFunction();
    configurePipelineBlending(pipelineKey, *material);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

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
    fillToneMappingUniforms(renderer, *material, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));

    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle);
}

void MetalRenderer::Impl::renderWater(id<MTLRenderCommandEncoder> encoder, Scene& scene, Water& water, BufferGeometry& geometry, Material& itemMaterial, Camera& camera, MTLPixelFormat colorPixelFormat) {
    auto* material = itemMaterial.as<ShaderMaterial>();
    if (!material || !material->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    if (!posAttr) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateWaterVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateWaterFragmentFunction();
    configurePipelineBlending(pipelineKey, *material);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

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
    fillToneMappingUniforms(renderer, *material, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));
    uniforms.outputColorSpaceSRGB = renderer.outputColorSpace == ColorSpace::sRGB || renderer.outputColorSpace == ColorSpace::Gamma ? 1u : 0u;
    fillFogUniforms(scene, *material, uniforms);

    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    auto normalSamplerTexture = uniformTexture(material->uniforms, "normalSampler");
    auto mirrorSamplerTexture = uniformTexture(material->uniforms, "mirrorSampler");

    bindTextureOrPlaceholder(encoder, normalSamplerTexture, normalTexture, 0);
    bindTextureOrPlaceholder(encoder, mirrorSamplerTexture, whiteTexture, 1, true);
    [encoder setFragmentSamplerState:samplerForTexture(normalSamplerTexture) atIndex:0];
    [encoder setFragmentSamplerState:samplerForTexture(mirrorSamplerTexture) atIndex:1];

    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle);
}

void MetalRenderer::Impl::renderReflector(id<MTLRenderCommandEncoder> encoder, Scene&, Reflector& reflector, BufferGeometry& geometry, Material& itemMaterial, Camera& camera, MTLPixelFormat colorPixelFormat) {
    auto* material = itemMaterial.as<ShaderMaterial>();
    if (!material || !material->visible) return;
    trackGeometry(geometry);

    auto* posAttr = getFloatAttribute(geometry, "position");
    if (!posAttr) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateReflectorVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateReflectorFragmentFunction();
    configurePipelineBlending(pipelineKey, *material);
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    configurePipelineColorFormats(pipelineKey, colorPixelFormat);
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

    bindDrawAttributes(encoder, geometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

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

    fillToneMappingUniforms(renderer, *material, uniforms, needsShaderOutputSRGBEncoding(activeOutputColorSpace, colorPixelFormat));

    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    auto mirrorSamplerTexture = uniformTexture(material->uniforms, "tDiffuse");
    bindTextureOrPlaceholder(encoder, mirrorSamplerTexture, whiteTexture, 0, true);

    drawGeometry(encoder, geometry, *posAttr, MTLPrimitiveTypeTriangle);
}
