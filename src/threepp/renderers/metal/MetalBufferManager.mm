
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
    };

    MetalBufferManager::MetalBufferManager(void* device)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>)device)) {}

    MetalBufferManager::~MetalBufferManager() = default;

    void* MetalBufferManager::getBuffer(BufferAttribute& attribute, size_t byteSize, const void* data) {
        return (__bridge void*)pimpl_->getBuffer(attribute, byteSize, data);
    }

}// namespace threepp::metal
