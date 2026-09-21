#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction; // object space, .xyz
    vec4 light_color;     // .xyz
    vec4 base_color;      // .xyz
    vec4 point_position;  // object space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    mat4 light_mvp;       // light_view_proj * model, for shadow sampling
} pc;
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec4 fragLightSpacePos;
void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = inNormal; // object space; lights are provided in object space too
    fragUV = inUV;
    fragPos = inPosition;  // object space
    fragLightSpacePos = pc.light_mvp * vec4(inPosition, 1.0);
}
