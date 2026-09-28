#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;         // object -> world
    mat4 normal_matrix; // transpose(inverse(model)); upper 3x3 used
    vec4 base_color;    // .xyz
    float shininess;
    float specular_strength;
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    vec4 camera_position; // world space .xyz
    mat4 light_view_proj;
} fl;
layout(location = 0) out vec3 fragNormal;   // world space
layout(location = 1) out vec2 fragUV;
layout(location = 2) out vec3 fragPos;      // world space
layout(location = 3) out vec4 fragLightSpacePos;
void main()
{
    vec4 world = pc.model * vec4(inPosition, 1.0);
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragNormal = mat3(pc.normal_matrix) * inNormal; // world space
    fragUV = inUV;
    fragPos = world.xyz;                            // world space
    fragLightSpacePos = fl.light_view_proj * world;
}
