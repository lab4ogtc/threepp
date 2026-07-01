#version 460

// Particle billboard fragment shader. Mirrors the GL ParticleSystem fragment
// shader (modulate per-particle vertex color × particle texture) and the HUD
// overlay_sprite.frag color convention.
// Blending (alpha vs additive) is set by the pipeline variant, not here.
// Untextured particle systems bind a 1×1 white default so the sampler is
// always valid. Color-space conversion is handled by image formats.

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 t = texture(tex, vUv);
    outColor = vec4(vColor.rgb * t.rgb, vColor.a * t.a);
}
