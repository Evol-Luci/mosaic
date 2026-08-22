#pragma once
#include "foundation/math/vec3.hpp"
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3 base_color{0.8f, 0.8f, 0.8f};
    // 36d-2 adds a texture AssetID.
};
template <class V> void reflect(V& visitor, Material& m) { visitor.member("base_color", m.base_color); }
}
