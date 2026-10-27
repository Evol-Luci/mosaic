#pragma once

namespace mosaic::engine::ui
{
// Per-tick pointer state in normalized screen space (top-left origin, matching UI
// rects). down = primary button pressed. Supplied by an injected provider; the engine
// never reads a window/mouse directly.
struct PointerState
{
    float x = 0.0f, y = 0.0f;
    bool  down = false;
    // Whether a pointer is actually PRESENT over the surface this state describes.
    //
    // Without this, "there is no pointer" had to be spelled as a zeroed state -- and
    // {0, 0} is a real position inside any button anchored TopLeft at {0,0}, which is
    // precisely what a default-constructed UiButton is. So every provider's "no pointer"
    // answer hovered a default-positioned button: headless `mosaic run` forever (the GLFW
    // window is never created), and in the editor whenever the cursor left the viewport
    // panel, where the position clamps to the panel edge instead.
    //
    // Defaults to TRUE so that existing `{x, y, down}` aggregate initialisation keeps
    // meaning "a pointer is here", which is what every such call site intends. A provider
    // that means "no pointer" must say so explicitly -- and each one that does is pinned by
    // a test, because a default cannot express both intents safely.
    bool  inside = true;
    // Wheel movement accumulated since the last frame boundary, in notches. Positive is
    // scroll UP / away from the user, which is what BOTH sources already mean: GLFW's
    // scroll-callback `yoffset` and ImGui's `io.MouseWheel` agree on that sign, so no
    // provider negates anything. Horizontal wheel is deliberately not carried (backlog).
    //
    // LAST, and that position is the point. `{x, y, down}` and `{0.0f, 0.0f, false, false}`
    // both appear in this repo, and a field inserted anywhere earlier re-binds them
    // SILENTLY: `false` is an integral constant representable in float, so bool -> float is
    // not a narrowing conversion and gcc diagnoses nothing. The default 0.0f then keeps
    // every existing aggregate initialisation meaning exactly what it means today -- "no
    // wheel movement this tick" -- which, unlike `inside`, is also the only intent a default
    // could have here: there is no "absent" wheel to tell apart from a still one.
    //
    // READING THIS DOES NOT CONSUME IT, and no provider may make it do so. The provider is
    // invoked TWICE PER TICK -- UiSystem::update (ui_system.hpp) and
    // ClickNudgeSystem::update (app/run/click_nudge.hpp) -- and app/main.cpp's provider
    // comment requires them to agree about the pointer within a tick, because that is what
    // the UI-claim arbitration rests on. A read that drained an accumulator would hand the
    // second caller 0 and break exactly that. The reset is driven by the FRAME LOOP instead:
    // see WheelAccumulator in app/run/window_pointer.hpp for the standalone runtime, and
    // ViewportPanel::set_pointer_sample for the editor (where ImGui has already done the
    // accumulating, and pointer_state is const and so cannot consume anything).
    float wheel = 0.0f;
};
} // namespace mosaic::engine::ui
