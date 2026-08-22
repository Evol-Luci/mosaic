#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
// `direction` is the direction the light travels (world space); shading uses -direction.
struct DirectionalLight
{
    mosaic::foundation::math::Vec3 direction{0.0f, -1.0f, 0.0f};
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
};
template <class V> void reflect(V& visitor, DirectionalLight& l)
{
    visitor.member("direction", l.direction);
    visitor.member("color", l.color);
}
}
