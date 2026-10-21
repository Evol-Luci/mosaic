#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <array>
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
#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_order_check.hpp"
#include "engine/ui/ui_system.hpp"
#include "engine/render/render_system.hpp"
#include "engine/vfx/bloom.hpp"
#include "engine/ui/ui_parent.hpp"
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
#include "engine/ui/ui_hover_style.hpp"
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
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;
using mosaic::engine::render::ParticleInstance;
using mosaic::engine::render::ParticleDrawParams;
using mosaic::engine::render::RibbonVertex;
using mosaic::engine::render::RibbonDrawParams;
using mosaic::engine::render::DecalInstance;
using mosaic::engine::render::ScreenEffectParams;
using mosaic::engine::render::TextureColorSpace;
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
    // Records the colour space each upload asked for, keyed by the image's first
    // pixel -- enough to tell this file's fixtures apart, and create_texture never
    // sees an AssetID to key by.
    std::map<std::array<std::uint8_t, 4>, TextureColorSpace> upload_spaces;

    TextureHandle create_texture(const Image& img, TextureColorSpace space) override
    {
        if (img.pixels.size() >= 4)
        {
            upload_spaces[{img.pixels[0], img.pixels[1], img.pixels[2], img.pixels[3]}] = space;
        }
        return img.pixels.empty() ? 0 : next_tex++;
    }
    void draw(MeshHandle m, TextureHandle t, const DrawParams& p) override
    {
        draws.push_back({m, t, p});
        ordered_calls.push_back("draw");
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

    // Recorded so the skybox's own copy of per-frame state can be checked against
    // FrameLighting's. The skybox pipeline does not bind the per-frame UBO, so anything it
    // needs travels through SkyboxParams and nothing else would notice if it stopped.
    std::vector<mosaic::engine::render::SkyboxParams> skybox_calls;
    void draw_skybox(mosaic::engine::render::TextureHandle,
                     const mosaic::engine::render::SkyboxParams& p) override
    {
        skybox_calls.push_back(p);
        ordered_calls.push_back("skybox");
    }

    struct ParticleDrawCall { std::vector<ParticleInstance> instances; ParticleDrawParams params; };
    std::vector<ParticleDrawCall> particle_draws;
    void draw_particles(const std::vector<ParticleInstance>& instances,
                        const ParticleDrawParams& params) override
    {
        particle_draws.push_back({instances, params});
        ordered_calls.push_back("particles");
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
        ordered_calls.push_back("screen_effect");
    }

    std::vector<std::vector<mosaic::engine::render::UiQuad>> ui_draws;
    void draw_ui(const std::vector<mosaic::engine::render::UiQuad>& quads) override
    {
        ui_draws.push_back(quads);
        ordered_calls.push_back("ui");
    }

    struct UiImageDrawCall { std::vector<mosaic::engine::render::UiImageQuad> quads;
                             mosaic::engine::render::TextureHandle texture; };
    std::vector<UiImageDrawCall> ui_image_draws;
    void draw_ui_image(const std::vector<mosaic::engine::render::UiImageQuad>& quads,
                       mosaic::engine::render::TextureHandle texture) override
    {
        ui_image_draws.push_back({quads, texture});
        ordered_calls.push_back("ui_image");
    }

    struct UiTextDrawCall { std::vector<mosaic::engine::render::UiGlyphQuad> quads;
                            mosaic::engine::render::TextureHandle atlas; };
    std::vector<UiTextDrawCall> ui_text_draws;
    void draw_ui_text(const std::vector<mosaic::engine::render::UiGlyphQuad>& quads,
                      mosaic::engine::render::TextureHandle atlas) override
    {
        ui_text_draws.push_back({quads, atlas});
        ordered_calls.push_back("ui_text");
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
        ordered_calls.push_back("world_text");

        // Model the Vulkan backend's real storage: ONE per-frame vertex buffer, written
        // immediately at offset 0 (a CPU memcpy), while the draw that reads it is only
        // RECORDED and executes at submit. Without this the fake records each call's
        // arguments intact, so a caller that issues two calls would look correct here
        // even though the real device would draw the second call's vertices twice.
        if (world_text_buffer.size() < verts.size()) { world_text_buffer.resize(verts.size()); }
        std::copy(verts.begin(), verts.end(), world_text_buffer.begin());
        world_text_recorded.push_back({verts.size(), atlas});
    }

    // What each recorded world-text draw would actually read when the frame executes:
    // the first `count` vertices of the shared buffer as they stand at submit time.
    // Resolved at end_hdr_scene_pass, the last point world-space text can be recorded.
    struct WorldTextRecorded { std::size_t count; mosaic::engine::render::TextureHandle atlas; };
    struct WorldTextExecuted { std::vector<mosaic::engine::render::WorldGlyphVertex> verts;
                               mosaic::engine::render::TextureHandle atlas; };
    std::vector<mosaic::engine::render::WorldGlyphVertex> world_text_buffer;
    std::vector<WorldTextRecorded>                        world_text_recorded;
    std::vector<WorldTextExecuted>                        world_text_executed;

    // A single ordered log, because the thing under test is RELATIVE order: "the resolve
    // happens after the world draws and before the UI ones". Separate per-call vectors
    // cannot express that -- they record counts, not sequence.
    std::vector<std::string> ordered_calls;
    std::vector<float>       hdr_exposures;

    std::vector<Color>       hdr_clears;

    void begin_hdr_scene_pass(const Color& c) override
    {
        ordered_calls.push_back("hdr_begin");
        hdr_clears.push_back(c);
    }
    void end_hdr_scene_pass() override
    {
        ordered_calls.push_back("hdr_end");
        for (const auto& r : world_text_recorded)
        {
            world_text_executed.push_back(
                {std::vector<mosaic::engine::render::WorldGlyphVertex>(
                     world_text_buffer.begin(),
                     world_text_buffer.begin() + static_cast<std::ptrdiff_t>(r.count)),
                 r.atlas});
        }
        world_text_recorded.clear();
    }
    std::vector<mosaic::engine::render::BloomParams> bloom_builds;
    std::vector<float>                               bloom_intensities;

    void build_bloom(const mosaic::engine::render::BloomParams& p) override
    {
        ordered_calls.push_back("bloom_build");
        bloom_builds.push_back(p);
    }
    void draw_hdr_resolve(float exposure, float bloom_intensity) override
    {
        ordered_calls.push_back("hdr_resolve");
        hdr_exposures.push_back(exposure);
        bloom_intensities.push_back(bloom_intensity);
    }

    // Index of the first occurrence, or -1. Comparing indices is how the tests say "before"
    // and "after" without depending on how many draws happened in between.
    int index_of(const std::string& name) const
    {
        for (std::size_t i = 0; i < ordered_calls.size(); ++i)
        {
            if (ordered_calls[i] == name) { return static_cast<int>(i); }
        }
        return -1;
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

TEST_CASE("RenderSystem emits equal-order UiPanels in iteration order", "[render][ui]")
{
    // A PRE-EXISTING GAP, found by a mutation probe while sorting the image and text passes:
    // swapping this pass's stable_sort for std::sort left the suite green. The panel pass is
    // the reference the other two now copy, and ui_hit.hpp's rule 2 documents the hit
    // tie-break as agreeing with "the entity later in iteration draws on top" -- so the
    // stability here is load-bearing for hit-testing, not a detail of this pass.
    //
    // Twenty, because libstdc++'s std::sort falls back to insertion sort below 17 elements
    // and is stable in practice there; the introsort boundary measured exactly 17.
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    constexpr int kCount = 20;
    for (int i = 0; i < kCount; ++i)
    {
        auto    e = world.create_entity();
        UiPanel p;
        p.anchor = static_cast<int>(Anchor::TopLeft);
        p.pos    = {0.01f * static_cast<float>(i), 0.1f};
        p.size   = {0.05f, 0.05f};
        p.order  = 7; // all equal
        world.add_component<UiPanel>(e, p);
    }

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == kCount);
    for (int i = 0; i < kCount; ++i)
    {
        REQUIRE(quads[static_cast<std::size_t>(i)].rect.x == Approx(0.01f * static_cast<float>(i)));
    }
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
// Dimensions default to 1x1 so every pre-existing caller is unchanged. They are a
// parameter at all because the aspect modes read them back through
// TextureCache::get_info: a helper hardcoded to 1x1 cannot exercise letterboxing.
mosaic::foundation::serialization::AssetID register_white_texture(AssetRegistry& registry,
                                                                  std::uint32_t w = 1,
                                                                  std::uint32_t h = 1)
{
    std::array<std::uint8_t, 16> bytes{};
    bytes[0] = 77;
    const mosaic::foundation::serialization::AssetID tex_id{bytes};
    auto img = std::make_shared<Image>();
    img->width = w;
    img->height = h;
    img->pixels.assign(static_cast<std::size_t>(w) * h * 4u, 255);
    registry.register_resource<Image>(AssetMetadata{tex_id, "texture"}, img);
    return tex_id;
}
mosaic::foundation::serialization::AssetID register_font(
    AssetRegistry& registry,
    const char*    uuid = "00000000-0000-0000-0000-0000000000fa")
{
    const auto bytes = mosaic::app::load_scene_file(
        std::string(MOSAIC_TEST_DIR) + "/fixtures/LiberationSans-Regular.ttf");
    REQUIRE(bytes.has_value());
    auto font = mosaic::app::read_ttf(*bytes, 48.0f);
    REQUIRE(font.has_value());
    const auto id = mosaic::foundation::serialization::AssetID::from_string(uuid);
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

TEST_CASE("RenderSystem draws UiImages in ascending order", "[render][ui]")
{
    // Authored high-then-low so a pass that just iterates emits 10 before 0. Higher `order`
    // must be drawn LAST, because these are alpha-blended quads and the last one drawn is
    // the one on top -- the same rule the panel pass already follows.
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiImage;
    AssetRegistry registry;
    const auto    tex_id = register_white_texture(registry);
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto    front = world.create_entity();
    UiImage f;
    f.texture = tex_id;
    f.anchor  = static_cast<int>(Anchor::TopLeft);
    f.pos     = {0.3f, 0.3f};
    f.order   = 10;
    world.add_component<UiImage>(front, f);

    auto    back = world.create_entity();
    UiImage b;
    b.texture = tex_id;
    b.anchor  = static_cast<int>(Anchor::TopLeft);
    b.pos     = {0.1f, 0.1f};
    b.order   = 0;
    world.add_component<UiImage>(back, b);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    // One call per image, as before -- different textures cannot be batched, so sorting
    // changes the ORDER of the calls and not their number.
    REQUIRE(device.ui_image_draws.size() == 2);
    REQUIRE(device.ui_image_draws[0].quads[0].rect.x == Approx(0.1f)); // order 0 first
    REQUIRE(device.ui_image_draws[1].quads[0].rect.x == Approx(0.3f)); // order 10 on top
}

TEST_CASE("RenderSystem draws equal-order UiImages in iteration order", "[render][ui]")
{
    // Stability, which is what makes the tie-break DETERMINISTIC rather than whatever
    // std::sort happens to do. ui_hit.hpp's rule 2 leans on "the entity later in iteration
    // draws on top", so a plain sort here would quietly break the agreement it documents.
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiImage;
    AssetRegistry registry;
    const auto    tex_id = register_white_texture(registry);
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    // TWENTY, and the count is load-bearing. libstdc++'s std::sort falls back to insertion
    // sort below 17 elements, which IS stable in practice -- so with three images a
    // std::sort mutant passes and the stability claim goes untested. Measured: the
    // introsort boundary is exactly 17 on this toolchain.
    constexpr int kCount = 20;
    for (int i = 0; i < kCount; ++i)
    {
        auto    e = world.create_entity();
        UiImage im;
        im.texture = tex_id;
        im.anchor  = static_cast<int>(Anchor::TopLeft);
        im.pos     = {0.01f * static_cast<float>(i), 0.1f};
        im.order   = 5; // all equal
        world.add_component<UiImage>(e, im);
    }

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == kCount);
    for (int i = 0; i < kCount; ++i)
    {
        REQUIRE(device.ui_image_draws[static_cast<std::size_t>(i)].quads[0].rect.x
                == Approx(0.01f * static_cast<float>(i)));
    }
}

TEST_CASE("RenderSystem draws a negative-order UiImage first", "[render][ui]")
{
    // `order` is signed, so -1 must sort BELOW a default-constructed 0. A comparison on an
    // unsigned type, or one on magnitudes, puts it on top instead.
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::UiImage;
    AssetRegistry registry;
    const auto    tex_id = register_white_texture(registry);
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto    normal = world.create_entity();
    UiImage n;
    n.texture = tex_id;
    n.anchor  = static_cast<int>(Anchor::TopLeft);
    n.pos     = {0.2f, 0.2f};
    n.order   = 0;
    world.add_component<UiImage>(normal, n);

    auto    behind = world.create_entity();
    UiImage bh;
    bh.texture = tex_id;
    bh.anchor  = static_cast<int>(Anchor::TopLeft);
    bh.pos     = {0.4f, 0.4f};
    bh.order   = -1;
    world.add_component<UiImage>(behind, bh);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 2);
    REQUIRE(device.ui_image_draws[0].quads[0].rect.x == Approx(0.4f)); // -1 draws first
    REQUIRE(device.ui_image_draws[1].quads[0].rect.x == Approx(0.2f));
}

TEST_CASE("RenderSystem draws UiText in ascending order", "[render][ui]")
{
    // Distinct glyph COUNTS identify which text each call came from without depending on
    // glyph geometry: "ABC" is three quads, "A" is one.
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto    font_id = register_font(registry);
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto   front = world.create_entity();
    UiText f;
    f.font   = font_id;
    f.text   = "ABC";
    f.height = 0.1f;
    f.order  = 10;
    world.add_component<UiText>(front, f);

    auto   back = world.create_entity();
    UiText b;
    b.font   = font_id;
    b.text   = "A";
    b.height = 0.1f;
    b.order  = 0;
    world.add_component<UiText>(back, b);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_text_draws.size() == 2);
    REQUIRE(device.ui_text_draws[0].quads.size() == 1); // order 0 ("A") first
    REQUIRE(device.ui_text_draws[1].quads.size() == 3); // order 10 ("ABC") on top
}

TEST_CASE("RenderSystem draws equal-order UiText in iteration order", "[render][ui]")
{
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto    font_id = register_font(registry);
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    // Twenty, for the same reason as the UiImage case above: below 17 elements std::sort is
    // incidentally stable and the mutant survives. Each text is one character longer than
    // the last, so its glyph count identifies which call it came from.
    constexpr int kCount = 20;
    for (int i = 1; i <= kCount; ++i)
    {
        auto   e = world.create_entity();
        UiText t;
        t.font   = font_id;
        t.text   = std::string(static_cast<std::size_t>(i), 'A');
        t.height = 0.1f;
        t.order  = 2; // all equal
        world.add_component<UiText>(e, t);
    }

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_text_draws.size() == kCount);
    for (int i = 1; i <= kCount; ++i)
    {
        REQUIRE(device.ui_text_draws[static_cast<std::size_t>(i - 1)].quads.size()
                == static_cast<std::size_t>(i));
    }
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

TEST_CASE("Two WorldLabels with different text both survive the frame intact", "[render][ui]")
{
    // The defect as observed: the orbiter's "Orbiter" label rendered as "ter" once a second
    // label ("GLOW") existed. The backend keeps one shared per-frame vertex buffer, so what
    // matters is what each recorded draw READS at submit, not what RenderSystem passed in.
    // The two labels differ in vertex colour, which is how glyphs are told apart below.
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});

    const Vec3 orbiter_color{0.1f, 0.2f, 0.3f};
    const Vec3 glow_color{0.7f, 0.8f, 0.9f};
    auto add_label = [&](const char* text, Vec3 color, Vec3 at)
    {
        auto e = world.create_entity();
        world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(at)});
        WorldLabel l; l.font = font_id; l.text = text; l.size = 1.0f; l.color = color;
        world.add_component<WorldLabel>(e, l);
    };
    add_label("Orbiter", orbiter_color, Vec3{-2, 0, 0}); // 7 glyphs
    add_label("GLOW", glow_color, Vec3{2, 0, 0});        // 4 glyphs

    FakeRenderDevice device;
    rs.render(world, device, registry);

    std::size_t orbiter_verts = 0;
    std::size_t glow_verts = 0;
    for (const auto& d : device.world_text_executed)
    {
        for (const auto& v : d.verts)
        {
            if (v.color.x == Approx(orbiter_color.x)) { ++orbiter_verts; }
            if (v.color.x == Approx(glow_color.x)) { ++glow_verts; }
        }
    }
    // Each glyph is 6 vertices. Each label must reach the screen exactly once, whole.
    REQUIRE(orbiter_verts == 7 * 6);
    REQUIRE(glow_verts == 4 * 6);
}

TEST_CASE("WorldLabels sharing a font are submitted as ONE world-text draw", "[render][ui]")
{
    // The backend contract: its vertex buffer is written at offset 0 on every call, so it
    // supports one draw_world_text per atlas per frame. Batching lives here so a fake can
    // see it; a second call for the same atlas would overwrite the first's vertices.
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_id = register_font(registry);
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});
    for (const char* text : {"Orbiter", "GLOW", "Third"})
    {
        auto e = world.create_entity();
        world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
        WorldLabel l; l.font = font_id; l.text = text; l.size = 1.0f;
        world.add_component<WorldLabel>(e, l);
    }

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.world_text_draws.size() == 1);
    REQUIRE(device.world_text_draws[0].verts.size() == (7 + 4 + 5) * 6);
}

TEST_CASE("WorldLabels in different fonts get one draw per atlas, in first-seen order", "[render][ui]")
{
    // Grouping is by resolved TextureHandle -- what the draw actually binds. Two AssetIDs
    // that resolve to different handles must never share a draw (the draw samples one
    // atlas), and labels within a group keep their iteration order.
    using mosaic::engine::ui::WorldLabel;
    AssetRegistry registry;
    const auto font_a = register_font(registry);
    const auto font_b = register_font(registry, "00000000-0000-0000-0000-0000000000fb");
    World world;
    RenderSystem rs;
    add_camera(world, Vec3{0, 0, 5});
    auto add_label = [&](mosaic::foundation::serialization::AssetID f, const char* text, float r)
    {
        auto e = world.create_entity();
        world.add_component<WorldTransform>(e, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
        WorldLabel l; l.font = f; l.text = text; l.size = 1.0f; l.color = Vec3{r, 0, 0};
        world.add_component<WorldLabel>(e, l);
    };
    add_label(font_a, "AA", 0.1f);
    add_label(font_b, "BBB", 0.2f);
    add_label(font_a, "CCCC", 0.3f); // interleaved: must join the first font's draw

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.world_text_draws.size() == 2);
    REQUIRE(device.world_text_draws[0].atlas != device.world_text_draws[1].atlas);
    // First-seen order: font A's draw first, carrying AA then CCCC; then font B's BBB.
    const auto& first = device.world_text_draws[0].verts;
    REQUIRE(first.size() == (2 + 4) * 6);
    REQUIRE(first.front().color.x == Approx(0.1f));
    REQUIRE(first.back().color.x == Approx(0.3f));
    REQUIRE(device.world_text_draws[1].verts.size() == 3 * 6);
    REQUIRE(device.world_text_draws[1].verts.front().color.x == Approx(0.2f));
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

TEST_CASE("RenderSystem carries the active camera's exposure into FrameLighting", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    auto cam = world.create_entity();
    world.add_component<WorldTransform>(cam, WorldTransform{Mat4::translation(Vec3{0, 0, 4})});
    Camera c{};
    c.exposure = 2.5f;
    world.add_component<Camera>(cam, c);

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    // 2.5, not 1.0: the field has to travel from the component the scene authored to the
    // per-frame block the shader reads, and nothing else in the pipeline would notice if
    // it did not -- the image would simply be the wrong brightness.
    REQUIRE(device.frame_lighting_calls[0].exposure == Approx(2.5f));
}

TEST_CASE("RenderSystem leaves exposure neutral when the world has no camera", "[render]")
{
    // The fallback path. find_active_camera writes exposure through an out-parameter, so
    // a fallback that forgets to initialise it ships whatever was on the stack -- which
    // could be anything, including 0 (a black frame) or a huge value (a white one).
    // 1.0 exactly, because neutral is the contract: a scene with no camera must render
    // as it did before exposure existed.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    rs.render(world, device, registry); // no camera entity at all

    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].exposure == Approx(1.0f));
}

TEST_CASE("an untouched Camera yields a neutral exposure in FrameLighting", "[render]")
{
    // Pins the migration guarantee, which nothing else did. The branch review found that
    // changing Camera's default from 1.0f to 0.0f left all 1167 tests GREEN while every
    // existing scene rendered completely BLACK -- because the round-trip case passes an
    // explicit 2.5, the carries-exposure case sets an explicit 2.5, and the no-camera case
    // asserts 1.0 that comes from RenderSystem's own local initialiser, never from the
    // component.
    //
    // This asserts the default AND its journey: a Camera nobody touched must arrive at the
    // device as 1.0. templates/default/scenes/main.json has a Camera with no "exposure"
    // key, so it takes exactly this path.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    auto cam = world.create_entity();
    world.add_component<WorldTransform>(cam, WorldTransform{Mat4::translation(Vec3{0, 0, 4})});
    world.add_component<Camera>(cam, Camera{}); // defaults, deliberately untouched

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].exposure == Approx(1.0f));
    // And directly, so the failure message distinguishes "the default changed" from
    // "the journey broke".
    REQUIRE(Camera{}.exposure == Approx(1.0f));
}

TEST_CASE("a Camera with no WorldTransform contributes no exposure", "[render]")
{
    // The guard-escape mutation, found by the branch review: move
    // `exposure = cam.exposure;` ABOVE find_active_camera's `wt == nullptr` check -- a
    // natural-looking "set it while we have the component in hand" refactor -- and all
    // 1167 tests stayed green.
    //
    // What it produces: an entity with a Camera and no WorldTransform (trivially reachable
    // -- add a camera in the editor before placing it, or hand-edit a scene) makes
    // find_active_camera return false, so view and projection come from the FALLBACK, while
    // exposure would come from that unplaced camera. The frame renders from the fallback
    // viewpoint at 4x exposure, blown out, with no camera in the scene a user would connect
    // it to.
    //
    // Exposure must travel with the view and projection or not at all -- that is the whole
    // reason it rides in the same traversal.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    auto cam = world.create_entity();
    Camera c{};
    c.exposure = 4.0f;
    world.add_component<Camera>(cam, c); // NO WorldTransform

    rs.render(world, device, registry);

    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls[0].exposure == Approx(1.0f));
}

TEST_CASE("the skybox is given the same exposure as the lit pass", "[render]")
{
    // The drift guard, and the one the shader contract test cannot give. That test pins
    // that each shader HAS an exposure input; this pins that the same number reaches both.
    //
    // It matters because the two take different routes: the lit pass reads exposure from
    // the per-frame UBO, and the skybox cannot -- it does not bind that descriptor set at
    // all -- so it arrives through SkyboxParams instead. Two routes, one value. If the
    // skybox's assignment were dropped it would silently fall back to SkyboxParams'
    // default of 1.0, and the sky would stay at neutral brightness while the geometry
    // moved: "the sky doesn't match the scene", which reads as an authoring complaint
    // rather than a bug report.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);
    rs.set_environment(42); // non-zero handle is all the skybox pass needs

    auto cam = world.create_entity();
    world.add_component<WorldTransform>(cam, WorldTransform{Mat4::translation(Vec3{0, 0, 4})});
    Camera c{};
    c.exposure = 3.25f;
    world.add_component<Camera>(cam, c);

    rs.render(world, device, registry);

    REQUIRE(device.skybox_calls.size() == 1);
    REQUIRE(device.frame_lighting_calls.size() == 1);
    REQUIRE(device.skybox_calls[0].exposure == Approx(3.25f));
    // Stated as an equality against the lit pass's value, not just against 3.25, so the
    // two cannot drift even if the authored value changes.
    REQUIRE(device.skybox_calls[0].exposure
            == Approx(device.frame_lighting_calls[0].exposure));
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

TEST_CASE("a Material with no metallic/roughness map draws exactly as before", "[render]")
{
    // Review Focus 3. The common real case, and a different code path from having a
    // map. An untextured material must be byte-identical to today's behaviour, or
    // this slice silently re-shades every scene that already exists.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});
    const AssetID mesh_id = register_cube(registry);
    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{});
    world.add_component<MeshRenderer>(e, MeshRenderer{mesh_id});
    Material mat{};
    mat.metallic = 0.25f;
    mat.roughness = 0.75f;
    world.add_component<Material>(e, mat);

    rs.render(world, device, registry);

    REQUIRE(device.draws.size() == 1);
    // A nil map resolves to 0 and must be REPLACED by the 1x1 linear white default,
    // never left at 0: the shader multiplies by this sampler unconditionally, and an
    // unbound one is undefined behaviour rather than a visible error.
    REQUIRE(device.draws[0].params.metallic_roughness != 0);
    REQUIRE(device.draws[0].params.metallic == Approx(0.25f));
    REQUIRE(device.draws[0].params.roughness == Approx(0.75f));
}

TEST_CASE("a metallic/roughness map is uploaded as linear, not sRGB", "[render]")
{
    // Review Focus 1, at the layer that CHOOSES. texture_colorspace_contract_test.cpp
    // pins the backend's mapping; this pins that RenderSystem asks for Linear in the
    // first place. Both halves are needed -- a correct backend asked for Srgb is still
    // a distorted roughness.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(1.0f);

    add_camera(world, Vec3{0, 0, 4});
    const AssetID mesh_id = register_cube(registry);

    std::array<std::uint8_t, 16> mr_bytes{};
    mr_bytes[0] = 0x5A;
    const AssetID mr_id{mr_bytes};
    auto image = std::make_shared<Image>();
    image->width = 1;
    image->height = 1;
    image->pixels = {0, 128, 255, 255};
    registry.register_resource<Image>(
        mosaic::engine::assets::AssetMetadata{mr_id, "texture"}, image);

    auto e = world.create_entity();
    world.add_component<WorldTransform>(e, WorldTransform{});
    world.add_component<MeshRenderer>(e, MeshRenderer{mesh_id});
    Material mat{};
    mat.metallic_roughness_texture = mr_id;
    world.add_component<Material>(e, mat);

    rs.render(world, device, registry);

    const std::array<std::uint8_t, 4> key{0, 128, 255, 255};
    REQUIRE(device.upload_spaces.count(key) == 1);
    REQUIRE(device.upload_spaces.at(key) == TextureColorSpace::Linear);
    // And the base-colour default white stays sRGB, so the two did not get crossed.
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    REQUIRE(device.upload_spaces.count(white) == 1);
    REQUIRE(device.upload_spaces.at(white) == TextureColorSpace::Srgb);
}

TEST_CASE("a default UiImage emits the authored rect and full UVs", "[render]")
{
    // The migration guarantee, through the REAL RenderSystem rather than through
    // resolve_image_quad alone -- this is the wiring, not the arithmetic. The demo's own
    // top-right checker image is exactly this case, so a regression here ships visibly.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f); // non-square on purpose: a square screen hides a dropped term

    const AssetID tex = register_white_texture(registry, 64, 32);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.pos = Vec2{0.1f, 0.2f};
    im.size = Vec2{0.2f, 0.2f};
    world.add_component<mosaic::engine::ui::UiImage>(e, im);

    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    const auto& q = device.ui_image_draws[0].quads.at(0);
    REQUIRE(q.u0 == Approx(0.0f));
    REQUIRE(q.v0 == Approx(0.0f));
    REQUIRE(q.u1 == Approx(1.0f));
    REQUIRE(q.v1 == Approx(1.0f));
    REQUIRE(q.rect.w == Approx(0.2f));
    REQUIRE(q.rect.h == Approx(0.2f));
}

TEST_CASE("RenderSystem passes the texture's real dimensions into the fit maths", "[render]")
{
    // The wiring the arithmetic tests cannot see: RenderSystem must hand the TEXTURE's
    // dimensions to resolve_image_quad, not a constant and not the rect's. Only the
    // dimensions get_info returned can tell it how much to letterbox.
    //
    // Worked through rather than guessed, because the first version of this case was
    // WRONG: a 50x100 texture (aspect 0.5) in rect {0.2, 0.2} at screen aspect 0.5 gives
    // rect_aspect (0.2 * 0.5) / 0.2 == 0.5 -- EQUAL to the image aspect, so a correct
    // implementation letterboxes nothing and the case would have failed against working
    // code.
    //
    // With screen aspect 2.0 the rect is 2:1 in pixels against a 1:2 image, so Fit shrinks
    // the width by image/rect == 0.5/2.0: w = 0.2 * 0.25 = 0.05, centred at
    // 0.1 + (0.2 - 0.05) / 2 = 0.175.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);

    const AssetID tex = register_white_texture(registry, 50, 100);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.pos = Vec2{0.1f, 0.2f};
    im.size = Vec2{0.2f, 0.2f};
    im.fit_mode = static_cast<int>(mosaic::engine::ui::ImageFitMode::Fit);
    world.add_component<mosaic::engine::ui::UiImage>(e, im);

    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    const auto& q = device.ui_image_draws[0].quads.at(0);
    INFO("rect " << q.rect.x << "," << q.rect.y << " " << q.rect.w << "x" << q.rect.h);
    REQUIRE(q.rect.w == Approx(0.05f));
    REQUIRE(q.rect.h == Approx(0.2f));
    REQUIRE(q.rect.x == Approx(0.175f)); // centred inside the authored rect
}

TEST_CASE("RenderSystem forwards a pixel sub-rect as UVs", "[render]")
{
    // Asymmetric numbers so a transposed axis fails.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);

    const AssetID tex = register_white_texture(registry, 128, 64);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.sub_rect_pos  = Vec2{32.0f, 16.0f};
    im.sub_rect_size = Vec2{16.0f, 32.0f};
    world.add_component<mosaic::engine::ui::UiImage>(e, im);

    rs.render(world, device, registry);

    const auto& q = device.ui_image_draws[0].quads.at(0);
    REQUIRE(q.u0 == Approx(0.25f));  // 32/128
    REQUIRE(q.v0 == Approx(0.25f));  // 16/64
    REQUIRE(q.u1 == Approx(0.375f)); // 48/128
    REQUIRE(q.v1 == Approx(0.75f));  // 48/64
}

TEST_CASE("a panel with no hover style emits its authored colour", "[render]")
{
    // The migration guarantee at the seam. Nothing in the repository has a UiHoverStyle, so
    // every existing HUD takes this path and must be byte-identical.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const auto e = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.color = Vec3{0.4f, 0.5f, 0.6f};
    p.alpha = 0.5f;
    world.add_component<mosaic::engine::ui::UiPanel>(e, p);

    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    // ui_draws is a vector OF vectors of UiQuad -- not draw-call structs with a `.quads`
    // member, unlike ui_image_draws and ui_text_draws. Existing cases use .front().
    const auto& q = device.ui_draws.front().at(0);
    REQUIRE(q.color.x == Approx(0.4f));
    REQUIRE(q.alpha == Approx(0.5f));
}

TEST_CASE("a hovered panel emits the multiplied colour and alpha", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const auto e = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.color = Vec3{0.4f, 0.5f, 0.6f};
    p.alpha = 0.5f;
    world.add_component<mosaic::engine::ui::UiPanel>(e, p);
    mosaic::engine::ui::UiHoverStyle s;
    s.tint = Vec3{2.0f, 1.0f, 0.5f};
    s.alpha_scale = 1.2f;
    world.add_component<mosaic::engine::ui::UiHoverStyle>(e, s);
    world.add_component<mosaic::engine::ui::UiHovered>(e, mosaic::engine::ui::UiHovered{});

    rs.render(world, device, registry);

    const auto& q = device.ui_draws.front().at(0);
    REQUIRE(q.color.x == Approx(0.8f)); // 0.4 * 2.0
    REQUIRE(q.color.y == Approx(0.5f)); // 0.5 * 1.0
    REQUIRE(q.color.z == Approx(0.3f)); // 0.6 * 0.5
    REQUIRE(q.alpha == Approx(0.6f));   // 0.5 * 1.2
}

TEST_CASE("only the hovered one of two styled panels is highlighted", "[render]")
{
    // Review focus 4. The natural wrong implementations -- "a style exists" or "anything is
    // hovered" -- highlight both, and with one element on screen that looks correct.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    mosaic::engine::ui::UiHoverStyle s;
    s.alpha_scale = 0.5f;

    const auto hovered = world.create_entity();
    mosaic::engine::ui::UiPanel a;
    a.alpha = 1.0f; a.order = 0;
    world.add_component<mosaic::engine::ui::UiPanel>(hovered, a);
    world.add_component<mosaic::engine::ui::UiHoverStyle>(hovered, s);
    world.add_component<mosaic::engine::ui::UiHovered>(hovered,
                                                       mosaic::engine::ui::UiHovered{});

    const auto plain = world.create_entity();
    mosaic::engine::ui::UiPanel b;
    b.alpha = 1.0f; b.order = 1; // drawn after, so it is the LAST quad
    world.add_component<mosaic::engine::ui::UiPanel>(plain, b);
    world.add_component<mosaic::engine::ui::UiHoverStyle>(plain, s);

    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2);
    REQUIRE(quads.front().alpha == Approx(0.5f)); // hovered -> styled
    REQUIRE(quads.back().alpha == Approx(1.0f));  // not hovered -> authored
}

TEST_CASE("a hover style never reveals a hidden panel", "[render]")
{
    // alpha 0 means hidden, and the early-out stays on the AUTHORED alpha so a style cannot
    // override that. A resolved-alpha early-out would make an invisible element appear on
    // hover, which reads as a rendering glitch rather than as a style.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const auto e = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.alpha = 0.0f; // hidden
    world.add_component<mosaic::engine::ui::UiPanel>(e, p);
    mosaic::engine::ui::UiHoverStyle s;
    s.alpha_scale = 100.0f; // would clamp to 1 if it were applied
    world.add_component<mosaic::engine::ui::UiHoverStyle>(e, s);
    world.add_component<mosaic::engine::ui::UiHovered>(e, mosaic::engine::ui::UiHovered{});

    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.empty());
}

TEST_CASE("a child UiText inherits its hovered ancestor's style", "[render]")
{
    // The inheritance no pure test can see: it is the PASS's lookup that walks the chain.
    // This is the demo's exact shape -- a hovered panel with a label parented to it.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const AssetID font = register_font(registry);

    const auto panel = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.alpha = 1.0f;
    world.add_component<mosaic::engine::ui::UiPanel>(panel, p);
    mosaic::engine::ui::UiHoverStyle s;
    s.alpha_scale = 0.5f;
    world.add_component<mosaic::engine::ui::UiHoverStyle>(panel, s);
    world.add_component<mosaic::engine::ui::UiHovered>(panel,
                                                       mosaic::engine::ui::UiHovered{});

    const auto label = world.create_entity();
    mosaic::engine::ui::UiText t;
    t.font = font;
    t.text = "HP";
    t.alpha = 1.0f;
    world.add_component<mosaic::engine::ui::UiText>(label, t);
    world.add_component<mosaic::engine::ui::UiParent>(
        label, mosaic::engine::ui::UiParent{panel});

    rs.render(world, device, registry);

    REQUIRE(!device.ui_text_draws.empty());
    REQUIRE(device.ui_text_draws[0].quads.at(0).alpha == Approx(0.5f));
}

TEST_CASE("a hovered UiImage emits the multiplied tint and alpha", "[render]")
{
    // The image pass had NO hover test. Reverting its two lines to `im.tint, im.alpha`
    // survived the whole suite, because the drop-the-style probe goes red via the panel and
    // text cases regardless. A hovered HUD icon would have rendered unstyled while the
    // panel behind it brightened -- with CI green.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const AssetID tex = register_white_texture(registry, 64, 32);
    const auto e = world.create_entity();
    mosaic::engine::ui::UiImage im;
    im.texture = tex;
    im.tint = Vec3{0.4f, 0.5f, 0.6f};
    im.alpha = 0.5f;
    world.add_component<mosaic::engine::ui::UiImage>(e, im);
    mosaic::engine::ui::UiHoverStyle s;
    s.tint = Vec3{2.0f, 1.0f, 0.5f};
    s.alpha_scale = 1.2f;
    world.add_component<mosaic::engine::ui::UiHoverStyle>(e, s);
    world.add_component<mosaic::engine::ui::UiHovered>(e, mosaic::engine::ui::UiHovered{});

    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    const auto& q = device.ui_image_draws[0].quads.at(0);
    REQUIRE(q.tint.x == Approx(0.8f)); // 0.4 * 2.0
    REQUIRE(q.tint.y == Approx(0.5f)); // 0.5 * 1.0
    REQUIRE(q.tint.z == Approx(0.3f)); // 0.6 * 0.5
    REQUIRE(q.alpha == Approx(0.6f));  // 0.5 * 1.2
    // The style must not have touched the U7 fit maths: full UVs, authored rect.
    REQUIRE(q.u0 == Approx(0.0f));
    REQUIRE(q.u1 == Approx(1.0f));
}

TEST_CASE("a conflicting pair really does misroute the click", "[render][ui][conflict]")
{
    // THE END-TO-END CLAIM find_order_conflicts cannot make about itself. It reports that two
    // overlapping buttons are ranked one way for the click and the other way for the draw;
    // that this CAUSES the wrong button to answer rests on two mechanisms that were covered
    // separately and never together -- topmost_hit's ordering and the panel pass's sort.
    //
    // One world, both halves, so the consequence is demonstrated rather than inferred:
    //
    //   clicked : UiButton order 5 (wins the click), UiPanel order 1 (drawn first, underneath)
    //   seen    : UiButton order 2 (loses the click), UiPanel order 7 (drawn last, on top)
    //
    // The user points at the overlap, sees `seen`, and `clicked` answers.
    using mosaic::engine::script::ScriptEvent;
    using mosaic::engine::ui::Anchor;
    using mosaic::engine::ui::PointerState;
    using mosaic::engine::ui::UiButton;
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiSystem;
    using mosaic::foundation::serialization::Table;

    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    const auto make = [&world](float x, int button_order, int panel_order, const char* event)
    {
        const auto e = world.create_entity();
        UiButton   b;
        b.anchor = static_cast<int>(Anchor::TopLeft);
        b.pos    = {x, 0.1f};
        b.size   = {0.3f, 0.3f};
        b.order  = button_order;
        b.event  = event;
        world.add_component<UiButton>(e, b);
        UiPanel p;
        p.anchor = static_cast<int>(Anchor::TopLeft);
        p.pos    = {x, 0.1f};
        p.size   = {0.3f, 0.3f};
        p.order  = panel_order;
        world.add_component<UiPanel>(e, p);
        return e;
    };

    make(0.1f, 5, 1, "clicked"); // x 0.1 .. 0.4
    make(0.2f, 2, 7, "seen");    // x 0.2 .. 0.5, so they overlap over x 0.2 .. 0.4

    // The diagnostic flags it...
    const auto conflicts = mosaic::engine::ui::find_order_conflicts(world);
    REQUIRE(conflicts.size() == 1);
    REQUIRE(conflicts[0].kind == mosaic::engine::ui::OrderConflictKind::Inverted);

    // ...HALF ONE: the click goes to the higher UiButton::order.
    PointerState ptr;
    world.add_system<UiSystem>([&ptr]() { return ptr; });
    std::vector<ScriptEvent> events;
    world.events().subscribe<ScriptEvent>([&events](const ScriptEvent& ev)
                                          { events.push_back(ev); });

    ptr = PointerState{0.3f, 0.2f, true}; // inside both rects
    world.tick(0.016f);
    ptr = PointerState{0.3f, 0.2f, false};
    world.tick(0.016f);

    std::vector<std::string> clicked;
    for (const auto& ev : events)
    {
        if (!ev.payload.is<Table>()) { continue; }
        const auto* action = ev.payload.as<Table>().find("action");
        if (action != nullptr && action->is<std::string>()
            && action->as<std::string>() == "click")
        {
            clicked.push_back(ev.name);
        }
    }
    REQUIRE(clicked == std::vector<std::string>{"clicked"});

    // ...HALF TWO: the panel pass draws the OTHER one last, so that is what the user sees.
    // Ascending order means the last quad in the batch is on top.
    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2);
    REQUIRE(quads[0].rect.x == Approx(0.1f)); // panel order 1 -- the one that was CLICKED
    REQUIRE(quads[1].rect.x == Approx(0.2f)); // panel order 7 -- the one that is SEEN

    // Said plainly: the entity that answered the click is the one drawn underneath.
    REQUIRE(quads.back().rect.x != Approx(0.1f));
}

TEST_CASE("world-space drawing happens inside the hdr pass and the UI after the resolve",
          "[render][hdr]")
{
    // THE BOUNDARY, which is the whole slice. A UiPanel's colour is authored -- {0.1,0.6,0.9}
    // is a value someone typed and expects to see -- so pushing it through ACES would make
    // what appears on screen depend on how bright the scene behind it is.
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});
    const AssetID mesh_id = register_cube(registry);
    auto          mesh    = world.create_entity();
    world.add_component<WorldTransform>(mesh, WorldTransform{Mat4::translation(Vec3{0, 0, 0})});
    world.add_component<MeshRenderer>(mesh, MeshRenderer{mesh_id});

    auto    e = world.create_entity();
    UiPanel p;
    p.anchor = static_cast<int>(mosaic::engine::ui::Anchor::TopLeft);
    p.pos    = {0.1f, 0.1f};
    p.size   = {0.2f, 0.2f};
    world.add_component<UiPanel>(e, p);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    const int begin   = device.index_of("hdr_begin");
    const int draw    = device.index_of("draw");
    const int end     = device.index_of("hdr_end");
    const int resolve = device.index_of("hdr_resolve");
    const int ui      = device.index_of("ui");

    REQUIRE(begin >= 0);
    REQUIRE(draw > begin);  // the mesh is inside the HDR pass
    REQUIRE(end > draw);    // the pass closes after the world draws
    REQUIRE(resolve > end); // the resolve follows it
    REQUIRE(ui > resolve);  // and the UI lands on the resolved image
}

TEST_CASE("a scene with no UI still resolves", "[render][hdr]")
{
    // `mosaic run --window` on a scene with no HUD. If the resolve were emitted by the UI
    // path, this frame would show the untouched swapchain -- a black screen.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_resolve") >= 0);
    REQUIRE(device.index_of("ui") == -1);
}

TEST_CASE("a frame with no world content still brackets the hdr pass", "[render][hdr]")
{
    // UI only. An unbalanced begin without an end would leave the backend in a pass it never
    // closes, which is a validation error rather than a visual one -- and therefore the kind
    // that only shows up on a real device.
    using mosaic::engine::ui::UiPanel;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto    e = world.create_entity();
    UiPanel p;
    p.anchor = static_cast<int>(mosaic::engine::ui::Anchor::TopLeft);
    p.size   = {0.2f, 0.2f};
    world.add_component<UiPanel>(e, p);

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_begin") >= 0);
    REQUIRE(device.index_of("hdr_end") > device.index_of("hdr_begin"));
    REQUIRE(device.index_of("hdr_resolve") > device.index_of("hdr_end"));
    REQUIRE(device.index_of("ui") > device.index_of("hdr_resolve"));
}

TEST_CASE("a world with no camera still runs the sequence once", "[render][hdr]")
{
    // The state every freshly created World is in. render() has NO early return for a missing
    // camera -- it falls back to a default view and projection -- so the pass structure is
    // unconditional, and a conditional resolve would make this a special case.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_begin") >= 0);
    REQUIRE(device.index_of("hdr_resolve") >= 0);
    REQUIRE(device.hdr_exposures.size() == 1);
    REQUIRE(device.hdr_exposures[0] == Approx(1.0f)); // Camera::exposure's default
}

TEST_CASE("the resolve receives the active camera's exposure", "[render][hdr]")
{
    // Exposure is passed to the resolve rather than read from the lighting UBO inside the
    // backend: it is a property of the resolve now, and a parameter is testable where a UBO
    // field read is not.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.get_component<mosaic::engine::render::Camera>(cam)->exposure = 2.5f;

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.hdr_exposures.size() == 1);
    REQUIRE(device.hdr_exposures[0] == Approx(2.5f));
}

TEST_CASE("the vignette and world text are inside the hdr pass", "[render][hdr]")
{
    // Both are world-space by the spec's boundary. The vignette attenuates light, which is
    // what a real lens does BEFORE exposure, so it belongs before the curve rather than as a
    // post-hoc darkening of the final image.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    auto e = world.create_entity();
    world.add_component<mosaic::engine::vfx::ScreenEffect>(
        e, mosaic::engine::vfx::ScreenEffect{Vec3{0, 0, 0}, 0.0f, 0.4f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    const int effect = device.index_of("screen_effect");
    REQUIRE(effect > device.index_of("hdr_begin"));
    REQUIRE(effect < device.index_of("hdr_end"));
}

TEST_CASE("the scene clear colour reaches the HDR pass", "[render]")
{
    // Found in the branch review, 2026-10-16. RenderSystem hard-coded black into
    // begin_hdr_scene_pass, on the reasoning that nothing downstream reads the clear
    // because the resolve writes every pixel. True of the DESTINATION's clear; false of
    // the HDR pass's, which is now the one the scene is composited over.
    //
    // Concrete failure: a scene with no environment draws no skybox, so the background IS
    // the clear. `mosaic run --window` set {0.1, 0.1, 0.12} and got it until this slice;
    // afterwards every such scene rendered pure black, because ACES(0) = 0. The demo has
    // an environment and its fullscreen skybox covers every pixel, which is why nothing
    // on the verification path would have shown it.
    //
    // It lives on RenderSystem rather than being read back off the device because the
    // pass order lives here: the HDR pass is the scene's background now, and the clear is
    // part of what that pass is.
    AssetRegistry    registry;
    World            world;
    RenderSystem     system;
    FakeRenderDevice device;

    system.set_clear_color(Color{0.25f, 0.5f, 0.75f, 1.0f});
    system.render(world, device, registry);

    REQUIRE(device.hdr_clears.size() == 1);
    REQUIRE(device.hdr_clears[0].r == Catch::Approx(0.25f));
    REQUIRE(device.hdr_clears[0].g == Catch::Approx(0.5f));
    REQUIRE(device.hdr_clears[0].b == Catch::Approx(0.75f));
    // Alpha stays opaque whatever was asked for. The editor composites the resolved panel
    // target with ImGui::Image, which RESPECTS alpha, so a translucent scene background
    // would let the panel behind it show through -- screen_effect.frag records what that
    // looked like the last time it happened.
    REQUIRE(device.hdr_clears[0].a == Catch::Approx(1.0f));
}

TEST_CASE("the scene clear colour defaults to opaque black", "[render]")
{
    // A caller that never sets one gets what RenderSystem used to hard-code, so the
    // default is a continuation rather than a new behaviour to discover.
    AssetRegistry    registry;
    World            world;
    RenderSystem     system;
    FakeRenderDevice device;

    system.render(world, device, registry);

    REQUIRE(device.hdr_clears.size() == 1);
    REQUIRE(device.hdr_clears[0].r == Catch::Approx(0.0f));
    REQUIRE(device.hdr_clears[0].g == Catch::Approx(0.0f));
    REQUIRE(device.hdr_clears[0].b == Catch::Approx(0.0f));
    REQUIRE(device.hdr_clears[0].a == Catch::Approx(1.0f));
}

// --- bloom boundary ---------------------------------------------------------------
//
// FakeRenderDevice records calls in order, so the part of bloom that matters here is
// testable headlessly: THE BOUNDARY. Whether the chain looks like a glow needs a GPU and
// a human; whether it is built once, from the right camera's knobs, between the right two
// calls, is pinned below.

TEST_CASE("build_bloom runs between the hdr pass and the resolve", "[render][bloom]")
{
    // The boundary the spec fixes: the chain is built from a FINISHED HDR target, and read
    // by the resolve. Before end_hdr_scene_pass the target is still being written; after
    // draw_hdr_resolve it is too late to be composited.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.add_component<Bloom>(cam, Bloom{1.0f, 1.0f, 0.5f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    const int end     = device.index_of("hdr_end");
    const int build   = device.index_of("bloom_build");
    const int resolve = device.index_of("hdr_resolve");
    REQUIRE(end >= 0);
    REQUIRE(build > end);
    REQUIRE(resolve > build);
    REQUIRE(device.bloom_builds.size() == 1); // once per frame, not once per camera
}

TEST_CASE("build_bloom is skipped at zero intensity", "[render][bloom]")
{
    // Bloom::intensity defaults to 0, so this is also what EVERY existing scene does.
    // Building a chain nobody reads would cost eleven render passes a frame to produce a
    // value that is then multiplied by zero.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.add_component<Bloom>(cam, Bloom{});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("bloom_build") == -1);
    // The resolve still runs, and still receives an intensity. It is the unconditional
    // half of the boundary: one code path, with the zero doing the work.
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Catch::Approx(0.0f));
}

TEST_CASE("a camera with no Bloom component resolves at zero intensity", "[render][bloom]")
{
    // Every scene authored before this feature existed. Not merely "does not crash": the
    // resolve must receive 0, because it samples the chain unconditionally.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Catch::Approx(0.0f));
}

TEST_CASE("the threshold and knee reaching build_bloom are the active camera's",
          "[render][bloom]")
{
    // Two cameras, each with a DIFFERENT Bloom. The active camera is whichever Camera is
    // found first, and the bloom parameters must come from that same entity -- a second
    // traversal could in principle disagree about which camera that is, which is why
    // find_active_camera carries them rather than a helper of its own.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    first  = add_camera(world, Vec3{0, 0, 5});
    const auto    second = add_camera(world, Vec3{0, 0, 9});
    world.add_component<Bloom>(first, Bloom{1.0f, 2.25f, 0.125f});
    world.add_component<Bloom>(second, Bloom{3.0f, 9.0f, 0.75f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.bloom_builds.size() == 1);
    REQUIRE(device.bloom_builds[0].threshold == Catch::Approx(2.25f));
    REQUIRE(device.bloom_builds[0].knee == Catch::Approx(0.125f));
    REQUIRE(device.bloom_intensities[0] == Catch::Approx(1.0f));
}

TEST_CASE("a Bloom on a non-camera entity is ignored", "[render][bloom]")
{
    // Bloom is a per-CAMERA knob; it is read from the camera entity and nowhere else. A
    // world.for_each<Bloom> would have picked this one up and bloomed a scene whose camera
    // never asked.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});
    const auto stray = world.create_entity();
    world.add_component<Bloom>(stray, Bloom{5.0f, 0.1f, 0.9f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities[0] == Catch::Approx(0.0f));
}

TEST_CASE("a camera with no WorldTransform falls back to the Bloom defaults",
          "[render][bloom]")
{
    // find_active_camera returns FALSE without touching its out-parameters when the camera
    // has no WorldTransform -- the exact trap its existing comment names about exposure.
    // Uninitialised bloom parameters would ship whatever was on the stack, and a large
    // intensity there is a white screen.
    //
    // A camera with no WorldTransform is the normal state of a freshly spawned entity: the
    // TransformSystem gives every Transform a WorldTransform on its first TICK, and the
    // editor opens stopped.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = world.create_entity();
    world.add_component<Camera>(cam, Camera{});
    world.add_component<Bloom>(cam, Bloom{4.0f, 0.2f, 0.3f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    // No active camera, so NONE of its knobs are read -- not the exposure, not the bloom.
    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Catch::Approx(0.0f));
    REQUIRE(device.hdr_exposures[0] == Catch::Approx(1.0f));
}

TEST_CASE("a negative bloom intensity is floored at zero", "[render][bloom]")
{
    // intensity multiplies the chain BEFORE the tone curve, so a negative value subtracts
    // light; tonemap_aces floors its own input, so the result is a hard black clip around
    // every bright object rather than a gradient -- a confident, plausible, completely
    // wrong image from one typed minus sign.
    //
    // Floored HERE and not only in the shader, so the fake device sees it: the shader floor
    // is unreachable from any headless test.
    using mosaic::engine::vfx::Bloom;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    const auto    cam = add_camera(world, Vec3{0, 0, 5});
    world.add_component<Bloom>(cam, Bloom{-2.0f, 1.0f, 0.5f});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.bloom_intensities[0] == Catch::Approx(0.0f));
    // And a non-positive intensity means there is nothing to build.
    REQUIRE(device.index_of("bloom_build") == -1);
}

TEST_CASE("a world with no camera still resolves with zero bloom", "[render][bloom]")
{
    // The state every freshly created World is in. render() has no early return for a
    // missing camera, so the sequence is unconditional -- and the resolve samples the chain
    // unconditionally too, which is why the intensity must be a real 0 and not whatever was
    // on the stack.
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.index_of("hdr_resolve") >= 0);
    REQUIRE(device.index_of("bloom_build") == -1);
    REQUIRE(device.bloom_intensities.size() == 1);
    REQUIRE(device.bloom_intensities[0] == Catch::Approx(0.0f));
}

TEST_CASE("a faded container fades its children", "[render][ui]")
{
    // The end-to-end half: the quad the device receives carries the PRODUCT. Asserted
    // against the fake device rather than against inherited_alpha, because the thing that
    // can be forgotten is the multiply at the call site, not the helper.
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    const auto container = world.create_entity();
    UiPanel    outer;
    outer.alpha = 0.5f;
    outer.order = 0;
    world.add_component<UiPanel>(container, outer);

    const auto child = world.create_entity();
    UiPanel    inner;
    inner.alpha = 0.5f;
    inner.order = 1;
    world.add_component<UiPanel>(child, inner);
    world.add_component<UiParent>(child, UiParent{container});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws[0];
    REQUIRE(quads.size() == 2);
    // order 0 then 1: the container first, then the child at 0.5 * 0.5.
    REQUIRE(quads[0].alpha == Catch::Approx(0.5f));
    REQUIRE(quads[1].alpha == Catch::Approx(0.25f));
}

TEST_CASE("a faded container fades a child image", "[render][ui]")
{
    // The image pass's multiply, which nothing covered: the panel case alone left TWO of
    // the three inherited-alpha call sites able to survive deletion with the suite green.
    // The principle the panel case states -- "the thing that can be forgotten is the
    // multiply at the CALL SITE, not the helper" -- applies three times, so it is asserted
    // three times.
    using mosaic::engine::ui::UiImage;
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    AssetRegistry registry;
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    const auto container = world.create_entity();
    UiPanel    outer;
    outer.alpha = 0.5f;
    world.add_component<UiPanel>(container, outer);

    const auto tex = register_white_texture(registry);
    const auto    child = world.create_entity();
    UiImage       im;
    im.texture = tex;
    im.alpha   = 0.5f;
    world.add_component<UiImage>(child, im);
    world.add_component<UiParent>(child, UiParent{container});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_image_draws.size() == 1);
    REQUIRE(device.ui_image_draws[0].quads.size() == 1);
    REQUIRE(device.ui_image_draws[0].quads[0].alpha == Catch::Approx(0.25f));
}

TEST_CASE("a faded container fades child text", "[render][ui]")
{
    // The third and last inherited-alpha call site. The text pass is the one that reaches
    // the hierarchy by a DIFFERENT route -- it calls resolve_parent_frame directly rather
    // than resolve_ui_rect -- so it is the site most likely to be missed by someone
    // wiring the other two.
    using mosaic::engine::ui::UiPanel;
    using mosaic::engine::ui::UiParent;
    using mosaic::engine::ui::UiText;
    AssetRegistry registry;
    const auto    font_id = register_font(registry);
    World         world;
    RenderSystem  rs;
    add_camera(world, Vec3{0, 0, 5});

    const auto container = world.create_entity();
    UiPanel    outer;
    outer.alpha = 0.5f;
    world.add_component<UiPanel>(container, outer);

    const auto child = world.create_entity();
    UiText     t;
    t.font   = font_id;
    t.text   = "A";
    t.height = 0.1f;
    t.alpha  = 0.5f;
    world.add_component<UiText>(child, t);
    world.add_component<UiParent>(child, UiParent{container});

    FakeRenderDevice device;
    rs.render(world, device, registry);

    REQUIRE(device.ui_text_draws.size() == 1);
    REQUIRE(device.ui_text_draws[0].quads.size() == 1);
    REQUIRE(device.ui_text_draws[0].quads[0].alpha == Catch::Approx(0.25f));
}
