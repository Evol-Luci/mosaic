#pragma once
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp"
namespace mosaic::engine::render
{
struct Material
{
    mosaic::foundation::math::Vec3          base_color{0.8f, 0.8f, 0.8f};
    mosaic::foundation::serialization::AssetID texture{}; // nil = untextured
    // Metallic/roughness workflow. base_color above serves two roles: albedo for a
    // dielectric, and the specular colour F0 for a metal -- which is why it is "base"
    // colour rather than albedo. roughness is perceptually linear across 0..1, which
    // the raw Phong exponent it replaces was not.
    //
    // Neither is clamped here: the shader clamps roughness to >= 0.045, deliberately,
    // so a scene file round-trips byte-for-byte. Same contract shininess had.
    float metallic = 0.0f;  // 0 = dielectric, 1 = metal
    float roughness = 0.5f; // 0 = mirror, 1 = fully rough
    // Per-texel metallic/roughness, glTF 2.0 packing: G = roughness, B = metallic.
    // Nil = untextured, and the two scalars above apply alone. When set, each scalar is
    // a FACTOR multiplied by its texel -- which is what lets an existing scene keep its
    // appearance, since the default map is white and multiplying by 1 changes nothing.
    // base_color and texture already have that same factor-times-texel relationship.
    mosaic::foundation::serialization::AssetID metallic_roughness_texture{};
};
template <class V> void reflect(V& v, Material& m)
{
    v.member("base_color", m.base_color);
    v.member("texture", m.texture);
    v.member("metallic", m.metallic);
    v.member("roughness", m.roughness);
    v.member("metallic_roughness_texture", m.metallic_roughness_texture);
}
} // namespace mosaic::engine::render
