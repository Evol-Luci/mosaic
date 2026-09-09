#version 450

layout(push_constant) uniform Push {
    vec4 rect;   // x, y, w, h in [0,1], top-left origin
    vec4 uv;     // u0, v0, u1, v1
    vec4 color;  // rgb + alpha
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

vec2 corner(int i) {
    if (i == 0) return vec2(0.0, 0.0);
    if (i == 1) return vec2(1.0, 0.0);
    if (i == 2) return vec2(0.0, 1.0);
    if (i == 3) return vec2(1.0, 0.0);
    if (i == 4) return vec2(1.0, 1.0);
    return vec2(0.0, 1.0);
}

void main() {
    vec2 c = corner(gl_VertexIndex);
    float nx = pc.rect.x + c.x * pc.rect.z;
    float ny = pc.rect.y + c.y * pc.rect.w;
    gl_Position = vec4(nx * 2.0 - 1.0, 1.0 - ny * 2.0, 0.0, 1.0);
    v_uv = vec2(mix(pc.uv.x, pc.uv.z, c.x), mix(pc.uv.y, pc.uv.w, c.y));
    v_color = pc.color;
}
