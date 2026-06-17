
#import "MetalPipelineCache.hpp"

#import <Metal/Metal.h>

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace threepp::metal {

    namespace {

        constexpr std::uint16_t VertexLayoutPosition = 1u << 0u;
        constexpr std::uint16_t VertexLayoutNormal = 1u << 1u;
        constexpr std::uint16_t VertexLayoutUv = 1u << 2u;
        constexpr std::uint16_t VertexLayoutColor = 1u << 3u;
        constexpr std::uint16_t VertexLayoutTangent = 1u << 4u;
        constexpr std::uint16_t VertexLayoutSkinning = 1u << 5u;
        constexpr std::uint16_t VertexLayoutColor4 = 1u << 6u;
        constexpr std::uint16_t VertexLayoutMorphTargets = 1u << 7u;
        constexpr std::uint16_t VertexLayoutMorphNormals = 1u << 8u;
        constexpr std::uint16_t VertexLayoutParticleSystem = 1u << 9u;

    }// namespace

    bool PipelineKey::operator==(const PipelineKey& other) const {
        if (vertexFunction != other.vertexFunction ||
            fragmentFunction != other.fragmentFunction ||
            alphaBlending != other.alphaBlending ||
            vertexLayoutBitmask != other.vertexLayoutBitmask ||
            colorPixelFormat != other.colorPixelFormat ||
            colorAttachmentCount != other.colorAttachmentCount ||
            colorPixelFormats != other.colorPixelFormats ||
            rasterSampleCount != other.rasterSampleCount) {
            return false;
        }

        if (!alphaBlending) return true;

        return blending == other.blending &&
               blendEquation == other.blendEquation &&
               blendEquationAlpha == other.blendEquationAlpha &&
               blendSrc == other.blendSrc &&
               blendDst == other.blendDst &&
               blendSrcAlpha == other.blendSrcAlpha &&
               blendDstAlpha == other.blendDstAlpha;
    }

    size_t PipelineKeyHash::operator()(const PipelineKey& key) const {
        auto h1 = std::hash<void*>{}(key.vertexFunction);
        auto h2 = std::hash<void*>{}(key.fragmentFunction);
        auto h3 = std::hash<bool>{}(key.alphaBlending);
        auto h4 = std::hash<std::uint16_t>{}(key.vertexLayoutBitmask);
        auto h5 = std::hash<std::uint64_t>{}(key.colorPixelFormat);
        auto h6 = std::hash<std::uint64_t>{}(key.colorAttachmentCount);
        auto h7 = std::hash<std::uint64_t>{}(key.rasterSampleCount);
        auto hash = h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5) ^ (h7 << 6);
        const auto colorAttachmentCount = std::min<std::uint64_t>(key.colorAttachmentCount, key.colorPixelFormats.size());
        for (std::uint64_t i = 0; i < colorAttachmentCount; ++i) {
            const auto format = key.colorPixelFormats[static_cast<std::size_t>(i)] != 0
                ? key.colorPixelFormats[static_cast<std::size_t>(i)]
                : key.colorPixelFormat;
            hash ^= std::hash<std::uint64_t>{}(format) << ((i % 8u) + 7u);
        }

        if (!key.alphaBlending) return hash;

        auto h8 = std::hash<int>{}(static_cast<int>(key.blending));
        auto h9 = std::hash<int>{}(static_cast<int>(key.blendEquation));
        auto h10 = std::hash<int>{}(static_cast<int>(key.blendEquationAlpha));
        auto h11 = std::hash<int>{}(static_cast<int>(key.blendSrc));
        auto h12 = std::hash<int>{}(static_cast<int>(key.blendDst));
        auto h13 = std::hash<int>{}(static_cast<int>(key.blendSrcAlpha));
        auto h14 = std::hash<int>{}(static_cast<int>(key.blendDstAlpha));
        return hash ^ (h8 << 7) ^ (h9 << 8) ^ (h10 << 9) ^ (h11 << 10) ^ (h12 << 11) ^
               (h13 << 12) ^ (h14 << 13);
    }

    namespace {

        struct DepthPipelineKey {
            void* vertexFunction = nullptr;
            void* fragmentFunction = nullptr;
            std::uint16_t vertexLayoutBitmask = VertexLayoutPosition;

            bool operator==(const DepthPipelineKey& other) const {
                return vertexFunction == other.vertexFunction &&
                       fragmentFunction == other.fragmentFunction &&
                       vertexLayoutBitmask == other.vertexLayoutBitmask;
            }
        };

        struct DepthPipelineKeyHash {
            std::size_t operator()(const DepthPipelineKey& key) const {
                auto h1 = std::hash<void*>{}(key.vertexFunction);
                auto h2 = std::hash<void*>{}(key.fragmentFunction);
                auto h3 = std::hash<std::uint16_t>{}(key.vertexLayoutBitmask);
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

        struct DepthStencilKey {
            bool depthTest = true;
            bool depthWrite = true;
            DepthFunc depthFunc = DepthFunc::LessEqual;

            bool operator==(const DepthStencilKey& other) const {
                return depthTest == other.depthTest && depthWrite == other.depthWrite && depthFunc == other.depthFunc;
            }
        };

        struct DepthStencilKeyHash {
            std::size_t operator()(const DepthStencilKey& key) const {
                auto h1 = std::hash<bool>{}(key.depthTest);
                auto h2 = std::hash<bool>{}(key.depthWrite);
                auto h3 = std::hash<int>{}(static_cast<int>(key.depthFunc));
                return h1 ^ (h2 << 1) ^ (h3 << 2);
            }
        };

        MTLCompareFunction toMetalCompareFunction(DepthFunc depthFunc) {
            switch (depthFunc) {
                case DepthFunc::Never:
                    return MTLCompareFunctionNever;
                case DepthFunc::Always:
                    return MTLCompareFunctionAlways;
                case DepthFunc::Less:
                    return MTLCompareFunctionLess;
                case DepthFunc::LessEqual:
                    return MTLCompareFunctionLessEqual;
                case DepthFunc::Equal:
                    return MTLCompareFunctionEqual;
                case DepthFunc::GreaterEqual:
                    return MTLCompareFunctionGreaterEqual;
                case DepthFunc::Greater:
                    return MTLCompareFunctionGreater;
                case DepthFunc::NotEqual:
                    return MTLCompareFunctionNotEqual;
            }

            return MTLCompareFunctionLessEqual;
        }

        MTLBlendOperation toMetalBlendOperation(BlendEquation equation) {
            switch (equation) {
                case BlendEquation::Add:
                    return MTLBlendOperationAdd;
                case BlendEquation::Subtract:
                    return MTLBlendOperationSubtract;
                case BlendEquation::ReverseSubtract:
                    return MTLBlendOperationReverseSubtract;
                case BlendEquation::Min:
                    return MTLBlendOperationMin;
                case BlendEquation::Max:
                    return MTLBlendOperationMax;
            }

            return MTLBlendOperationAdd;
        }

        MTLBlendFactor toMetalBlendFactor(BlendFactor factor) {
            switch (factor) {
                case BlendFactor::Zero:
                    return MTLBlendFactorZero;
                case BlendFactor::One:
                    return MTLBlendFactorOne;
                case BlendFactor::SrcColor:
                    return MTLBlendFactorSourceColor;
                case BlendFactor::OneMinusSrcColor:
                    return MTLBlendFactorOneMinusSourceColor;
                case BlendFactor::SrcAlpha:
                    return MTLBlendFactorSourceAlpha;
                case BlendFactor::OneMinusSrcAlpha:
                    return MTLBlendFactorOneMinusSourceAlpha;
                case BlendFactor::DstAlpha:
                    return MTLBlendFactorDestinationAlpha;
                case BlendFactor::OneMinusDstAlpha:
                    return MTLBlendFactorOneMinusDestinationAlpha;
                case BlendFactor::DstColor:
                    return MTLBlendFactorDestinationColor;
                case BlendFactor::OneMinusDstColor:
                    return MTLBlendFactorOneMinusDestinationColor;
                case BlendFactor::SrcAlphaSaturate:
                    return MTLBlendFactorSourceAlphaSaturated;
            }

            return MTLBlendFactorOne;
        }

        void enableAttribute(MTLVertexDescriptor* descriptor, NSUInteger index, MTLVertexFormat format, NSUInteger stride, NSUInteger bufferIndex) {
            descriptor.attributes[index].format = format;
            descriptor.attributes[index].offset = 0;
            descriptor.attributes[index].bufferIndex = bufferIndex;
            descriptor.layouts[bufferIndex].stride = stride;
            descriptor.layouts[bufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
        }

        void enableAttribute(MTLVertexDescriptor* descriptor, NSUInteger index, MTLVertexFormat format, NSUInteger stride) {
            enableAttribute(descriptor, index, format, stride, index);
        }

        MTLVertexDescriptor* createVertexDescriptor(std::uint16_t bitmask) {
            auto* descriptor = [[MTLVertexDescriptor alloc] init];

            enableAttribute(descriptor, 0, MTLVertexFormatFloat3, sizeof(float) * 3);

            if ((bitmask & VertexLayoutNormal) != 0) {
                enableAttribute(descriptor, 1, MTLVertexFormatFloat3, sizeof(float) * 3);
            }

            if ((bitmask & VertexLayoutUv) != 0) {
                enableAttribute(descriptor, 2, MTLVertexFormatFloat2, sizeof(float) * 2);
            }

            if ((bitmask & VertexLayoutColor4) != 0) {
                enableAttribute(descriptor, 3, MTLVertexFormatFloat4, sizeof(float) * 4);
            } else if ((bitmask & VertexLayoutColor) != 0) {
                enableAttribute(descriptor, 3, MTLVertexFormatFloat3, sizeof(float) * 3);
            }

            if ((bitmask & VertexLayoutSkinning) != 0) {
                enableAttribute(descriptor, 4, MTLVertexFormatFloat4, sizeof(float) * 4, 6);
                enableAttribute(descriptor, 5, MTLVertexFormatFloat4, sizeof(float) * 4, 7);
            }

            if ((bitmask & VertexLayoutTangent) != 0) {
                enableAttribute(descriptor, 6, MTLVertexFormatFloat4, sizeof(float) * 4, 8);
            }

            if ((bitmask & VertexLayoutMorphTargets) != 0) {
                enableAttribute(descriptor, 7, MTLVertexFormatFloat3, sizeof(float) * 3, 11);
                enableAttribute(descriptor, 8, MTLVertexFormatFloat3, sizeof(float) * 3, 12);
                enableAttribute(descriptor, 9, MTLVertexFormatFloat3, sizeof(float) * 3, 13);
                enableAttribute(descriptor, 10, MTLVertexFormatFloat3, sizeof(float) * 3, 14);

                enableAttribute(descriptor, 11, MTLVertexFormatFloat3, sizeof(float) * 3, 15);
                enableAttribute(descriptor, 12, MTLVertexFormatFloat3, sizeof(float) * 3, 16);
                enableAttribute(descriptor, 13, MTLVertexFormatFloat3, sizeof(float) * 3, 17);
                enableAttribute(descriptor, 14, MTLVertexFormatFloat3, sizeof(float) * 3, 18);
            }

            if ((bitmask & VertexLayoutParticleSystem) != 0) {
                enableAttribute(descriptor, 1, MTLVertexFormatFloat, sizeof(float));
                enableAttribute(descriptor, 2, MTLVertexFormatFloat, sizeof(float));
                enableAttribute(descriptor, 3, MTLVertexFormatFloat, sizeof(float));
                enableAttribute(descriptor, 4, MTLVertexFormatFloat3, sizeof(float) * 3);
                enableAttribute(descriptor, 5, MTLVertexFormatFloat, sizeof(float));
            }

            return descriptor;
        }

    }// namespace

    struct MetalPipelineCache::Impl {
        id<MTLDevice> device;
        std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> pipelineStates;
        std::unordered_set<PipelineKey, PipelineKeyHash> pendingPipelineStates;
        std::unordered_set<PipelineKey, PipelineKeyHash> cancelledPipelineStates;
        std::unordered_set<PipelineKey, PipelineKeyHash> failedPipelineStates;
        std::unordered_map<DepthPipelineKey, id<MTLRenderPipelineState>, DepthPipelineKeyHash> depthOnlyPipelineStates;
        std::unordered_map<DepthStencilKey, id<MTLDepthStencilState>, DepthStencilKeyHash> depthStencilStates;
        std::mutex mutex;
        std::condition_variable condition;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        ~Impl() {
            std::unique_lock lock(mutex);
            condition.wait(lock, [this] {
                return pendingPipelineStates.empty();
            });
        }

        MTLRenderPipelineDescriptor* createPipelineDescriptor(const PipelineKey& key) const {
            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = (__bridge id<MTLFunction>) key.vertexFunction;
            desc.fragmentFunction = (__bridge id<MTLFunction>) key.fragmentFunction;
            desc.vertexDescriptor = createVertexDescriptor(key.vertexLayoutBitmask);

            const auto colorAttachmentCount = std::min<std::uint64_t>(
                    std::max<std::uint64_t>(key.colorAttachmentCount, 1),
                    key.colorPixelFormats.size());
            for (NSUInteger i = 0; i < static_cast<NSUInteger>(colorAttachmentCount); ++i) {
                const auto format = key.colorPixelFormats[static_cast<std::size_t>(i)] != 0
                    ? key.colorPixelFormats[static_cast<std::size_t>(i)]
                    : key.colorPixelFormat;
                desc.colorAttachments[i].pixelFormat = static_cast<MTLPixelFormat>(format);
            }
            desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            desc.rasterSampleCount = static_cast<NSUInteger>(std::max<std::uint64_t>(key.rasterSampleCount, 1));

            if (key.alphaBlending) {
                for (NSUInteger i = 0; i < static_cast<NSUInteger>(colorAttachmentCount); ++i) {
                    desc.colorAttachments[i].blendingEnabled = YES;
                    desc.colorAttachments[i].rgbBlendOperation = toMetalBlendOperation(key.blendEquation);
                    desc.colorAttachments[i].alphaBlendOperation = toMetalBlendOperation(key.blendEquationAlpha);
                    desc.colorAttachments[i].sourceRGBBlendFactor = toMetalBlendFactor(key.blendSrc);
                    desc.colorAttachments[i].sourceAlphaBlendFactor = toMetalBlendFactor(key.blendSrcAlpha);
                    desc.colorAttachments[i].destinationRGBBlendFactor = toMetalBlendFactor(key.blendDst);
                    desc.colorAttachments[i].destinationAlphaBlendFactor = toMetalBlendFactor(key.blendDstAlpha);
                }
            } else {
                for (NSUInteger i = 0; i < static_cast<NSUInteger>(colorAttachmentCount); ++i) {
                    desc.colorAttachments[i].blendingEnabled = NO;
                }
            }

            return desc;
        }

        id<MTLRenderPipelineState> getOrCreatePipelineState(const PipelineKey& key) {
            std::lock_guard lock(mutex);

            auto it = pipelineStates.find(key);
            if (it != pipelineStates.end()) {
                return it->second;
            }

            MTLRenderPipelineDescriptor* desc = createPipelineDescriptor(key);

            NSError* error = nil;
            id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!pso) {
                NSString* msg = [NSString stringWithFormat:@"Failed to create PSO: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            pipelineStates[key] = pso;
            failedPipelineStates.erase(key);
            if (pendingPipelineStates.erase(key) > 0) {
                cancelledPipelineStates.insert(key);
                condition.notify_all();
            }
            return pso;
        }

        PipelinePrewarmStatus prewarmPipelineState(const PipelineKey& key) {
            {
                std::lock_guard lock(mutex);
                if (pipelineStates.find(key) != pipelineStates.end()) {
                    return PipelinePrewarmStatus::Ready;
                }
                if (failedPipelineStates.find(key) != failedPipelineStates.end()) {
                    return PipelinePrewarmStatus::Failed;
                }
                if (pendingPipelineStates.find(key) != pendingPipelineStates.end()) {
                    return PipelinePrewarmStatus::Compiling;
                }
                cancelledPipelineStates.erase(key);
                pendingPipelineStates.insert(key);
            }

            MTLRenderPipelineDescriptor* desc = createPipelineDescriptor(key);
            [device newRenderPipelineStateWithDescriptor:desc
                                       completionHandler:^(id<MTLRenderPipelineState> pso, NSError* error) {
                std::lock_guard lock(mutex);
                const auto wasCancelled = cancelledPipelineStates.erase(key) > 0;
                pendingPipelineStates.erase(key);
                if (wasCancelled) {
                    condition.notify_all();
                    return;
                }
                if (pso) {
                    pipelineStates[key] = [pso retain];
                    failedPipelineStates.erase(key);
                } else {
                    failedPipelineStates.insert(key);
                    NSString* message = error ? error.localizedDescription : @"unknown error";
                    std::cerr << "MetalRenderer: async PSO prewarm failed: " << [message UTF8String] << "\n";
                }
                condition.notify_all();
            }];
            return PipelinePrewarmStatus::Compiling;
        }

        id<MTLRenderPipelineState> getOrCreateDepthOnlyPipelineState(void* vertexFunction, void* fragmentFunction, std::uint16_t vertexLayoutBitmask) {
            std::lock_guard lock(mutex);
            const DepthPipelineKey key{vertexFunction, fragmentFunction, vertexLayoutBitmask};
            auto it = depthOnlyPipelineStates.find(key);
            if (it != depthOnlyPipelineStates.end()) {
                return it->second;
            }

            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = (__bridge id<MTLFunction>) vertexFunction;
            desc.fragmentFunction = (__bridge id<MTLFunction>) fragmentFunction;
            desc.vertexDescriptor = createVertexDescriptor(vertexLayoutBitmask);
            desc.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid;
            desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            NSError* error = nil;
            id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!pso) {
                NSString* msg = [NSString stringWithFormat:@"Failed to create depth-only PSO: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            depthOnlyPipelineStates[key] = pso;
            return pso;
        }

        id<MTLDepthStencilState> getOrCreateDepthStencilState(bool depthTest, bool depthWrite, DepthFunc depthFunc) {
            std::lock_guard lock(mutex);
            const DepthStencilKey key{depthTest, depthWrite, depthFunc};
            auto it = depthStencilStates.find(key);
            if (it != depthStencilStates.end()) {
                return it->second;
            }

            MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
            desc.depthCompareFunction = depthTest ? toMetalCompareFunction(depthFunc) : MTLCompareFunctionAlways;
            desc.depthWriteEnabled = depthTest && depthWrite ? YES : NO;

            id<MTLDepthStencilState> state = [device newDepthStencilStateWithDescriptor:desc];
            depthStencilStates[key] = state;
            return state;
        }

        void removePipelineStatesReferencing(void* function) {
            std::lock_guard lock(mutex);
            for (auto it = pipelineStates.begin(); it != pipelineStates.end();) {
                if (it->first.vertexFunction == function || it->first.fragmentFunction == function) {
                    it = pipelineStates.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto it = depthOnlyPipelineStates.begin(); it != depthOnlyPipelineStates.end();) {
                if (it->first.vertexFunction == function || it->first.fragmentFunction == function) {
                    it = depthOnlyPipelineStates.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto it = failedPipelineStates.begin(); it != failedPipelineStates.end();) {
                if (it->vertexFunction == function || it->fragmentFunction == function) {
                    it = failedPipelineStates.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& key : pendingPipelineStates) {
                if (key.vertexFunction == function || key.fragmentFunction == function) {
                    cancelledPipelineStates.insert(key);
                }
            }
            condition.notify_all();
        }
    };

    MetalPipelineCache::MetalPipelineCache(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device)) {}

    MetalPipelineCache::~MetalPipelineCache() = default;

    void* MetalPipelineCache::getOrCreatePipelineState(const PipelineKey& key) {
        return (__bridge void*) pimpl_->getOrCreatePipelineState(key);
    }

    PipelinePrewarmStatus MetalPipelineCache::prewarmPipelineState(const PipelineKey& key) {
        return pimpl_->prewarmPipelineState(key);
    }

    void* MetalPipelineCache::getOrCreateDepthOnlyPipelineState(void* vertexFunction, std::uint16_t vertexLayoutBitmask) {
        return getOrCreateDepthOnlyPipelineState(vertexFunction, nullptr, vertexLayoutBitmask);
    }

    void* MetalPipelineCache::getOrCreateDepthOnlyPipelineState(void* vertexFunction, void* fragmentFunction, std::uint16_t vertexLayoutBitmask) {
        return (__bridge void*) pimpl_->getOrCreateDepthOnlyPipelineState(vertexFunction, fragmentFunction, vertexLayoutBitmask);
    }

    void* MetalPipelineCache::getOrCreateDepthStencilState() {
        return getOrCreateDepthStencilState(true, true, DepthFunc::LessEqual);
    }

    void* MetalPipelineCache::getOrCreateDepthStencilState(bool depthTest, bool depthWrite, DepthFunc depthFunc) {
        return (__bridge void*) pimpl_->getOrCreateDepthStencilState(depthTest, depthWrite, depthFunc);
    }

    void MetalPipelineCache::removePipelineStatesReferencing(void* function) {
        pimpl_->removePipelineStatesReferencing(function);
    }

}// namespace threepp::metal
