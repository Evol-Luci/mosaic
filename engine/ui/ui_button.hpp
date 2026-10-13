#pragma once

#include <string>

#include "foundation/math/vec2.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;

// A non-visual rectangular hit region (reuses resolve_rect). Overlay a
// UiPanel/UiImage/UiText for the visual.
//
// UiSystem emits a ScriptEvent named `event` on this one channel for SEVEN actions. Three
// carry { entity, action }; the three drag actions also carry { x, y }, the pointer position
// in normalized screen space:
//
//   action "click"       -- pointer pressed and released on this button
//   action "hover_enter" -- the pointer moved onto it (fires once, on the transition)
//   action "hover_exit"  -- the pointer moved off it
//   action "drag_start"  -- +x,y. The pointer moved past kDragThreshold while this button
//                           held the press. Fires ONCE per press, on movement rather than on
//                           the press itself -- otherwise every click would emit one too.
//   action "drag"        -- +x,y. Each later frame the pointer moves while still held.
//   action "drag_end"    -- +x,y. The press was released while the pointer was still on the
//                           surface. A release inside this button emits drag_end and THEN
//                           click; a drag does not suppress the click.
//   action "drag_cancel" -- no position. The pointer LEFT the surface mid-drag, which is a
//                           different outcome from a release: the position at that moment is
//                           clamped to the surface edge and would be a point the user never
//                           pointed at. No click follows.
//
// A press that never crosses the threshold emits no drag action at all -- it is a click.
//
// **A handler MUST filter on `action`.** Before hover existed, "click" was the only action,
// so matching the event NAME alone was correct -- and any handler still doing that now
// fires on every pointer crossing. Adding hover had to fix three such handlers in this
// repository, including the demo's own.
//
// Hover is SUBORDINATE to the press state, as of pointer capture. While a press is held, the
// pointer belongs to the button it landed on: no other button hovers, and the captured one
// is hovered only while the cursor is inside its own rect -- so it un-highlights when you
// drag off and re-highlights when you drag back. With no press held, hover tracks whatever
// is topmost under the cursor. (This comment previously said hover was independent of the
// press, which was true until capture shipped.)
//
// It requires a pointer to be PRESENT (see PointerState::inside), so it is silent headless
// and while the cursor is outside the surface.
struct UiButton
{
    int         anchor = 0;
    Vec2        pos    = Vec2{0.0f, 0.0f};
    Vec2        size   = Vec2{0.1f, 0.1f};
    std::string event  = "ui_click";
    // Z-order for hit resolution: higher wins when regions overlap. Among EQUAL orders the
    // entity later in iteration wins, which usually -- but not provably -- matches the
    // panel pass's draw order: buttons and panels live in separate sparse sets whose
    // relative order can change when any button is destroyed (swap-and-pop). Author
    // distinct values when the resolution matters. Signed, so a scene can use -1 to push a
    // button behind its neighbours.
    //
    // UiButton was the only UI component without this -- UiPanel, UiImage and UiText each
    // carry one, all declared last, which is why this goes last too: it also keeps
    // demo_scene.hpp's positional brace init compiling, since adding it before `event`
    // would bind "hud_click" to an int.
    //
    // KNOWN COST: this order and the UiPanel order that decides what a user actually SEES
    // are independent, so they can disagree. A button can claim to be above a panel drawn
    // below it, and the only symptom is a click landing on the wrong thing.
    //
    // IT IS NOW VALIDATED, in two places, and this comment used to say the opposite -- that
    // validating it "needs an ownership relation ... which does not exist". That was true
    // when written and false three slices later: the ownership rule is entity identity (a
    // button owns the UI components on its own entity), and on top of it
    // engine/ui/ui_order_check.hpp provides find_order_disagreements (this entity's button
    // order against its own panel's -- a heuristic lint for the authoring slip) and
    // find_order_conflicts (overlapping PAIRS whose rankings genuinely disagree -- the
    // correctness check). The editor reports both into its Console every frame, except that the
    // per-entity lint stands down for any entity the pairwise check already names -- one
    // authoring mistake earns one line.
    //
    // The cost that remains is narrower than the original note: the fields are still
    // independent, so nothing PREVENTS the disagreement -- a diagnostic tells you about it.
    // Only UiPanel ranks a button's visual, because since the HUD passes were sorted the
    // image and text passes are separate and always above panels; UiImage::order cannot be
    // compared with a button's at all.
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
