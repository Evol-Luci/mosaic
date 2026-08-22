#pragma once
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3          base_color{0.8f, 0.8f, 0.8f};
    mosaic::foundation::serialization::AssetID texture{}; // nil = untextured
};
template <class V> void reflect(V& v, Material& m)
{
    v.member("base_color", m.base_color);
    v.member("texture", m.texture);
}
} // namespace mosaic::engine::render
