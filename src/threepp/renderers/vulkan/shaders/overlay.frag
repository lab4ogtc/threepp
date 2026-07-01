#version 460

// Pair to overlay.vert. Emits a constant tint for HUD/overlay geometry
// (ortho-HUD filled meshes + lines, and the world-space wireframe/line
// overlay). threepp material colors are linear; color-space conversion is
// handled by the configured image formats rather than by this shader.

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
