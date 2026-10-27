#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/run/window_pointer.hpp"

using Catch::Approx;
using mosaic::app::window_pointer_state;

TEST_CASE("a cursor inside the window reports a normalized position", "[app][pointer]")
{
    const auto p = window_pointer_state(400.0, 300.0, 800, 600, false, /*wheel_delta=*/0.0);
    REQUIRE(p.inside);
    REQUIRE(p.x == Approx(0.5f));
    REQUIRE(p.y == Approx(0.5f));
    REQUIRE_FALSE(p.down);
}

TEST_CASE("a cursor outside the window is not present", "[app][pointer]")
{
    // THE WHOLE POINT OF THIS SLICE. Until 2026-10-12 the standalone provider left `inside`
    // at its default true and merely clamped x/y, so a cursor on the desktop still read as a
    // pointer resting against the window edge -- which hovered an edge-anchored button, and
    // made drag_cancel unreachable outside the editor. The editor's provider has always
    // gated presence on the cursor being over its panel; this is the same rule.
    REQUIRE_FALSE(window_pointer_state(-5.0, 300.0, 800, 600, false, /*wheel_delta=*/0.0).inside);
    REQUIRE_FALSE(window_pointer_state(400.0, -5.0, 800, 600, false, /*wheel_delta=*/0.0).inside);
    REQUIRE_FALSE(window_pointer_state(801.0, 300.0, 800, 600, false, /*wheel_delta=*/0.0).inside);
    REQUIRE_FALSE(window_pointer_state(400.0, 601.0, 800, 600, false, /*wheel_delta=*/0.0).inside);
}

TEST_CASE("the window edges count as inside", "[app][pointer]")
{
    // Inclusive, matching topmost_hit's inclusive containment: a pointer exactly on the edge
    // is a pointer on the surface, and an edge-anchored button is hit there.
    REQUIRE(window_pointer_state(0.0, 0.0, 800, 600, false, /*wheel_delta=*/0.0).inside);
    REQUIRE(window_pointer_state(800.0, 600.0, 800, 600, false, /*wheel_delta=*/0.0).inside);
}

TEST_CASE("the button is only down while the cursor is inside", "[app][pointer]")
{
    // The rule that makes drag_cancel reachable in the standalone runtime. GLFW keeps
    // reporting a held button while the cursor is outside a focused window, so without this
    // a drag off the window edge ends as a drag_end carrying a CLAMPED coordinate -- a point
    // the user never pointed at. Dropping `down` with presence turns it into the cancel it
    // actually is.
    REQUIRE(window_pointer_state(400.0, 300.0, 800, 600, true, /*wheel_delta=*/0.0).down);
    REQUIRE_FALSE(window_pointer_state(900.0, 300.0, 800, 600, true, /*wheel_delta=*/0.0).down);
}

TEST_CASE("a position outside the window still clamps into range", "[app][pointer]")
{
    // x/y stay in [0,1] even when absent, so a consumer that reads them without checking
    // `inside` gets a sane number rather than one off the end of the screen. `inside` is what
    // says the position means anything.
    const auto p = window_pointer_state(-50.0, 900.0, 800, 600, false, /*wheel_delta=*/0.0);
    REQUIRE(p.x == Approx(0.0f));
    REQUIRE(p.y == Approx(1.0f));
}

TEST_CASE("a degenerate window reports no pointer at all", "[app][pointer]")
{
    // Minimised, or queried before the window has a size. Returning presence here would
    // divide by zero on the way to a position.
    REQUIRE_FALSE(window_pointer_state(10.0, 10.0, 0, 600, true, /*wheel_delta=*/0.0).inside);
    REQUIRE_FALSE(window_pointer_state(10.0, 10.0, 800, 0, true, /*wheel_delta=*/0.0).inside);
    REQUIRE_FALSE(window_pointer_state(10.0, 10.0, 0, 600, true, /*wheel_delta=*/0.0).down);
}

// ---------------------------------------------------------------------------------------
// U9c: the wheel delta. The policy, then the accumulator that feeds it.
// ---------------------------------------------------------------------------------------

TEST_CASE("the standalone pointer policy reports the wheel delta it is handed",
          "[app][pointer][wheel]")
{
    // Pinned per PointerState::wheel's own rule, the same one `inside` states: a field with a
    // default that every aggregate init leans on needs each provider that sets it pinned,
    // because the default cannot express both intents. Nothing reads `wheel` yet (U9d does),
    // so this test and the editor's are the only things standing between the field and a
    // provider that silently never sets it.
    const auto up = window_pointer_state(400.0, 300.0, 800, 600, false, 3.0);
    REQUIRE(up.wheel == Approx(3.0f));
    // Sign is pass-through: GLFW's yoffset is already positive away from the user, and so is
    // ImGui's io.MouseWheel, so neither provider negates anything. A provider that flipped it
    // would make the two disagree about which way "up" is.
    const auto down = window_pointer_state(400.0, 300.0, 800, 600, false, -2.5);
    REQUIRE(down.wheel == Approx(-2.5f));
    // And the default: no scroll is 0, not "absent".
    REQUIRE(window_pointer_state(400.0, 300.0, 800, 600, false, 0.0).wheel == Approx(0.0f));
}

TEST_CASE("a wheel delta is dropped when the cursor is not on the window",
          "[app][pointer][wheel]")
{
    // GLFW delivers scroll to the FOCUSED window regardless of where the cursor is, and x/y
    // are clamped even when absent -- so without this gate a scroll with the cursor out on
    // the desktop would scroll whichever region sits under the clamped edge position, a place
    // the user is not pointing at. Same rule as `down`, same rule as the editor's `hovered`.
    REQUIRE(window_pointer_state(900.0, 300.0, 800, 600, false, 4.0).wheel == Approx(0.0f));
    REQUIRE(window_pointer_state(-5.0, 300.0, 800, 600, false, 4.0).wheel == Approx(0.0f));
    // A degenerate window has no coordinate space to scroll in either.
    REQUIRE(window_pointer_state(10.0, 10.0, 0, 600, false, 4.0).wheel == Approx(0.0f));
    REQUIRE(window_pointer_state(10.0, 10.0, 800, 0, false, 4.0).wheel == Approx(0.0f));
}

TEST_CASE("WheelAccumulator sums every scroll event between two clears",
          "[app][pointer][wheel]")
{
    // One glfwPollEvents can fire the scroll callback any number of times -- a fast flick on
    // a free-spinning wheel delivers several notches. Taking the last would lose most of the
    // gesture.
    mosaic::app::WheelAccumulator w;
    REQUIRE(w.value() == Approx(0.0f));
    w.add(1.0);
    w.add(2.0);
    w.add(-0.5);
    REQUIRE(w.value() == Approx(2.5f));
    // A tick ran, so the delta has been observed and is dropped. tick_count starts at 0 and
    // any move off it counts.
    w.drop_observed(1);
    REQUIRE(w.value() == Approx(0.0f));
}

TEST_CASE("reading the wheel accumulator does not consume it", "[app][pointer][wheel]")
{
    // THE ORACLE OF THIS SLICE, and it was written down before anything was measured.
    //
    // The pointer provider is invoked TWICE PER TICK -- UiSystem::update (ui_system.hpp) and
    // ClickNudgeSystem::update (app/run/click_nudge.hpp) -- and app/main.cpp says in words
    // that they MUST agree about the pointer within a tick, because the UI-claim arbitration
    // rests on it. The tempting design (reading the accumulator takes its contents) hands the
    // second caller 0 and breaks exactly that, silently, with every test green: no existing
    // test calls the provider twice without a tick between.
    //
    // So: if the second read below sees 0 where the first saw 3, the accumulator is being
    // consumed by the read and this slice is wrong.
    mosaic::app::WheelAccumulator w;
    w.add(3.0);

    auto provider = [&w] {
        return window_pointer_state(400.0, 300.0, 800, 600, false, w.value());
    };

    const float first  = provider().wheel; // UiSystem
    const float second = provider().wheel; // ClickNudgeSystem, same tick
    REQUIRE(first == Approx(3.0f));
    REQUIRE(second == Approx(first));
    // And it survives arbitrarily many reads, because RuntimeLoop::advance can run up to
    // eight fixed ticks before on_frame gets to reset anything -- sixteen reads in one frame.
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(provider().wheel == Approx(3.0f));
    }
}

TEST_CASE("the frame loop, not the reader, is what ends a wheel delta's life",
          "[app][pointer][wheel]")
{
    // Models FramePump::run's real order, which is: advance(world, dt) -- zero or more fixed
    // ticks, each reading the provider twice -- and THEN on_frame, which clears and polls.
    // So a frame's scroll events are seen by the NEXT frame's ticks, and the clear at the top
    // of on_frame drops exactly what those ticks already saw.
    //
    // This is the mutation target named in the slice's gate: drop the clear and the delta
    // persists into the next tick, i.e. one flick of the wheel scrolls forever.
    mosaic::app::WheelAccumulator w;
    auto tick_reads = [&w] {
        return window_pointer_state(400.0, 300.0, 800, 600, false, w.value()).wheel;
    };
    std::uint64_t ticks = 0;

    // Frame 1: one tick runs, before anything has been polled.
    ++ticks;
    REQUIRE(tick_reads() == Approx(0.0f));
    // ...then on_frame: clear (a tick ran), then poll, which delivers three notches.
    w.drop_observed(ticks);
    w.add(3.0);

    // Frame 2: one tick, and every reader within it agrees.
    ++ticks;
    REQUIRE(tick_reads() == Approx(3.0f));
    REQUIRE(tick_reads() == Approx(3.0f));
    // ...then on_frame: clear, then a poll that delivers nothing.
    w.drop_observed(ticks);

    // Frame 3: THE ASSERTION THE MUTATION MUST BREAK. The delta is gone, because the frame
    // loop ended it. If the clear were a no-op, this reads 3.0f and the user scrolls forever
    // from one flick.
    ++ticks;
    REQUIRE(tick_reads() == Approx(0.0f));
}

TEST_CASE("a frame that runs no ticks keeps its wheel delta", "[app][pointer][wheel]")
{
    // THE ZERO-TICK DROP, closed inside U9c rather than left to U9d.
    //
    // RuntimeLoop::advance runs zero ticks whenever the banked remainder is below fixed_dt.
    // At the DEFAULT options (fps == fixed_hz == 60, options.hpp:45-46) the accumulator sits
    // right at fixed_dt, so ordinary timing jitter alone produces zero-tick frames; and
    // `--fps` above `--fixed-hz` makes them the majority. An ungated per-frame clear
    // therefore discards whatever the previous poll delivered, at a rate set by an unrelated
    // CLI flag -- a wheel that drops a jitter-determined fraction of the user's notches.
    //
    // Worse, it would have been handed to U9d, which adds the first consumer AND carries the
    // human's eye check on a real scroll: "scrolling feels like it stutters" would have been
    // indistinguishable from a bug in U9d's own offset arithmetic.
    //
    // So the clear is gated on the loop's tick_count having moved. This is the mutation
    // target for that gate: remove the gate and the notch below is lost.
    mosaic::app::WheelAccumulator w;
    auto tick_reads = [&w] {
        return window_pointer_state(400.0, 300.0, 800, 600, false, w.value()).wheel;
    };
    std::uint64_t ticks = 1; // one tick has run at some point before this frame

    // Frame 1's on_frame: a tick ran, so clear; then the poll delivers two notches.
    w.drop_observed(ticks);
    w.add(2.0);

    // Frame 2: advance banks time but runs NO tick, so tick_count does not move and nothing
    // has read the delta. on_frame must NOT clear it.
    w.drop_observed(ticks); // same count: no tick ran
    w.add(1.0);             // and this frame's poll adds another notch
    // Frame 3: likewise no tick.
    w.drop_observed(ticks);

    // Frame 4: a tick finally runs, and it must see the WHOLE gesture -- both notches.
    ++ticks;
    REQUIRE(tick_reads() == Approx(3.0f));
    // ...and only now is it dropped.
    w.drop_observed(ticks);
    REQUIRE(w.value() == Approx(0.0f));
}

TEST_CASE("an unconsumed wheel delta saturates instead of growing without bound",
          "[app][pointer][wheel]")
{
    // The price of gating the clear on a tick: a stretch of frames with no tick accumulates
    // with nothing to stop it. Reachable, not hypothetical -- a low `--fixed-hz` leaves
    // hundreds of frames between ticks, and `--fps` above `--fixed-hz` makes zero-tick frames
    // the common case.
    //
    // Letting it grow would hand U9d's first consumer a delta whose magnitude encodes how
    // long the sim was stalled, which is not information about what the user did. So it
    // saturates at kMaxPendingWheel, keeping its sign.
    mosaic::app::WheelAccumulator w;
    for (int i = 0; i < 10000; ++i)
    {
        w.add(1.0);
        w.drop_observed(0); // no tick ever runs: tick_count never moves off its initial 0
    }
    REQUIRE(w.value() == Approx(mosaic::app::kMaxPendingWheel));

    // And the saturation is not a one-way latch: scrolling back still cancels, one notch per
    // notch, because the cap clamps the SUM rather than discarding events.
    w.add(-1.0);
    REQUIRE(w.value() == Approx(mosaic::app::kMaxPendingWheel - 1.0f));

    // Symmetric in the negative direction.
    mosaic::app::WheelAccumulator down;
    for (int i = 0; i < 10000; ++i) { down.add(-1.0); }
    REQUIRE(down.value() == Approx(-mosaic::app::kMaxPendingWheel));
}
