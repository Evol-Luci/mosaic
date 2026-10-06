#pragma once

#include "engine/assets/asset_registry.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/spot_light.hpp"
#include "engine/render/image.hpp"
#include <cmath>
#include "engine/render/material.hpp"
#include "engine/render/mesh_cache.hpp"
#include "engine/render/mesh_renderer.hpp"
#include <array>

#include "engine/render/render_device.hpp"
#include "engine/render/texture_cache.hpp"
#include "engine/render/shadow.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "engine/particles/particle_system.hpp"
#include "engine/vfx/ribbon.hpp"
#include "engine/vfx/decal.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
#include "engine/vfx/screen_effect.hpp"
#include "engine/render/font_texture_cache.hpp"
#include "engine/ui/text_layout.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/world_label.hpp"
#include "engine/ui/world_text.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"
#include <algorithm>
#include <utility>
#include <vector>

namespace mosaic::engine::render
{
inline mosaic::engine::ui::Anchor to_anchor(int a)
{
    return (a >= 0 && a <= 8) ? static_cast<mosaic::engine::ui::Anchor>(a)
                              : mosaic::engine::ui::Anchor::TopLeft;
}

// Render-time traversal: finds the active camera and lights, sends the frame's
// world-space lighting once via set_frame_lighting, then draws every entity with
// WorldTransform + MeshRenderer. Shading is done in WORLD space (Lambert), so each
// draw carries the model matrix and a normal matrix rather than lights pre-transformed
// into its object space. Fallbacks keep the scene lit/rendered when a camera, light,
// or material is absent.
class RenderSystem
{
public:
    // The environment cubemap to draw as the sky. 0 means no sky, which is the state
    // every scene was in before this existed -- so an unset environment renders exactly
    // as it did before rather than failing.
    void set_environment(TextureHandle cubemap) { environment_ = cubemap; }
    TextureHandle environment() const { return environment_; }

    // Point the renderer at an environment ASSET. Resolved on the first frame that has
    // a device, because nothing can upload a cubemap before one exists -- the same lazy
    // pattern TextureCache uses for 2D textures. Setting a new id re-resolves.
    void set_environment_asset(mosaic::foundation::serialization::AssetID id)
    {
        environment_asset_ = id;
        environment_ = 0;
    }
    mosaic::foundation::serialization::AssetID environment_asset() const
    {
        return environment_asset_;
    }

    void set_aspect(float aspect) { aspect_ = aspect; }

    // Records the shadow depth pass. MUST be called before the caller opens its own
    // main render pass (the swapchain pass or, in the editor, the offscreen viewport
    // pass via render_scene_to_viewport) -- a render pass cannot begin while another
    // is already active. `mosaic run --window` opens its swapchain pass lazily on
    // the first draw() call inside render(), so calling render_shadows() immediately
    // before render() is always safe there; the editor opens its viewport pass
    // explicitly and earlier, so render_shadows() must be called before
    // render_scene_to_viewport(), not from inside its draw_scene callback.
    void render_shadows(World& world, RenderDevice& device,
                        mosaic::engine::assets::AssetRegistry& registry) const
    {
        const DirectionalLight light = active_light(world);
        const mosaic::foundation::math::Mat4 light_view_proj =
            directional_light_view_projection(light);

        device.begin_shadow_pass();
        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                const MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device);
                if (mesh == 0) { return; }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr) { return; }
                device.draw_shadow(mesh, light_view_proj * wt->matrix);
            });
        device.end_shadow_pass();
    }

    void render(World& world, RenderDevice& device,
                mosaic::engine::assets::AssetRegistry& registry) const
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;

        Mat4 view;
        Mat4 projection;
        if (!find_active_camera(world, view, projection))
        {
            view = fallback_view();
            projection = Mat4::perspective(kFallbackFovY, aspect_, kFallbackNear, kFallbackFar);
        }

        DirectionalLight light = active_light(world);
        const PointLightState point = active_point_light(world);
        const SpotLightState spot = active_spot_light(world);

        const Mat4 light_view_proj = directional_light_view_projection(light);

        // The camera's world matrix is the inverse of the view matrix; its
        // translation is the eye position, which world-space specular needs. Computed
        // once here and reused by the billboard and world-label passes below, which
        // each previously computed it for themselves.
        const Mat4 cam_world = Mat4::inverse_affine(view);

        // Per-frame, world-space lighting. Sent unconditionally, before (and
        // independently of) any mesh draw -- a frame with no meshes must still
        // refresh the backend's uniform buffer, or it would shade from a previous
        // frame's contents.
        FrameLighting frame_lighting;
        frame_lighting.light_direction = mosaic::foundation::math::normalize(light.direction);
        frame_lighting.light_color = light.color;
        if (point.present)
        {
            frame_lighting.point_position = point.world_pos;
            frame_lighting.point_color = point.color;
            frame_lighting.point_range = point.range;
        }
        if (spot.present)
        {
            frame_lighting.spot_position = spot.world_pos;
            frame_lighting.spot_direction = spot.direction;
            frame_lighting.spot_color = spot.color;
            frame_lighting.spot_range = spot.range;
            frame_lighting.spot_cos_inner = spot.cos_inner;
            frame_lighting.spot_cos_outer = spot.cos_outer;
        }
        frame_lighting.camera_position = Vec3{cam_world.m[12], cam_world.m[13], cam_world.m[14]};
        frame_lighting.light_view_proj = light_view_proj;
        device.set_frame_lighting(frame_lighting);

        world.for_each<MeshRenderer>(
            [&](mosaic::foundation::EntityID e, MeshRenderer& mr)
            {
                const MeshHandle mesh = mesh_cache_.get(mr.mesh, registry, device);
                if (mesh == 0) // nil / unregistered / failed upload -> nothing to draw
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                // Defaults here must match Material's own defaults, so a mesh with no
                // Material shades the same as one with an untouched Material.
                Vec3  base_color{0.8f, 0.8f, 0.8f};
                float metallic = 0.0f;
                float roughness = 0.5f;
                TextureHandle texture = 0;
                TextureHandle mr_texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    metallic = mat->metallic;
                    roughness = mat->roughness;
                    texture = texture_cache_.get(mat->texture, registry, device);
                    // Linear, not Srgb: these are numbers, not a picture. See
                    // TextureColorSpace for what sRGB would do to them.
                    mr_texture = texture_cache_.get(mat->metallic_roughness_texture, registry,
                                                    device, TextureColorSpace::Linear);
                }
                if (texture == 0)
                {
                    texture = default_white(device);
                }
                if (mr_texture == 0)
                {
                    mr_texture = default_linear_white(device);
                }

                // Normals transform by the inverse-transpose of the model matrix, or
                // non-uniformly-scaled meshes shade wrong. inverse_affine already
                // returns identity for a singular linear part (zero scale), so this
                // stays finite without extra guarding.
                const Mat4 normal_matrix = Mat4::transpose(Mat4::inverse_affine(wt->matrix));

                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, wt->matrix,
                                       normal_matrix, base_color, metallic, roughness,
                                       mr_texture});
            });

        // Resolve the environment asset to a cubemap handle the first time we have a
        // device. A failed or missing asset leaves the handle at 0 and the pass is
        // skipped, so the scene renders exactly as it did before environments existed.
        if (environment_ == 0
            && environment_asset_ != mosaic::foundation::serialization::AssetID{})
        {
            if (auto faces = registry.load<std::array<Image, 6>>(environment_asset_))
            {
                environment_ = device.create_cubemap(*faces);
            }
            // Cleared on EITHER failure -- a registry miss or an upload that returns 0
            // (the texture cap reached, or faces that disagree). Clearing only the miss
            // left a failed upload retrying every frame, and create_cubemap logs an
            // error on both its failure paths, so the log filled at frame rate. Found
            // in review 2026-10-05.
            if (environment_ == 0) { environment_asset_ = {}; }
        }

        // Skybox pass: after the opaque meshes so it only shades pixels they did not
        // write (it draws at the far plane with depth writes off), and before the
        // transparent passes below so they blend over it correctly.
        if (environment_ != 0)
        {
            SkyboxParams sky{};
            // Translation removed: only the camera's ROTATION may affect the sky, or it
            // would slide around as the camera moves.
            Mat4 view_rotation = view;
            view_rotation.m[12] = 0.0f;
            view_rotation.m[13] = 0.0f;
            view_rotation.m[14] = 0.0f;
            sky.inverse_view_rotation = Mat4::inverse_affine(view_rotation);
            // Read back out of the projection matrix rather than stored separately, so
            // these cannot drift from the camera actually in use. For a standard
            // perspective matrix m[5] is 1/tan(fovY/2) and m[0] is that divided by the
            // aspect ratio. Guarded because a degenerate projection would divide by zero.
            sky.tan_half_fov_y = (projection.m[5] != 0.0f) ? (1.0f / projection.m[5]) : 1.0f;
            sky.aspect = (projection.m[0] != 0.0f) ? (projection.m[5] / projection.m[0]) : 1.0f;
            device.draw_skybox(environment_, sky);
        }

        // Decal pass: alpha-blended textured quads placed by WorldTransform. After opaque
        // meshes so they blend onto surfaces; MVP composed here (device does no matrix math).
        {
            std::vector<DecalInstance> decals;
            world.for_each<mosaic::engine::vfx::Decal>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::Decal& d)
                {
                    const auto* wt = world.get_component<transform::WorldTransform>(e);
                    if (wt == nullptr) { return; }
                    const TextureHandle tex = texture_cache_.get(d.texture, registry, device);
                    decals.push_back(DecalInstance{projection * view * wt->matrix, tex, d.color, d.alpha});
                });
            if (!decals.empty()) { device.draw_decals(decals); }
        }

        // Billboard pass: particles + trails share one instanced, camera-facing draw. Runs
        // last in the pass (the device binds the particle pipeline; no mesh draw may follow).
        auto* particles = world.get_system<mosaic::engine::particles::ParticleSystem>();
        auto* trails    = world.get_system<mosaic::engine::vfx::TrailSystem>();
        if (particles != nullptr || trails != nullptr)
        {
            const Vec3 cam_right = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[0], cam_world.m[1], cam_world.m[2]});
            const Vec3 cam_up = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[4], cam_world.m[5], cam_world.m[6]});

            std::vector<ParticleInstance> instances;

            if (particles != nullptr)
            {
                world.for_each<mosaic::engine::particles::ParticleEmitter>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::particles::ParticleEmitter& em)
                    {
                        const auto* pool = particles->particles_for(e);
                        if (pool == nullptr) { return; }
                        for (const auto& p : *pool)
                        {
                            const float t = (p.lifetime > 0.0f) ? p.age / p.lifetime : 0.0f;
                            instances.push_back(ParticleInstance{
                                p.position,
                                mosaic::engine::particles::size_at(em, t),
                                mosaic::engine::particles::color_at(em, t),
                                mosaic::engine::particles::alpha_at(em, t)});
                        }
                    });
            }

            if (!instances.empty())
            {
                device.draw_particles(instances,
                                      ParticleDrawParams{projection * view, cam_right, cam_up});
            }

            if (trails != nullptr)
            {
                const Vec3 cam_pos{cam_world.m[12], cam_world.m[13], cam_world.m[14]};
                std::vector<RibbonVertex> ribbon;
                world.for_each<mosaic::engine::vfx::TrailRenderer>(
                    [&](mosaic::foundation::EntityID e, mosaic::engine::vfx::TrailRenderer& tr)
                    {
                        const auto* pts = trails->points_for(e);
                        if (pts == nullptr) { return; }
                        mosaic::engine::vfx::build_ribbon(*pts, cam_pos, tr.size, tr.color, ribbon);
                    });
                if (!ribbon.empty())
                {
                    device.draw_ribbons(ribbon, RibbonDrawParams{projection * view});
                }
            }
        }

        // Screen-effect overlay: fullscreen fade/tint/vignette, drawn last (over everything).
        {
            bool done = false;
            world.for_each<mosaic::engine::vfx::ScreenEffect>(
                [&](mosaic::foundation::EntityID, mosaic::engine::vfx::ScreenEffect& se)
                {
                    if (done) { return; }
                    done = true;
                    if (se.tint_strength > 0.0f || se.vignette > 0.0f)
                    {
                        device.draw_screen_effect(
                            ScreenEffectParams{se.tint, se.tint_strength, se.vignette});
                    }
                });
        }

        // World-label pass: camera-facing 3D text. Part of the scene (depth-tested),
        // under the screen-space HUD passes.
        {
            const Vec3 cam_right = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[0], cam_world.m[1], cam_world.m[2]});
            const Vec3 cam_up = mosaic::foundation::math::normalize(
                Vec3{cam_world.m[4], cam_world.m[5], cam_world.m[6]});
            world.for_each<mosaic::engine::ui::WorldLabel>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::WorldLabel& l)
                {
                    if (l.alpha <= 0.0f || l.text.empty()) { return; }
                    const auto* wt = world.get_component<transform::WorldTransform>(e);
                    if (wt == nullptr) { return; }
                    std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                        registry.load<mosaic::engine::ui::FontAtlas>(l.font);
                    if (!font) { return; }
                    const TextureHandle atlas = font_textures_.get(l.font, registry, device);
                    if (atlas == 0) { return; }
                    const Vec3 pos{wt->matrix.m[12], wt->matrix.m[13], wt->matrix.m[14]};
                    const Vec3 anchor{pos.x + l.offset.x, pos.y + l.offset.y, pos.z + l.offset.z};
                    const auto verts = mosaic::engine::ui::build_world_text(
                        *font, l.text, anchor, cam_right, cam_up, l.size, l.color, l.alpha);
                    if (!verts.empty()) { device.draw_world_text(verts, projection * view, atlas); }
                });
        }

        // HUD pass: screen-space alpha-blended panels, drawn last (topmost, over the
        // screen-effect overlay). order-sorted so higher `order` draws on top.
        {
            std::vector<std::pair<int, UiQuad>> pending;
            world.for_each<mosaic::engine::ui::UiPanel>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiPanel& p)
                {
                    if (p.alpha <= 0.0f) { return; }
                    const auto rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, to_anchor(p.anchor), p.pos, p.size);
                    pending.push_back({p.order, UiQuad{rect, p.color, p.alpha}});
                });
            std::stable_sort(pending.begin(), pending.end(),
                             [](const auto& l, const auto& r) { return l.first < r.first; });
            if (!pending.empty())
            {
                std::vector<UiQuad> quads;
                quads.reserve(pending.size());
                for (auto& pr : pending) { quads.push_back(pr.second); }
                device.draw_ui(quads);
            }
        }

        // HUD image pass: textured sprites, drawn over panels. Reuses texture_cache_.
        {
            world.for_each<mosaic::engine::ui::UiImage>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiImage& im)
                {
                    if (im.alpha <= 0.0f) { return; }
                    const TextureHandle tex = texture_cache_.get(im.texture, registry, device);
                    if (tex == 0) { return; }
                    const mosaic::engine::ui::Rect rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, to_anchor(im.anchor), im.pos, im.size);
                    device.draw_ui_image(
                        std::vector<UiImageQuad>{UiImageQuad{rect, im.tint, im.alpha}}, tex);
                });
        }

        // HUD text pass: single-line UiText, drawn over panels + images (topmost). Uses aspect_.
        {
            world.for_each<mosaic::engine::ui::UiText>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiText& t)
                {
                    if (t.alpha <= 0.0f || t.text.empty()) { return; }
                    std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                        registry.load<mosaic::engine::ui::FontAtlas>(t.font);
                    if (!font) { return; }
                    const TextureHandle atlas = font_textures_.get(t.font, registry, device);
                    if (atlas == 0) { return; }
                    const mosaic::engine::ui::Rect frame =
                        mosaic::engine::ui::resolve_parent_frame(world, e);
                    const std::vector<mosaic::engine::ui::TextQuad> tq =
                        mosaic::engine::ui::layout_text(*font, t.text, to_anchor(t.anchor),
                                                        t.pos, t.height, aspect_, t.color, t.alpha,
                                                        frame);
                    if (tq.empty()) { return; }
                    std::vector<UiGlyphQuad> quads;
                    quads.reserve(tq.size());
                    for (const auto& q : tq)
                    {
                        quads.push_back(UiGlyphQuad{q.rect, q.u0, q.v0, q.u1, q.v1, q.color, q.alpha});
                    }
                    device.draw_ui_text(quads, atlas);
                });
        }
    }

private:
    static constexpr float kFallbackFovY = 1.0472f;
    static constexpr float kFallbackNear = 0.1f;
    static constexpr float kFallbackFar = 100.0f;

    // A SEPARATE 1x1 white from default_white, uploaded Linear rather than Srgb.
    // White happens to be identical under both transfer functions, so sharing one
    // would work today -- and would quietly stop working the moment someone changed
    // either default to a non-extreme value. Two defaults, each correct by
    // construction, is cheaper than that bug.
    TextureHandle default_linear_white(RenderDevice& device) const
    {
        if (default_linear_white_ == 0)
        {
            Image white;
            white.width = 1;
            white.height = 1;
            white.pixels = {255, 255, 255, 255};
            default_linear_white_ = device.create_texture(white, TextureColorSpace::Linear);
        }
        return default_linear_white_;
    }

    TextureHandle default_white(RenderDevice& device) const
    {
        if (default_white_ == 0)
        {
            Image white;
            white.width = 1;
            white.height = 1;
            white.pixels = {255, 255, 255, 255};
            default_white_ = device.create_texture(white);
        }
        return default_white_;
    }

    static mosaic::foundation::math::Mat4 fallback_view()
    {
        using mosaic::foundation::math::Mat4;
        using mosaic::foundation::math::Vec3;
        return Mat4::look_at(Vec3{0.0f, 1.5f, 4.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    }

    struct PointLightState
    {
        bool                           present = false;
        mosaic::foundation::math::Vec3 world_pos{};
        mosaic::foundation::math::Vec3 color{}; // premultiplied by intensity
        float                          range = 0.0f;
    };

    // First PointLight whose entity has a WorldTransform; world position = its translation.
    PointLightState active_point_light(World& world) const
    {
        using mosaic::foundation::math::Vec3;
        PointLightState s;
        world.for_each<PointLight>(
            [&](mosaic::foundation::EntityID e, PointLight& p)
            {
                if (s.present)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                s.present = true;
                s.world_pos =
                    mosaic::foundation::math::transform_point(wt->matrix, Vec3{0.0f, 0.0f, 0.0f});
                s.color = p.color * p.intensity;
                s.range = p.range;
            });
        return s;
    }

    struct SpotLightState
    {
        bool                           present = false;
        mosaic::foundation::math::Vec3 world_pos{};
        mosaic::foundation::math::Vec3 direction{}; // world space, normalized
        mosaic::foundation::math::Vec3 color{};     // premultiplied by intensity
        float                          range = 0.0f;
        float                          cos_inner = 1.0f;
        float                          cos_outer = 1.0f;
    };

    // First SpotLight whose entity has a WorldTransform; world position = its
    // translation, direction = its local -Z axis in world space (Mat4::forward()).
    SpotLightState active_spot_light(World& world) const
    {
        using mosaic::foundation::math::Vec3;
        SpotLightState s;
        world.for_each<SpotLight>(
            [&](mosaic::foundation::EntityID e, SpotLight& l)
            {
                if (s.present)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                s.present = true;
                s.world_pos =
                    mosaic::foundation::math::transform_point(wt->matrix, Vec3{0.0f, 0.0f, 0.0f});
                s.direction = wt->matrix.forward();
                s.color = l.color * l.intensity;
                s.range = l.range;
                s.cos_inner = std::cos(l.inner_angle);
                s.cos_outer = std::cos(l.outer_angle);
            });
        return s;
    }


    // First DirectionalLight, else a default.
    DirectionalLight active_light(World& world) const
    {
        DirectionalLight light{mosaic::foundation::math::Vec3{-0.5f, -1.0f, -0.3f},
                               mosaic::foundation::math::Vec3{1.0f, 1.0f, 1.0f}};
        bool found = false;
        world.for_each<DirectionalLight>(
            [&](mosaic::foundation::EntityID, DirectionalLight& l)
            {
                if (!found)
                {
                    light = l;
                    found = true;
                }
            });
        return light;
    }

    bool find_active_camera(World& world, mosaic::foundation::math::Mat4& view,
                            mosaic::foundation::math::Mat4& projection) const
    {
        using mosaic::foundation::math::Mat4;
        bool found = false;
        world.for_each<Camera>(
            [&](mosaic::foundation::EntityID e, Camera& cam)
            {
                if (found)
                {
                    return;
                }
                const auto* wt = world.get_component<transform::WorldTransform>(e);
                if (wt == nullptr)
                {
                    return;
                }
                view = Mat4::inverse_affine(wt->matrix);
                projection = Mat4::perspective(cam.fov_y_radians, aspect_, cam.near_z, cam.far_z);
                found = true;
            });
        return found;
    }

    float aspect_ = 16.0f / 9.0f;
    mutable MeshCache     mesh_cache_;
    mutable TextureCache  texture_cache_;
    mutable FontTextureCache font_textures_;
    mutable TextureHandle default_white_ = 0;
    mutable TextureHandle default_linear_white_ = 0;
    // mutable for the same reason default_white_ is: render() is const, and these are
    // lazily resolved caches rather than observable state.
    mutable TextureHandle environment_ = 0;
    mutable mosaic::foundation::serialization::AssetID environment_asset_{};
};
} // namespace mosaic::engine::render
