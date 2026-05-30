
#import "MetalBufferManager.hpp"

#import "threepp/core/BufferAttribute.hpp"

#import <Metal/Metal.h>

#include <unordered_map>

namespace threepp::metal {

    struct MetalBufferManager::Impl {

        id<MTLDevice> device;

        struct CachedBuffer {
            id<MTLBuffer> mtlBuffer;
            unsigned int lastVersion = 0;
        };

        std::unordered_map<BufferAttribute*, CachedBuffer> cache;
        std::unordered_map<const void*, id<MTLBuffer>> dynamicCache;

        explicit Impl(id<MTLDevice> dev)
            : device(dev) {}

        id<MTLBuffer> getBuffer(BufferAttribute& attribute, size_t byteSize, const void* data) {
            auto it = cache.find(&attribute);
            if (it != cache.end()) {
                auto& cached = it->second;
                if (cached.lastVersion != attribute.version) {
                    if (byteSize > 0) {
                        if (byteSize > cached.mtlBuffer.length) {
                            cached.mtlBuffer = [device newBufferWithBytes:data
                                                                   length:byteSize
                                                                  options:MTLResourceStorageModeShared];
                        } else {
                            memcpy(cached.mtlBuffer.contents, data, byteSize);
                        }
                    }
                    cached.lastVersion = attribute.version;
                }
                return cached.mtlBuffer;
            }

            CachedBuffer cb;
            cb.mtlBuffer = [device newBufferWithBytes:data
                                               length:byteSize
                                              options:MTLResourceStorageModeShared];
            cb.lastVersion = attribute.version;
            cache[&attribute] = cb;
            return cb.mtlBuffer;
        }

        id<MTLBuffer> getDynamicBuffer(const void* key, size_t byteSize, const void* data) {
            auto it = dynamicCache.find(key);
            if (it != dynamicCache.end()) {
                if (byteSize > it->second.length) {
                    it->second = [device newBufferWithBytes:data
                                                     length:byteSize
                                                    options:MTLResourceStorageModeShared];
                } else if (byteSize > 0) {
                    memcpy(it->second.contents, data, byteSize);
                }
                return it->second;
            }

            id<MTLBuffer> buffer = [device newBufferWithBytes:data
                                                       length:byteSize
                                                      options:MTLResourceStorageModeShared];
            dynamicCache[key] = buffer;
            return buffer;
        }
    };

    MetalBufferManager::MetalBufferManager(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device)) {}

    MetalBufferManager::~MetalBufferManager() = default;

    void* MetalBufferManager::getBuffer(BufferAttribute& attribute, size_t byteSize, const void* data) {
        return (__bridge void*) pimpl_->getBuffer(attribute, byteSize, data);
    }

    void* MetalBufferManager::getDynamicBuffer(const void* key, size_t byteSize, const void* data) {
        return (__bridge void*) pimpl_->getDynamicBuffer(key, byteSize, data);
    }

}// namespace threepp::metal
