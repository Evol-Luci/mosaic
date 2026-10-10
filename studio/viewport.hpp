#pragma once

#include <cstdint>
#include <functional>

#include "engine/render/render_device.hpp"
#include "engine/ui/pointer.hpp"

namespace mosaic::studio
{

struct ViewportSize
{
    std::uint32_t width;
    std::uint32_t height;
};

// The raw per-frame facts ImGui reports about the viewport image. No policy -- every
// suppression rule lives in ViewportPanel::pointer_state, which is what lets the rules be
// tested with no window and no GPU.
struct ViewportPointerSample
{
    float mouse_x = 0.0f, mouse_y = 0.0f;       // screen space, ImGui::GetMousePos
    float rect_min_x = 0.0f, rect_min_y = 0.0f; // the image's top-left, ImGui::GetItemRectMin
    // ImGui::IsItemHovered() on the image, and it carries MORE than its name suggests --
    // which is why there is no separate "is ImGui claiming the mouse" field here.
    //
    // There WAS one, fed from ImGui::GetIO().WantCaptureMouse, and it made this feature
    // SHIPPED DEAD. WantCaptureMouse is not "ImGui wants the mouse for a widget", it is
    // "the cursor is over ANY ImGui window, or any button is held"
    // (imgui.cpp:5184 -- `(mouse_avail && (g.HoveredWindow != NULL || mouse_any_down))
    // || has_open_popup`). The Viewport panel IS an ImGui window, so hovering the scene
    // image sets it. And IsItemHovered() returns false unless g.HoveredWindow == window
    // (imgui.cpp:4575). So `hovered && !capturing` was mutually exclusive and `down`
    // was unreachable. Found in review 2026-10-07; every test fed the flag by hand, so
    // nothing could be red.
    //
    // IsItemHovered() already encodes every case the removed flag was meant to cover:
    // another item active, e.g. a slider being dragged (imgui.cpp:4581, which excludes
    // MoveId/TabId so a drag INSIDE the viewport still counts as hovered); the viewport
    // window sitting behind another; and an open modal, which clears HoveredWindow.
    bool  hovered = false;
    bool  button_down = false;                  // the raw left button, ungated
};

// Tracks the Viewport panel's measured content size vs. the size last applied to the
// device's offscreen target, so the editor can decide when to resize. One-frame
// latency: the size measured during frame N feeds the offscreen pass of frame N+1.
class ViewportPanel
{
public:
    // Record ImGui's content-region avail for this frame. NaN / < 1 / negative inputs
    // floor to 1 so the offscreen target is never degenerate; fractional sizes truncate.
    void set_measured_size(float avail_w, float avail_h)
    {
        measured_.width  = to_extent(avail_w);
        measured_.height = to_extent(avail_h);
    }

    ViewportSize size() const { return measured_; }

    bool needs_resize() const
    {
        return measured_.width != applied_.width || measured_.height != applied_.height;
    }

    void mark_applied() { applied_ = measured_; }

    float aspect() const
    {
        return static_cast<float>(measured_.width) / static_cast<float>(measured_.height);
    }

    // Record this frame's raw pointer facts, and advance the press latch.
    //
    // Takes `play_active` because the latch has to judge eligibility at the MOMENT a press
    // begins, and play mode is part of that -- see the latch comment below for why that
    // matters more than it looks.
    //
    // ONE FRAME OF LATENCY, inherited rather than invented: editor.cpp's on_frame ticks the
    // world BEFORE drawing the UI, so a tick reads the sample captured during the previous
    // frame's draw (~16ms at 60Hz). That is the same latency this class already documents
    // for sizing, where the size measured in frame N feeds frame N+1's offscreen pass.
    void set_pointer_sample(const ViewportPointerSample& s, bool play_active)
    {
        // THE LATCH, and omitting it produces a click the user never made.
        //
        // ClickNudgeSystem fires on a press EDGE (`p.down && !was_down_`), so any
        // false->true transition of `down` that is not a fresh physical press is a
        // synthetic click landing wherever the cursor happens to sit. Hold the left button
        // in edit mode and then press Play: without this, `down` flips true with no new
        // press. Same for dragging an inspector slider and releasing over the viewport.
        //
        // So a press that began while anything made it ineligible stays blocked until the
        // raw button goes UP. Judged here, against the PREVIOUS frame's button, because
        // this is the only place that sees every frame.
        if (!s.button_down)
        {
            press_blocked_ = false; // released: the next press starts clean
        }
        else if (!pointer_.button_down)
        {
            press_blocked_ = !(play_active && s.hovered);
        }
        // A press already in flight keeps the verdict it was given. That is the point:
        // becoming eligible mid-press must not retroactively make it a click.
        pointer_ = s;
    }

    // The pointer as the engine should see it: panel-relative, normalised to [0,1] with a
    // TOP-LEFT origin, with `down` gated by every suppression rule.
    //
    // `play_active` is a PARAMETER rather than stored state, so this header stays free of
    // PlayState (owned by StudioApp) and of ImGui. That is the whole reason the policy is
    // testable headlessly.
    engine::ui::PointerState pointer_state(bool play_active) const
    {
        engine::ui::PointerState p;
        // Divide by the MEASURED size, which to_extent floors at 1, so this cannot divide
        // by zero even on the first frame before ImGui has measured anything.
        const float w = static_cast<float>(measured_.width);
        const float h = static_cast<float>(measured_.height);
        p.x = clamp01((pointer_.mouse_x - pointer_.rect_min_x) / w);
        // No Y flip. PointerState is top-left-origin by contract, and
        // ClickNudgeSystem::pointer_to_ndc_y already flips it on the way to NDC -- flipping
        // here too would double-flip and drive the vertically mirrored button.
        p.y = clamp01((pointer_.mouse_y - pointer_.rect_min_y) / h);
        // Re-checked here as well as in the latch, and not redundantly: the latch decides
        // whether a press was legitimate WHEN IT STARTED, these decide whether it is still
        // legitimate NOW. Play stopping mid-press, or the cursor leaving the panel
        // mid-press, must stop reporting `down` immediately; the latch alone would keep
        // reporting it.
        p.down = play_active && pointer_.hovered && pointer_.button_down && !press_blocked_;
        // Presence is NOT gated on play_active: a stopped editor still has a cursor, and
        // conflating the two would mean "no pointer" and "not playing" could not be told
        // apart. It IS gated on `hovered`, because x/y above are clamped to the panel --
        // so without this, a cursor that moved onto the Inspector kept reporting a position
        // inside the viewport, and hover latched an edge-anchored button lit while the user
        // worked elsewhere. The default sample (Viewport closed, collapsed, or without a
        // texture) has hovered == false and so reads as absent rather than as (0,0).
        p.inside = pointer_.hovered;
        return p;
    }

private:
    static float clamp01(float v)
    {
        if (!(v > 0.0f)) { return 0.0f; } // also catches NaN
        return (v < 1.0f) ? v : 1.0f;
    }

    static std::uint32_t to_extent(float v)
    {
        if (!(v >= 1.0f)) // catches NaN, negatives, and < 1
        {
            return 1u;
        }
        return static_cast<std::uint32_t>(v);
    }

    ViewportSize measured_{1, 1};
    ViewportSize applied_{0, 0}; // 0,0 == nothing applied yet -> first frame needs a resize
    ViewportPointerSample pointer_{};
    // True when the press currently in flight began while something made it ineligible.
    // Cleared only by the raw button going up, so it cannot be escaped by changing mode or
    // by moving the cursor.
    bool press_blocked_ = false;
};

// Records the offscreen scene pass in the canonical order. `draw_scene` is invoked
// between begin_viewport_pass and end_viewport_pass. A degenerate size renders nothing.
inline void render_scene_to_viewport(
    engine::render::RenderDevice&                              device,
    const ViewportPanel&                                      panel,
    const engine::render::Color&                              clear,
    const std::function<void(engine::render::RenderDevice&)>& draw_scene)
{
    const ViewportSize s = panel.size();
    if (s.width == 0 || s.height == 0)
    {
        return;
    }
    device.set_viewport_size(s.width, s.height);
    device.begin_viewport_pass(clear);
    draw_scene(device);
    device.end_viewport_pass();
}

} // namespace mosaic::studio
