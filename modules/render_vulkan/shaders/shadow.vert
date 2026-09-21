#version 450
layout(location = 0) in vec3 inPosition;
layout(push_constant) uniform Push {
    mat4 light_mvp;
} pc;
void main()
{
    gl_Position = pc.light_mvp * vec4(inPosition, 1.0);
}
