#version 450

// Per-instance attributes (instance-rate vertex binding 0).
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in float inSize;
layout(location = 2) in vec3 inColor;
layout(location = 3) in float inAlpha;

layout(push_constant) uniform Push {
    mat4 view_proj;
    vec4 camera_right; // world-space, .xyz
    vec4 camera_up;    // world-space, .xyz
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;
layout(location = 2) out vec2 fragUV;

// Two triangles forming a quad centered on the particle, in [-0.5, 0.5].
const vec2 kCorners[6] = vec2[6](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
);

void main()
{
    vec2 corner = kCorners[gl_VertexIndex];
    vec3 worldPos = inWorldPos
        + corner.x * inSize * pc.camera_right.xyz
        + corner.y * inSize * pc.camera_up.xyz;
    gl_Position = pc.view_proj * vec4(worldPos, 1.0);
    fragColor = inColor;
    fragAlpha = inAlpha;
    fragUV = corner + vec2(0.5); // [0,1] across the quad
}
