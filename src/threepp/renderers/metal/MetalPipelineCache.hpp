
#ifndef THREEPP_METAL_PIPELINE_CACHE_HPP
#define THREEPP_METAL_PIPELINE_CACHE_HPP

#include <memory>

namespace threepp::metal {

    struct PipelineKey {
        void* vertexFunction = nullptr;
        void* fragmentFunction = nullptr;
        bool alphaBlending = false;

        bool operator==(const PipelineKey& other) const;
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& key) const;
    };

    class MetalPipelineCache {

    public:
        explicit MetalPipelineCache(void* device);

        ~MetalPipelineCache();

        void* getOrCreatePipelineState(const PipelineKey& key, void* vertexDescriptor);

        void* getOrCreateDepthStencilState();

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp::metal

#endif
