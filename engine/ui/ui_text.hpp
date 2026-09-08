#pragma once

#include <string>

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A single line of screen-space HUD text. `font` is a "font" AssetID (FontAtlas).
// anchor is an int cast to ui::Anchor (clamped to TopLeft if out of range). pos is a
// normalized offset from the anchor; height is the line height as a fraction of screen
// height. A normal component: scripts can change `text`, `color`, `alpha`, `pos`.
struct UiText
{
    mosaic::foundation::serialization::AssetID font;
    std::string text;
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    float height = 0.05f;
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
};

template <class V>
void reflect(V& visitor, UiText& t)
{
    visitor.member("font", t.font);
    visitor.member("text", t.text);
    visitor.member("anchor", t.anchor);
    visitor.member("pos", t.pos);
    visitor.member("height", t.height);
    visitor.member("color", t.color);
    visitor.member("alpha", t.alpha);
    visitor.member("order", t.order);
}
} // namespace mosaic::engine::ui
