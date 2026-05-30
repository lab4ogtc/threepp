#ifndef THREEPP_METAL_TEXTURE_MANAGER_HPP
#define THREEPP_METAL_TEXTURE_MANAGER_HPP

#include <memory>

namespace threepp {

    class Texture;

    namespace metal {

        class MetalTextureManager {

        public:
            explicit MetalTextureManager(void* device, void* commandQueue);

            ~MetalTextureManager();

            void* getOrCreateTexture(Texture& texture, bool allowPlaceholder = false);

            void* getOrCreateSampler(Texture& texture);

            void registerExternalTexture(Texture& texture, void* mtlTexture);

            void deallocateTexture(Texture* texture);

        private:
            struct Impl;
            std::unique_ptr<Impl> pimpl_;
        };

    }// namespace metal
}// namespace threepp

#endif
