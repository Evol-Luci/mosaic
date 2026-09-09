#pragma once

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A screen-space HUD sprite: `texture` stretched to an anchored rect, tinted and
// alpha-blended. anchor is an int cast to ui::Anchor (clamped to TopLeft out of range).
// pos is a normalized offset from the anchor; size is a fraction of the screen. tint
// multiplies the texture rgb; alpha multiplies the texture alpha. Script-mutable.
struct UiImage
{
    mosaic::foundation::serialization::AssetID texture;
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    Vec2  size   = Vec2{0.1f, 0.1f};
    Vec3  tint   = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
};

template <class V>
void reflect(V& visitor, UiImage& i)
{
    visitor.member("texture", i.texture);
    visitor.member("anchor", i.anchor);
    visitor.member("pos", i.pos);
    visitor.member("size", i.size);
    visitor.member("tint", i.tint);
    visitor.member("alpha", i.alpha);
    visitor.member("order", i.order);
}
} // namespace mosaic::engine::ui
