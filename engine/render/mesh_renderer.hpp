#pragma once

#include "engine/render/render_device.hpp" // MeshHandle

namespace mosaic::engine::render
{
// Runtime component: which uploaded mesh an entity draws. NOT reflected/serialized
// (the handle is a per-run device id); the serializable asset-referenced form is 36d.
struct MeshRenderer
{
    MeshHandle mesh = 0;
};
} // namespace mosaic::engine::render
