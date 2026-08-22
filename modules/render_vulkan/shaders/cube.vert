#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction; // object space, .xyz
    vec4 light_color;     // .xyz
    vec4 base_color;      // .xyz
} pc;
layout(location = 0) out vec3 fragNormal;
void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = inNormal; // object space; light is provided in object space too
}
