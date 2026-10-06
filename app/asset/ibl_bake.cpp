#include "app/asset/ibl_bake.hpp"

#include <cmath>
#include <cstddef>

#include "app/asset/cubemap_cook.hpp" // cube_face_direction
#include "app/asset/image_cook.hpp"   // float_to_half / half_to_float
#include "engine/render/mip_levels.hpp"

namespace mosaic::app
{
namespace
{
using mosaic::engine::render::Image;
using mosaic::engine::render::ImageFormat;
using mosaic::foundation::math::Vec3;

constexpr float kPi = 3.14159265358979323846f;

Vec3 normalise(const Vec3& v)
{
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return (len > 0.0f) ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{0, 0, 1};
}

// Reads one texel as linear RGB, whichever storage format the image uses.
Vec3 texel(const Image& img, std::uint32_t x, std::uint32_t y)
{
    const std::size_t bpp = mosaic::engine::render::bytes_per_pixel(img.format);
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * bpp;
    if (img.format == ImageFormat::Rgba16F)
    {
        auto ch = [&](int c) {
            return half_to_float(static_cast<std::uint16_t>(img.pixels[i + c * 2]
                                                            | (img.pixels[i + c * 2 + 1] << 8)));
        };
        return Vec3{ch(0), ch(1), ch(2)};
    }
    return Vec3{static_cast<float>(img.pixels[i]) / 255.0f,
                static_cast<float>(img.pixels[i + 1]) / 255.0f,
                static_cast<float>(img.pixels[i + 2]) / 255.0f};
}

void write_texel(Image& img, std::uint32_t x, std::uint32_t y, const Vec3& c)
{
    const std::size_t bpp = mosaic::engine::render::bytes_per_pixel(img.format);
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * bpp;
    const float v[3] = {c.x, c.y, c.z};
    if (img.format == ImageFormat::Rgba16F)
    {
        for (int ch = 0; ch < 3; ++ch)
        {
            const std::uint16_t h = float_to_half(v[ch]);
            img.pixels[i + ch * 2 + 0] = static_cast<std::uint8_t>(h & 0xFF);
            img.pixels[i + ch * 2 + 1] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
        }
        const std::uint16_t one = float_to_half(1.0f);
        img.pixels[i + 6] = static_cast<std::uint8_t>(one & 0xFF);
        img.pixels[i + 7] = static_cast<std::uint8_t>((one >> 8) & 0xFF);
        return;
    }
    for (int ch = 0; ch < 3; ++ch)
    {
        const float s = v[ch] * 255.0f + 0.5f;
        img.pixels[i + ch] = static_cast<std::uint8_t>(s < 0.0f ? 0.0f : (s > 255.0f ? 255.0f : s));
    }
    img.pixels[i + 3] = 255;
}

Image make_face(std::uint32_t size, ImageFormat format)
{
    Image f;
    f.width = size;
    f.height = size;
    f.format = format;
    f.pixels.assign(static_cast<std::size_t>(size) * size
                        * mosaic::engine::render::bytes_per_pixel(format),
                    0);
    return f;
}

// A tangent basis about `n`. The "up" vector is swapped near the poles because a
// cross product with a parallel vector is degenerate -- which would silently collapse
// the hemisphere integral to a line at exactly the two directions that matter most for
// a sky.
void basis_about(const Vec3& n, Vec3& tangent, Vec3& bitangent)
{
    const Vec3 up = (std::fabs(n.y) < 0.999f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    tangent = normalise(Vec3{up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z,
                             up.x * n.y - up.y * n.x});
    bitangent = Vec3{n.y * tangent.z - n.z * tangent.y, n.z * tangent.x - n.x * tangent.z,
                     n.x * tangent.y - n.y * tangent.x};
}

// Van der Corput radical inverse, the low-discrepancy half of a Hammersley sequence.
float radical_inverse(std::uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

// A GGX-distributed half-vector about `n` for sample `i` of `count`.
Vec3 importance_sample_ggx(std::uint32_t i, std::uint32_t count, const Vec3& n, float roughness)
{
    const float a = roughness * roughness;
    const float u1 = static_cast<float>(i) / static_cast<float>(count);
    const float u2 = radical_inverse(i);

    const float phi = 2.0f * kPi * u1;
    const float cos_theta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
    const float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

    const Vec3 h_tangent{std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta};
    Vec3 t{}, b{};
    basis_about(n, t, b);
    return normalise(Vec3{t.x * h_tangent.x + b.x * h_tangent.y + n.x * h_tangent.z,
                          t.y * h_tangent.x + b.y * h_tangent.y + n.y * h_tangent.z,
                          t.z * h_tangent.x + b.z * h_tangent.y + n.z * h_tangent.z});
}
} // namespace

Vec3 sample_cubemap(const std::array<Image, 6>& faces, const Vec3& dir)
{
    const float ax = std::fabs(dir.x), ay = std::fabs(dir.y), az = std::fabs(dir.z);
    int face = 0;
    float ma = 0, sc = 0, tc = 0;
    // Vulkan's cube-map face selection: the largest-magnitude component picks the face,
    // and the other two become the in-face coordinates. This inverts
    // cube_face_direction, so the two must agree -- cubemap_cook_test's corner cases pin
    // that function's half.
    if (ax >= ay && ax >= az)
    {
        ma = ax;
        if (dir.x > 0) { face = 0; sc = -dir.z; tc = -dir.y; }
        else           { face = 1; sc =  dir.z; tc = -dir.y; }
    }
    else if (ay >= az)
    {
        ma = ay;
        if (dir.y > 0) { face = 2; sc =  dir.x; tc =  dir.z; }
        else           { face = 3; sc =  dir.x; tc = -dir.z; }
    }
    else
    {
        ma = az;
        if (dir.z > 0) { face = 4; sc =  dir.x; tc = -dir.y; }
        else           { face = 5; sc = -dir.x; tc = -dir.y; }
    }
    if (ma <= 0.0f) { return Vec3{0, 0, 0}; }

    const Image& img = faces[static_cast<std::size_t>(face)];
    if (img.width == 0 || img.height == 0) { return Vec3{0, 0, 0}; }

    const float u = 0.5f * (sc / ma + 1.0f);
    const float v = 0.5f * (tc / ma + 1.0f);
    auto clampi = [](int value, int hi) { return (value < 0) ? 0 : ((value > hi) ? hi : value); };
    const int x = clampi(static_cast<int>(u * static_cast<float>(img.width)),
                         static_cast<int>(img.width) - 1);
    const int y = clampi(static_cast<int>(v * static_cast<float>(img.height)),
                         static_cast<int>(img.height) - 1);
    return texel(img, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
}

std::array<Image, 6> bake_irradiance(const std::array<Image, 6>& env, std::uint32_t size)
{
    std::array<Image, 6> out;
    for (int f = 0; f < 6; ++f) { out[static_cast<std::size_t>(f)] = make_face(size, env[0].format); }
    if (size == 0 || env[0].width == 0) { return out; }

    // 0.025 rad is the conventional step. Smaller buys accuracy the output's 32x32
    // resolution cannot represent; larger shows as banding on a smooth sky.
    constexpr float kStep = 0.025f;

    for (int f = 0; f < 6; ++f)
    {
        Image& face = out[static_cast<std::size_t>(f)];
        for (std::uint32_t y = 0; y < size; ++y)
        {
            for (std::uint32_t x = 0; x < size; ++x)
            {
                const float fu = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float fv = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                const Vec3 n = cube_face_direction(f, fu, fv);
                Vec3 t{}, b{};
                basis_about(n, t, b);

                Vec3 sum{0, 0, 0};
                int samples = 0;
                for (float phi = 0.0f; phi < 2.0f * kPi; phi += kStep)
                {
                    const float sp = std::sin(phi), cp = std::cos(phi);
                    for (float theta = 0.0f; theta < 0.5f * kPi; theta += kStep)
                    {
                        const float st = std::sin(theta), ct = std::cos(theta);
                        // Tangent-space direction, rotated into world space.
                        const Vec3 d{st * cp * t.x + st * sp * b.x + ct * n.x,
                                     st * cp * t.y + st * sp * b.y + ct * n.y,
                                     st * cp * t.z + st * sp * b.z + ct * n.z};
                        const Vec3 L = sample_cubemap(env, d);
                        // cos(theta) is the Lambert weight; sin(theta) is the solid-angle
                        // measure of the spherical parameterisation. Dropping either
                        // leaves a result that still looks plausible.
                        sum.x += L.x * ct * st;
                        sum.y += L.y * ct * st;
                        sum.z += L.z * ct * st;
                        ++samples;
                    }
                }
                // pi * mean. The pi and the division together are what make a uniform
                // environment return itself -- the furnace test checks exactly this, so
                // if it fails, the normalisation is where to look, not the sampling.
                const float k = (samples > 0) ? (kPi / static_cast<float>(samples)) : 0.0f;
                write_texel(face, x, y, Vec3{sum.x * k, sum.y * k, sum.z * k});
            }
        }
    }
    return out;
}

std::vector<std::array<Image, 6>> bake_prefiltered(const std::array<Image, 6>& env,
                                                   std::uint32_t base_size, std::uint32_t levels)
{
    std::vector<std::array<Image, 6>> chain;
    chain.resize(levels);
    if (levels == 0 || base_size == 0 || env[0].width == 0)
    {
        for (auto& level : chain)
        {
            for (auto& f : level) { f = make_face(1, env[0].format); }
        }
        return chain;
    }

    constexpr std::uint32_t kSamples = 1024;

    for (std::uint32_t m = 0; m < levels; ++m)
    {
        // Level extents come from mip_extent_for rather than a restated halving, for the
        // reason that header gives: two independent statements of the same formula drift.
        const std::uint32_t size = mosaic::engine::render::mip_extent_for(base_size, m);
        const float roughness =
            (levels > 1) ? (static_cast<float>(m) / static_cast<float>(levels - 1)) : 0.0f;

        for (int f = 0; f < 6; ++f)
        {
            Image& face = chain[m][static_cast<std::size_t>(f)] = make_face(size, env[0].format);
            for (std::uint32_t y = 0; y < size; ++y)
            {
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    const float fu = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                    const float fv = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                    const Vec3 n = cube_face_direction(f, fu, fv);

                    if (m == 0)
                    {
                        // Roughness 0 is a mirror: its lobe is a delta function, so
                        // convolving is an expensive way to reproduce the input.
                        write_texel(face, x, y, sample_cubemap(env, n));
                        continue;
                    }

                    // The split-sum's standing approximation: N = V = R. It is what makes
                    // a single prefiltered chain usable from any viewing angle, and it is
                    // also why grazing reflections are wrong by construction.
                    Vec3 sum{0, 0, 0};
                    float weight = 0.0f;
                    for (std::uint32_t i = 0; i < kSamples; ++i)
                    {
                        const Vec3 h = importance_sample_ggx(i, kSamples, n, roughness);
                        const float ndoth = n.x * h.x + n.y * h.y + n.z * h.z;
                        const Vec3 l{2.0f * ndoth * h.x - n.x, 2.0f * ndoth * h.y - n.y,
                                     2.0f * ndoth * h.z - n.z};
                        const float ndotl = n.x * l.x + n.y * l.y + n.z * l.z;
                        if (ndotl <= 0.0f) { continue; }
                        const Vec3 L = sample_cubemap(env, normalise(l));
                        sum.x += L.x * ndotl;
                        sum.y += L.y * ndotl;
                        sum.z += L.z * ndotl;
                        weight += ndotl;
                    }
                    const float k = (weight > 0.0f) ? (1.0f / weight) : 0.0f;
                    write_texel(face, x, y, Vec3{sum.x * k, sum.y * k, sum.z * k});
                }
            }
        }
    }
    return chain;
}
Image bake_brdf_lut(std::uint32_t size)
{
    Image lut = make_face(size, ImageFormat::Rgba16F);
    if (size == 0) { return lut; }

    constexpr std::uint32_t kSamples = 1024;

    for (std::uint32_t y = 0; y < size; ++y)
    {
        // Row 0 is roughness 0 and the last row is 1. The shader indexes this axis with
        // roughness directly, so the mapping must stay linear.
        const float roughness =
            (size > 1) ? (static_cast<float>(y) / static_cast<float>(size - 1)) : 0.0f;
        for (std::uint32_t x = 0; x < size; ++x)
        {
            // Column 0 would be NdotV = 0 exactly -- a surface seen edge-on, where the
            // integral is degenerate. Offset by half a texel, as the shader's own
            // sampling does.
            const float ndotv = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const Vec3 v{std::sqrt(1.0f - ndotv * ndotv), 0.0f, ndotv};
            const Vec3 n{0.0f, 0.0f, 1.0f};

            float scale = 0.0f;
            float bias = 0.0f;
            for (std::uint32_t i = 0; i < kSamples; ++i)
            {
                const Vec3 h = importance_sample_ggx(i, kSamples, n, roughness);
                const float vdoth = v.x * h.x + v.y * h.y + v.z * h.z;
                const Vec3 l{2.0f * vdoth * h.x - v.x, 2.0f * vdoth * h.y - v.y,
                             2.0f * vdoth * h.z - v.z};
                if (l.z <= 0.0f) { continue; }

                const float ndotl = l.z;
                const float ndoth = (h.z > 0.0f) ? h.z : 0.0f;
                const float vdoth_c = (vdoth > 0.0f) ? vdoth : 0.0f;

                // Smith geometry with the IBL remap k = alpha^2 / 2 -- NOT the direct
                // lighting k = alpha / 2 that cube.frag uses. The two differ only in how
                // alpha is squared and using the wrong one is a silent energy error.
                const float a = roughness * roughness;
                const float k = (a * a) / 2.0f;
                const float gv = ndotv / (ndotv * (1.0f - k) + k);
                const float gl = ndotl / (ndotl * (1.0f - k) + k);
                const float g = gv * gl;

                const float g_vis = (g * vdoth_c) / ((ndoth * ndotv > 0.0f)
                                                         ? (ndoth * ndotv)
                                                         : 1e-6f);
                const float fc = std::pow(1.0f - vdoth_c, 5.0f);
                scale += (1.0f - fc) * g_vis;
                bias += fc * g_vis;
            }
            const float inv = 1.0f / static_cast<float>(kSamples);
            float a_term = scale * inv;
            float b_term = bias * inv;

            // scale + bias is the specular directional albedo, which is at most 1
            // analytically -- a surface cannot return more than it receives. Excess is
            // SAMPLING ERROR, not a property of the model: g_vis divides by NdotV, which
            // approaches zero at grazing angles, so with a finite sample count the
            // integral diverges there. Measured before adding this: the most grazing
            // column reached 4.1, meaning a surface seen edge-on would have emitted four
            // times the light falling on it.
            //
            // Renormalise rather than clamping each term separately, which would not
            // bound the sum and would also distort the scale/bias split that carries the
            // F0 dependence.
            //
            // Note what this deliberately does NOT do: it does not raise a value that
            // falls SHORT. The shortfall at high roughness is the real single-scatter
            // energy loss and must stay visible -- see the white-furnace test, which pins
            // the measured number.
            const float total = a_term + b_term;
            if (total > 1.0f)
            {
                a_term /= total;
                b_term /= total;
            }
            write_texel(lut, x, y, Vec3{a_term, b_term, 0.0f});
        }
    }
    return lut;
}

} // namespace mosaic::app
