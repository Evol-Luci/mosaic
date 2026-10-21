#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace mosaic::engine::render
{
// What a pixel in Image::pixels means.
//
// Rgba8: four bytes per pixel, the historical and default form.
//
// Rgba16F: four HALF floats per pixel, eight bytes, little-endian. Half-floats are
// bytes, so the storage does not change shape -- only the stride does. This exists
// because an HDR environment carries values above 1.0, which is the whole point of
// .hdr: an LDR sky clips the sun. RGBA16F is the standard runtime format for one --
// half the memory of RGBA32F, natively filterable, and with ample mantissa for a sky.
//
// Rgba16F is ALWAYS linear. A float format has no sRGB variant, so asking for an sRGB
// colour space with one is a category error rather than a choice; the device ignores
// the colour space for this format.
enum class ImageFormat { Rgba8, Rgba16F };

constexpr std::size_t bytes_per_pixel(ImageFormat f)
{
    return (f == ImageFormat::Rgba16F) ? 8u : 4u;
}

// CPU image (row-major). The asset resource a texture AssetID resolves to; uploaded by
// RenderDevice::create_texture.
struct Image
{
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    // Defaulting to Rgba8 is what keeps every caller that predates the HDR path correct.
    ImageFormat               format = ImageFormat::Rgba8;
    std::vector<std::uint8_t> pixels; // width*height*bytes_per_pixel(format)
};
} // namespace mosaic::engine::render
inline constexpr int ccprobe_img = 1;
