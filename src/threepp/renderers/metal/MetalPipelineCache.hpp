
#ifndef THREEPP_METAL_PIPELINE_CACHE_HPP
#define THREEPP_METAL_PIPELINE_CACHE_HPP

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>

#include "threepp/constants.hpp"

namespace threepp::metal {

    struct PipelineKey {
        void* vertexFunction = nullptr;
        void* fragmentFunction = nullptr;
        bool alphaBlending = false;
        Blending blending = Blending::Normal;
        BlendEquation blendEquation = BlendEquation::Add;
        BlendEquation blendEquationAlpha = BlendEquation::Add;
        BlendFactor blendSrc = BlendFactor::SrcAlpha;
        BlendFactor blendDst = BlendFactor::OneMinusSrcAlpha;
        BlendFactor blendSrcAlpha = BlendFactor::One;
        BlendFactor blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
        std::uint16_t vertexLayoutBitmask = 0b0001;
        std::uint64_t colorPixelFormat = 80;// MTLPixelFormatBGRA8Unorm
        std::uint64_t colorAttachmentCount = 1;
        std::array<std::uint64_t, 8> colorPixelFormats{};
        std::uint64_t rasterSampleCount = 1;

        bool operator==(const PipelineKey& other) const;
    };

    struct PipelineKeyHash {
        size_t operator()(const PipelineKey& key) const;
    };

    enum class PipelinePrewarmStatus {
        Ready,
        Compiling,
        Failed
    };

    class MetalPipelineCache {

    public:
        explicit MetalPipelineCache(void* device);

        ~MetalPipelineCache();

        void* getOrCreatePipelineState(const PipelineKey& key);

        PipelinePrewarmStatus prewarmPipelineState(const PipelineKey& key);

        void* getOrCreateDepthOnlyPipelineState(void* vertexFunction, std::uint16_t vertexLayoutBitmask);

        void* getOrCreateDepthOnlyPipelineState(void* vertexFunction, void* fragmentFunction, std::uint16_t vertexLayoutBitmask);

        void* getOrCreateDepthStencilState();

        void* getOrCreateDepthStencilState(bool depthTest, bool depthWrite, DepthFunc depthFunc);

        void removePipelineStatesReferencing(void* function);

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

}// namespace threepp::metal

#endif
