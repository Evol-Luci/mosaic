#pragma once
#include <cstdint>
#include <vector>
#include "engine/render/render_device.hpp" // Vertex

namespace mosaic::engine::render
{
// CPU mesh: the asset resource a mesh AssetID resolves to; uploaded by
// RenderDevice::create_mesh. Not reflected — it is a resource, referenced by AssetID
// (the same relationship Image has to a texture AssetID).
struct Mesh
{
    std::vector<Vertex>        vertices;
    std::vector<std::uint32_t> indices;
};
} // namespace mosaic::engine::render
