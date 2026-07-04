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

float viewZToOrthographicDepth(float viewZ, float near, float far) {
    return (viewZ + near) / (near - far);
}

void main() {
    vec2 uv = vec2(vUv.x, mix(vUv.y, 1.0 - vUv.y, pc.params.z));
    float reverseDepth = texture(depthMap, uv).x;
    float glDepth = 1.0 - reverseDepth;
    float viewZ = perspectiveDepthToViewZ(glDepth, pc.params.x, pc.params.y);
    float depth = viewZToOrthographicDepth(viewZ, pc.params.x, pc.params.y);
    outColor = vec4(vec3(depth), pc.params.w);
}
