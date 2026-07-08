#version 460

layout(set = 0, binding = 0) uniform sampler2D presentTex;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(presentTex, vUv).rgb, 1.0);
}
