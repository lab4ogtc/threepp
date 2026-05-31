
#import "MetalBufferManager.hpp"

#import "threepp/core/BufferAttribute.hpp"

#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace threepp::metal {

    namespace {

        constexpr std::size_t maxFramesInFlight = 3;

        id<MTLBuffer> createSharedBuffer(id<MTLDevice> device, size_t byteSize, const void* data) {
            const auto length = static_cast<NSUInteger>(std::max<std::size_t>(byteSize, 1));
            if (byteSize > 0 && data) {
                return [device newBufferWithBytes:data
                                           length:length
                                          options:MTLResourceStorageModeShared];
            }
            return [device newBufferWithLength:length options:MTLResourceStorageModeShared];
        }

    }// namespace

    struct MetalBufferManager::Impl {

        id<MTLDevice> device;
        std::uint32_t frameIndex = 0;

        struct CachedBuffer {
            id<MTLBuffer> mtlBuffer;
            unsigned int attributeId = 0;
            unsigned int lastVersion = 0;
            size_t byteSize = 0;
        };

        std::unordered_map<BufferAttribute*, CachedBuffer> cache;
        std::unordered_map<const void*, std::array<id<MTLBuffer>, maxFramesInFlight>> dynamicCache;
        std::array<std::vector<id<MTLBuffer>>, maxFramesInFlight> transientBuffers;
        std::array<std::size_t, maxFramesInFlight> transientCursor{};

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        void beginFrame() {
            frameIndex = (frameIndex + 1u) % maxFramesInFlight;
            transientCursor[frameIndex] = 0;
        }

        id<MTLBuffer> getBuffer(BufferAttribute& attribute, size_t byteSize, const void* data) {
            auto it = cache.find(&attribute);
            if (it != cache.end()) {
                auto& cached = it->second;
                if (cached.attributeId != attribute.id || cached.lastVersion != attribute.version || cached.byteSize != byteSize) {
                    if (byteSize > 0) {
                        if (byteSize > cached.mtlBuffer.length) {
                            cached.mtlBuffer = createSharedBuffer(device, byteSize, data);
                        } else {
                            memcpy(cached.mtlBuffer.contents, data, byteSize);
                        }
                    }
                    cached.attributeId = attribute.id;
                    cached.lastVersion = attribute.version;
                    cached.byteSize = byteSize;
                }
                return cached.mtlBuffer;
            }

            CachedBuffer cb;
            cb.mtlBuffer = createSharedBuffer(device, byteSize, data);
            cb.attributeId = attribute.id;
            cb.lastVersion = attribute.version;
            cb.byteSize = byteSize;
            cache[&attribute] = cb;
            return cb.mtlBuffer;
        }

        void remove(BufferAttribute& attribute) {
            cache.erase(&attribute);
        }

        id<MTLBuffer> getDynamicBuffer(const void* key, size_t byteSize, const void* data) {
            auto& buffers = dynamicCache[key];
            auto& buffer = buffers[frameIndex];
            if (!buffer || byteSize > buffer.length) {
                buffer = createSharedBuffer(device, byteSize, data);
            } else if (byteSize > 0) {
                memcpy(buffer.contents, data, byteSize);
            }
            return buffer;
        }

        id<MTLBuffer> getTransientBuffer(size_t byteSize, const void* data) {
            auto& buffers = transientBuffers[frameIndex];
            auto& cursor = transientCursor[frameIndex];
            if (cursor == buffers.size()) {
                buffers.push_back(createSharedBuffer(device, byteSize, data));
                return buffers[cursor++];
            }

            auto& buffer = buffers[cursor++];
            if (!buffer || byteSize > buffer.length) {
                buffer = createSharedBuffer(device, byteSize, data);
            } else if (byteSize > 0) {
                memcpy(buffer.contents, data, byteSize);
            }
            return buffer;
        }
    };

    MetalBufferManager::MetalBufferManager(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device)) {}

    MetalBufferManager::~MetalBufferManager() = default;

    void MetalBufferManager::beginFrame() {
        pimpl_->beginFrame();
    }

    void* MetalBufferManager::getBuffer(BufferAttribute& attribute, size_t byteSize, const void* data) {
        return (__bridge void*) pimpl_->getBuffer(attribute, byteSize, data);
    }

    void MetalBufferManager::remove(BufferAttribute& attribute) {
        pimpl_->remove(attribute);
    }

    void* MetalBufferManager::getDynamicBuffer(const void* key, size_t byteSize, const void* data) {
        return (__bridge void*) pimpl_->getDynamicBuffer(key, byteSize, data);
    }

    void* MetalBufferManager::getTransientBuffer(size_t byteSize, const void* data) {
        return (__bridge void*) pimpl_->getTransientBuffer(byteSize, data);
    }

}// namespace threepp::metal
