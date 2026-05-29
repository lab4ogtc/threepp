
#ifndef THREEPP_METAL_SHADERS_HPP
#define THREEPP_METAL_SHADERS_HPP

namespace threepp::metal {

    constexpr auto basic_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float3 color    [[attribute(3)]];
};

struct VertexOutput {
    float4 position [[position]];
    float4 color;
};

vertex VertexOutput basic_vertex(
    VertexInput in [[stage_in]],
    constant float4x4& mvp [[buffer(4)]])
{
    VertexOutput out;
    out.position = mvp * float4(in.position, 1.0);
    out.color = float4(in.color, 1.0);
    return out;
}
)metal";

    constexpr auto basic_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct FragmentParams {
    float4 color;
    int useVertexColors;
};

fragment float4 basic_fragment(
    VertexOutput in [[stage_in]],
    constant FragmentParams& params [[buffer(0)]])
{
    if (params.useVertexColors != 0) {
        return in.color;
    }
    return params.color;
}
)metal";

}// namespace threepp::metal

#endif
