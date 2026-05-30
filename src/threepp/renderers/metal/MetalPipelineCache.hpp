
#ifndef THREEPP_METAL_PIPELINE_CACHE_HPP
#define THREEPP_METAL_PIPELINE_CACHE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "threepp/constants.hpp"

namespace threepp::metal {

    struct PipelineKey {
        void* vertexFunction = nullptr;
        void* fragmentFunction = nullptr;
        bool alphaBlending = false;
        std::uint8_t vertexLayoutBitmask = 0b0001;
        std::uint64_t colorPixelFormat = 80;// MTLPixelFormatBGRA8Unorm

        bool operator==(const PipelineKey& other) const;
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& key) const;
    };

    class MetalPipelineCache {

    public:
        explicit MetalPipelineCache(void* device);

        ~MetalPipelineCache();

        void* getOrCreatePipelineState(const PipelineKey& key);

        void* getOrCreateDepthOnlyPipelineState(void* vertexFunction, std::uint8_t vertexLayoutBitmask);

        void* getOrCreateDepthStencilState();

        void* getOrCreateDepthStencilState(bool depthTest, bool depthWrite, DepthFunc depthFunc);

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp::metal

#endif
