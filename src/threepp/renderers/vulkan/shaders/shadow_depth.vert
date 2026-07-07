#version 460

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    gl_Position.y = -gl_Position.y;
}
