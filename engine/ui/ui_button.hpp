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
    // Z-order for hit resolution: higher wins when regions overlap, and among equal orders
    // the entity later in iteration wins (matching the panel pass's draw order). Signed so
    // a scene can author -1 to push a button behind its neighbours.
    //
    // UiButton was the only UI component without this -- UiPanel, UiImage and UiText each
    // carry one, all declared last, which is why this goes last too: it also keeps
    // demo_scene.hpp's positional brace init compiling, since adding it before `event`
    // would bind "hud_click" to an int.
    //
    // KNOWN COST, accepted deliberately: this order and the order of the UiPanel/UiImage a
    // user actually SEES are independent, so they can disagree. A button can claim to be
    // above a visual drawn below it, and the only symptom is a click landing on the wrong
    // thing. Validating that needs an ownership relation between a button and its visual,
    // which does not exist (UiParent is a transform hierarchy); recorded in the backlog.
    int         order  = 0;
};

template <class V>
void reflect(V& visitor, UiButton& b)
{
    visitor.member("anchor", b.anchor);
    visitor.member("pos", b.pos);
    visitor.member("size", b.size);
    visitor.member("event", b.event);
    visitor.member("order", b.order);
}
} // namespace mosaic::engine::ui
