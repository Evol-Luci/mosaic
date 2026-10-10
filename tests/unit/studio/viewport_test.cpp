#include <cmath>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/render_device.hpp"
#include "studio/viewport.hpp"
#include "../render/shader_source.hpp"

using Catch::Approx;
using mosaic::studio::ViewportPanel;
using mosaic::studio::ViewportSize;

TEST_CASE("ViewportPanel starts wanting a resize with a 1x1 min size", "[studio][viewport]")
{
    ViewportPanel panel;
    REQUIRE(panel.size().width == 1u);
    REQUIRE(panel.size().height == 1u);
    REQUIRE(panel.needs_resize()); // nothing applied yet
    REQUIRE(panel.aspect() == Approx(1.0f));
}

TEST_CASE("ViewportPanel records measured size and detects change", "[studio][viewport]")
{
    ViewportPanel panel;
    panel.set_measured_size(800.0f, 600.0f);
    REQUIRE(panel.size().width == 800u);
    REQUIRE(panel.size().height == 600u);
    REQUIRE(panel.aspect() == Approx(800.0f / 600.0f));
    REQUIRE(panel.needs_resize());

    panel.mark_applied();
    REQUIRE_FALSE(panel.needs_resize());

    panel.set_measured_size(800.0f, 600.0f); // same
    REQUIRE_FALSE(panel.needs_resize());

    panel.set_measured_size(1024.0f, 768.0f); // changed
    REQUIRE(panel.needs_resize());
}

TEST_CASE("ViewportPanel clamps degenerate and fractional sizes", "[studio][viewport]")
{
    ViewportPanel panel;
    panel.set_measured_size(0.0f, 0.0f);
    REQUIRE(panel.size().width == 1u);
    REQUIRE(panel.size().height == 1u);

    panel.set_measured_size(-5.0f, 3.9f);
    REQUIRE(panel.size().width == 1u);   // negative -> 1
    REQUIRE(panel.size().height == 3u);  // truncated toward zero

    panel.set_measured_size(100.9f, 50.2f);
    REQUIRE(panel.size().width == 100u);
    REQUIRE(panel.size().height == 50u);
}

using mosaic::engine::render::Color;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::ViewportTextureId;
using mosaic::engine::render::TextureColorSpace;

namespace
{
// Records the sequence of device calls the viewport seam makes.
class RecordingDevice : public RenderDevice
{
public:
    std::vector<std::string> calls;
    ViewportTextureId        tex = 7; // non-zero sentinel

    void          begin_frame() override {}
    void          clear(const Color&) override {}
    void          end_frame() override {}
    MeshHandle    create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override
    {
        return 1;
    }
    TextureHandle create_texture(const Image&, TextureColorSpace) override { return 1; }
    void          draw(MeshHandle, TextureHandle, const DrawParams&) override
    {
        calls.emplace_back("draw");
    }
    void set_viewport_size(std::uint32_t w, std::uint32_t h) override
    {
        calls.emplace_back("size:" + std::to_string(w) + "x" + std::to_string(h));
    }
    void              begin_viewport_pass(const Color&) override { calls.emplace_back("begin_vp"); }
    void              end_viewport_pass() override { calls.emplace_back("end_vp"); }
    ViewportTextureId viewport_texture() const override { return tex; }
};
} // namespace

TEST_CASE("render_scene_to_viewport records the offscreen pass in order", "[studio][viewport]")
{
    RecordingDevice               device;
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(640.0f, 480.0f);

    mosaic::studio::render_scene_to_viewport(
        device, panel, Color{0.1f, 0.1f, 0.12f, 1.0f},
        [](RenderDevice& d) { d.draw(1, 1, DrawParams{}); });

    REQUIRE(device.calls == std::vector<std::string>{"size:640x480", "begin_vp", "draw", "end_vp"});
    REQUIRE(device.viewport_texture() == static_cast<ViewportTextureId>(7));
}

TEST_CASE("ViewportPanel reports the cursor relative to the panel with a top-left origin",
          "[studio][viewport]")
{
    // The panel is 800x600 at screen (100, 50). PointerState is normalised [0,1] with a
    // TOP-LEFT origin, matching what engine/ui/pointer.hpp documents and what UiButton
    // rects use.
    //
    // The off-centre assertions are the point. A centre-only check passes with the Y axis
    // inverted, because the centre is its own mirror -- and an inverted provider still
    // "works", it just drives the vertically mirrored button. ClickNudgeSystem already
    // owns the flip to NDC in pointer_to_ndc_y, so flipping here too would double-flip.
    ViewportPanel panel;
    panel.set_measured_size(800.0f, 600.0f);

    mosaic::studio::ViewportPointerSample s;
    s.rect_min_x = 100.0f;
    s.rect_min_y = 50.0f;
    s.hovered = true;
    s.button_down = false;

    s.mouse_x = 500.0f; s.mouse_y = 350.0f; // exact centre
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.5f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.5f));

    s.mouse_x = 100.0f; s.mouse_y = 50.0f;  // top-left corner
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.0f));

    s.mouse_x = 900.0f; s.mouse_y = 650.0f; // bottom-right corner
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(1.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(1.0f));

    // The asymmetric case that pins the origin: upper quarter must be y = 0.25, not 0.75.
    s.mouse_x = 300.0f; s.mouse_y = 200.0f;
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.25f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.25f));
}

TEST_CASE("ViewportPanel clamps the reported cursor into the unit range",
          "[studio][viewport]")
{
    // The cursor can sit outside the panel while a press is still being tracked, and
    // main.cpp's GLFW provider already clamps for the same reason. Unclamped values let a
    // UiButton rect match outside itself and make ClickNudgeSystem's NDC exceed [-1,1],
    // producing a camera ray that misses the frustum entirely.
    ViewportPanel panel;
    panel.set_measured_size(200.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.rect_min_x = 0.0f; s.rect_min_y = 0.0f; s.hovered = true;

    s.mouse_x = -50.0f; s.mouse_y = -10.0f;
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(0.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(0.0f));

    s.mouse_x = 10000.0f; s.mouse_y = 10000.0f;
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).x == Approx(1.0f));
    REQUIRE(panel.pointer_state(true).y == Approx(1.0f));
}

TEST_CASE("ViewportPanel survives a degenerate one-by-one panel", "[studio][viewport]")
{
    // to_extent floors the stored size at 1, so this is the smallest reachable panel --
    // it happens on the first frame before ImGui has measured anything. The normalisation
    // divides by the size, so a 0 would be a divide-by-zero and a NaN would propagate
    // into every UiButton hit test as a silently-never-matching rect.
    ViewportPanel panel;
    panel.set_measured_size(0.0f, 0.0f); // floors to 1x1
    REQUIRE(panel.size().width == 1u);
    mosaic::studio::ViewportPointerSample s;
    s.rect_min_x = 0.0f; s.rect_min_y = 0.0f; s.mouse_x = 0.5f; s.mouse_y = 0.5f;
    s.hovered = true;
    panel.set_pointer_sample(s, true);
    const auto p = panel.pointer_state(true);
    REQUIRE(std::isfinite(p.x));
    REQUIRE(std::isfinite(p.y));
    REQUIRE(p.x >= 0.0f);
    REQUIRE(p.x <= 1.0f);
    REQUIRE(p.y >= 0.0f);
    REQUIRE(p.y <= 1.0f);
}

TEST_CASE("ViewportPanel stops reporting a press the moment a condition stops holding",
          "[studio][viewport]")
{
    // One case per condition, and the SHAPE matters more than the conditions do.
    //
    // The naive construction -- record a press with one condition already unmet, assert no
    // down -- cannot distinguish the gate in pointer_state from the latch in
    // set_pointer_sample, because a press that STARTS ineligible is blocked by the latch
    // regardless. Dropping `pointer_.hovered &&` from pointer_state would leave that test
    // green, so it would be a guard that guards nothing.
    //
    // So each case starts a LEGITIMATE press -- every condition met, latch clear -- and then
    // removes one condition mid-press. Only pointer_state's gate can make `down` false
    // then, which is exactly what each probe deletes. These are also the real scenarios:
    // press in the viewport and drag off it; press and then hit Stop.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    auto held = [] {
        mosaic::studio::ViewportPointerSample s;
        s.mouse_x = 50.0f; s.mouse_y = 50.0f;
        s.hovered = true; s.button_down = true;
        return s;
    };

    SECTION("a legitimate press is reported")
    {
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
    }
    SECTION("play stops mid-press -> no down")
    {
        // The slice's behaviour decision, enforced while a press is in flight. Press, then
        // hit Stop without releasing: the scene must stop seeing the press at once.
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
        panel.set_pointer_sample(held(), false);
        REQUIRE_FALSE(panel.pointer_state(false).down);
    }
    SECTION("cursor leaves the panel mid-press -> no down")
    {
        panel.set_pointer_sample(held(), true);
        REQUIRE(panel.pointer_state(true).down);
        auto s = held(); s.hovered = false;
        panel.set_pointer_sample(s, true);
        REQUIRE_FALSE(panel.pointer_state(true).down);
    }
    SECTION("button released -> no down")
    {
        panel.set_pointer_sample(held(), true);
        auto s = held(); s.button_down = false;
        panel.set_pointer_sample(s, true);
        REQUIRE_FALSE(panel.pointer_state(true).down);
    }
}

TEST_CASE("ViewportPanel suppresses a press that began while ineligible",
          "[studio][viewport]")
{
    // The subtle one, and omitting it produces a click the user never made.
    //
    // ClickNudgeSystem fires on a press EDGE, so any false->true transition of `down` that
    // is not a fresh physical press is a synthetic click landing wherever the cursor
    // happens to be. Hold the left button in edit mode, press Play, and without this latch
    // `down` flips true with no new press.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f; s.hovered = true;

    s.button_down = true;                                  // frame 1: held in EDIT mode
    panel.set_pointer_sample(s, /*play_active=*/false);
    REQUIRE_FALSE(panel.pointer_state(false).down);

    panel.set_pointer_sample(s, /*play_active=*/true);     // frame 2: now playing, still held
    REQUIRE_FALSE(panel.pointer_state(true).down);

    panel.set_pointer_sample(s, /*play_active=*/true);     // frame 3: the latch does not time out
    REQUIRE_FALSE(panel.pointer_state(true).down);

    s.button_down = false;                                 // frame 4: released, latch clears
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    s.button_down = true;                                  // frame 5: a genuine fresh press
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE(panel.pointer_state(true).down);
}

TEST_CASE("ViewportPanel suppresses a press dragged in from outside the panel",
          "[studio][viewport]")
{
    // The same synthesis reached by moving the cursor rather than by changing modes. Press
    // on the Inspector, drag onto the viewport, release. Without the latch, `hovered` going
    // true mid-press flips `down` true and nudges whatever is under the cursor.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f;

    s.hovered = false; s.button_down = true; // pressed elsewhere
    panel.set_pointer_sample(s, true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    s.hovered = true;                        // dragged over the viewport, still held
    panel.set_pointer_sample(s, true);
    REQUIRE_FALSE(panel.pointer_state(true).down);

    s.button_down = false;                   // released
    panel.set_pointer_sample(s, true);
    s.button_down = true;                    // fresh press, now over the viewport
    panel.set_pointer_sample(s, true);
    REQUIRE(panel.pointer_state(true).down);
}

TEST_CASE("ViewportPanel reports coordinates even with no press", "[studio][viewport]")
{
    // Hover-driven UI needs the position without a press -- a UiButton highlighting under
    // the cursor, for instance. A provider that zeroed x/y when `down` is false would make
    // every hover effect snap to the panel's top-left corner.
    ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 75.0f; s.mouse_y = 25.0f; s.hovered = true; s.button_down = false;
    panel.set_pointer_sample(s, true);
    const auto p = panel.pointer_state(true);
    REQUIRE_FALSE(p.down);
    REQUIRE(p.x == Approx(0.75f));
    REQUIRE(p.y == Approx(0.25f));
}

// ---------------------------------------------------------------------------------------
// Textual pins for the wiring decisions CI cannot execute: the four lines that READ ImGui
// and the two provider choices in editor.cpp. Same instrument the Vulkan-backend and
// shader contract tests use, and for the same reason -- no window, no GPU, no ImGui
// context here. The policy these feed is fully covered by the cases above; this only
// pins that the right facts are fed in and the right providers are chosen.
// ---------------------------------------------------------------------------------------

namespace
{
// Comments STRIPPED, via the same helper the shader contract tests use. That is not
// tidiness: the first version of these cases searched raw text and failed, because
// studio_app.cpp's comment explains why ImGui::GetMousePos is used INSTEAD of
// glfwGetCursorPos -- so a case forbidding glfwGetCursorPos tripped on the sentence
// documenting that very decision. shader_source.hpp's own comment records the identical
// lesson from 2026-10-04: "a test that forbids documenting the decision it enforces is the
// wrong test."
std::string read_repo_source(const std::string& rel)
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/../" + rel;
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return mosaic::tests::normalize_ws(mosaic::tests::code_only(ss.str()));
}
} // namespace

TEST_CASE("the editor gates click-to-select on not playing", "[studio][wiring]")
{
    // Without the gate one play-mode click both nudges the body under the cursor AND
    // re-selects it, so the Inspector jumps to whatever the user just poked. That reads as
    // an editor quirk rather than a bug, and no headless test can see it.
    const std::string src = read_repo_source("studio/studio_app.cpp");
    INFO("studio_app.cpp must gate the viewport pick on !play_.is_playing()");
    REQUIRE(src.find("if (!play_.is_playing() && ImGui::IsItemClicked())") != std::string::npos);
}

TEST_CASE("the editor sources the nudge aspect from the viewport panel", "[studio][wiring]")
{
    // Silent and position-dependent: a window-derived aspect makes ClickNudgeSystem's
    // camera ray diverge from the image the user clicked, picking a different object the
    // further the cursor is from centre. Wrong whenever the viewport is not the whole
    // window, which is always.
    const std::string src = read_repo_source("app/run/editor.cpp");
    // The whole assignment, not just the expression: editor.cpp already calls
    // `vp.aspect()` elsewhere for the render system, so a refactor that spelled THAT one
    // `studio.viewport().aspect()` while switching pointer_deps.aspect to a window-derived
    // value would leave a bare-expression pin green and the camera ray wrong.
    REQUIRE(src.find("pointer_deps.aspect = [&studio] { return studio.viewport().aspect(); }")
            != std::string::npos);
}

TEST_CASE("the editor feeds the viewport sample from ImGui and not from GLFW",
          "[studio][wiring]")
{
    // Both systems must see the same pointer within one tick. ImGui's GetMousePos is
    // frame-snapshotted; glfwGetCursorPos is not, so reading it here could hand UiSystem
    // and ClickNudgeSystem different positions inside a single tick -- and the UI-claim
    // arbitration would then be declining a different click than the one ClickNudgeSystem
    // acts on.
    const std::string src = read_repo_source("studio/studio_app.cpp");
    REQUIRE(src.find("ImGui::GetMousePos()") != std::string::npos);
    REQUIRE(src.find("ImGui::IsItemHovered()") != std::string::npos);
    REQUIRE(src.find("set_pointer_sample(sample, play_.is_playing())") != std::string::npos);
    INFO("studio_app.cpp must not read the cursor from GLFW -- it is not frame-snapshotted");
    REQUIRE(src.find("glfwGetCursorPos") == std::string::npos);

    // AND must not read WantCaptureMouse. This pin is inverted on purpose, because the
    // first version of this slice ASSERTED its presence and thereby cemented a bug that
    // made the whole feature unreachable.
    //
    // WantCaptureMouse reads like "ImGui wants the mouse for a widget". It is not: it is
    // true whenever the cursor is over ANY ImGui window, or any button is held
    // (imgui.cpp:5184). The Viewport panel is an ImGui window, and IsItemHovered() is
    // false unless g.HoveredWindow == that window (imgui.cpp:4575) -- so
    // `hovered && !capturing` could never both hold and `down` was unreachable. With
    // click-to-select also gated on !is_playing(), the net effect was that a play-mode
    // viewport click did NOTHING, where before the slice it selected an entity.
    //
    // No headless test could see it: every case feeds the sample by hand. This pin is the
    // only automated guard, which is exactly why it must forbid rather than require.
    REQUIRE(src.find("WantCaptureMouse") == std::string::npos);
}

TEST_CASE("the editor hands the same provider to both pointer consumers",
          "[studio][wiring]")
{
    // The backlog's warning: UiSystem and ClickNudgeSystem must share one provider or the
    // UI-claim arbitration breaks. Two separately-constructed lambdas reading the same
    // cached sample would in fact agree -- but one named provider passed twice makes that
    // structural rather than coincidental, and this pins it.
    const std::string src = read_repo_source("app/run/editor.cpp");
    REQUIRE(src.find("auto viewport_pointer = [&studio]") != std::string::npos);
    // Pinned at the CALL SITE, not as a bare `viewport_pointer)` substring. That weaker
    // form was satisfied by `pointer_deps.pointer = std::move(viewport_pointer);` alone --
    // i.e. it survived deleting the setup_runtime argument, leaving UiSystem holding the
    // default {}, which is the exact bug this case exists to prevent.
    REQUIRE(src.find("std::move(audio_backend), viewport_pointer)") != std::string::npos);
    REQUIRE(src.find("pointer_deps.pointer = viewport_pointer;") != std::string::npos);
    // `true` and not `/*windowed=*/true`: comments are stripped before the search, so the
    // inline argument comment is gone by the time this runs. Whitespace is normalised too,
    // so a reformat of the call cannot break this.
    REQUIRE(src.find("register_windowed_systems(world, true, std::move(pointer_deps))")
            != std::string::npos);
}

TEST_CASE("the viewport provider reports no pointer when the panel is not hovered",
          "[studio][viewport]")
{
    // pointer_state gated only `down` on `hovered`; x and y were clamped unconditionally.
    // So moving the cursor off the viewport onto another panel kept reporting a position
    // INSIDE the viewport -- clamped to its edge, or to (0,0) leaving up-left -- and with
    // hover events (U6) that latches an edge-anchored button lit while the user works
    // elsewhere. No hover_exit ever fires.
    //
    // The signal already existed: ViewportPointerSample::hovered was already sampled from
    // ImGui and already consulted for `down`. It just was not consulted for presence.
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);

    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f;

    s.hovered = true;
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE(panel.pointer_state(true).inside);

    s.hovered = false;                       // cursor left the viewport panel
    panel.set_pointer_sample(s, /*play_active=*/true);
    REQUIRE_FALSE(panel.pointer_state(true).inside);

    // And the default sample -- what StudioApp pushes when the Viewport window is closed,
    // collapsed or has no texture -- must also read as absent rather than as (0,0).
    panel.set_pointer_sample(mosaic::studio::ViewportPointerSample{}, /*play_active=*/true);
    REQUIRE_FALSE(panel.pointer_state(true).inside);
}
