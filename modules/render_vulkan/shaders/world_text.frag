#version 450

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;

void main()
{
    float cov = texture(atlas, v_uv).a;
    o_color = vec4(v_color.rgb, cov * v_color.a);
}
