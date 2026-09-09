#pragma once

#include <string>

#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// A non-visual rectangular hit region (reuses resolve_rect). On a click (pointer press
// + release inside), UiSystem emits a ScriptEvent named `event` with payload
// { entity, action:"click" }. Overlay a UiPanel/UiImage/UiText for the visual.
struct UiButton
{
    int         anchor = 0;
    Vec2        pos    = Vec2{0.0f, 0.0f};
    Vec2        size   = Vec2{0.1f, 0.1f};
    std::string event  = "ui_click";
};

template <class V>
void reflect(V& visitor, UiButton& b)
{
    visitor.member("anchor", b.anchor);
    visitor.member("pos", b.pos);
    visitor.member("size", b.size);
    visitor.member("event", b.event);
}
} // namespace mosaic::engine::ui
