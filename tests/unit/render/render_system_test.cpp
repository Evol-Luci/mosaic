#include <cmath>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/mat4.hpp"

using mosaic::engine::World;
using mosaic::engine::render::Camera;
using mosaic::engine::render::Color;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Material;
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
    struct DrawCall { MeshHandle mesh; DrawParams params; };
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
    void draw(MeshHandle m, const DrawParams& p) override { draws.push_back({m, p}); }
};

World::EntityID add_camera(World& world, Vec3 pos)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(pos)});
    world.add_component<Camera>(e, Camera{});
    return e;
}
} // namespace

TEST_CASE("RenderSystem produces lit DrawParams from Material + DirectionalLight", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    // Light NOT aligned with the mesh's rotation axis (Y), so the object-space transform
    // produces a genuinely different vector than the world direction — this is what makes
    // the assertion below actually exercise inverse_affine + transform_direction.
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{1, -1, 0}, Vec3{1, 0.9f, 0.8f}});

    // Mesh with a 90-degree-about-Y world rotation, so object-space light differs from world.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{7});
    world.add_component<Material>(mesh, Material{Vec3{0.2f, 0.4f, 0.6f}});

    rs.render(world, device);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(device.draws[0].mesh == 7);
    // base_color from the Material:
    REQUIRE(p.base_color.x == Approx(0.2f));
    REQUIRE(p.base_color.y == Approx(0.4f));
    REQUIRE(p.base_color.z == Approx(0.6f));
    // light_color from the DirectionalLight:
    REQUIRE(p.light_color.x == Approx(1.0f));
    REQUIRE(p.light_color.z == Approx(0.8f));
    // light_direction is the world light transformed into the mesh's object space + normalized.
    const Vec3 world_dir{1, -1, 0};
    const Vec3 expected_obj = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(Mat4::inverse_affine(mesh_world), world_dir));
    REQUIRE(p.light_direction.x == Approx(expected_obj.x).margin(1e-5));
    REQUIRE(p.light_direction.y == Approx(expected_obj.y).margin(1e-5));
    REQUIRE(p.light_direction.z == Approx(expected_obj.z).margin(1e-5));
    // Guard: the object-space direction must actually differ from the normalized world
    // direction, so this test fails if the RenderSystem ever skips the object-space transform.
    const Vec3 world_norm = mosaic::foundation::math::normalize(world_dir);
    REQUIRE(std::abs(p.light_direction.x - world_norm.x) > 0.1f);
}

TEST_CASE("RenderSystem uses default base color and light when none present", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{5}); // no Material

    rs.render(world, device);
    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.base_color.x == Approx(0.8f)); // default material
    REQUIRE(p.light_color.x == Approx(1.0f)); // default light is white
}

TEST_CASE("RenderSystem skips mesh 0 / missing WorldTransform", "[render]")
{
    World world;
    FakeRenderDevice device;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto only_mesh = world.create_entity();
    world.add_component<MeshRenderer>(only_mesh, MeshRenderer{3}); // no WorldTransform

    auto zero = world.create_entity();
    world.add_component<WorldTransform>(zero, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(zero, MeshRenderer{0});

    rs.render(world, device);
    REQUIRE(device.draws.empty());
}

TEST_CASE("make_cube_mesh yields 24 vertices with unit, per-face outward normals", "[render]")
{
    std::vector<Vertex>        v;
    std::vector<std::uint32_t> i;
    mosaic::engine::render::make_cube_mesh(v, i);
    REQUIRE(v.size() == 24);
    REQUIRE(i.size() == 36);
    for (const auto& vert : v)
    {
        const float len2 = vert.normal.x * vert.normal.x + vert.normal.y * vert.normal.y
                           + vert.normal.z * vert.normal.z;
        REQUIRE(len2 == Approx(1.0f)); // unit normal
    }
    // The first face is +X: all four normals are (1,0,0).
    for (int k = 0; k < 4; ++k)
    {
        REQUIRE(v[k].normal.x == Approx(1.0f));
        REQUIRE(v[k].normal.y == Approx(0.0f));
        REQUIRE(v[k].normal.z == Approx(0.0f));
    }
}
