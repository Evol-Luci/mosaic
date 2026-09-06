#pragma once

#include <cstdint>
#include <vector>

#include "engine/render/image.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

struct Color { float r, g, b, a; };

// Unlit vertex was {position, color}; lighting needs a normal. 36d-2 adds uv.
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;
    mosaic::foundation::math::Vec2 uv;
};

using MeshHandle = std::uint32_t;
using TextureHandle = std::uint32_t; // opaque; 0 == invalid/default
using ViewportTextureId = std::uint64_t; // ImTextureID-compatible; 0 == none

// Per-draw parameters. `light_direction` and `point_position` are in the mesh's OBJECT
// space (the RenderSystem transforms world lights per mesh), so the backend needs no
// model/normal matrix. `point_color` is premultiplied by intensity; `point_range == 0`
// disables the point-light contribution.
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Vec3 light_direction;
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 base_color;
    mosaic::foundation::math::Vec3 point_position;
    mosaic::foundation::math::Vec3 point_color;
    float                          point_range = 0.0f; // 0 == no point light (disabled)
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

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;
    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;
    virtual void end_frame() = 0;
    virtual MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices) = 0;
    virtual TextureHandle create_texture(const Image& image) = 0;
    virtual void draw(MeshHandle mesh, TextureHandle texture, const DrawParams& params) = 0;

    // Editor UI overlay hooks. Default no-ops so headless devices and test fakes
    // need not implement them. On a UI-capable backend, call init_ui() once after
    // construction, then per frame: begin_ui_frame() before building the UI and
    // end_ui_frame() between begin_frame() and end_frame() to record the UI.
    virtual void init_ui() {}
    virtual void begin_ui_frame() {}
    virtual void end_ui_frame() {}

    // Draw camera-facing, alpha-blended billboards, one per instance. Default no-op so
    // headless devices and test fakes need not implement it (like the UI/viewport hooks).
    // Must be called after mesh draws in the same pass (see RenderSystem ordering contract).
    virtual void draw_particles(const std::vector<ParticleInstance>& /*instances*/,
                                const ParticleDrawParams& /*params*/) {}

    // Offscreen viewport hooks. Default no-ops so headless devices and test fakes
    // need not implement them. The editor renders the scene into a panel-sized
    // offscreen target (set_viewport_size + begin/end_viewport_pass), then samples
    // it in the UI via viewport_texture().
    virtual void set_viewport_size(std::uint32_t /*width*/, std::uint32_t /*height*/) {}
    virtual void begin_viewport_pass(const Color& /*clear*/) {}
    virtual void end_viewport_pass() {}
    virtual ViewportTextureId viewport_texture() const { return 0; }
};

} // namespace mosaic::engine::render
