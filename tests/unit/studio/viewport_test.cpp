#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/render_device.hpp"
#include "studio/viewport.hpp"

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
    TextureHandle create_texture(const Image&) override { return 1; }
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
