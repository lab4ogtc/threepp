
#import "MetalPipelineCache.hpp"

#import <Metal/Metal.h>

#include <unordered_map>

namespace threepp::metal {

    bool PipelineKey::operator==(const PipelineKey& other) const {
        return vertexFunction == other.vertexFunction
            && fragmentFunction == other.fragmentFunction
            && alphaBlending == other.alphaBlending;
    }

    size_t PipelineKeyHash::operator()(const PipelineKey& key) const {
        auto h1 = std::hash<void*>{}(key.vertexFunction);
        auto h2 = std::hash<void*>{}(key.fragmentFunction);
        auto h3 = std::hash<bool>{}(key.alphaBlending);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }

    struct MetalPipelineCache::Impl {
        id<MTLDevice> device;
        std::unordered_map<PipelineKey, id<MTLRenderPipelineState>, PipelineKeyHash> pipelineStates;
        id<MTLDepthStencilState> depthStencilState = nil;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        id<MTLRenderPipelineState> getOrCreatePipelineState(
            const PipelineKey& key,
            MTLVertexDescriptor* vertexDescriptor) {

            auto it = pipelineStates.find(key);
            if (it != pipelineStates.end()) {
                return it->second;
            }

            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction = (__bridge id<MTLFunction>)key.vertexFunction;
            desc.fragmentFunction = (__bridge id<MTLFunction>)key.fragmentFunction;
            desc.vertexDescriptor = vertexDescriptor;

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

    void* MetalPipelineCache::getOrCreatePipelineState(const PipelineKey& key, void* vertexDescriptor) {
        return (__bridge void*)pimpl_->getOrCreatePipelineState(
            key, (__bridge MTLVertexDescriptor*)vertexDescriptor);
    }

    void* MetalPipelineCache::getOrCreateDepthStencilState() {
        return (__bridge void*)pimpl_->getOrCreateDepthStencilState();
    }

}// namespace threepp::metal
