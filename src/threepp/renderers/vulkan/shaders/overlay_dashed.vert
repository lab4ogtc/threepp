#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in float inLineDistance;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
    vec4 dash; // x=dashSize, y=gapSize, z=scale
} pc;

layout(location = 0) out float vLineDistance;

void main() {
    vec4 clip = pc.mvp * vec4(inPos, 1.0);
    clip.y = -clip.y;
    gl_Position = clip;
    vLineDistance = inLineDistance * pc.dash.z;
}
