#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 vUv;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
} pc;

void main() {
    vec4 clip = pc.mvp * vec4(inPos, 1.0);
    clip.y = -clip.y;
    gl_Position = clip;
    vUv = inUv;
}
