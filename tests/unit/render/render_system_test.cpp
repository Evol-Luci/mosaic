#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/assets/asset_registry.hpp"
#include "engine/render/builtin_meshes.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/mesh.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "engine/particles/particle_emitter.hpp"
#include "engine/particles/particle_system.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
#include "foundation/math/mat4.hpp"

using mosaic::engine::World;
using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::render::Camera;
using mosaic::engine::render::Color;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::Image;
using mosaic::engine::render::Material;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::MeshRenderer;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::RenderSystem;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::PointLight;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;
using mosaic::engine::render::ParticleInstance;
using mosaic::engine::render::ParticleDrawParams;
using mosaic::engine::render::RibbonVertex;
using mosaic::engine::render::RibbonDrawParams;
using mosaic::engine::particles::ParticleEmitter;
using mosaic::engine::particles::ParticleSystem;
using Catch::Approx;

namespace
{
class FakeRenderDevice : public RenderDevice
{
public:
    struct DrawCall { MeshHandle mesh; TextureHandle texture; DrawParams params; };
    std::vector<DrawCall> draws;
    MeshHandle            next = 1;
    TextureHandle         next_tex = 1;

    void begin_frame() override {}
    void clear(const Color&) override {}
    void end_frame() override {}
    MeshHandle create_mesh(const std::vector<Vertex>& v,
                           const std::vector<std::uint32_t>& i) override
    {
        return (v.empty() || i.empty()) ? 0 : next++;
    }
    TextureHandle create_texture(const Image& img) override
    {
        return img.pixels.empty() ? 0 : next_tex++;
    }
    void draw(MeshHandle m, TextureHandle t, const DrawParams& p) override
    {
        draws.push_back({m, t, p});
    }

    struct ParticleDrawCall { std::vector<ParticleInstance> instances; ParticleDrawParams params; };
    std::vector<ParticleDrawCall> particle_draws;
    void draw_particles(const std::vector<ParticleInstance>& instances,
                        const ParticleDrawParams& params) override
    {
        particle_draws.push_back({instances, params});
    }

    struct RibbonDrawCall { std::vector<RibbonVertex> vertices; RibbonDrawParams params; };
    std::vector<RibbonDrawCall> ribbon_draws;
    void draw_ribbons(const std::vector<RibbonVertex>& vertices,
                      const RibbonDrawParams& params) override
    {
        ribbon_draws.push_back({vertices, params});
    }
};

World::EntityID add_camera(World& world, Vec3 pos)
{
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(pos)});
    world.add_component<Camera>(e, Camera{});
    return e;
}

// Registers the built-in cube in `registry` and returns its AssetID, so a MeshRenderer
// referencing it resolves to a non-zero MeshHandle through the RenderSystem's MeshCache.
AssetID register_cube(AssetRegistry& registry)
{
    mosaic::engine::render::register_builtin_meshes(registry);
    return mosaic::engine::render::kCubeMeshId;
}
} // namespace

TEST_CASE("RenderSystem produces lit DrawParams from Material + DirectionalLight", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
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
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});
    world.add_component<Material>(mesh, Material{Vec3{0.2f, 0.4f, 0.6f}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(device.draws[0].mesh != 0);
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
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id}); // no Material

    rs.render(world, device, registry);
    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.base_color.x == Approx(0.8f)); // default material
    REQUIRE(p.light_color.x == Approx(1.0f)); // default light is white
}

TEST_CASE("RenderSystem skips a nil mesh or a missing WorldTransform", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto only_mesh = world.create_entity();
    world.add_component<MeshRenderer>(only_mesh, MeshRenderer{mesh_id}); // no WorldTransform -> skipped

    auto nil = world.create_entity();
    world.add_component<WorldTransform>(nil, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(nil, MeshRenderer{}); // nil mesh -> skipped by cache

    rs.render(world, device, registry);
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

TEST_CASE("RenderSystem resolves Material texture via TextureCache", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;

    // Register a 1x1 white image in the registry.
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 42;
    const AssetID tex_id{bytes};
    auto img = std::make_shared<Image>();
    img->width = 1; img->height = 1; img->pixels = {255, 255, 255, 255};
    registry.register_resource<Image>(AssetMetadata{tex_id, "texture"}, img);

    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(e, MeshRenderer{mesh_id});
    Material mat{Vec3{1.0f, 1.0f, 1.0f}};
    mat.texture = tex_id;
    world.add_component<Material>(e, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    // The texture handle must be non-zero (resolved from registry).
    REQUIRE(device.draws[0].texture != 0);
}

TEST_CASE("RenderSystem uses non-zero default white texture for untextured material", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(e, MeshRenderer{mesh_id});
    world.add_component<Material>(e, Material{Vec3{0.5f, 0.5f, 0.5f}}); // nil texture

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    // Default white texture must be non-zero.
    REQUIRE(device.draws[0].texture != 0);
}

TEST_CASE("RenderSystem transforms point light into mesh object space", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    // Point light at a known world position, placed via its WorldTransform.
    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<PointLight>(light, PointLight{Vec3{0.5f, 0.25f, 0.1f}, 2.0f, 6.0f});

    // Mesh with a 90-degree-about-Y world rotation, so object-space pos differs from world.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;

    // point_position = light world pos transformed into the mesh's object space.
    // Golden value derived independently of the production formula: the mesh is rotated
    // +90 deg about Y, so world->object is the inverse rotation R_y(-90). Applying it to
    // the world light {2,0,0} maps +X -> +Z, giving object-space {0,0,2}. Hardcoding this
    // (rather than recomputing with transform_point/inverse_affine) means a systematic
    // error in that formula cannot make the test pass by matching itself on both sides.
    REQUIRE(p.point_position.x == Approx(0.0f).margin(1e-5));
    REQUIRE(p.point_position.y == Approx(0.0f).margin(1e-5));
    REQUIRE(p.point_position.z == Approx(2.0f).margin(1e-5));
    // Guard: object-space position must actually differ from the raw world position,
    // so this fails if the RenderSystem ever skips the object-space transform.
    REQUIRE(std::abs(p.point_position.z - light_world_pos.z) > 0.1f);

    // point_color is color * intensity (premultiplied); point_range passes through.
    REQUIRE(p.point_color.x == Approx(1.0f));   // 0.5 * 2
    REQUIRE(p.point_color.y == Approx(0.5f));   // 0.25 * 2
    REQUIRE(p.point_color.z == Approx(0.2f));   // 0.1 * 2
    REQUIRE(p.point_range == Approx(6.0f));
}

TEST_CASE("RenderSystem disables point light when none present", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id}); // no PointLight in the world

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    // range 0 == contribution disabled in the shader.
    REQUIRE(device.draws[0].params.point_range == Approx(0.0f));
}

TEST_CASE("RenderSystem emits one batched draw_particles with per-particle instances", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    world.add_system<ParticleSystem>();
    auto* psys = world.get_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{Vec3{1, 2, 3}, {}, Vec3{1, 1, 1}, {}});
    ParticleEmitter em;
    em.emitting = true;
    em.rate = 100.0f;
    em.lifetime = 10.0f;
    em.initial_velocity = Vec3{0, 0, 0};
    em.acceleration = Vec3{0, 0, 0};
    em.color = Vec3{0.9f, 0.5f, 0.1f};
    em.size = 0.25f;
    em.end_color = em.color; // constant color/size for this exact-value check
    em.end_size = em.size;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // spawns ~10 particles at (1,2,3)
    const auto* pool = psys->particles_for(e);
    REQUIRE(pool != nullptr);
    REQUIRE(!pool->empty());

    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    const auto& call = device.particle_draws[0];
    REQUIRE(call.instances.size() == pool->size());
    REQUIRE(call.instances[0].color.x == Approx(0.9f));
    REQUIRE(call.instances[0].size == Approx(0.25f));
    REQUIRE(call.instances[0].world_position.x == Approx(1.0f));
    REQUIRE(call.instances[0].world_position.y == Approx(2.0f));
}

TEST_CASE("RenderSystem particle alpha fades with age", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = true;
    em.rate = 1000.0f;   // spawn many so ages differ across the pool
    em.lifetime = 1.0f;
    em.acceleration = Vec3{0, 0, 0};
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.5f); // pool now holds a spread of ages, all < lifetime
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    for (const auto& inst : device.particle_draws[0].instances)
    {
        REQUIRE(inst.alpha >= 0.0f);
        REQUIRE(inst.alpha <= 1.0f);
    }
}

TEST_CASE("RenderSystem batches two emitters with distinct colors", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto make = [&](Vec3 color)
    {
        auto e = world.create_entity();
        world.add_component<mosaic::engine::transform::Transform>(
            e, mosaic::engine::transform::Transform{});
        ParticleEmitter em;
        em.emitting = true; em.rate = 50.0f; em.lifetime = 10.0f;
        em.acceleration = Vec3{0, 0, 0}; em.color = color;
        world.add_component<ParticleEmitter>(e, em);
    };
    make(Vec3{1, 0, 0});
    make(Vec3{0, 0, 1});

    world.tick(0.1f);
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    bool saw_red = false, saw_blue = false;
    for (const auto& inst : device.particle_draws[0].instances)
    {
        if (inst.color.x > 0.5f) saw_red = true;
        if (inst.color.z > 0.5f) saw_blue = true;
    }
    REQUIRE(saw_red);
    REQUIRE(saw_blue);
}

TEST_CASE("RenderSystem skips particle pass when no ParticleSystem present", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<ParticleEmitter>(e, ParticleEmitter{}); // emitter but no system

    rs.render(world, device, registry); // must not crash
    REQUIRE(device.particle_draws.empty());
}

TEST_CASE("RenderSystem emits no particle draw when there are no live particles", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = false; em.burst = 0; // never spawns
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    rs.render(world, device, registry);
    REQUIRE(device.particle_draws.empty());
}

TEST_CASE("RenderSystem derives an orthonormal camera basis for billboards", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4}); // identity-rotation camera looking down -Z
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = true; em.rate = 50.0f; em.lifetime = 10.0f; em.acceleration = Vec3{0, 0, 0};
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f);
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    const auto& params = device.particle_draws[0].params;
    // A camera at +Z looking at the origin (no roll) has world right = +X, up = +Y.
    REQUIRE(params.camera_right.x == Approx(1.0f).margin(1e-4));
    REQUIRE(params.camera_right.y == Approx(0.0f).margin(1e-4));
    REQUIRE(params.camera_up.y == Approx(1.0f).margin(1e-4));
    // Right and up are perpendicular.
    const float dot = params.camera_right.x * params.camera_up.x +
                      params.camera_right.y * params.camera_up.y +
                      params.camera_right.z * params.camera_up.z;
    REQUIRE(dot == Approx(0.0f).margin(1e-4));
}

TEST_CASE("RenderSystem interpolates particle color/size/alpha over life", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<ParticleSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    ParticleEmitter em;
    em.emitting = true;
    em.rate = 10.0f;                 // rate*dt = 1.0 -> exactly one spawn this tick
    em.lifetime = 1.0f;
    em.initial_velocity = Vec3{0, 0, 0};
    em.acceleration = Vec3{0, 0, 0};
    em.color = Vec3{0.0f, 0.0f, 0.0f};
    em.end_color = Vec3{1.0f, 1.0f, 1.0f};
    em.size = 0.0f;
    em.end_size = 1.0f;
    em.alpha = 1.0f;
    em.end_alpha = 0.0f;
    world.add_component<ParticleEmitter>(e, em);

    world.tick(0.1f); // one particle, aged 0.1 -> t = 0.1
    rs.render(world, device, registry);

    REQUIRE(device.particle_draws.size() == 1);
    const auto& call = device.particle_draws[0];
    REQUIRE(call.instances.size() == 1);
    REQUIRE(call.instances[0].color.x == Approx(0.1f));
    REQUIRE(call.instances[0].size == Approx(0.1f));
    REQUIRE(call.instances[0].alpha == Approx(0.9f));
}

TEST_CASE("RenderSystem draws a trail as a ribbon", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});
    world.add_system<mosaic::engine::vfx::TrailSystem>();

    auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    mosaic::engine::vfx::TrailRenderer tr;
    tr.size            = 0.2f;
    tr.color           = Vec3{0.1f, 0.2f, 0.3f};
    tr.sample_interval = 0.05f;
    tr.max_points      = 10;
    world.add_component<mosaic::engine::vfx::TrailRenderer>(e, tr);

    for (int i = 0; i < 4; ++i) { world.tick(0.05f); } // seed + 3 samples -> 4 points -> 3 segments

    rs.render(world, device, registry);
    REQUIRE(device.ribbon_draws.size() == 1);
    REQUIRE(device.ribbon_draws[0].vertices.size() == 6 * 3); // 6 verts/segment, 3 segments
    // Trails no longer contribute to the particle batch.
    REQUIRE(device.particle_draws.empty());
}
