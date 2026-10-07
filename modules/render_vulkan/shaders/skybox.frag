#version 450

layout(location = 0) in vec2 fragNdc;
layout(set = 0, binding = 0) uniform samplerCube environment;
layout(push_constant) uniform Push {
    mat4  inv_view_rotation; // world-from-view, translation removed
    float tan_half_fov_y;
    float aspect;
    // Must be the same value the lit pass scales by, or the sky and the geometry drift
    // apart in brightness. It arrives as a push constant because this pipeline does not
    // bind the per-frame UBO -- set 0 here is the environment cubemap.
    float exposure;
} pc;
layout(location = 0) out vec4 outColor;

// ACES filmic approximation (Narkowicz). IDENTICAL to cube.frag's copy, and a contract
// test pins them equal -- a shared GLSL include would be better, but the shader build
// has no include step, so the test is what holds them together.
//
// This shader must tone map, which is a change from slice 1's reasoning. That slice
// applied the curve in cube.frag ALONE, on the grounds that every other
// colour-producing shader is unlit and cannot exceed 1.0. A skybox sampling HDR
// radiance can, and routinely does.
vec3 tonemap_aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    // The ray is built from the camera basis and the field of view rather than from an
    // inverted view-projection: Mat4 offers only inverse_affine, and a projection matrix
    // is not affine. tan(fovY/2) and the aspect ratio are read back out of the
    // projection matrix on the CPU and passed in, which needs no new matrix code.
    vec3 view_dir = vec3(fragNdc.x * pc.tan_half_fov_y * pc.aspect,
                         fragNdc.y * pc.tan_half_fov_y,
                         -1.0);
    vec3 dir = normalize(mat3(pc.inv_view_rotation) * view_dir);

    vec3 radiance = texture(environment, dir).rgb;
    outColor = vec4(tonemap_aces(radiance * pc.exposure), 1.0);
}
