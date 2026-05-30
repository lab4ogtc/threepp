
#ifndef THREEPP_METAL_BUFFER_MANAGER_HPP
#define THREEPP_METAL_BUFFER_MANAGER_HPP

#include <memory>

namespace threepp {

    class BufferAttribute;

    namespace metal {

        class MetalBufferManager {

        public:
            explicit MetalBufferManager(void* device);

            ~MetalBufferManager();

            void beginFrame();

            void* getBuffer(BufferAttribute& attribute, size_t byteSize, const void* data);

            void* getDynamicBuffer(const void* key, size_t byteSize, const void* data);

            void* getTransientBuffer(size_t byteSize, const void* data);

        private:
            struct Impl;
            std::unique_ptr<Impl> pimpl_;
        };

    }// namespace metal
}// namespace threepp

#endif
