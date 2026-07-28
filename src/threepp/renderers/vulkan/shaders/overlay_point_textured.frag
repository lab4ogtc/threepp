#version 460

layout(set = 0, binding = 0) uniform sampler2D mapTex;
layout(set = 0, binding = 1) uniform sampler2D alphaTex;

layout(location = 0) in vec3 vColor;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
    vec4 point;  // .z = hasMap, .w = hasAlphaMap
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex = pc.point.z > 0.5 ? texture(mapTex, gl_PointCoord) : vec4(1.0);
    float alpha = tex.a;
    if (pc.point.w > 0.5) {
        alpha *= texture(alphaTex, gl_PointCoord).r;
    }
    if (alpha <= 0.01) discard;

    outColor = vec4(pc.color.rgb * vColor * tex.rgb, alpha);
}
