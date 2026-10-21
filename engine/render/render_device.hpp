#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/image.hpp"
#include "engine/ui/ui_rect.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

struct Color { float r, g, b, a; };

// The editor UI's built-in ImGui color scheme. A plain enum (not enum class-in-a-
// reflected-struct; nothing here is serialized directly -- app/studio_prefs.hpp
// stores the persisted choice as a plain int and maps it to this type at the two
// call sites below, the same "reflection layer supports int, not enum class"
// convention engine/particles/particle_emitter.hpp's Easing already established).
enum class UiTheme { Dark, Light, Classic };

// Unlit vertex was {position, color}; lighting needs a normal. 36d-2 adds uv.
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;
    mosaic::foundation::math::Vec2 uv;
};

using MeshHandle = std::uint32_t;
using TextureHandle = std::uint32_t; // opaque; 0 == invalid/default

// Whether a texture's bytes are COLOUR or DATA.
//
// Srgb: the hardware applies the sRGB EOTF on sample, so the shader receives linear
// values. Correct for base colour, and the historical behaviour of every texture.
//
// Linear: the bytes are numbers that merely happen to be stored in an image -- a
// metallic/roughness map, and every future data map. Uploading one as Srgb distorts it:
// an authored 0.5 arrives as roughly 0.21, with no crash and no validation error, and
// the result looks like a plausible material rather than a bug.
//
// The default is Srgb so every call site predating this parameter stays correct. The
// cost of that convenience is that a FUTURE data map added by someone who has not read
// this comment gets sRGB silently; texture_colorspace_contract_test.cpp is what makes
// the decision visible when it changes.
enum class TextureColorSpace { Srgb, Linear };
using ViewportTextureId = std::uint64_t; // ImTextureID-compatible; 0 == none

// Per-frame, world-space lighting shared by every draw this frame. One light of
// each type ("first found"), matching the RenderSystem accessors. A zero `range`
// means that positional light is absent and contributes nothing.
struct FrameLighting
{
    mosaic::foundation::math::Vec3 light_direction;  // world space, normalized
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 point_position;   // world space
    mosaic::foundation::math::Vec3 point_color;      // premultiplied by intensity
    float                          point_range = 0.0f; // 0 == no point light
    mosaic::foundation::math::Vec3 spot_position;    // world space
    mosaic::foundation::math::Vec3 spot_direction;   // world space, normalized
    mosaic::foundation::math::Vec3 spot_color;       // premultiplied by intensity
    float                          spot_range = 0.0f;  // 0 == no spot light
    float                          spot_cos_inner = 1.0f;
    float                          spot_cos_outer = 1.0f;
    mosaic::foundation::math::Vec3 camera_position; // world space, for specular
    mosaic::foundation::math::Mat4 light_view_proj;  // for shadow-map sampling
    // The active Camera's exposure, or 1.0 when the world has no camera. Per-FRAME rather
    // than per-draw because it is a property of the viewer, not of any surface.
    float                          exposure = 1.0f;
};

// Per-draw parameters: the transforms plus the material's own shading parameters.
// Lighting lives in FrameLighting now; shading is done in WORLD space, so the
// backend needs the model matrix (to place the fragment) and the normal matrix
// (transpose of the inverse model, so normals stay correct under non-uniform scale).
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Mat4 model;         // object -> world
    mosaic::foundation::math::Mat4 normal_matrix; // transpose(inverse_affine(model)); upper 3x3 used
    mosaic::foundation::math::Vec3 base_color;
    // Metallic/roughness. Defaults match Material's, so the no-Material draw path and
    // a default-constructed DrawParams agree. roughness is NOT clamped here; the shader
    // clamps it to >= 0.045.
    float metallic = 0.0f;
    float roughness = 0.5f;
    // The metallic/roughness map's device handle. 0 means "none"; RenderSystem
    // substitutes a 1x1 LINEAR white texture rather than leaving it 0, because the
    // shader multiplies by this sampler unconditionally and an unbound sampler is
    // undefined behaviour rather than a visible error.
    TextureHandle metallic_roughness = 0;
};

// One billboarded particle. world_position/size are world-space; alpha is the derived
// fade (1 at spawn -> 0 at end of life), computed by the RenderSystem, not stored.
struct ParticleInstance
{
    mosaic::foundation::math::Vec3 world_position;
    float                          size = 0.0f;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// Camera data for billboarding. view_proj = projection * view (no model matrix; each
// instance is positioned in the vertex shader). camera_right/up are the world-space
// camera basis vectors the billboard quad is spanned along.
struct ParticleDrawParams
{
    mosaic::foundation::math::Mat4 view_proj;
    mosaic::foundation::math::Vec3 camera_right;
    mosaic::foundation::math::Vec3 camera_up;
};

// One world-space glyph vertex (camera-facing text). CPU-expanded; drawn by draw_world_text.
struct WorldGlyphVertex
{
    mosaic::foundation::math::Vec3 position; // world space
    float                          u = 0, v = 0;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// One vertex of a ribbon strip (pre-expanded, world-space). alpha is the taper factor.
struct RibbonVertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// Camera data for ribbons. view_proj = projection * view (vertices are already world-space).
struct RibbonDrawParams
{
    mosaic::foundation::math::Mat4 view_proj;
};

// One decal quad: mvp = projection * view * world (composed by the RenderSystem).
struct DecalInstance
{
    mosaic::foundation::math::Mat4 mvp;
    TextureHandle                  texture = 0; // 0 / out-of-range -> default white
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// Fullscreen post overlay parameters (fade/tint/vignette).
// Per-frame state for the skybox pass.
//
// The ray is built in the shader from the camera basis and the field of view rather
// than from an inverted view-projection, because Mat4 offers only inverse_affine and a
// projection matrix is not affine. RenderSystem reads tan(fovY/2) and the aspect ratio
// back out of the projection matrix it already holds.
struct SkyboxParams
{
    // World-from-view with the translation removed, so the sky does not move with the
    // camera -- only its rotation matters.
    mosaic::foundation::math::Mat4 inverse_view_rotation;
    float tan_half_fov_y = 1.0f;
    float aspect = 1.0f;
    // The same exposure FrameLighting carries. Passed explicitly rather than read from the
    // device's cached lighting state, because the skybox pipeline does not bind the
    // per-frame UBO at all -- and an explicit parameter is visible to the fake device the
    // RenderSystem tests use, where a cached one would not be.
    float exposure = 1.0f;
};

struct ScreenEffectParams
{
    mosaic::foundation::math::Vec3 tint;
    float                          tint_strength = 0.0f;
    float                          vignette      = 0.0f;
};

// What the backend needs to build the bloom chain. The threshold and knee travel; the
// INTENSITY does not -- it goes to draw_hdr_resolve instead, so that a frame which skips
// building the chain still contributes exactly zero rather than the previous frame's
// glow. Safety by arithmetic rather than by a staleness flag that has to be right.
struct BloomParams
{
    float threshold = 1.0f; // display white
    float knee      = 0.5f; // the soft ramp below it
};

// One HUD quad in normalized screen space (top-left origin), alpha-blended.
struct UiQuad
{
    mosaic::engine::ui::Rect       rect;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

// One textured HUD quad in normalized screen space; samples the bound texture x tint.
struct UiImageQuad
{
    mosaic::engine::ui::Rect       rect;
    // u0..v1 select a sub-region of the texture, resolved on the CPU by
    // engine/ui/ui_fit.hpp -- the backend does no arithmetic. Spelled exactly as
    // UiGlyphQuad spells them so the two read the same. Defaults are the whole texture.
    float                          u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    mosaic::foundation::math::Vec3 tint;
    float                          alpha = 1.0f;
};

// One textured glyph quad in normalized screen space; samples the bound font atlas.
struct UiGlyphQuad
{
    mosaic::engine::ui::Rect       rect;
    float                          u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    mosaic::foundation::math::Vec3 color;
    float                          alpha = 1.0f;
};

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;
    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;
    virtual void end_frame() = 0;
    virtual MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices) = 0;
    virtual TextureHandle create_texture(const Image& image,
                                         TextureColorSpace space = TextureColorSpace::Srgb) = 0;

    // Uploads six square faces as a cubemap, in Vulkan's layer order:
    // +X, -X, +Y, -Y, +Z, -Z.
    //
    // Returns a handle from the SAME space as create_texture -- a cubemap is a texture
    // to everything above the device, and only the view type differs. Faces must all be
    // the same size and format; a mismatch returns 0 rather than uploading something
    // the shader would sample as garbage.
    //
    // Defaulted to 0 so the test doubles that do no rendering need not implement it.
    virtual TextureHandle create_cubemap(const std::array<Image, 6>& /*faces*/) { return 0; }

    // Uploads a cubemap WITH a mip chain: `levels[m]` is level m's six faces, level 0 the
    // base. Each level must be mip_extent_for(base, m) square.
    //
    // It UPLOADS the chain; it does not generate one. The prefiltered-specular levels are
    // convolutions at increasing roughness, not downsamples -- blitting the base down
    // would silently replace them with a blur that still looks plausible.
    virtual TextureHandle create_cubemap(const std::vector<std::array<Image, 6>>& /*levels*/)
    {
        return 0;
    }

    // Points the lit pass at the image-based lighting products.
    //
    // All three must be valid. Passing 0 for any of them is REFUSED -- the previous
    // state is kept and an error logged -- rather than treated as "turn IBL off", because
    // a partial set would light the scene with a mixture of real products and stand-ins,
    // which looks like a bad bake rather than a failed call. There is deliberately no way
    // to turn IBL off again once it is on; nothing needs one.
    virtual void set_image_based_lighting(TextureHandle /*irradiance*/,
                                          TextureHandle /*prefiltered*/,
                                          TextureHandle /*brdf_lut*/)
    {
    }
    virtual void draw(MeshHandle mesh, TextureHandle texture, const DrawParams& params) = 0;

    // Editor UI overlay hooks. Default no-ops so headless devices and test fakes
    // need not implement them. On a UI-capable backend, call init_ui() once after
    // construction, then per frame: begin_ui_frame() before building the UI and
    // end_ui_frame() between begin_frame() and end_frame() to record the UI.
    // `ini_path` is where ImGui persists its panel layout; empty keeps ImGui's own
    // default ("imgui.ini" in the working directory). `theme` is applied once,
    // immediately, before the first frame; switch it later at runtime via
    // set_ui_theme().
    virtual void init_ui(const std::string& ini_path = "", UiTheme theme = UiTheme::Dark) {}
    virtual void begin_ui_frame() {}
    virtual void end_ui_frame() {}
    // Switches the live ImGui color scheme. A no-op on a headless/non-UI device or
    // before init_ui() has run, matching every other UI hook's default-no-op
    // contract.
    virtual void set_ui_theme(UiTheme theme) {}

    // Draw camera-facing, alpha-blended billboards, one per instance. Default no-op so
    // headless devices and test fakes need not implement it (like the UI/viewport hooks).
    // Must be called after mesh draws in the same pass (see RenderSystem ordering contract).
    virtual void draw_particles(const std::vector<ParticleInstance>& /*instances*/,
                                const ParticleDrawParams& /*params*/) {}

    // Draw pre-expanded, alpha-blended ribbon triangles (world-space). Default no-op so
    // headless devices and test fakes need not implement it (like draw_particles).
    virtual void draw_ribbons(const std::vector<RibbonVertex>& /*vertices*/,
                              const RibbonDrawParams& /*params*/) {}

    // Draw camera-facing world-space text glyphs (pre-expanded world verts), sampling the
    // font atlas. view_proj = projection * view. Default no-op (like draw_ribbons).
    // RenderSystem batches every label sharing an atlas into one call (a call per label made
    // each overwrite the last in the Vulkan backend). Backends must still treat each call as
    // independent: the vertices are consumed at submit, not at call time.
    virtual void draw_world_text(const std::vector<WorldGlyphVertex>& /*verts*/,
                                 const mosaic::foundation::math::Mat4& /*view_proj*/,
                                 TextureHandle /*atlas*/) {}

    // Draw alpha-blended textured quads (decals). Default no-op (like draw_particles).
    virtual void draw_decals(const std::vector<DecalInstance>& /*decals*/) {}

    // Draw a fullscreen fade/tint/vignette overlay. Default no-op (like draw_particles).
    // Draws `cubemap` as the sky. A no-op by default so test doubles need not care.
    virtual void draw_skybox(TextureHandle /*cubemap*/, const SkyboxParams& /*params*/) {}

    virtual void draw_screen_effect(const ScreenEffectParams& /*params*/) {}

    // Draw alpha-blended screen-space HUD quads (topmost). Default no-op (like draw_decals).
    virtual void draw_ui(const std::vector<UiQuad>& /*quads*/) {}

    // Draw alpha-blended textured HUD quads (full RGBA × tint) sampling `texture`.
    // Default no-op (like draw_ui). Called after draw_ui (panels), so images sit over panels.
    virtual void draw_ui_image(const std::vector<UiImageQuad>& /*quads*/,
                               TextureHandle /*texture*/) {}
    // Draw alpha-blended textured glyph quads sampling `atlas` (coverage in alpha).
    // Default no-op (like draw_ui). Called in the HUD pass after draw_ui.
    virtual void draw_ui_text(const std::vector<UiGlyphQuad>& /*quads*/,
                              TextureHandle /*atlas*/) {}

    // Offscreen viewport hooks. Default no-ops so headless devices and test fakes
    // need not implement them. The editor renders the scene into a panel-sized
    // offscreen target (set_viewport_size + begin/end_viewport_pass), then samples
    // it in the UI via viewport_texture().
    virtual void set_viewport_size(std::uint32_t /*width*/, std::uint32_t /*height*/) {}
    virtual void begin_viewport_pass(const Color& /*clear*/) {}
    virtual void end_viewport_pass() {}
    virtual ViewportTextureId viewport_texture() const { return 0; }

    // --- offscreen HDR scene target -------------------------------------------------
    // World-space rendering goes into a linear HDR target; draw_hdr_resolve then tone maps it
    // as one image. Tone mapping used to live in cube.frag and skybox.frag, which meant
    // decals, particles, ribbons and world text were never tone mapped at all -- and a
    // "bright" particle could not be bright: it was written into a buffer whose ceiling is
    // 1.0, leaving no headroom for a bloom pass to find.
    //
    // draw_hdr_resolve is a DRAW, not a pass, and that is load-bearing: it renders a
    // fullscreen triangle into whatever colour target is currently bound -- the swapchain in
    // `mosaic run --window`, the offscreen viewport target in the editor. Neither caller
    // needs to know which, so both stay on one code path, which is what 37d worked to achieve
    // and a resolve-as-a-pass would have undone.
    //
    // Default no-ops, like the viewport hooks above: a device implementing none renders
    // untone-mapped. Acceptable because only the Vulkan backend produces pixels; every other
    // implementation in this tree records calls and draws nothing.
    virtual void begin_hdr_scene_pass(const Color& /*clear*/) {}
    virtual void end_hdr_scene_pass() {}
    // ONE HOOK, NOT N. The engine says "bloom, with these parameters" and the backend
    // decides how many passes that takes -- a mip chain's length is a backend fact about
    // a backend resource, and driving the loop from RenderSystem would have carried it
    // upward through foundation -> engine -> modules for nothing.
    //
    // Called between end_hdr_scene_pass and draw_hdr_resolve, and ONLY when the active
    // camera's Bloom::intensity is above zero. Default no-op, like the hooks around it.
    virtual void build_bloom(const BloomParams& /*params*/) {}
    // `bloom_intensity` is passed ALWAYS, including on frames where build_bloom was
    // skipped: at 0 the chain contributes 0 whatever it contains, so a stale chain cannot
    // show through and no "is it stale?" flag has to be right.
    virtual void draw_hdr_resolve(float /*exposure*/, float /*bloom_intensity*/) {}

    // Shadow pass: render depth-only geometry from the directional light's point of
    // view into a dedicated shadow map. Default no-op (a backend that doesn't
    // implement shadows simply never populates one; the main pass's shadow lookup
    // then must degrade to "fully lit" on the shader side, not fail).
    virtual void begin_shadow_pass() {}
    virtual void draw_shadow(MeshHandle /*mesh*/, const mosaic::foundation::math::Mat4& /*light_mvp*/) {}
    virtual void end_shadow_pass() {}

    // Per-frame world-space lighting, uploaded once before the frame's mesh draws.
    // Default no-op: a backend with no uniform-buffer path simply renders unlit
    // rather than failing.
    virtual void set_frame_lighting(const FrameLighting& /*lighting*/) {}
};

} // namespace mosaic::engine::render
