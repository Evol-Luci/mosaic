#version 450

layout(push_constant) uniform Push { mat4 mvp; vec4 color; } pc; // color.w = alpha

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

// A unit quad on the local XY plane (z = 0), two triangles.
const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

void main()
{
    vec2 c = kCorners[gl_VertexIndex];
    gl_Position = pc.mvp * vec4(c, 0.0, 1.0);
    fragUV = c + vec2(0.5);
    fragColor = pc.color;
}
