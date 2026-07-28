#version 460

layout(push_constant) uniform Pc {
    mat4 mvp;
    vec4 color;
    vec4 dash; // x=dashSize, y=gapSize, z=scale
} pc;

layout(location = 0) in float vLineDistance;
layout(location = 0) out vec4 outColor;

void main() {
    float period = pc.dash.x + pc.dash.y;
    if (period > 0.0 && mod(vLineDistance, period) > pc.dash.x) {
        discard;
    }
    outColor = pc.color;
}
