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
};

} // namespace mosaic::engine::render
