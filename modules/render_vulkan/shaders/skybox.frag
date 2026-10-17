#version 450

layout(location = 0) in vec2 fragNdc;
layout(set = 0, binding = 0) uniform samplerCube environment;
layout(push_constant) uniform Push {
    mat4  inv_view_rotation; // world-from-view, translation removed
    float tan_half_fov_y;
    float aspect;
    // NO LONGER READ BY THIS SHADER. Exposure is applied once in hdr_resolve.frag. The
    // member stays declared because this block's layout is shared byte-for-byte with the
    // C++ SkyboxPush struct and dropping it here alone is silent corruption, not a
    // compile error. Removing it from both sides is tracked in the deferred backlog.
    float exposure;
} pc;
layout(location = 0) out vec4 outColor;


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
    // LINEAR HDR, like every other world-space shader now: hdr_resolve.frag applies the
    // curve and the exposure once over the whole scene. The sky is the shader that most
    // needs the headroom -- it samples HDR radiance that routinely exceeds 1.0, and until
    // the resolve existed that excess was clamped away at this attachment.
    outColor = vec4(radiance, 1.0);
}
