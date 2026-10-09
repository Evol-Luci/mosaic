#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;         // object -> world
    mat4 normal_matrix; // transpose(inverse(model)); upper 3x3 used
    vec4 base_color;    // .xyz
    float metallic;
    float roughness;
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    // .xyz is the camera in world space. .w is 1 when image-based lighting is active and
    // 0 otherwise -- it rides in this vec4's documented-unused padding slot rather than
    // growing the UBO, which would change its size assertion for one boolean.
    vec4 camera_position;
    mat4 light_view_proj;
    // Exposure in .x, .yzw unused. A trailing vec4 rather than one of the unused .w slots
    // above: camera_position.w is already the IBL flag, and the backend's comment warns
    // what claiming such a slot costs. Appended last so no existing member's offset moves.
    //
    // Declared here although the VERTEX stage never reads it: the block is bound to both
    // stages and the two declarations must agree exactly. The contract test
    // "cube.vert and cube.frag declare matching FrameLighting blocks" is what caught the
    // first version of this change, which added the member to cube.frag alone.
    vec4 exposure;
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
