#include "app/asset/image_cook.hpp"

#include <cstddef>
#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO // we always decode from memory; no file path ever reaches stb
#define STBI_NO_HDR   // float HDR has no route into our RGBA8 Image
#define STBI_NO_GIF   // animation has no meaning for a texture
#include "third_party/stb/stb_image.h"

namespace mosaic::app
{
std::optional<mosaic::engine::render::Image> read_image(const std::string& bytes)
{
    if (bytes.empty()) { return std::nullopt; }

    // stb takes an int length. A buffer larger than INT_MAX cannot be handed over
    // safely, and truncating it would decode a prefix and call it a success -- so
    // refuse instead. No real texture is 2 GB.
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::nullopt;
    }

    int w = 0, h = 0, channels_in_file = 0;
    stbi_uc* decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                             static_cast<int>(bytes.size()), &w, &h,
                                             &channels_in_file, 4); // 4 == force RGBA

    if (decoded == nullptr)
    {
        // Not a format stb understands. PPM is ours, so try it before giving up.
        return read_ppm(bytes);
    }
    if (w <= 0 || h <= 0)
    {
        stbi_image_free(decoded);
        return std::nullopt;
    }

    mosaic::engine::render::Image img;
    img.width = static_cast<std::uint32_t>(w);
    img.height = static_cast<std::uint32_t>(h);
    const std::size_t count =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    img.pixels.assign(decoded, decoded + count);
    stbi_image_free(decoded);
    return img;
}
} // namespace mosaic::app
