#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"

using mosaic::engine::World;
using mosaic::engine::render::Camera;
using mosaic::engine::render::Color;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::MeshRenderer;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::RenderSystem;
using mosaic::engine::render::Vertex;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using Catch::Approx;

namespace
{
class FakeRenderDevice : public RenderDevice
{
public:
    struct DrawCall { MeshHandle mesh; Mat4 mvp; };
    std::vector<DrawCall> draws;
    MeshHandle            next = 1;

    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>& v,
                           const std::vector<std::uint32_t>& i) override
    {
        return (v.empty() || i.empty()) ? 0 : next++;
    }
    void draw(MeshHandle m, const Mat4& mvp) override { draws.push_back({m, mvp}); }
};

// Adds a camera entity at `pos` (identity rotation) with the given lens.
World::EntityID add_camera(World& world, Vec3 pos, Camera lens)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(pos)});
    world.add_component<Camera>(e, lens);
    return e;
}

// Adds a mesh entity at world matrix `wm` with handle `h`.
World::EntityID add_mesh(World& world, const Mat4& wm, MeshHandle h)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{wm});
    world.add_component<MeshRenderer>(e, MeshRenderer{h});
    return e;
}
} // namespace

TEST_CASE("RenderSystem renders meshes from the active camera", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    rs.set_aspect(16.0f / 9.0f);

    const Camera lens{1.0472f, 0.1f, 100.0f};
    add_camera(world, Vec3{0, 0, 4}, lens);
    add_mesh(world, Mat4::identity(), 7);

    rs.render(world, device);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.draws[0].mesh == 7);

    const Mat4 view = Mat4::inverse_affine(Mat4::translation(Vec3{0, 0, 4}));
    const Mat4 proj = Mat4::perspective(lens.fov_y_radians, 16.0f / 9.0f, lens.near_z, lens.far_z);
    const Mat4 expected = proj * view; // mesh world == identity
    for (int k = 0; k < 16; ++k)
    {
        REQUIRE(device.draws[0].mvp.m[k] == Approx(expected.m[k]).margin(1e-5));
    }
    // Camera at (0,0,4) => view is translation (0,0,-4).
    REQUIRE(view.m[14] == Approx(-4.0f));
}

TEST_CASE("RenderSystem still renders meshes when no camera exists (fallback)", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;

    add_mesh(world, Mat4::identity(), 3);
    add_mesh(world, Mat4::identity(), 4);

    rs.render(world, device); // no camera entity -> default fallback
    REQUIRE(device.draws.size() == 2);
}

TEST_CASE("RenderSystem uses the first-found camera when several exist", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    // Two cameras with distinct fov; the first-created should win.
    add_camera(world, Vec3{0, 0, 4}, Camera{0.5f, 0.1f, 100.0f});
    add_camera(world, Vec3{0, 0, 8}, Camera{1.4f, 0.1f, 100.0f});
    add_mesh(world, Mat4::identity(), 9);

    rs.render(world, device);
    REQUIRE(device.draws.size() == 1);

    const Mat4 view = Mat4::inverse_affine(Mat4::translation(Vec3{0, 0, 4}));
    const Mat4 proj = Mat4::perspective(0.5f, 1.0f, 0.1f, 100.0f);
    const Mat4 expected = proj * view;
    for (int k = 0; k < 16; ++k)
    {
        REQUIRE(device.draws[0].mvp.m[k] == Approx(expected.m[k]).margin(1e-5));
    }
}

TEST_CASE("RenderSystem skips entities missing a component or with mesh 0", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4}, Camera{});

    auto only_transform = world.create_entity();
    world.add_component<WorldTransform>(only_transform, WorldTransform{Mat4::identity()});

    auto only_mesh = world.create_entity();
    world.add_component<MeshRenderer>(only_mesh, MeshRenderer{3});

    auto zero_mesh = world.create_entity();
    world.add_component<WorldTransform>(zero_mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(zero_mesh, MeshRenderer{0});

    rs.render(world, device);
    REQUIRE(device.draws.empty());
}

TEST_CASE("make_cube_mesh yields 24 vertices, 36 in-range indices", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    mosaic::engine::render::make_cube_mesh(v, i);
    REQUIRE(v.size() == 24);
    REQUIRE(i.size() == 36);
    for (auto idx : i)
    {
        REQUIRE(idx < 24u);
    }
}
