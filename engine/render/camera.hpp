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
};

template <class V>
void reflect(V& visitor, Camera& c)
{
    visitor.member("fov_y_radians", c.fov_y_radians);
    visitor.member("near_z", c.near_z);
    visitor.member("far_z", c.far_z);
}

} // namespace mosaic::engine::render
