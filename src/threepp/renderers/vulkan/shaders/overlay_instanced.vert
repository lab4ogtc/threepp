#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 instanceMatrix0;
layout(location = 2) in vec4 instanceMatrix1;
layout(location = 3) in vec4 instanceMatrix2;
layout(location = 4) in vec4 instanceMatrix3;
layout(location = 5) in vec3 instanceColor;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec3 vColor;

void main() {
    mat4 instanceMatrix = mat4(instanceMatrix0, instanceMatrix1,
                               instanceMatrix2, instanceMatrix3);
    vec4 clip = pc.mvp * instanceMatrix * vec4(inPos, 1.0);
    clip.y = -clip.y;
    gl_Position = clip;
    vColor = instanceColor;
}
