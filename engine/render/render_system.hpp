#pragma once

#include "engine/assets/asset_registry.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_cache.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/texture_cache.hpp"
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
#include "engine/ui/ui_text.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
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

// Render-time traversal: finds the active camera + directional light and draws every
// entity with WorldTransform + MeshRenderer, lit (Lambert; light transformed into each
// mesh's object space so the backend needs no model matrix). Fallbacks keep the scene
// lit/rendered when a camera/light/material is absent.
class RenderSystem
{
public:
    void set_aspect(float aspect) { aspect_ = aspect; }

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
                Vec3 base_color{0.8f, 0.8f, 0.8f};
                TextureHandle texture = 0;
                if (const Material* mat = world.get_component<Material>(e))
                {
                    base_color = mat->base_color;
                    texture = texture_cache_.get(mat->texture, registry, device);
                }
                if (texture == 0)
                {
                    texture = default_white(device);
                }
                const Vec3 light_obj = mosaic::foundation::math::normalize(
                    mosaic::foundation::math::transform_direction(
                        Mat4::inverse_affine(wt->matrix), light.direction));

                Vec3 point_position{0.0f, 0.0f, 0.0f};
                Vec3 point_color{0.0f, 0.0f, 0.0f};
                float point_range = 0.0f;
                if (point.present)
                {
                    point_position = mosaic::foundation::math::transform_point(
                        Mat4::inverse_affine(wt->matrix), point.world_pos);
                    point_color = point.color;
                    point_range = point.range;
                }

                device.draw(mesh, texture,
                            DrawParams{projection * view * wt->matrix, light_obj,
                                       light.color, base_color, point_position, point_color, point_range});
            });

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
            const Mat4 cam_world = Mat4::inverse_affine(view);
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

        // HUD pass: screen-space alpha-blended panels, drawn last (topmost, over the
        // screen-effect overlay). order-sorted so higher `order` draws on top.
        {
            std::vector<std::pair<int, UiQuad>> pending;
            world.for_each<mosaic::engine::ui::UiPanel>(
                [&](mosaic::foundation::EntityID, mosaic::engine::ui::UiPanel& p)
                {
                    if (p.alpha <= 0.0f) { return; }
                    const auto rect = mosaic::engine::ui::resolve_rect(
                        to_anchor(p.anchor), p.pos, p.size);
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

        // HUD text pass: single-line UiText, drawn over panels (topmost). Uses aspect_.
        {
            world.for_each<mosaic::engine::ui::UiText>(
                [&](mosaic::foundation::EntityID, mosaic::engine::ui::UiText& t)
                {
                    if (t.alpha <= 0.0f || t.text.empty()) { return; }
                    std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                        registry.load<mosaic::engine::ui::FontAtlas>(t.font);
                    if (!font) { return; }
                    const TextureHandle atlas = font_textures_.get(t.font, registry, device);
                    if (atlas == 0) { return; }
                    const std::vector<mosaic::engine::ui::TextQuad> tq =
                        mosaic::engine::ui::layout_text(*font, t.text, to_anchor(t.anchor),
                                                        t.pos, t.height, aspect_, t.color, t.alpha);
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
};
} // namespace mosaic::engine::render
