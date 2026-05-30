
#import "MetalPipelineCache.hpp"

#import <Metal/Metal.h>

#include <unordered_map>

namespace threepp::metal {

    namespace {

        constexpr std::uint8_t VertexLayoutPosition = 1u << 0u;
        constexpr std::uint8_t VertexLayoutNormal = 1u << 1u;
        constexpr std::uint8_t VertexLayoutUv = 1u << 2u;
        constexpr std::uint8_t VertexLayoutColor = 1u << 3u;
        constexpr std::uint8_t VertexLayoutTangent = 1u << 4u;
        constexpr std::uint8_t VertexLayoutSkinning = 1u << 5u;
        constexpr std::uint8_t VertexLayoutColor4 = 1u << 6u;

    }// namespace

    bool PipelineKey::operator==(const PipelineKey& other) const {
        return vertexFunction == other.vertexFunction &&
               fragmentFunction == other.fragmentFunction &&
               alphaBlending == other.alphaBlending &&
               vertexLayoutBitmask == other.vertexLayoutBitmask &&
               colorPixelFormat == other.colorPixelFormat;
    }

    size_t PipelineKeyHash::operator()(const PipelineKey& key) const {
        auto h1 = std::hash<void*>{}(key.vertexFunction);
        auto h2 = std::hash<void*>{}(key.fragmentFunction);
        auto h3 = std::hash<bool>{}(key.alphaBlending);
        auto h4 = std::hash<std::uint8_t>{}(key.vertexLayoutBitmask);
        auto h5 = std::hash<std::uint64_t>{}(key.colorPixelFormat);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
    }

    namespace {

        struct DepthPipelineKey {
            void* vertexFunction = nullptr;
            void* fragmentFunction = nullptr;
            std::uint8_t vertexLayoutBitmask = VertexLayoutPosition;

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
                auto h3 = std::hash<std::uint8_t>{}(key.vertexLayoutBitmask);
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

        MTLVertexDescriptor* createVertexDescriptor(std::uint8_t bitmask) {
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

            return descriptor;
        }

    }// namespace

    struct MetalPipelineCache::Impl {
        id<MTLDevice> device;
        std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> pipelineStates;
        std::unordered_map<DepthPipelineKey, id<MTLRenderPipelineState>, DepthPipelineKeyHash> depthOnlyPipelineStates;
        std::unordered_map<DepthStencilKey, id<MTLDepthStencilState>, DepthStencilKeyHash> depthStencilStates;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        id<MTLRenderPipelineState> getOrCreatePipelineState(const PipelineKey& key) {

            auto it = pipelineStates.find(key);
            if (it != pipelineStates.end()) {
                return it->second;
            }

            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = (__bridge id<MTLFunction>) key.vertexFunction;
            desc.fragmentFunction = (__bridge id<MTLFunction>) key.fragmentFunction;
            desc.vertexDescriptor = createVertexDescriptor(key.vertexLayoutBitmask);

            desc.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(key.colorPixelFormat);
            desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

            if (key.alphaBlending) {
                desc.colorAttachments[0].blendingEnabled = YES;
                desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
                desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
                desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
                desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            } else {
                desc.colorAttachments[0].blendingEnabled = NO;
            }

            NSError* error = nil;
            id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!pso) {
                NSString* msg = [NSString stringWithFormat:@"Failed to create PSO: %@", error.localizedDescription];
                throw std::runtime_error([msg UTF8String]);
            }

            pipelineStates[key] = pso;
            return pso;
        }

        id<MTLRenderPipelineState> getOrCreateDepthOnlyPipelineState(void* vertexFunction, void* fragmentFunction, std::uint8_t vertexLayoutBitmask) {
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
    };

    MetalPipelineCache::MetalPipelineCache(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device)) {}

    MetalPipelineCache::~MetalPipelineCache() = default;

    void* MetalPipelineCache::getOrCreatePipelineState(const PipelineKey& key) {
        return (__bridge void*) pimpl_->getOrCreatePipelineState(key);
    }

    void* MetalPipelineCache::getOrCreateDepthOnlyPipelineState(void* vertexFunction, std::uint8_t vertexLayoutBitmask) {
        return getOrCreateDepthOnlyPipelineState(vertexFunction, nullptr, vertexLayoutBitmask);
    }

    void* MetalPipelineCache::getOrCreateDepthOnlyPipelineState(void* vertexFunction, void* fragmentFunction, std::uint8_t vertexLayoutBitmask) {
        return (__bridge void*) pimpl_->getOrCreateDepthOnlyPipelineState(vertexFunction, fragmentFunction, vertexLayoutBitmask);
    }

    void* MetalPipelineCache::getOrCreateDepthStencilState() {
        return getOrCreateDepthStencilState(true, true, DepthFunc::LessEqual);
    }

    void* MetalPipelineCache::getOrCreateDepthStencilState(bool depthTest, bool depthWrite, DepthFunc depthFunc) {
        return (__bridge void*) pimpl_->getOrCreateDepthStencilState(depthTest, depthWrite, depthFunc);
    }

}// namespace threepp::metal
