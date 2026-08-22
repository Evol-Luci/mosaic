#pragma once
#include <cstdint>
#include <vector>
namespace mosaic::engine::render
{
// CPU RGBA8 image (row-major, 4 bytes/pixel). The asset resource a texture AssetID
// resolves to; uploaded by RenderDevice::create_texture.
struct Image
{
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    std::vector<std::uint8_t> pixels; // width*height*4
};
} // namespace mosaic::engine::render
