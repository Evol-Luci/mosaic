#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_button_rects.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_hit.hpp"
#include "engine/ui/ui_hover_style.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::ui
{
// First stateful UI system: reads an injected pointer each tick and emits a named
// ScriptEvent when the pointer presses and releases inside the same UiButton.
class UiSystem : public mosaic::engine::ecs::System
{
public:
    explicit UiSystem(std::function<PointerState()> pointer)
        : pointer_(std::move(pointer)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        // An absent provider means there is no pointer, NOT a pointer at the origin.
        // PointerState{} is {0, 0, false}, which sits inside any button anchored TopLeft at
        // {0,0} -- so falling back to it would emit a phantom hover for a pointer that does
        // not exist. Harmless for clicks (no press ever arrives) and not for hover.
        if (!pointer_)
        {
            return;
        }
        const PointerState p = pointer_();
        const std::vector<HitCandidate> cands = candidates(world);
        const mosaic::foundation::EntityID top = topmost_hit(cands, p);

        // --- hover, before press, so a script sees the enter for the button it is about
        // --- to be told was clicked.
        if (top != hovered_)
        {
            if (hovered_ != mosaic::foundation::EntityID{})
            {
                // Return value deliberately ignored: a false means the hovered entity lost
                // its UiButton, and clearing hovered_ below is the whole response.
                emit_button_action(world, hovered_, "hover_exit");
                // The TAG comes off even when the event could not be emitted. The event
                // needs the component's channel name; the tag does not, and an element left
                // tagged stays highlighted forever with nothing able to clear it.
                world.remove_component<UiHovered>(hovered_);
            }
            if (top != mosaic::foundation::EntityID{})
            {
                emit_button_action(world, top, "hover_enter");
                world.add_component<UiHovered>(top, UiHovered{});
            }
            hovered_ = top;
        }

        if (p.down && !was_down_)
        {
            // Press: the TOPMOST button under the pointer, not the first one iterated.
            pressed_ = top;
        }
        else if (!p.down && was_down_)
        {
            // `top`, not a second `topmost_hit(cands, p)` call. That call was literally
            // equal to `top` -- same `cands`, same `p` -- but READ as "re-resolve after the
            // hover emission", which it was not: `cands` is snapshotted before any event
            // fires. Saying `top` makes the snapshot semantics visible instead of implying
            // a freshness the code does not provide.
            if (pressed_ != mosaic::foundation::EntityID{} && top == pressed_)
            {
                emit_button_action(world, pressed_, "click");
            }
            pressed_ = mosaic::foundation::EntityID{};
        }
        was_down_ = p.down;
    }

    // True when the most recent press landed on a UiButton, so world-space click
    // handling (picking, nudging) can decline a click the UI already claimed.
    // Valid after this system's update() has run for the frame.
    bool press_consumed() const { return pressed_ != mosaic::foundation::EntityID{}; }

private:
    // Emit `action` on the button's own event channel. One helper for click and hover
    // both: two copies of this would be two chances for the payload shapes to diverge,
    // and a script filtering on `action` would then work for one and not the other.
    //
    // Looks the button up rather than taking it by reference, because the hover path must
    // emit for an entity it recorded on a PREVIOUS update -- which may since have lost its
    // UiButton. Returns false in that case, with nothing emitted: the channel name lived on
    // the component, so there is no honest name to emit on.
    static bool emit_button_action(mosaic::engine::World& world,
                                   mosaic::foundation::EntityID e, const char* action)
    {
        UiButton* b = world.get_component<UiButton>(e);
        if (b == nullptr)
        {
            return false;
        }
        namespace ser = mosaic::foundation::serialization;
        ser::Table payload;
        payload["entity"] = ser::Value(static_cast<std::uint64_t>(e.value()));
        payload["action"] = ser::Value(std::string(action));
        world.events().emit(
            mosaic::engine::script::ScriptEvent{b->event, ser::Value(std::move(payload))});
        return true;
    }

    // Every UiButton flattened into hit candidates. One pass per update, shared by the
    // press path and the hover path so the two can never disagree about which button is
    // under the pointer -- if they could, a click and its hover feedback would land on
    // different buttons, which reads as the UI lying rather than as a bug.
    //
    // The flatten itself now lives in ui_button_rects.hpp, because the order-conflict
    // diagnostic has to agree with it exactly -- a second implementation would police rects
    // nobody clicks on.
    static std::vector<HitCandidate> candidates(mosaic::engine::World& world)
    {
        return button_hit_candidates(world);
    }


    std::function<PointerState()> pointer_;
    bool                         was_down_ = false;
    mosaic::foundation::EntityID pressed_  = {};
    mosaic::foundation::EntityID hovered_  = {};
};
} // namespace mosaic::engine::ui
