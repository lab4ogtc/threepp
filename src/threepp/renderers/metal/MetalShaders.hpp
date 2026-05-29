
#ifndef THREEPP_METAL_SHADERS_HPP
#define THREEPP_METAL_SHADERS_HPP

namespace threepp::metal {

    constexpr auto basic_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float3 position [[attribute(0)]];
#if USE_NORMAL
    float3 normal   [[attribute(1)]];
#endif
#if USE_MAP
    float2 uv       [[attribute(2)]];
#endif
#if USE_VERTEX_COLORS
    float3 color    [[attribute(3)]];
#endif
};

struct VertexOutput {
    float4 position [[position]];
#if USE_MAP
    float2 uv;
#endif
#if USE_VERTEX_COLORS
    float4 color;
#endif
};

vertex VertexOutput basic_vertex(
    VertexInput in [[stage_in]],
    constant float4x4& mvp [[buffer(4)]])
{
    VertexOutput out;
    out.position = mvp * float4(in.position, 1.0);
#if USE_NORMAL
    out.position += float4(in.normal * 0.0, 0.0);
#endif
#if USE_MAP
    out.uv = in.uv;
#endif
#if USE_VERTEX_COLORS
    out.color = float4(in.color, 1.0);
#endif
    return out;
}
)metal";

    constexpr auto basic_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct FragmentParams {
    float4 color;
};

fragment float4 basic_fragment(
    VertexOutput in [[stage_in]],
    constant FragmentParams& params [[buffer(0)]]
#if USE_MAP
    , texture2d<float> map [[texture(0)]]
    , sampler mapSampler [[sampler(0)]]
#endif
)
{
    float4 color = params.color;
#if USE_VERTEX_COLORS
    color *= in.color;
#endif
#if USE_MAP
    color *= map.sample(mapSampler, in.uv);
#endif
    return color;
}
)metal";

}// namespace threepp::metal

#endif
