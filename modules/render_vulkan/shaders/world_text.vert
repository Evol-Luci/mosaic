#version 450

layout(push_constant) uniform Push { mat4 view_proj; } pc;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;
layout(location = 3) in float in_alpha;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    gl_Position = pc.view_proj * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_color = vec4(in_color, in_alpha);
}
