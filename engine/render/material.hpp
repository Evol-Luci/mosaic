#pragma once
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3          base_color{0.8f, 0.8f, 0.8f};
    mosaic::foundation::serialization::AssetID texture{}; // nil = untextured
    // Blinn-Phong specular. shininess is a raw Phong exponent (perceptually
    // non-linear; a 0..1 perceptual remap is deferred to the PBR work). The shader
    // clamps it to >= 1 at shading time -- deliberately NOT clamped here, so a
    // scene file round-trips byte-for-byte.
    float shininess = 32.0f;
    float specular_strength = 0.5f; // 0 = matte
};
template <class V> void reflect(V& v, Material& m)
{
    v.member("base_color", m.base_color);
    v.member("texture", m.texture);
    v.member("shininess", m.shininess);
    v.member("specular_strength", m.specular_strength);
}
} // namespace mosaic::engine::render
