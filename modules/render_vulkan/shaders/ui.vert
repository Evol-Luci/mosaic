#version 450

// Push constant: normalized rect (top-left origin) + color + alpha.
layout(push_constant) uniform Push {
    vec4 rect;   // x, y, w, h in [0,1]
    vec4 color;  // rgb + a (a unused here, passed through)
} pc;

layout(location = 0) out vec4 v_color;

// Unit quad as two triangles via gl_VertexIndex (0..5), corners in [0,1].
vec2 corner(int i) {
    if (i == 0) return vec2(0.0, 0.0);
    if (i == 1) return vec2(1.0, 0.0);
    if (i == 2) return vec2(0.0, 1.0);
    if (i == 3) return vec2(1.0, 0.0);
    if (i == 4) return vec2(1.0, 1.0);
    return vec2(0.0, 1.0);
}

void main() {
    vec2 uv = corner(gl_VertexIndex);
    float nx = pc.rect.x + uv.x * pc.rect.z; // normalized [0,1], top-left origin
    float ny = pc.rect.y + uv.y * pc.rect.w;
    // The pass uses a negative-height viewport (Y-flip), so a top-left-origin ny (0 = top)
    // maps to clip.y = 1 - 2*ny to land at the window top. x is left->right as usual.
    gl_Position = vec4(nx * 2.0 - 1.0, 1.0 - ny * 2.0, 0.0, 1.0);
    v_color = pc.color;
}
