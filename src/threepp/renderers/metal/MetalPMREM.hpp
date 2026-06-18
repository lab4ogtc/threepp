#ifndef THREEPP_METAL_PMREM_HPP
#define THREEPP_METAL_PMREM_HPP

#include <memory>

namespace threepp {

    class Texture;

    namespace metal {

        class MetalPMREM {

        public:
            explicit MetalPMREM(void* device, void* commandQueue);

            ~MetalPMREM();

            void* getOrCreate(Texture& texture, void* sourceTexture);

            void deallocateTexture(Texture* texture);

        private:
            struct Impl;
            std::unique_ptr<Impl> pimpl_;
        };

    }// namespace metal
}// namespace threepp

#endif//THREEPP_METAL_PMREM_HPP
