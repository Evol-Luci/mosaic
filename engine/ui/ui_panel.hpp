#pragma once

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A screen-space HUD panel: an anchored, alpha-blended colored rectangle.
// anchor is an int cast to ui::Anchor (0 = TopLeft); the render path clamps
// out-of-range values to TopLeft. pos is a normalized offset from the anchor
// toward the interior; size is a fraction of the screen. order stacks panels
// (higher draws on top). A normal component: scripts can animate alpha/pos.
struct UiPanel
{
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    Vec2  size   = Vec2{0.2f, 0.1f};
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
};

template <class V>
void reflect(V& visitor, UiPanel& p)
{
    visitor.member("anchor", p.anchor);
    visitor.member("pos", p.pos);
    visitor.member("size", p.size);
    visitor.member("color", p.color);
    visitor.member("alpha", p.alpha);
    visitor.member("order", p.order);
}
} // namespace mosaic::engine::ui
