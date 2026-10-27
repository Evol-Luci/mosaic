#pragma once

#include <algorithm>
#include <cstdint>

#include "engine/ui/pointer.hpp"

namespace mosaic::app
{
// The standalone runtime's pointer policy, as a pure function of what GLFW reports.
//
// Pure and parameterised rather than reading the window itself, for the same reason
// studio/viewport.hpp's pointer_state is a method on a plain struct: the POLICY is the part
// worth testing, and it stays testable headlessly only if the GLFW calls live at the call
// site. The lambda in main.cpp does the three glfw* reads and hands the numbers here.
//
// PRESENCE IS GATED ON THE CURSOR BEING OVER THE WINDOW, matching the editor's provider,
// which has always gated on the cursor being over its viewport panel. Until 2026-10-12 this
// runtime left `inside` at its default true and merely clamped x/y, with two consequences:
// a cursor anywhere on the desktop read as a pointer resting against the window edge, which
// hovered an edge-anchored button; and because GLFW keeps reporting a held button while the
// cursor is outside a focused window, a drag off the edge ended as a `drag_end` carrying a
// clamped coordinate -- a point the user never pointed at -- instead of the `drag_cancel` it
// actually was.
//
// `down` is gated on presence too, which is what makes that cancel reachable. The editor
// does the same and says why: a press must stop being reported the moment the pointer leaves
// the surface, or a drag keeps running somewhere the user is no longer pointing.
//
// Edges are INCLUSIVE, matching topmost_hit's inclusive containment: a pointer exactly on
// the window edge is on the surface, and an edge-anchored button is hit there.
//
// `wheel_delta` is the accumulated scroll for this frame, handed in the same way: the
// callback that collects it lives at the call site (see WheelAccumulator below), the policy
// -- including gating it on presence -- lives here.
inline mosaic::engine::ui::PointerState window_pointer_state(double cursor_x, double cursor_y,
                                                             int window_w, int window_h,
                                                             bool button_down,
                                                             double wheel_delta)
{
    mosaic::engine::ui::PointerState p;
    if (window_w <= 0 || window_h <= 0)
    {
        // Minimised, or asked before the window has a size. Not merely "no pointer": there is
        // no coordinate space to report one in, and dividing by it is how that becomes a
        // crash rather than a wrong answer.
        return {0.0f, 0.0f, false, false, 0.0f};
    }
    const bool inside = cursor_x >= 0.0 && cursor_y >= 0.0
                        && cursor_x <= static_cast<double>(window_w)
                        && cursor_y <= static_cast<double>(window_h);
    // Clamped even when absent, so a consumer that reads x/y without checking `inside` gets a
    // sane number rather than one off the end of the screen. `inside` is what says the
    // position means anything.
    p.x      = std::clamp(static_cast<float>(cursor_x / window_w), 0.0f, 1.0f);
    p.y      = std::clamp(static_cast<float>(cursor_y / window_h), 0.0f, 1.0f);
    p.inside = inside;
    p.down   = inside && button_down;
    // Gated on presence for the same reason `down` is: a wheel event GLFW delivers to a
    // focused window whose cursor sits on the desktop is not a scroll of anything on this
    // surface, and reporting it would scroll whatever region the clamped x/y happens to
    // land in -- a region the user is not pointing at. The editor's provider gates on
    // `hovered` identically.
    p.wheel  = inside ? static_cast<float>(wheel_delta) : 0.0f;
    return p;
}

// The magnitude at which an UNCONSUMED wheel delta saturates.
//
// Needed because the reset below is gated on a tick having run, which is the only way to
// stop dropping notches on a zero-tick frame -- and the price of that gate is that a stretch
// of frames with no tick accumulates without a bound. That stretch is reachable, not
// hypothetical: `--fps` above `--fixed-hz` makes zero-tick frames the majority, and a low
// `--fixed-hz` (a tick every few seconds) leaves hundreds of frames between ticks.
//
// THE VALUE 120 IS A CHOSEN BOUND, NOT A MEASURED ONE, and nothing here has been scrolled by
// a human. No wheel-rate measurement exists on this project, so the number was picked to
// satisfy a shape rather than derived: large enough that no plausible real gesture is clipped,
// small enough that a tick arriving after a long stall gets a jump a consumer can reason
// about. Treat it as replaceable. **What would settle it** is a real scroll gesture on real
// hardware once U9d has a consumer -- a question for U9d's eye pass, not for a test here.
//
// The SHAPE, unlike the number, is argued: letting the delta grow instead would hand U9d's
// first consumer an arbitrarily large value whose magnitude encodes how long the sim was
// stalled, which is not information about what the user did; and discarding events instead
// would reintroduce the dropped-notch loss that gating the clear on a tick exists to fix.
// So it SATURATES, clamping the sum rather than dropping events, and keeps its sign:
// scrolling up then down still cancels one notch per notch, asserted in both directions.
inline constexpr float kMaxPendingWheel = 120.0f;

// Accumulates GLFW's scroll into a per-frame delta.
//
// It exists because GLFW's scroll is EVENT-DRIVEN ONLY: there is no glfwGetScroll, only
// glfwSetScrollCallback, which can fire any number of times during one glfwPollEvents. So an
// accumulator is forced; the only real questions are WHO CLEARS IT and WHEN, and the answer
// to the first must not be "the reader".
//
// THE READ DOES NOT CONSUME -- `value()` is const. The pointer provider is called twice per
// tick (UiSystem and ClickNudgeSystem) and up to eight times that per frame, since
// RuntimeLoop::advance runs as many fixed ticks as its accumulator holds. If reading drained
// this, the second caller within a tick would see 0 and the two systems would disagree about
// the pointer inside one tick -- which is precisely what app/main.cpp's provider comment
// forbids.
//
// `drop_observed()` IS CALLED BY THE FRAME LOOP, at the top of main.cpp's on_frame,
// IMMEDIATELY BEFORE glfwPollEvents(), and it is GATED ON A TICK HAVING RUN. Both halves are
// load-bearing:
//   * The POSITION: FramePump::run ticks (loop_.advance) and THEN calls on_frame, so by the
//     time on_frame runs, the ticks that were going to see this delta have seen it. Clearing
//     after the poll instead would discard every event the poll just delivered -- the wheel
//     would never work at all, with the suite fully green. Clearing from the reader would
//     hand the tick's second caller zero.
//   * The GATE: `advance` runs ZERO ticks whenever the banked remainder is below fixed_dt,
//     which ordinary timing jitter produces at the default fps == fixed_hz == 60, and which
//     `--fps` above `--fixed-hz` makes the common case. An ungated per-frame clear therefore
//     drops whatever the previous poll delivered at a rate set by an unrelated CLI flag. The
//     gate is the loop's own tick_count: if no tick has run since the last clear, nothing has
//     observed this delta and it must survive the frame boundary.
//
// One consequence is known, deliberate and backlogged for U9d, which is the slice that
// decides what applying a delta means: every tick of ONE `advance` sees the SAME delta, so a
// catch-up frame can show it to as many as eight ticks.
class WheelAccumulator
{
public:
    // From the GLFW scroll callback. May be called many times between two clears; GLFW
    // reports notches as a double, and they accumulate additively up to the saturation cap.
    void add(double y_offset)
    {
        accumulated_ = std::clamp(accumulated_ + static_cast<float>(y_offset),
                                  -kMaxPendingWheel, kMaxPendingWheel);
    }

    // Non-consuming, and const so that it cannot become consuming by accident.
    float value() const { return accumulated_; }

    // Frame-loop driven: `tick_count` is RuntimeLoop::tick_count() as of this frame, read
    // AFTER advance has run. Clears only if it has moved since the last clear, i.e. only if
    // some tick actually observed what is in here. See the class comment for both halves.
    //
    // There is deliberately no unconditional reset(): an ungated clear is the lossy design
    // this gate exists to prevent, and leaving one callable would leave it reachable.
    void drop_observed(std::uint64_t tick_count)
    {
        if (tick_count == last_cleared_at_)
        {
            return; // no tick has run since the last clear -- nothing has seen this yet
        }
        accumulated_     = 0.0f;
        last_cleared_at_ = tick_count;
    }

private:
    float         accumulated_     = 0.0f;
    std::uint64_t last_cleared_at_ = 0;
};
} // namespace mosaic::app
