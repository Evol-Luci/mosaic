#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
// A positional light. World position is NOT stored here — it is derived from the
// light entity's WorldTransform (like Camera derives its view). `color` is premultiplied
// by `intensity` at shading time; `range` is the attenuation window (falloff reaches 0).
struct PointLight
{
    mosaic::foundation::math::Vec3 color{1.0f, 1.0f, 1.0f};
    float                          intensity{1.0f};
    float                          range{10.0f};
};
template <class V> void reflect(V& visitor, PointLight& l)
{
    visitor.member("color", l.color);
    visitor.member("intensity", l.intensity);
    visitor.member("range", l.range);
}
} // namespace mosaic::engine::render
