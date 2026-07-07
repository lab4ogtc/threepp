#version 460

// Pair to overlay_point.vert. Modulates the material color tint
// (pc.color.rgb) by the per-vertex color, with full alpha — the
// push constant's .w slot encodes point size for this pipeline, not
// opacity, so blend alpha is hard-coded to 1.0.
//
// GL 的 PointsMaterial 默认不裁圆，保持方形点覆盖。

layout(location = 0) in vec3 vColor;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
    vec4 point;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(pc.color.rgb * vColor, 1.0);
}
