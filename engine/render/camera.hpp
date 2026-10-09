#pragma once

namespace mosaic::engine::render
{

// The camera "lens". Placement comes from the entity's Transform (view = inverse of
// its WorldTransform). Pure data — reflected and serializable (unlike MeshRenderer,
// whose value is a runtime device handle).
struct Camera
{
    float fov_y_radians = 1.0472f; // 60 degrees
    float near_z = 0.1f;
    float far_z = 100.0f;
    // Linear multiplier applied to the colour ENTERING the tone-mapping curve, so 1.0 is
    // neutral and this is the one scene-authored way to make tone mapping brighter or
    // darker. On the Camera because exposure is a camera property physically and in every
    // DCC tool: it serialises with the scene, is per-camera, and reaches the inspector
    // through reflection with no extra code. NOT in EV stops -- see the backlog entry;
    // stops would put a pow() in two fragment shaders and make the neutral value 0.0.
    float exposure = 1.0f;
};

template <class V>
void reflect(V& visitor, Camera& c)
{
    visitor.member("fov_y_radians", c.fov_y_radians);
    visitor.member("near_z", c.near_z);
    visitor.member("far_z", c.far_z);
    visitor.member("exposure", c.exposure);
}

} // namespace mosaic::engine::render
