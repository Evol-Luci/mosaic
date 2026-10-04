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
#include "engine/render/spot_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/render/shadow.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "engine/particles/particle_emitter.hpp"
#include "engine/particles/particle_system.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
#include "app/asset/font_cook.hpp"   // read_ttf for a real FontAtlas
#include "app/run/scene_io.hpp"
#include "engine/render/font_texture_cache.hpp"
#include "engine/ui/font_atlas.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/world_label.hpp"
#include "foundation/math/mat4.hpp"

using mosaic::engine::World;
using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::render::Camera;
using mosaic::engine::render::Color;
using mosaic::engine::render::DirectionalLight;
using mosaic::engine::render::DrawParams;
using mosaic::engine::render::FrameLighting;
using mosaic::engine::render::Image;
using mosaic::engine::render::Material;
using mosaic::engine::render::MeshHandle;
using mosaic::engine::render::MeshRenderer;
using mosaic::engine::render::RenderDevice;
using mosaic::engine::render::RenderSystem;
using mosaic::engine::render::TextureHandle;
using mosaic::engine::render::Vertex;
using mosaic::engine::render::PointLight;
using mosaic::engine::render::SpotLight;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::Mat4;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;
using mosaic::engine::render::ParticleInstance;
using mosaic::engine::render::ParticleDrawParams;
using mosaic::engine::render::RibbonVertex;
using mosaic::engine::render::RibbonDrawParams;
using mosaic::engine::render::DecalInstance;
using mosaic::engine::render::ScreenEffectParams;
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

    struct ShadowDrawCall { MeshHandle mesh; Mat4 light_mvp; };
    std::vector<ShadowDrawCall> shadow_draws;
    int shadow_pass_begins = 0;
    int shadow_pass_ends = 0;
    void begin_shadow_pass() override { ++shadow_pass_begins; }
    void draw_shadow(MeshHandle m, const Mat4& light_mvp) override
    {
        shadow_draws.push_back({m, light_mvp});
    }
    void end_shadow_pass() override { ++shadow_pass_ends; }

    std::vector<FrameLighting> frame_lighting_calls;
    void set_frame_lighting(const FrameLighting& fl) override
    {
        frame_lighting_calls.push_back(fl);
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

    std::vector<std::vector<DecalInstance>> decal_draws;
    void draw_decals(const std::vector<DecalInstance>& decals) override
    {
        decal_draws.push_back(decals);
    }

    std::vector<ScreenEffectParams> screen_effect_draws;
    void draw_screen_effect(const ScreenEffectParams& params) override
    {
        screen_effect_draws.push_back(params);
    }

    std::vector<std::vector<mosaic::engine::render::UiQuad>> ui_draws;
    void draw_ui(const std::vector<mosaic::engine::render::UiQuad>& quads) override
    {
        ui_draws.push_back(quads);
    }

    struct UiImageDrawCall { std::vector<mosaic::engine::render::UiImageQuad> quads;
                             mosaic::engine::render::TextureHandle texture; };
    std::vector<UiImageDrawCall> ui_image_draws;
    void draw_ui_image(const std::vector<mosaic::engine::render::UiImageQuad>& quads,
                       mosaic::engine::render::TextureHandle texture) override
    {
        ui_image_draws.push_back({quads, texture});
    }

    struct UiTextDrawCall { std::vector<mosaic::engine::render::UiGlyphQuad> quads;
                            mosaic::engine::render::TextureHandle atlas; };
    std::vector<UiTextDrawCall> ui_text_draws;
    void draw_ui_text(const std::vector<mosaic::engine::render::UiGlyphQuad>& quads,
                      mosaic::engine::render::TextureHandle atlas) override
    {
        ui_text_draws.push_back({quads, atlas});
    }

    struct WorldTextDrawCall { std::vector<mosaic::engine::render::WorldGlyphVertex> verts;
                               Mat4 view_proj;
                               mosaic::engine::render::TextureHandle atlas; };
    std::vector<WorldTextDrawCall> world_text_draws;
    void draw_world_text(const std::vector<mosaic::engine::render::WorldGlyphVertex>& verts,
                         const Mat4& view_proj,
                         mosaic::engine::render::TextureHandle atlas) override
    {
        world_text_draws.push_back({verts, view_proj, atlas});
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

TEST_CASE("RenderSystem reports world-space directional light in FrameLighting, untouched by mesh rotation", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    // Light NOT aligned with the mesh's rotation axis (Y): if any leftover
    // object-space transform were still applied, the result would visibly differ
    // from the world direction -- which the final guard below checks for.
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{1, -1, 0}, Vec3{1, 0.9f, 0.8f}});

    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});
    world.add_component<Material>(mesh, Material{Vec3{0.2f, 0.4f, 0.6f}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.frame_lighting_calls.size() == 1);
    const DrawParams&    p  = device.draws[0].params;
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(device.draws[0].mesh != 0);

    // base_color stays per-draw.
    REQUIRE(p.base_color.x == Approx(0.2f));
    REQUIRE(p.base_color.y == Approx(0.4f));
    REQUIRE(p.base_color.z == Approx(0.6f));

    // Light color and direction are now per-frame and in WORLD space.
    REQUIRE(fl.light_color.x == Approx(1.0f));
    REQUIRE(fl.light_color.z == Approx(0.8f));
    const Vec3 world_norm = mosaic::foundation::math::normalize(Vec3{1, -1, 0});
    REQUIRE(fl.light_direction.x == Approx(world_norm.x).margin(1e-5));
    REQUIRE(fl.light_direction.y == Approx(world_norm.y).margin(1e-5));
    REQUIRE(fl.light_direction.z == Approx(world_norm.z).margin(1e-5));

    // Guard, inverted from the object-space era: the mesh's 90-degree rotation must
    // NOT affect the light direction any more. If a leftover object-space transform
    // is applied, the x component differs from the world value by well over 0.1.
    const Vec3 object_space = mosaic::foundation::math::normalize(
        mosaic::foundation::math::transform_direction(Mat4::inverse_affine(mesh_world),
                                                      Vec3{1, -1, 0}));
    REQUIRE(std::abs(fl.light_direction.x - object_space.x) > 0.1f);
}

TEST_CASE("render_shadows' light_mvp agrees with FrameLighting's light_view_proj and the per-draw model", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    const Mat4 mesh_world = Mat4::translation(Vec3{1.0f, 0.0f, 0.0f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render_shadows(world, device, registry);
    rs.render(world, device, registry);

    REQUIRE(device.shadow_pass_begins == 1);
    REQUIRE(device.shadow_pass_ends == 1);
    REQUIRE(device.shadow_draws.size() == 1);
    REQUIRE(device.shadow_draws[0].mesh == device.draws[0].mesh);
    REQUIRE(device.frame_lighting_calls.size() == 1);

    const Mat4 light_vp = mosaic::engine::render::directional_light_view_projection(
        DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    // The shadow pass still gets a fully composed light_mvp (its own pipeline is
    // untouched by this migration).
    const Mat4 expected_shadow = light_vp * mesh_world;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(device.shadow_draws[0].light_mvp.m[i] == Approx(expected_shadow.m[i]).margin(1e-4));
    }

    // The main pass now composes it in the shader from these two halves. If they ever
    // disagree with what the shadow pass rendered, the shadow map would misalign --
    // so assert both halves rather than the product.
    const FrameLighting& fl = device.frame_lighting_calls[0];
    const DrawParams&    p  = device.draws[0].params;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(fl.light_view_proj.m[i] == Approx(light_vp.m[i]).margin(1e-4));
        REQUIRE(p.model.m[i] == Approx(mesh_world.m[i]).margin(1e-4));
    }
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
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].light_color.x == Approx(1.0f)); // default light is white
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

TEST_CASE("RenderSystem reports point light in world space, unaffected by mesh rotation", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<PointLight>(light, PointLight{Vec3{0.5f, 0.25f, 0.1f}, 2.0f, 6.0f});

    // Mesh rotated 90 degrees about Y -- kept deliberately from the object-space era:
    // under the old behavior this produced object-space {0,0,2}, so if any leftover
    // transform is applied these assertions fail.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];

    REQUIRE(fl.point_position.x == Approx(2.0f).margin(1e-5));
    REQUIRE(fl.point_position.y == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.point_position.z == Approx(0.0f).margin(1e-5));

    REQUIRE(fl.point_color.x == Approx(1.0f));   // 0.5 * 2
    REQUIRE(fl.point_color.y == Approx(0.5f));   // 0.25 * 2
    REQUIRE(fl.point_color.z == Approx(0.2f));   // 0.1 * 2
    REQUIRE(fl.point_range == Approx(6.0f));
}

TEST_CASE("RenderSystem reports spot light position and direction in world space", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});

    const Vec3 light_world_pos{2.0f, 0.0f, 0.0f};
    auto light = world.create_entity();
    world.add_component<WorldTransform>(light, WorldTransform{Mat4::translation(light_world_pos)});
    world.add_component<SpotLight>(light, SpotLight{Vec3{0.3f, 0.6f, 0.9f}, 2.0f, 8.0f, 0.4f, 0.6f});

    // Same deliberately-rotated mesh as before: under object-space shading this
    // produced position {0,0,2} and direction {1,0,0}, so a leftover transform fails here.
    const Mat4 mesh_world =
        Mat4::rotation(mosaic::foundation::math::Quat{0, 0.70710678f, 0, 0.70710678f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];

    REQUIRE(fl.spot_position.x == Approx(2.0f).margin(1e-5));
    REQUIRE(fl.spot_position.y == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.spot_position.z == Approx(0.0f).margin(1e-5));

    // Identity-rotation light, so Mat4::forward() gives world -Z.
    REQUIRE(fl.spot_direction.x == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.spot_direction.y == Approx(0.0f).margin(1e-5));
    REQUIRE(fl.spot_direction.z == Approx(-1.0f).margin(1e-5));

    REQUIRE(fl.spot_color.x == Approx(0.6f));  // 0.3 * 2
    REQUIRE(fl.spot_color.y == Approx(1.2f));  // 0.6 * 2
    REQUIRE(fl.spot_color.z == Approx(1.8f));  // 0.9 * 2
    REQUIRE(fl.spot_range == Approx(8.0f));
    REQUIRE(fl.spot_cos_inner == Approx(std::cos(0.4f)));
    REQUIRE(fl.spot_cos_outer == Approx(std::cos(0.6f)));
}

TEST_CASE("RenderSystem disables spot light when none present", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].spot_range == Approx(0.0f)); // 0 == disabled
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
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].point_range == Approx(0.0f)); // 0 == disabled
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

TEST_CASE("RenderSystem emits a decal instance per Decal entity", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::identity()}); // as the mesh tests do
    mosaic::engine::vfx::Decal d;
    d.color = Vec3{0.3f, 0.6f, 0.9f};
    d.alpha = 0.5f;
    world.add_component<mosaic::engine::vfx::Decal>(e, d);

    rs.render(world, device, registry);

    REQUIRE(device.decal_draws.size() == 1);
    REQUIRE(device.decal_draws[0].size() == 1);
    REQUIRE(device.decal_draws[0][0].color.x == Approx(0.3f));
    REQUIRE(device.decal_draws[0][0].alpha == Approx(0.5f));
}

TEST_CASE("RenderSystem draws a screen effect when active", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    mosaic::engine::vfx::ScreenEffect se;
    se.tint          = Vec3{1.0f, 0.0f, 0.0f};
    se.tint_strength = 0.5f;
    world.add_component<mosaic::engine::vfx::ScreenEffect>(e, se);

    rs.render(world, device, registry);
    REQUIRE(device.screen_effect_draws.size() == 1);
    REQUIRE(device.screen_effect_draws[0].tint_strength == Approx(0.5f));
    REQUIRE(device.screen_effect_draws[0].tint.x == Approx(1.0f));
}

TEST_CASE("RenderSystem skips an inactive (all-zero) screen effect", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto e = world.create_entity();
    world.add_component<mosaic::engine::vfx::ScreenEffect>(e, mosaic::engine::vfx::ScreenEffect{});

    rs.render(world, device, registry);
    REQUIRE(device.screen_effect_draws.empty());
}

TEST_CASE("RenderSystem emits a UiQuad per visible UiPanel, order-sorted", "[render][ui]")
{
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto back = world.create_entity();
    UiPanel bp; bp.anchor = static_cast<int>(Anchor::TopLeft);
    bp.pos = {0.1f, 0.1f}; bp.size = {0.2f, 0.2f}; bp.order = 0;
    world.add_component<UiPanel>(back, bp);

    auto front = world.create_entity();
    UiPanel fp; fp.anchor = static_cast<int>(Anchor::TopLeft);
    fp.pos = {0.3f, 0.3f}; fp.size = {0.1f, 0.1f}; fp.order = 10;
    world.add_component<UiPanel>(front, fp);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2);
    // order-sorted: order 0 first, order 10 last.
    REQUIRE(quads[0].rect.x == Approx(0.1f));  // the back panel
    REQUIRE(quads[1].rect.x == Approx(0.3f));  // the front panel
    REQUIRE(quads[1].rect.w == Approx(0.1f));
}

TEST_CASE("RenderSystem skips fully transparent UiPanels", "[render][ui]")
{
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    UiPanel p; p.alpha = 0.0f;
    world.add_component<UiPanel>(e, p);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    // No visible panels -> draw_ui not called at all.
    REQUIRE(device.ui_draws.empty());
}

namespace
{
mosaic::foundation::serialization::AssetID register_white_texture(AssetRegistry& registry)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 77;
    const mosaic::foundation::serialization::AssetID tex_id{bytes};
    auto img = std::make_shared<Image>();
    img->width = 1; img->height = 1; img->pixels = {255, 255, 255, 255};
    registry.register_resource<Image>(AssetMetadata{tex_id, "texture"}, img);
    return tex_id;
}
mosaic::foundation::serialization::AssetID register_font(AssetRegistry& registry)
{
    const auto bytes = mosaic::app::load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(bytes.has_value());
    auto font = mosaic::app::read_ttf(*bytes, 48.0f);
    REQUIRE(font.has_value());
    const auto id = mosaic::foundation::serialization::AssetID::from_string(
        "00000000-0000-0000-0000-0000000000fa");
    registry.register_resource<mosaic::engine::ui::FontAtlas>(
        mosaic::engine::assets::AssetMetadata{id, "font"},
        std::make_shared<mosaic::engine::ui::FontAtlas>(*font));
    return id;
}
} // namespace

TEST_CASE("RenderSystem emits a UiImageQuad for a UiImage with a registered texture", "[render][ui]")
{
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiImage;
    AssetRegistry registry;
    const auto tex_id = register_white_texture(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    UiImage im; im.texture = tex_id; im.anchor = static_cast<int>(Anchor::TopLeft);
    im.pos = {0.1f, 0.1f}; im.size = {0.2f, 0.2f}; im.tint = Vec3{0.5f, 0.6f, 0.7f}; im.alpha = 0.9f;
    world.add_component<UiImage>(e, im);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    REQUIRE(device.ui_image_draws[0].texture != 0);
    const auto& quads = device.ui_image_draws[0].quads;
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].rect.x == Approx(0.1f));   // resolve_rect TopLeft
    REQUIRE(quads[0].rect.w == Approx(0.2f));
    REQUIRE(quads[0].tint.y == Approx(0.6f));
    REQUIRE(quads[0].alpha == Approx(0.9f));
}

TEST_CASE("RenderSystem skips a transparent or textureless UiImage", "[render][ui]")
{
    using mosaic::engine::ui::UiImage;
    AssetRegistry registry;
    const auto tex_id = register_white_texture(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto clear = world.create_entity();
    UiImage a; a.texture = tex_id; a.alpha = 0.0f; world.add_component<UiImage>(clear, a);
    auto notex = world.create_entity();
    UiImage b; world.add_component<UiImage>(notex, b); // nil texture

    FakeRenderDevice device;
    rs.render(world, device, registry);
    REQUIRE(device.ui_image_draws.empty());
}

TEST_CASE("RenderSystem emits glyph quads for a UiText with a registered font", "[render][ui]")
{
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    UiText t; t.font = font_id; t.text = "AB"; t.height = 0.1f;
    world.add_component<UiText>(e, t);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_text_draws.size() == 1);
    REQUIRE(device.ui_text_draws[0].quads.size() == 2); // A, B
    REQUIRE(device.ui_text_draws[0].atlas != 0);        // atlas uploaded
}

TEST_CASE("RenderSystem skips UiText that is empty, transparent, or unresolved", "[render][ui]")
{
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto empty = world.create_entity();
    UiText a; a.font = font_id; a.text = ""; world.add_component<UiText>(empty, a);
    auto clear = world.create_entity();
    UiText b; b.font = font_id; b.text = "X"; b.alpha = 0.0f; world.add_component<UiText>(clear, b);
    auto nofont = world.create_entity();
    UiText c; c.text = "X"; world.add_component<UiText>(nofont, c); // nil font

    FakeRenderDevice device;
    rs.render(world, device, registry);
    REQUIRE(device.ui_text_draws.empty());
}

TEST_CASE("FontTextureCache uploads a font atlas at most once", "[render][ui]")
{
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    FakeRenderDevice device;
    mosaic::engine::render::FontTextureCache cache;

    const auto h1 = cache.get(font_id, registry, device);
    const auto h2 = cache.get(font_id, registry, device);
    REQUIRE(h1 != 0);
    REQUIRE(h1 == h2);
    REQUIRE(cache.get(mosaic::foundation::serialization::AssetID{}, registry, device) == 0);
}

TEST_CASE("RenderSystem resolves a child UiPanel within its parent panel", "[render][ui]")
{
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    AssetRegistry registry;
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto parent = world.create_entity();
    UiPanel pp; pp.anchor = 0; pp.pos = {0.5f, 0.5f}; pp.size = {0.4f, 0.4f}; pp.alpha = 1.0f;
    world.add_component<UiPanel>(parent, pp);

    auto child = world.create_entity();
    UiPanel cp; cp.anchor = 0; cp.pos = {0.0f, 0.0f}; cp.size = {0.5f, 0.5f}; cp.alpha = 1.0f;
    world.add_component<UiPanel>(child, cp);
    world.add_component<UiParent>(child, UiParent{parent});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2); // parent + child
    // The child sits inside the parent frame {0.5,0.5,0.4,0.4}: top-left (0.5,0.5), 0.2 wide.
    bool found_child = false;
    for (const auto& q : quads)
    {
        if (q.rect.w == Approx(0.2f) && q.rect.x == Approx(0.5f) && q.rect.y == Approx(0.5f))
        {
            found_child = true;
        }
    }
    REQUIRE(found_child);
}

TEST_CASE("RenderSystem emits world-text verts for a WorldLabel with a WorldTransform", "[render][ui]")
{
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
    WorldLabel l; l.font = font_id; l.text = "Hi"; l.size = 1.0f;
    world.add_component<WorldLabel>(e, l);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.world_text_draws.size() == 1);
    REQUIRE(device.world_text_draws[0].verts.size() == 12); // "Hi" = 2 glyphs * 6
    REQUIRE(device.world_text_draws[0].atlas != 0);
}

TEST_CASE("RenderSystem skips a WorldLabel with no WorldTransform or empty text", "[render][ui]")
{
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    auto no_wt = world.create_entity();
    WorldLabel a; a.font = font_id; a.text = "X"; world.add_component<WorldLabel>(no_wt, a);
    auto empty = world.create_entity();
    world.add_component<WorldTransform>(empty, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
    WorldLabel b; b.font = font_id; b.text = ""; world.add_component<WorldLabel>(empty, b);

    FakeRenderDevice device;
    rs.render(world, device, registry);
    REQUIRE(device.world_text_draws.empty());
}

TEST_CASE("RenderSystem sets frame lighting even when the scene has no meshes", "[render]")
{
    // Review Focus 1: if the UBO were only refreshed alongside mesh draws, an empty
    // frame would leave the backend shading from the previous frame's buffer -- or,
    // on the very first frame, from uninitialized memory.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, -1, 0}, Vec3{1, 1, 1}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.empty());
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].light_color.x == Approx(1.0f));
}

TEST_CASE("RenderSystem's normal_matrix is the inverse-transpose under non-uniform scale", "[render]")
{
    // Review Focus 2: the migration claims to fix non-uniform scale. A mesh scaled 2x
    // on X only must get normals scaled by 1/2 on X -- passing the model matrix
    // straight through would give 2x, which is exactly the bug this pins.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const Mat4 mesh_world = Mat4::scale(Vec3{2.0f, 1.0f, 1.0f});
    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{mesh_world});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.normal_matrix.m[0] == Approx(0.5f).margin(1e-5));
    REQUIRE(p.normal_matrix.m[5] == Approx(1.0f).margin(1e-5));
    REQUIRE(p.normal_matrix.m[10] == Approx(1.0f).margin(1e-5));
    // Guard: the model matrix itself carries 2.0 there, so this fails loudly if
    // someone passes `model` as the normal matrix.
    REQUIRE(p.model.m[0] == Approx(2.0f));
}

TEST_CASE("RenderSystem emits a sane default FrameLighting for a world with no lights", "[render]")
{
    // Review Focus 3: no lights at all -- the default directional light must come
    // through and both positional lights must be range 0 (contributing nothing).
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(fl.light_color.x == Approx(1.0f));
    REQUIRE(fl.point_range == Approx(0.0f));
    REQUIRE(fl.spot_range == Approx(0.0f));
    // The default direction is normalized, so it is a unit vector rather than zero.
    const float len = mosaic::foundation::math::length(fl.light_direction);
    REQUIRE(len == Approx(1.0f).margin(1e-5));
}

TEST_CASE("RenderSystem emits no NaN for a zero-length directional light direction", "[render]")
{
    // Review Focus 4: authorable in the editor and in scene files. normalize()
    // already returns the zero vector rather than NaN, so the expected behavior is
    // "unlit, still finite" -- pinned here because the value is now shared by every
    // fragment through the UBO instead of being recomputed per mesh.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    auto light = world.create_entity();
    world.add_component<DirectionalLight>(light, DirectionalLight{Vec3{0, 0, 0}, Vec3{1, 1, 1}});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(std::isfinite(fl.light_direction.x));
    REQUIRE(std::isfinite(fl.light_direction.y));
    REQUIRE(std::isfinite(fl.light_direction.z));
}

TEST_CASE("RenderSystem emits finite per-draw matrices for a zero-scaled mesh", "[render]")
{
    // Review Focus 5: a zero-scaled Transform makes the model matrix singular.
    // Mat4::inverse_affine already returns identity for a singular linear part, so
    // the normal matrix must stay finite rather than filling with NaN/inf.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::scale(Vec3{1.0f, 1.0f, 0.0f})});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(std::isfinite(p.normal_matrix.m[i]));
    }
}

TEST_CASE("RenderSystem reports the camera's world position in FrameLighting", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{2.0f, 3.0f, 4.0f});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(fl.camera_position.x == Approx(2.0f).margin(1e-5));
    REQUIRE(fl.camera_position.y == Approx(3.0f).margin(1e-5));
    REQUIRE(fl.camera_position.z == Approx(4.0f).margin(1e-5));
}

TEST_CASE("RenderSystem recovers the camera position from the fallback view", "[render]")
{
    // No Camera component at all: the position has to be recovered from the fallback
    // view matrix rather than read off a WorldTransform. fallback_view() looks from
    // {0, 1.5, 4} at the origin, so that is the expected eye position.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    const FrameLighting& fl = device.frame_lighting_calls[0];
    REQUIRE(fl.camera_position.x == Approx(0.0f).margin(1e-4));
    REQUIRE(fl.camera_position.y == Approx(1.5f).margin(1e-4));
    REQUIRE(fl.camera_position.z == Approx(4.0f).margin(1e-4));
}

TEST_CASE("RenderSystem sends the camera position even with no meshes", "[render]")
{
    // Review Focus 1: the backend's UBO is only refreshed by set_frame_lighting. If a
    // mesh-less frame skipped it, the camera position would go stale and highlights
    // would freeze in place while the camera moved. Specular is the first feature
    // that makes this hazard visible.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{5.0f, 0.0f, 0.0f});

    rs.render(world, device, registry);

    REQUIRE(device.draws.empty());
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].camera_position.x == Approx(5.0f).margin(1e-5));
}

TEST_CASE("RenderSystem's camera position follows a moving camera", "[render]")
{
    // Review Focus 2: the whole point of specular is that it tracks the camera. A
    // value computed once and cached would pass every single-frame test above.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    auto cam = add_camera(world, Vec3{1.0f, 0.0f, 0.0f});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    rs.render(world, device, registry);

    auto* cam_wt = world.get_component<WorldTransform>(cam);
    REQUIRE(cam_wt != nullptr);
    cam_wt->matrix = Mat4::translation(Vec3{-7.0f, 0.0f, 0.0f});

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 2);
    REQUIRE(device.frame_lighting_calls[0].camera_position.x == Approx(1.0f).margin(1e-5));
    REQUIRE(device.frame_lighting_calls[1].camera_position.x == Approx(-7.0f).margin(1e-5));
}

TEST_CASE("RenderSystem forwards a Material's specular parameters to the draw call", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});
    Material mat{Vec3{0.2f, 0.4f, 0.6f}};
    mat.metallic = 1.0f;
    mat.roughness = 0.25f;
    world.add_component<Material>(mesh, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    const DrawParams& p = device.draws[0].params;
    REQUIRE(p.metallic == Approx(1.0f));
    REQUIRE(p.roughness == Approx(0.25f));
}

TEST_CASE("RenderSystem uses the documented shading defaults with and without a Material", "[render]")
{
    // Review Focus 5: a Material that exists but leaves the shading fields alone is
    // the common real case, and it is a DIFFERENT code path from having no Material
    // at all. Both must yield metallic 0.0 / roughness 0.5 -- Material's documented
    // defaults, which render_system.hpp's own comment requires these to match.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto bare = world.create_entity();
    world.add_component<WorldTransform>(bare, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(bare, MeshRenderer{mesh_id});

    auto with_material = world.create_entity();
    world.add_component<WorldTransform>(with_material, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(with_material, MeshRenderer{mesh_id});
    world.add_component<Material>(with_material, Material{Vec3{1.0f, 0.0f, 0.0f}});

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 2);
    for (const auto& d : device.draws)
    {
        REQUIRE(d.params.metallic == Approx(0.0f));
        REQUIRE(d.params.roughness == Approx(0.5f));
    }
}

TEST_CASE("RenderSystem forwards out-of-range shading parameters unchanged", "[render]")
{
    // The engine must not clamp roughness or metallic -- cube.frag does, and
    // shader_specular_contract_test.cpp pins that it still does. An engine-side clamp
    // added later would silently diverge from the shader, and a scene file would stop
    // round-tripping byte-for-byte.
    //
    // This replaces a case about specular_strength 0 being the opt-out from specular.
    // There is no opt-out under PBR: a dielectric's F0 is a fixed 0.04, not a knob, so
    // that half of the old contract has no successor rather than a renamed one.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    add_camera(world, Vec3{0, 0, 4});

    const AssetID mesh_id = register_cube(registry);
    auto mesh = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::identity()});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});
    Material mat{};
    mat.roughness = -1.0f;
    mat.metallic = 2.0f;
    world.add_component<Material>(mesh, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    REQUIRE(device.draws[0].params.roughness == Approx(-1.0f));
    REQUIRE(device.draws[0].params.metallic == Approx(2.0f));
}
