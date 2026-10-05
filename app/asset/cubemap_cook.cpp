#include "app/asset/cubemap_cook.hpp"

#include <cmath>
#include <cstring>
#include <limits>

#include "app/asset/image_cook.hpp" // float_to_half / half_to_float

namespace mosaic::app
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

// Bilinear sample of an equirect image, wrapping in u (it is a full 360 degrees) and
// clamping in v (the poles do not wrap). Returns the texel as raw bytes in the source's
// format, blended per channel -- for Rgba16F the blend is done in float and re-narrowed.
void sample_equirect(const mosaic::engine::render::Image& src, float u, float v,
                     std::uint8_t* out)
{
    using mosaic::engine::render::ImageFormat;
    const std::size_t bpp = mosaic::engine::render::bytes_per_pixel(src.format);
    const int w = static_cast<int>(src.width);
    const int h = static_cast<int>(src.height);

    // -0.5 puts the sample at texel centres; without it the whole image shifts by half
    // a texel, which shows up as a seam at the face edges rather than as blur.
    float fx = u * static_cast<float>(w) - 0.5f;
    float fy = v * static_cast<float>(h) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    auto wrap_x = [w](int x) { const int m = x % w; return (m < 0) ? m + w : m; };
    auto clamp_y = [h](int y) { return (y < 0) ? 0 : ((y >= h) ? h - 1 : y); };

    const int xs[2] = {wrap_x(x0), wrap_x(x0 + 1)};
    const int ys[2] = {clamp_y(y0), clamp_y(y0 + 1)};
    const float wx[2] = {1.0f - tx, tx};
    const float wy[2] = {1.0f - ty, ty};

    if (src.format == ImageFormat::Rgba8)
    {
        float acc[4] = {0, 0, 0, 0};
        for (int j = 0; j < 2; ++j)
        {
            for (int i = 0; i < 2; ++i)
            {
                const std::size_t off =
                    (static_cast<std::size_t>(ys[j]) * src.width + xs[i]) * bpp;
                const float weight = wx[i] * wy[j];
                for (int k = 0; k < 4; ++k)
                {
                    acc[k] += weight * static_cast<float>(src.pixels[off + k]);
                }
            }
        }
        for (int k = 0; k < 4; ++k)
        {
            const float c = acc[k] + 0.5f; // round rather than truncate
            out[k] = static_cast<std::uint8_t>(c < 0.0f ? 0.0f : (c > 255.0f ? 255.0f : c));
        }
        return;
    }

    // Rgba16F: widen to float, blend, narrow back. Blending the raw half bit patterns
    // would be meaningless arithmetic on an exponent field.
    float acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2; ++j)
    {
        for (int i = 0; i < 2; ++i)
        {
            const std::size_t off = (static_cast<std::size_t>(ys[j]) * src.width + xs[i]) * bpp;
            const float weight = wx[i] * wy[j];
            for (int k = 0; k < 4; ++k)
            {
                const std::uint16_t bits = static_cast<std::uint16_t>(
                    src.pixels[off + k * 2] | (src.pixels[off + k * 2 + 1] << 8));
                acc[k] += weight * half_to_float(bits);
            }
        }
    }
    for (int k = 0; k < 4; ++k)
    {
        const std::uint16_t bits = float_to_half(acc[k]);
        out[k * 2 + 0] = static_cast<std::uint8_t>(bits & 0xFF);
        out[k * 2 + 1] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
    }
}
} // namespace

mosaic::foundation::math::Vec3 cube_face_direction(int face, float u, float v)
{
    using mosaic::foundation::math::Vec3;
    // u,v run 0..1 across the face; a,b remap them to -1..1 so the face centre is 0.
    const float a = 2.0f * u - 1.0f;
    const float b = 2.0f * v - 1.0f;
    Vec3 d{};
    switch (face)
    {
        case 0: d = Vec3{ 1.0f,   -b,   -a}; break; // +X
        case 1: d = Vec3{-1.0f,   -b,    a}; break; // -X
        case 2: d = Vec3{    a, 1.0f,    b}; break; // +Y
        case 3: d = Vec3{    a,-1.0f,   -b}; break; // -Y
        case 4: d = Vec3{    a,   -b, 1.0f}; break; // +Z
        default: d = Vec3{  -a,   -b,-1.0f}; break; // -Z
    }
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return Vec3{d.x / len, d.y / len, d.z / len};
}

void equirect_to_uv(const mosaic::foundation::math::Vec3& dir, float& u, float& v)
{
    u = std::atan2(dir.z, dir.x) / (2.0f * kPi) + 0.5f;
    // Clamp before acos: a direction one ulp outside [-1,1] from normalisation would
    // produce NaN, and a NaN v samples garbage rather than failing.
    const float y = (dir.y < -1.0f) ? -1.0f : ((dir.y > 1.0f) ? 1.0f : dir.y);
    v = std::acos(y) / kPi;
}

std::array<mosaic::engine::render::Image, 6> equirect_to_cubemap(
    const mosaic::engine::render::Image& equirect, std::uint32_t face_size)
{
    using mosaic::engine::render::Image;
    const std::size_t bpp = mosaic::engine::render::bytes_per_pixel(equirect.format);
    std::array<Image, 6> faces;
    for (int f = 0; f < 6; ++f)
    {
        Image& out = faces[static_cast<std::size_t>(f)];
        out.width = face_size;
        out.height = face_size;
        out.format = equirect.format;
        out.pixels.assign(static_cast<std::size_t>(face_size) * face_size * bpp, 0);
        if (equirect.width == 0 || equirect.height == 0) { continue; }

        for (std::uint32_t y = 0; y < face_size; ++y)
        {
            for (std::uint32_t x = 0; x < face_size; ++x)
            {
                // +0.5 samples the texel centre rather than its corner.
                const float fu = (static_cast<float>(x) + 0.5f) / static_cast<float>(face_size);
                const float fv = (static_cast<float>(y) + 0.5f) / static_cast<float>(face_size);
                const auto dir = cube_face_direction(f, fu, fv);
                float su = 0, sv = 0;
                equirect_to_uv(dir, su, sv);
                sample_equirect(equirect, su, sv,
                                &out.pixels[(static_cast<std::size_t>(y) * face_size + x) * bpp]);
            }
        }
    }
    return faces;
}

std::string cook_cubemap(const std::array<mosaic::engine::render::Image, 6>& faces)
{
    std::string out;
    out += "MOSAICUBE1";
    auto put_u32 = [&](std::uint32_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(faces[0].width);
    put_u32(static_cast<std::uint32_t>(faces[0].format));
    for (const auto& f : faces)
    {
        out.append(reinterpret_cast<const char*>(f.pixels.data()), f.pixels.size());
    }
    return out;
}

std::optional<std::array<mosaic::engine::render::Image, 6>> uncook_cubemap(
    const std::string& bytes)
{
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    constexpr std::size_t kMagic = 10; // "MOSAICUBE1"
    constexpr std::size_t kHeader = kMagic + 8;
    if (bytes.size() < kHeader || bytes.compare(0, kMagic, "MOSAICUBE1") != 0)
    {
        return std::nullopt;
    }
    auto get_u32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off])) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 3])) << 24);
    };
    const std::uint32_t size = get_u32(kMagic);
    const std::uint32_t tag = get_u32(kMagic + 4);
    if (size == 0 || tag > static_cast<std::uint32_t>(ImageFormat::Rgba16F))
    {
        return std::nullopt;
    }
    const auto format = static_cast<ImageFormat>(tag);
    const std::uint64_t bpp = mosaic::engine::render::bytes_per_pixel(format);
    // Division rather than a product, so nothing can wrap -- the same guard
    // uncook_image needed, and for the same reason.
    const std::uint64_t per_face = static_cast<std::uint64_t>(size) * size;
    if (per_face > std::numeric_limits<std::uint64_t>::max() / (bpp * 6u))
    {
        return std::nullopt;
    }
    const std::uint64_t need64 = per_face * bpp * 6u;
    if (need64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
        || bytes.size() - kHeader != static_cast<std::size_t>(need64))
    {
        return std::nullopt;
    }

    const std::size_t face_bytes = static_cast<std::size_t>(per_face * bpp);
    std::array<Image, 6> faces;
    for (int f = 0; f < 6; ++f)
    {
        Image& img = faces[static_cast<std::size_t>(f)];
        img.width = size;
        img.height = size;
        img.format = format;
        const std::size_t off = kHeader + static_cast<std::size_t>(f) * face_bytes;
        img.pixels.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off),
                          bytes.begin() + static_cast<std::ptrdiff_t>(off + face_bytes));
    }
    return faces;
}
} // namespace mosaic::app
