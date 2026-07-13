#version 460

layout(set = 0, binding = 0) uniform sampler2D depthMap;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 params;
} pc;

float perspectiveDepthToViewZ(float invClipZ, float near, float far) {
    return (near * far) / ((far - near) * invClipZ - far);
}

float srgbToLinear(float value) {
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

void main() {
    vec2 uv = vec2(vUv.x, mix(vUv.y, 1.0 - vUv.y, pc.params.z));
    float reverseDepth = texture(depthMap, uv).x;
    float glDepth = 1.0 - reverseDepth;
    float viewZ = perspectiveDepthToViewZ(glDepth, pc.params.x, pc.params.y);
    // 与 GL 传感器后处理一致：按 far 归一化，并将深度拆成 RG 高低字节。
    float depth = clamp(-viewZ / pc.params.y, 0.0, 1.0);
    float highByte = floor(depth * 255.0) / 255.0;
    float lowByte = fract(depth * 255.0);
    if (pc.params.w < 0.0) {
        // sRGB attachment 会在写出时再次编码，预先转线性以保持最终字节值不变。
        highByte = srgbToLinear(highByte);
        lowByte = srgbToLinear(lowByte);
    }
    outColor = vec4(highByte, lowByte, 0.0, abs(pc.params.w));
}
