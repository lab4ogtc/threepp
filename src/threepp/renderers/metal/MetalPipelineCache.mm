
#import "MetalPipelineCache.hpp"

#import <Metal/Metal.h>

#include <unordered_map>

namespace threepp::metal {

    bool PipelineKey::operator==(const PipelineKey& other) const {
        return vertexFunction == other.vertexFunction
            && fragmentFunction == other.fragmentFunction
            && alphaBlending == other.alphaBlending
            && vertexLayoutBitmask == other.vertexLayoutBitmask;
    }

    size_t PipelineKeyHash::operator()(const PipelineKey& key) const {
        auto h1 = std::hash<void*>{}(key.vertexFunction);
        auto h2 = std::hash<void*>{}(key.fragmentFunction);
        auto h3 = std::hash<bool>{}(key.alphaBlending);
        auto h4 = std::hash<std::uint8_t>{}(key.vertexLayoutBitmask);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }

    namespace {

        void enableAttribute(MTLVertexDescriptor* descriptor, NSUInteger index, MTLVertexFormat format, NSUInteger stride) {
            descriptor.attributes[index].format = format;
            descriptor.attributes[index].offset = 0;
            descriptor.attributes[index].bufferIndex = index;
            descriptor.layouts[index].stride = stride;
            descriptor.layouts[index].stepFunction = MTLVertexStepFunctionPerVertex;
        }

        MTLVertexDescriptor* createVertexDescriptor(std::uint8_t bitmask) {
            auto* descriptor = [[MTLVertexDescriptor alloc] init];

            enableAttribute(descriptor, 0, MTLVertexFormatFloat3, sizeof(float) * 3);

            if ((bitmask & 0b0010) != 0) {
                enableAttribute(descriptor, 1, MTLVertexFormatFloat3, sizeof(float) * 3);
            }

            if ((bitmask & 0b0100) != 0) {
                enableAttribute(descriptor, 2, MTLVertexFormatFloat2, sizeof(float) * 2);
            }

            if ((bitmask & 0b1000) != 0) {
                enableAttribute(descriptor, 3, MTLVertexFormatFloat3, sizeof(float) * 3);
            }

            return descriptor;
        }

    }// namespace

    struct MetalPipelineCache::Impl {
        id<MTLDevice> device;
        std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> pipelineStates;
        id<MTLDepthStencilState> depthStencilState = nil;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        id<MTLRenderPipelineState> getOrCreatePipelineState(const PipelineKey& key) {

            auto it = pipelineStates.find(key);
            if (it != pipelineStates.end()) {
                return it->second;
            }

            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = (__bridge id<MTLFunction>)key.vertexFunction;
            desc.fragmentFunction = (__bridge id<MTLFunction>)key.fragmentFunction;
            desc.vertexDescriptor = createVertexDescriptor(key.vertexLayoutBitmask);

            desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
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

        id<MTLDepthStencilState> getOrCreateDepthStencilState() {
            if (depthStencilState) return depthStencilState;

            MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
            desc.depthCompareFunction = MTLCompareFunctionLessEqual;
            desc.depthWriteEnabled = YES;

            depthStencilState = [device newDepthStencilStateWithDescriptor:desc];
            return depthStencilState;
        }
    };

    MetalPipelineCache::MetalPipelineCache(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>)device)) {}

    MetalPipelineCache::~MetalPipelineCache() = default;

    void* MetalPipelineCache::getOrCreatePipelineState(const PipelineKey& key) {
        return (__bridge void*)pimpl_->getOrCreatePipelineState(key);
    }

    void* MetalPipelineCache::getOrCreateDepthStencilState() {
        return (__bridge void*)pimpl_->getOrCreateDepthStencilState();
    }

}// namespace threepp::metal
