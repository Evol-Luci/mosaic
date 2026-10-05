#include "app/asset/image_cook.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <new>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO // we always decode from memory; no file path ever reaches stb
#define STBI_NO_HDR   // float HDR has no route into our RGBA8 Image
#define STBI_NO_GIF   // animation has no meaning for a texture
// PNM support OFF, deliberately. stb decodes P5/P6 too, so leaving it on made read_ppm
// UNREACHABLE -- and stb accepts any maxval <= 65535 while copying the bytes with NO
// rescaling, so a legal "P6 2 1 15" silently imported as a near-black texture where it
// used to fail loudly. read_ppm is the one decoder with no third-party code in its path,
// which is the whole reason the spec keeps it. Found in review 2026-10-04.
#define STBI_NO_PNM
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

    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int len = static_cast<int>(bytes.size());

    // Read the HEADER first and refuse an implausible size before decoding anything.
    // Doing this after the decode would be too late: the allocation is the attack.
    int iw = 0, ih = 0, ic = 0;
    if (stbi_info_from_memory(data, len, &iw, &ih, &ic) != 0)
    {
        if (!image_dimensions_are_sane(static_cast<std::uint64_t>(iw < 0 ? 0 : iw),
                                       static_cast<std::uint64_t>(ih < 0 ? 0 : ih)))
        {
            return std::nullopt;
        }
    }

    stbi_uc* decoded = stbi_load_from_memory(data, len, &iw, &ih, &ic, 4); // 4 == RGBA
    if (decoded == nullptr)
    {
        // Not a format stb understands. PPM is ours (STBI_NO_PNM above), so it lands
        // here and read_ppm -- with its stricter maxval rule -- handles it.
        return read_ppm(bytes);
    }
    // Frees on every exit, including an exception out of the copy below.
    const std::unique_ptr<stbi_uc, void (*)(void*)> owned{decoded, stbi_image_free};

    if (!image_dimensions_are_sane(static_cast<std::uint64_t>(iw < 0 ? 0 : iw),
                                   static_cast<std::uint64_t>(ih < 0 ? 0 : ih)))
    {
        return std::nullopt;
    }

    mosaic::engine::render::Image img;
    img.width = static_cast<std::uint32_t>(iw);
    img.height = static_cast<std::uint32_t>(ih);
    const std::size_t count =
        static_cast<std::size_t>(iw) * static_cast<std::size_t>(ih) * 4u;
    try
    {
        img.pixels.assign(decoded, decoded + count);
    }
    catch (const std::bad_alloc&)
    {
        // Documented never to throw: a bad_alloc escaping into import_asset is a crash
        // where the contract promises a logged failure.
        return std::nullopt;
    }
    return img;
}
} // namespace mosaic::app
