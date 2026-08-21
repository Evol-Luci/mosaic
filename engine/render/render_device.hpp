#pragma once

#include <cstdint>
#include <vector>

#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::render
{

struct Color { float r, g, b, a; };

// Unlit vertex: position + per-vertex color.
struct Vertex
{
    mosaic::foundation::math::Vec3 position;
    mosaic::foundation::math::Vec3 color;
};

using MeshHandle = std::uint32_t; // opaque device mesh id; 0 == invalid

class RenderDevice
{
public:
    virtual ~RenderDevice() = default;

    virtual void begin_frame() = 0;
    virtual void clear(const Color&) = 0;
    virtual void end_frame() = 0;

    // Upload a mesh; returns an opaque handle (0 on empty input).
    virtual MeshHandle create_mesh(const std::vector<Vertex>& vertices,
                                   const std::vector<std::uint32_t>& indices) = 0;

    // Record one indexed draw of `mesh` transformed by `mvp`, between begin/end_frame.
    virtual void draw(MeshHandle mesh, const mosaic::foundation::math::Mat4& mvp) = 0;
};

} // namespace mosaic::engine::render
