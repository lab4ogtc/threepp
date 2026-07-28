#version 460

layout(set = 0, binding = 0) uniform sampler2D meshMap;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
} pc;

void main() {
    vec4 t = texture(meshMap, vUv);
    outColor = vec4(pc.color.rgb * t.rgb, pc.color.a * t.a);
}
