#pragma once

#include <algorithm>

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
inline mosaic::engine::ui::PointerState window_pointer_state(double cursor_x, double cursor_y,
                                                             int window_w, int window_h,
                                                             bool button_down)
{
    mosaic::engine::ui::PointerState p;
    if (window_w <= 0 || window_h <= 0)
    {
        // Minimised, or asked before the window has a size. Not merely "no pointer": there is
        // no coordinate space to report one in, and dividing by it is how that becomes a
        // crash rather than a wrong answer.
        return {0.0f, 0.0f, false, false};
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
    return p;
}
} // namespace mosaic::app
