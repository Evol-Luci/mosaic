#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inAlpha;

layout(push_constant) uniform Push { mat4 view_proj; } pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;

void main()
{
    gl_Position = pc.view_proj * vec4(inPos, 1.0);
    fragColor = inColor;
    fragAlpha = inAlpha;
}
