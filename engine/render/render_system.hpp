#pragma once

#include "engine/assets/asset_registry.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/spot_light.hpp"
#include "foundation/logging/log.hpp"
#include "engine/render/environment.hpp"
#include "engine/render/image.hpp"
#include <cmath>
#include "engine/render/material.hpp"
#include "engine/render/mesh_cache.hpp"
#include "engine/render/mesh_renderer.hpp"
#include <array>
#include <functional>

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
#include "engine/vfx/bloom.hpp"
#include "engine/vfx/screen_effect.hpp"
#include "engine/render/font_texture_cache.hpp"
#include "engine/ui/text_layout.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_text.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_fit.hpp"
#include "engine/ui/ui_hover_style.hpp"
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
    // How to build the split-sum BRDF table, supplied rather than called directly: the
    // integration lives in the app tier (beside the other bakers it shares helpers with)
    // and the tier rules allow engine -> foundation only.
    //
    // A factory rather than an Image because integrating it costs seconds -- this is
    // invoked once, on the first frame that actually needs it, the same laziness the
    // environment asset uses.
    void set_brdf_lut_factory(std::function<Image()> factory)
    {
        brdf_lut_factory_ = std::move(factory);
        brdf_lut_ = 0;
    }

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

    // The colour the scene is composited over, and the one a caller means by "background".
    //
    // It lives here rather than being read back off the device because the pass order
    // lives here: the HDR pass is the scene's background now, and the clear is part of
    // what that pass is. RenderDevice::clear() still sets the DESTINATION pass's clear,
    // which the resolve overwrites in full -- so a caller that sets only that one gets
    // black, which is the bug this replaced (found in review 2026-10-16: a scene with no
    // environment draws no skybox, so the background IS the clear, and every such scene
    // went pure black).
    //
    // Alpha is forced opaque at the call site rather than here, so what a caller set is
    // what it reads back.
    void set_clear_color(const Color& c) { clear_color_ = c; }
    const Color& clear_color() const { return clear_color_; }

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
        // Initialised here, not inside find_active_camera: the fallback path below returns
        // false without touching it, and an uninitialised exposure would ship whatever was
        // on the stack -- a black frame at 0, a white one at a large value.
        float exposure = 1.0f;
        // The bloom knobs are initialised here for the same reason, and have the sharper
        // failure: a large intensity left on the stack is a white screen, and a zero knee
        // is a NaN chain.
        mosaic::engine::render::BloomParams bloom{};
        float bloom_intensity = 0.0f;
        if (!find_active_camera(world, view, projection, exposure, bloom, bloom_intensity))
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
        frame_lighting.exposure = exposure;
        device.set_frame_lighting(frame_lighting);

        // EVERYTHING WORLD-SPACE FROM HERE goes into the linear HDR target: opaque meshes,
        // the skybox, decals, particles, ribbons, the vignette and world text. They are tone
        // mapped together by the resolve below, which is what gives a bright particle
        // somewhere above 1.0 to live until bloom can find it. Tone mapping used to happen
        // per object in cube.frag and skybox.frag, so the other four were never tone mapped
        // at all -- they blended against a buffer already through the curve.
        //
        // The clear is the scene's background: with no environment there is no skybox to
        // cover it, so this IS what the window shows. Alpha is forced opaque whatever was
        // asked for -- the editor composites the resolved panel target with ImGui::Image,
        // which respects alpha, and a translucent scene background would let the panel
        // behind it show through.
        device.begin_hdr_scene_pass(
            Color{clear_color_.r, clear_color_.g, clear_color_.b, 1.0f});

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
            if (auto env = registry.load<Environment>(environment_asset_))
            {
                environment_ = device.create_cubemap(env->cubemap);

                // The IBL products, uploaded alongside the sky. All three must succeed
                // or none is used: a partial set would light the scene with a mixture of
                // the real environment and 1x1 stand-ins, which looks like a bad bake
                // rather than like a missing upload.
                const TextureHandle irradiance = device.create_cubemap(env->irradiance);
                const TextureHandle prefiltered = device.create_cubemap(env->prefiltered);
                const TextureHandle lut = brdf_lut_handle(device);
                if (irradiance != 0 && prefiltered != 0 && lut != 0)
                {
                    device.set_image_based_lighting(irradiance, prefiltered, lut);
                    // Said out loud because the alternative -- IBL silently not
                    // activating -- looks exactly like an environment that is simply
                    // dim, which is the one failure this slice cannot tell apart by eye.
                    mosaic::foundation::log::info(
                        "render", "image-based lighting active ({} prefiltered levels)",
                        env->prefiltered.size());
                }
                else
                {
                    mosaic::foundation::log::warn(
                        "render",
                        "image-based lighting unavailable (irradiance {}, prefiltered {}, "
                        "lut {}); the flat ambient stays in use",
                        irradiance, prefiltered, lut);
                }
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
            sky.exposure = exposure;
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
            // ONE draw_world_text per atlas, carrying every label that uses it. The backend
            // keeps a single per-frame vertex buffer written at offset 0 while the draw is
            // only recorded, so a call per label made each label overwrite the one before
            // (the "Orbiter" label rendered as "ter" under a second label, "GLOW").
            //
            // Grouped by the resolved TextureHandle, not the font AssetID: the handle is
            // what the draw binds, and FontTextureCache yields 0 for a failed upload.
            //
            // Order: groups in first-seen order, labels within a group in for_each order.
            // That is the same order the per-label calls had for any single-font scene.
            // Across fonts it can reorder interleaved labels (A,B,A -> A,A,B), which only
            // matters where two labels overlap on screen; for_each order is dense order over
            // a sparse set, so it is not stable across an unrelated entity's destruction and
            // was never a guarantee anything could rely on.
            struct WorldTextBatch
            {
                TextureHandle                                           atlas = 0;
                std::vector<mosaic::engine::render::WorldGlyphVertex>   verts;
            };
            std::vector<WorldTextBatch> batches;
            world.for_each<mosaic::engine::ui::WorldLabel>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::WorldLabel& l)
                {
                    if (!l.visible || l.alpha <= 0.0f || l.text.empty()) { return; }
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
                    if (verts.empty()) { return; }
                    auto it = std::find_if(batches.begin(), batches.end(),
                                           [&](const WorldTextBatch& b) { return b.atlas == atlas; });
                    if (it == batches.end())
                    {
                        batches.push_back(WorldTextBatch{atlas, {}});
                        it = std::prev(batches.end());
                    }
                    it->verts.insert(it->verts.end(), verts.begin(), verts.end());
                });
            for (const auto& batch : batches)
            {
                device.draw_world_text(batch.verts, projection * view, batch.atlas);
            }
        }

        // END OF WORLD SPACE. The UI passes below draw onto the RESOLVED image, deliberately:
        // a UiPanel's colour is authored rather than captured, so tone mapping it would make
        // what appears on screen depend on how bright the scene behind it is -- the HUD would
        // dim whenever the world got bright.
        device.end_hdr_scene_pass();

        // Build the glow from the finished HDR target, BEFORE the resolve reads it. Only
        // when there is something to multiply it by: at zero intensity the chain would cost
        // eleven render passes a frame to produce a value the resolve then multiplies by 0.
        // That is also what every scene authored before this feature does.
        if (bloom_intensity > 0.0f)
        {
            device.build_bloom(bloom);
        }
        // A DRAW, not a pass, into whatever colour target is currently bound: the swapchain
        // in `mosaic run --window`, the offscreen viewport target in the editor. That is what
        // keeps both paths on one code path.
        // The intensity is passed UNCONDITIONALLY, including on the frames above that
        // skipped the build. That is the whole reason it is a resolve parameter rather than
        // a build one: at 0 the chain contributes 0 whatever it contains, so a stale chain
        // cannot show through and no "is it stale?" flag has to be right.
        device.draw_hdr_resolve(exposure, bloom_intensity);

        // HUD pass: screen-space alpha-blended panels, drawn last (topmost, over the
        // screen-effect overlay). order-sorted so higher `order` draws on top.
        {
            std::vector<std::pair<int, UiQuad>> pending;
            world.for_each<mosaic::engine::ui::UiPanel>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiPanel& p)
                {
                    // `visible` sits BESIDE the authored-alpha early-out, not inside
                    // resolve_appearance and not in resolve_parent_frame. Beside it, because
                    // it is the same kind of statement -- "this component is not drawn" --
                    // and a hover style must no more reveal a hidden panel than it may
                    // reveal an alpha-0 one. NOT in resolve_parent_frame, because that
                    // returns the whole SCREEN for a parent with no UiPanel: treating an
                    // invisible panel as absent there would resolve every child of a
                    // layout-only container against the screen instead of its slot. That is
                    // the trap this field exists to escape, so escaping it is a property of
                    // WHERE this line is.
                    if (!p.visible || p.alpha <= 0.0f) { return; }
                    const auto rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, mosaic::engine::ui::to_anchor(p.anchor), p.pos, p.size);
                    // Hover feedback is a render-time override: nothing writes to p.color,
                    // so the authored value survives and a scene saved while hovering
                    // carries no hover tint. The early-out above stays on the AUTHORED
                    // alpha -- 0 means hidden, and a style must not reveal it.
                    const auto app = mosaic::engine::ui::resolve_appearance(
                        mosaic::engine::ui::active_hover_style(world, e), p.color, p.alpha);
                    // Inherited alpha multiplies the RESOLVED alpha, AFTER the authored
                    // early-out above. Both directions matter: an element authored at 0
                    // stays hidden whatever its ancestors say, and a container at 0 hides
                    // a child whose own alpha is 1.
                    pending.push_back(
                        {p.order,
                         UiQuad{rect, app.color,
                                app.alpha * mosaic::engine::ui::inherited_alpha(world, e)}});
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
        //
        // Collected, sorted, then drawn -- the same shape as the panel pass above, so all
        // three HUD passes read alike. Unlike that pass this cannot batch into ONE call,
        // because each image carries its own texture handle, so the sort reorders the calls
        // rather than the quads inside one. Same call count as before.
        {
            std::vector<std::pair<int, std::pair<UiImageQuad, TextureHandle>>> pending;
            world.for_each<mosaic::engine::ui::UiImage>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiImage& im)
                {
                    if (!im.visible || im.alpha <= 0.0f) { return; }
                    // get_info rather than get: the aspect modes need the texture's pixel
                    // dimensions, and a non-zero handle guarantees they are non-zero.
                    const TextureInfo info = texture_cache_.get_info(im.texture, registry, device);
                    if (info.handle == 0) { return; }
                    const mosaic::engine::ui::Rect rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, mosaic::engine::ui::to_anchor(im.anchor), im.pos, im.size);
                    // All the arithmetic lives in ui_fit.hpp, which is testable with no
                    // world and no device. aspect_ is the SCREEN aspect and is load-bearing:
                    // a normalized rect is not square, so the correction needs it as well as
                    // the texture's own aspect.
                    const auto q = mosaic::engine::ui::resolve_image_quad(
                        info.width, info.height, rect, aspect_, im.sub_rect_pos,
                        im.sub_rect_size, im.fit_mode);
                    const auto app = mosaic::engine::ui::resolve_appearance(
                        mosaic::engine::ui::active_hover_style(world, e), im.tint, im.alpha);
                    pending.push_back(
                        {im.order,
                         {UiImageQuad{q.rect, q.u0, q.v0, q.u1, q.v1, app.color,
                                      app.alpha
                                          * mosaic::engine::ui::inherited_alpha(world, e)},
                          info.handle}});
                });
            // stable_sort ASCENDING, matching the panel pass: higher `order` is drawn last
            // and therefore appears on top, and equal orders keep `for_each` order. The
            // stability is load-bearing rather than incidental -- ui_hit.hpp's rule 2
            // documents the hit tie-break as agreeing with "the entity later in iteration
            // draws on top", which a plain sort would quietly break.
            std::stable_sort(pending.begin(), pending.end(),
                             [](const auto& l, const auto& r) { return l.first < r.first; });
            for (auto& pr : pending)
            {
                device.draw_ui_image(std::vector<UiImageQuad>{pr.second.first},
                                     pr.second.second);
            }
        }

        // HUD text pass: single-line UiText, drawn over panels + images (topmost). Uses aspect_.
        //
        // Collect/sort/draw as above. Each UiText already emits ONE call carrying all its
        // glyphs, so like the image pass this reorders calls rather than merging them.
        {
            std::vector<std::pair<int, std::pair<std::vector<UiGlyphQuad>, TextureHandle>>>
                pending;
            world.for_each<mosaic::engine::ui::UiText>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiText& t)
                {
                    if (!t.visible || t.alpha <= 0.0f || t.text.empty()) { return; }
                    std::shared_ptr<mosaic::engine::ui::FontAtlas> font =
                        registry.load<mosaic::engine::ui::FontAtlas>(t.font);
                    if (!font) { return; }
                    const TextureHandle atlas = font_textures_.get(t.font, registry, device);
                    if (atlas == 0) { return; }
                    const mosaic::engine::ui::Rect frame =
                        mosaic::engine::ui::resolve_parent_frame(world, e);
                    const auto app = mosaic::engine::ui::resolve_appearance(
                        mosaic::engine::ui::active_hover_style(world, e), t.color, t.alpha);
                    const std::vector<mosaic::engine::ui::TextQuad> tq =
                        mosaic::engine::ui::layout_text(*font, t.text, mosaic::engine::ui::to_anchor(t.anchor),
                                                        t.pos, t.height, aspect_,
                                                        app.color,
                                                        app.alpha
                                                            * mosaic::engine::ui::inherited_alpha(
                                                                world, e),
                                                        frame);
                    if (tq.empty()) { return; }
                    std::vector<UiGlyphQuad> quads;
                    quads.reserve(tq.size());
                    for (const auto& q : tq)
                    {
                        quads.push_back(UiGlyphQuad{q.rect, q.u0, q.v0, q.u1, q.v1, q.color, q.alpha});
                    }
                    pending.push_back({t.order, {std::move(quads), atlas}});
                });
            std::stable_sort(pending.begin(), pending.end(),
                             [](const auto& l, const auto& r) { return l.first < r.first; });
            for (auto& pr : pending)
            {
                device.draw_ui_text(pr.second.first, pr.second.second);
            }
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
    // The split-sum BRDF table. Built once and cached: the integral does not depend on
    // the environment, so it would be identical for every one -- which is also why it is
    // not an asset.
    TextureHandle brdf_lut_handle(RenderDevice& device) const
    {
        if (brdf_lut_ == 0 && brdf_lut_factory_)
        {
            brdf_lut_ = device.create_texture(brdf_lut_factory_(), TextureColorSpace::Linear);
        }
        return brdf_lut_;
    }

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

    // `exposure` and the bloom knobs ride along rather than getting their own traversal:
    // the active camera is whichever Camera is found first, so a second search could in
    // principle disagree with this one about which camera that is.
    //
    // All four out-parameters are left UNTOUCHED when this returns false, which is why
    // every caller must initialise them first.
    bool find_active_camera(World& world, mosaic::foundation::math::Mat4& view,
                            mosaic::foundation::math::Mat4& projection,
                            float& exposure,
                            mosaic::engine::render::BloomParams& bloom,
                            float& bloom_intensity) const
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
                exposure = cam.exposure;
                // Read from the CAMERA ENTITY, the way ScreenEffect's knobs are authored
                // on it. A world.for_each<Bloom> would have found one anywhere in the
                // scene and bloomed a frame whose camera never asked for it.
                if (const auto* b = world.get_component<mosaic::engine::vfx::Bloom>(e))
                {
                    bloom.threshold = b->threshold;
                    bloom.knee = b->knee;
                    // Floored at 0 HERE, where the fake device can see it -- the shader's
                    // own floor is unreachable from any headless test. A negative intensity
                    // subtracts light before a curve that floors its input, which is a hard
                    // black clip around every bright object rather than a gradient.
                    bloom_intensity = b->intensity > 0.0f ? b->intensity : 0.0f;
                }
                found = true;
            });
        return found;
    }

    float aspect_ = 16.0f / 9.0f;

    Color clear_color_{0.0f, 0.0f, 0.0f, 1.0f};
    mutable MeshCache     mesh_cache_;
    mutable TextureCache  texture_cache_;
    mutable FontTextureCache font_textures_;
    mutable TextureHandle default_white_ = 0;
    mutable TextureHandle default_linear_white_ = 0;
    // mutable for the same reason default_white_ is: render() is const, and these are
    // lazily resolved caches rather than observable state.
    mutable TextureHandle environment_ = 0;
    mutable TextureHandle brdf_lut_ = 0;
    std::function<Image()> brdf_lut_factory_;
    mutable mosaic::foundation::serialization::AssetID environment_asset_{};
};
} // namespace mosaic::engine::render
