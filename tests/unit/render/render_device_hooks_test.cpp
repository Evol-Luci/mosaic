#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/render_device.hpp"
#include "foundation/math/mat4.hpp"

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
// A device implementing only the required pure virtuals; the viewport hooks
// keep their base-class defaults.
class MinimalDevice : public RenderDevice
{
public:
    void       begin_frame() override {}
    void       clear(const Color&) override {}
    void       end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>&, const std::vector<std::uint32_t>&) override
    {
        return 1;
    }
    TextureHandle create_texture(const Image&, TextureColorSpace = TextureColorSpace::Srgb) override { return 1; }
    void          draw(MeshHandle, TextureHandle, const DrawParams&) override {}
};
} // namespace

TEST_CASE("viewport hooks default to no-op and no texture", "[render][viewport]")
{
    MinimalDevice device;
    // Default-no-op: these must be safe to call and must not throw.
    device.set_viewport_size(640, 480);
    device.begin_viewport_pass(Color{0, 0, 0, 1});
    device.end_viewport_pass();
    // No offscreen target on a base device.
    REQUIRE(device.viewport_texture() == static_cast<ViewportTextureId>(0));
}

TEST_CASE("shadow hooks default to no-op", "[render][shadow]")
{
    MinimalDevice device;
    const mosaic::foundation::math::Mat4 mat = mosaic::foundation::math::Mat4::identity();
    device.begin_shadow_pass();
    device.draw_shadow(1, mat);
    device.end_shadow_pass();
    // No assertion beyond "must not throw" -- these are pure no-ops on a base device.
}
