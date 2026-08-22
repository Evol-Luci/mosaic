#pragma once

#include <cstdint>
#include <vector>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

struct Color { float r, g, b, a; };

// Unlit vertex was {position, color}; lighting needs a normal.
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 normal;
};

using MeshHandle = std::uint32_t;

// Per-draw parameters. `light_direction` is in the mesh's OBJECT space (the RenderSystem
// transforms the world light per mesh), so the backend needs no model/normal matrix.
struct DrawParams
{
    mosaic::foundation::math::Mat4 mvp;
    mosaic::foundation::math::Vec3 light_direction;
    mosaic::foundation::math::Vec3 light_color;
    mosaic::foundation::math::Vec3 base_color;
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
    virtual void draw(MeshHandle mesh, const DrawParams& params) = 0;
};

} // namespace mosaic::engine::render
