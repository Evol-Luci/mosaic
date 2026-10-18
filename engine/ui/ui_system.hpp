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
// How far the pointer must travel from the press point before a press becomes a drag.
//
// NORMALIZED SCREEN UNITS, so this is ANISOTROPIC: at 1920x1080 it is about 19px
// horizontally and 10px vertically. Accepted for now -- a pixel-accurate threshold needs the
// screen aspect, which UiSystem does not have and would have to be threaded in the way
// resolve_image_quad takes it. Recorded in the backlog.
//
// The VALUE is a guess until a human drags something; its feel is the one claim no test
// here can make.
inline constexpr float kDragThreshold = 0.01f;

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

        // A CAPTURED ENTITY CAN DIE MID-PRESS -- destroyed, or merely stripped of its
        // UiButton by a script or an interactivity toggle. Drop the capture, silently: the
        // channel name lived on the component, so there is no honest name to emit a
        // drag_end or drag_cancel on, which is the same reason the hover path gives for
        // emitting nothing for an entity that lost its button.
        //
        // Dropped HERE, before hover resolves, and that placement is the point. Holding a
        // capture for a ghost does not crash -- every emit already returns false -- it makes
        // the UI go DEAD: hover resolves against an entity that no longer exists, so nothing
        // can highlight until the button comes up, and press_consumed() keeps world-space
        // picking declining a press the UI no longer owns. Dropping it restores ordinary
        // hover on the very next frame.
        //
        // Tested on get_component rather than is_valid because losing the UiButton is the
        // broader condition and the one emit_button_action itself guards on; a destroyed
        // entity satisfies it too.
        if (captured_ != mosaic::foundation::EntityID{}
            && world.get_component<UiButton>(captured_) == nullptr)
        {
            captured_ = mosaic::foundation::EntityID{};
            dragging_ = false;
        }

        const std::vector<HitCandidate> cands = candidates(world);
        const mosaic::foundation::EntityID top = topmost_hit(cands, p);

        // WHILE CAPTURED, THE POINTER BELONGS TO ONE BUTTON. Hover resolves against the
        // captured entity alone: it is hovered when the cursor is inside it and not
        // otherwise, and nothing else can hover at all. That is what makes a press-and-slide
        // read correctly -- the highlight tracks whether releasing here would fire, rather
        // than tracking whatever happens to be under the cursor.
        //
        // `top == captured_` rather than a fresh containment test, because `top` already
        // answers it from the same snapshot the press path uses. A second test could
        // disagree with the first.
        const mosaic::foundation::EntityID hover_target =
            (captured_ != mosaic::foundation::EntityID{})
                ? ((top == captured_) ? captured_ : mosaic::foundation::EntityID{})
                : top;

        // --- hover, before press, so a script sees the enter for the button it is about
        // --- to be told was clicked.
        if (hover_target != hovered_)
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
            if (hover_target != mosaic::foundation::EntityID{})
            {
                emit_button_action(world, hover_target, "hover_enter");
                world.add_component<UiHovered>(hover_target, UiHovered{});
            }
            hovered_ = hover_target;
        }

        if (p.down && !was_down_)
        {
            // Press: the TOPMOST button under the pointer, not the first one iterated.
            captured_ = top;
            press_at_ = Vec2{p.x, p.y};
            last_at_  = press_at_;
            dragging_ = false;
        }
        else if (!p.down && was_down_)
        {
            const Vec2 at{p.x, p.y};
            // THE TWO ENDINGS, and they are distinguishable without new plumbing. `inside`
            // false means the pointer LEFT THE SURFACE rather than that the button came up
            // where the user meant it to -- the editor's provider drops `down` the moment
            // the cursor leaves the viewport panel, on purpose, so a drag cannot keep
            // running while the user clicks in the Inspector.
            //
            // Both are gated on `dragging_`: a press that never crossed the threshold
            // started no drag, so it ends none. Without that gate, every press the user
            // simply moved away from would fire drag_cancel.
            if (dragging_)
            {
                if (p.inside)
                {
                    emit_button_action(world, captured_, "drag_end", &at);
                }
                else
                {
                    // No position: the provider clamps x/y to the panel edge once the cursor
                    // is outside, so reporting it would report a point the user never
                    // pointed at.
                    emit_button_action(world, captured_, "drag_cancel");
                }
            }
            // `top`, not a second `topmost_hit(cands, p)` call. That call was literally
            // equal to `top` -- same `cands`, same `p` -- but READ as "re-resolve after the
            // hover emission", which it was not: `cands` is snapshotted before any event
            // fires. Saying `top` makes the snapshot semantics visible instead of implying
            // a freshness the code does not provide.
            //
            // Emitted AFTER drag_end so a handler that commits a drag and then sees the
            // click reads them in that order; the reverse would look like a click that
            // preceded the drag ending. The click rule itself is unchanged: the release must
            // land on the captured button, which is stricter than merely still being on the
            // surface.
            if (captured_ != mosaic::foundation::EntityID{} && top == captured_)
            {
                emit_button_action(world, captured_, "click");
            }
            captured_ = mosaic::foundation::EntityID{};
            // Defence in depth, not the thing that makes this safe: the PRESS edge already
            // sets dragging_ = false, and between a release and the next press nothing reads
            // it -- the drag branch requires a non-nil captured_, which the line above just
            // cleared. Verified by a mutation probe: removing this changes no observable
            // behaviour. It stays because it states the invariant locally and costs nothing;
            // a reader should not believe it is load-bearing.
            dragging_ = false;
        }
        // Drag, while the press is still held by a button. Nothing is emitted for a press
        // that has not moved past the threshold: that is a click, and a click emits no drag
        // action at all.
        if (p.down && was_down_ && captured_ != mosaic::foundation::EntityID{})
        {
            const Vec2 at{p.x, p.y};
            if (!dragging_)
            {
                // Squared comparison, so the threshold needs no square root and the constant
                // stays readable as a distance.
                const float dx = at.x - press_at_.x;
                const float dy = at.y - press_at_.y;
                if (dx * dx + dy * dy > kDragThreshold * kDragThreshold)
                {
                    dragging_ = true;
                    emit_button_action(world, captured_, "drag_start", &at);
                    last_at_ = at;
                }
            }
            else if (at.x != last_at_.x || at.y != last_at_.y)
            {
                emit_button_action(world, captured_, "drag", &at);
                last_at_ = at;
            }
        }

        was_down_ = p.down;
    }

    // True when the most recent press landed on a UiButton, so world-space click
    // handling (picking, nudging) can decline a click the UI already claimed.
    // Valid after this system's update() has run for the frame.
    //
    // Named for the question its CALLER asks ("did the UI take this press?"), not for the
    // field behind it -- ClickNudgeSystem does not care that the press is also a capture.
    bool press_consumed() const { return captured_ != mosaic::foundation::EntityID{}; }

    // Abandon any in-progress press or drag, emitting NOTHING.
    //
    // For a caller that knows the gesture has stopped being meaningful -- the editor, across
    // a play-state transition. While the editor is stopped, World::tick never runs, so this
    // system freezes mid-press with was_down_ true; press the HUD button, drag past the
    // threshold, hit Stop, then Play or Step, and the first tick takes the RELEASE edge and
    // emits drag_cancel (or drag_end plus click, if the cursor happens to be back over the
    // button) for a gesture that ended seconds earlier.
    //
    // SILENT, deliberately. The drag did not end, it stopped existing, and emitting
    // drag_cancel here would fire a handler into a world that is in the middle of being
    // stopped or started -- which is a worse version of the problem, not a fix for it.
    //
    // Hover is NOT cleared: it is not part of the gesture. Clearing it would emit a spurious
    // hover_enter next tick for a cursor that never moved, and if the cursor HAS moved away
    // the ordinary hover path emits hover_exit, which is correct rather than stale.
    //
    // was_down_ is set FALSE rather than left alone so the next update sees no release edge.
    // If the button is genuinely still held, that update's `p.down && !was_down_` sees a
    // fresh press instead -- which is the honest reading: as far as this system is
    // concerned, a new gesture started.
    void reset_press_state()
    {
        was_down_  = false;
        captured_  = mosaic::foundation::EntityID{};
        dragging_  = false;
    }

private:
    // Emit `action` on the button's own event channel. One helper for click and hover
    // both: two copies of this would be two chances for the payload shapes to diverge,
    // and a script filtering on `action` would then work for one and not the other.
    //
    // Looks the button up rather than taking it by reference, because the hover path must
    // emit for an entity it recorded on a PREVIOUS update -- which may since have lost its
    // UiButton. Returns false in that case, with nothing emitted: the channel name lived on
    // the component, so there is no honest name to emit on.
    //
    // `at` is the pointer position, included only for the drag actions. Passing nullptr
    // keeps the payload at exactly {entity, action} -- the shipped shape that click,
    // hover_enter and hover_exit must keep, since handlers already read it.
    static bool emit_button_action(mosaic::engine::World& world,
                                   mosaic::foundation::EntityID e, const char* action,
                                   const Vec2* at = nullptr)
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
        if (at != nullptr)
        {
            payload["x"] = ser::Value(static_cast<double>(at->x));
            payload["y"] = ser::Value(static_cast<double>(at->y));
        }
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
    // WHO OWNS THE POINTER, not merely who was pressed. It already persisted across frames
    // and gated the click on a same-button release -- that was half a capture rule without
    // the name. The rename is what makes the routing readable: while this is set, the
    // pointer belongs to this entity and hover resolves against it alone.
    mosaic::foundation::EntityID captured_ = {};
    mosaic::foundation::EntityID hovered_  = {};
    // Where the press landed, so the threshold is measured from the PRESS rather than from
    // the previous frame -- otherwise a slow drag never accumulates enough to cross it.
    Vec2                         press_at_{0.0f, 0.0f};
    // The previous frame's position, so a motionless frame emits no drag.
    Vec2                         last_at_{0.0f, 0.0f};
    // Has drag_start fired for this press? drag_start is an edge; drag is a stream.
    bool                         dragging_ = false;
};
} // namespace mosaic::engine::ui
