// The furnace test, and the reason this slice convolves on the CPU.
//
// A furnace test puts a surface in a UNIFORM WHITE environment. A correct
// energy-conserving model returns exactly the input radiance -- the surface disappears
// into the background. Too dark means the model loses energy; too bright means it
// invents it. PBR slice 2's spec recorded as its honest limit that no test there could
// show the BRDF correct, and named this as what would.
//
// Because these integrals are C++ rather than shader code, this runs in CI with no GPU.

#include <array>
#include <cmath>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/asset/ibl_bake.hpp"
#include "app/asset/image_cook.hpp"

using Catch::Approx;
using mosaic::engine::render::Image;
using mosaic::engine::render::ImageFormat;

namespace
{
// Six faces of constant radiance L, in the Rgba16F form the real pipeline uses.
std::array<Image, 6> uniform_environment(std::uint32_t size, float L)
{
    std::array<Image, 6> env;
    const std::uint16_t h = mosaic::app::float_to_half(L);
    for (auto& f : env)
    {
        f.width = size;
        f.height = size;
        f.format = ImageFormat::Rgba16F;
        f.pixels.assign(static_cast<std::size_t>(size) * size * 8u, 0);
        for (std::size_t i = 0; i < static_cast<std::size_t>(size) * size; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                f.pixels[i * 8 + c * 2 + 0] = static_cast<std::uint8_t>(h & 0xFF);
                f.pixels[i * 8 + c * 2 + 1] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
            }
        }
    }
    return env;
}

float red_of(const Image& img, std::uint32_t x, std::uint32_t y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 8;
    return mosaic::app::half_to_float(
        static_cast<std::uint16_t>(img.pixels[i] | (img.pixels[i + 1] << 8)));
}
} // namespace

TEST_CASE("furnace: irradiance of a uniform environment is that same uniform value", "[asset]")
{
    // The cosine-weighted hemisphere integral of a constant L, divided by pi, is L. Any
    // other answer means the normalisation is wrong -- which makes every diffuse surface
    // uniformly too bright or too dark, a mistake that reads as an exposure problem
    // rather than as a bug.
    const auto env = uniform_environment(16, 1.0f);
    const auto irr = mosaic::app::bake_irradiance(env, 8);
    REQUIRE(irr[0].width == 8);
    for (int f = 0; f < 6; ++f)
    {
        for (std::uint32_t y = 0; y < 8; ++y)
        {
            for (std::uint32_t x = 0; x < 8; ++x)
            {
                INFO("face " << f << " texel " << x << "," << y);
                REQUIRE(red_of(irr[static_cast<std::size_t>(f)], x, y)
                        == Approx(1.0f).epsilon(0.02));
            }
        }
    }
}

TEST_CASE("furnace: irradiance scales linearly with input radiance", "[asset]")
{
    // Catches a normalisation that happens to be right at L = 1 -- an additive offset, or
    // a clamp -- which the case above alone could not distinguish.
    for (float L : {0.25f, 2.0f, 8.0f})
    {
        const auto irr = mosaic::app::bake_irradiance(uniform_environment(16, L), 4);
        INFO("L = " << L);
        REQUIRE(red_of(irr[0], 2, 2) == Approx(L).epsilon(0.02));
    }
}

TEST_CASE("furnace: prefiltered specular of a uniform environment is unchanged at every roughness",
          "[asset]")
{
    // The specular half. Convolving a constant with ANY normalised kernel returns that
    // constant, so every level must come back at L regardless of roughness.
    //
    // HONEST LIMIT OF THIS CASE, measured rather than assumed: it is close to
    // tautological. The convolution normalises by its own summed NdotL weights, so
    // sum(L*NdotL)/sum(NdotL) returns L for a constant L whatever the kernel does --
    // a mutation replacing the reflected ray with N (no blur at all) passes it. It still
    // earns its place by catching a normalisation that forgets to divide, but the case
    // below is what actually constrains the kernel.
    //
    // Nor does it show the BRDF conserves energy: that requires the full split-sum
    // reconstruction including the LUT, which is the white-furnace case in Task 2.
    const auto env = uniform_environment(16, 1.0f);
    const auto chain = mosaic::app::bake_prefiltered(env, 16, 5);
    REQUIRE(chain.size() == 5);
    for (std::size_t m = 0; m < chain.size(); ++m)
    {
        INFO("mip level " << m << " (roughness " << (static_cast<float>(m) / 4.0f) << ")");
        REQUIRE(red_of(chain[m][0], 0, 0) == Approx(1.0f).epsilon(0.02));
    }
}

TEST_CASE("irradiance of a single bright direction falls off with the cosine", "[asset]")
{
    // The integral's defining SHAPE, which the furnace cases cannot see: a constant
    // environment is rotationally symmetric, so they would pass even with the cosine
    // weight missing entirely.
    std::array<Image, 6> env;
    for (auto& f : env)
    {
        f.width = 8; f.height = 8; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(8u * 8u * 8u, 0);
    }
    const std::uint16_t h = mosaic::app::float_to_half(1.0f);
    for (std::size_t i = 0; i < 8u * 8u; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            env[2].pixels[i * 8 + c * 2 + 0] = static_cast<std::uint8_t>(h & 0xFF);
            env[2].pixels[i * 8 + c * 2 + 1] = static_cast<std::uint8_t>((h >> 8) & 0xFF);
        }
    }
    const auto irr = mosaic::app::bake_irradiance(env, 4);
    const float up = red_of(irr[2], 2, 2);   // +Y, facing the light
    const float down = red_of(irr[3], 2, 2); // -Y, facing away
    INFO("up " << up << " down " << down);
    REQUIRE(up > 0.2f);
    REQUIRE(down < up * 0.1f);
}

TEST_CASE("prefiltered roughness cannot sharpen", "[asset]")
{
    // A blur never increases local contrast. Catches a roughness-to-level mapping that
    // runs backwards, which looks like a material that is too shiny rather than a bug.
    std::array<Image, 6> env;
    for (auto& f : env)
    {
        f.width = 16; f.height = 16; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(16u * 16u * 8u, 0);
    }
    for (std::uint32_t y = 0; y < 16; ++y)
    {
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::uint16_t v = mosaic::app::float_to_half(((x + y) % 2) ? 1.0f : 0.0f);
            const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 8;
            for (int c = 0; c < 3; ++c)
            {
                env[0].pixels[i + c * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                env[0].pixels[i + c * 2 + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            }
        }
    }
    const auto chain = mosaic::app::bake_prefiltered(env, 16, 5);
    auto variance = [](const Image& img) {
        double mean = 0, m2 = 0;
        const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x) { mean += red_of(img, x, y); }
        mean /= static_cast<double>(n);
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x)
            { const double d = red_of(img, x, y) - mean; m2 += d * d; }
        return m2 / static_cast<double>(n);
    };
    const double v0 = variance(chain[0][0]);
    for (std::size_t m = 1; m < chain.size(); ++m)
    {
        INFO("mip level " << m << " variance " << variance(chain[m][0]) << " vs mip0 " << v0);
        REQUIRE(variance(chain[m][0]) <= v0 + 1e-6);
    }
}

TEST_CASE("prefiltered roughness blurs beyond what downsampling alone would", "[asset]")
{
    // The case that actually constrains the convolution, and the reason it exists:
    // sampling N rather than the GGX-reflected ray -- no blur whatsoever -- passed every
    // other test here. The furnace case is tautological for a uniform input, and the
    // variance case is satisfied by the resolution halving on its own.
    //
    // So compare like with like: a rough level against a PURE RESAMPLE at the SAME size.
    // Any difference between them is the kernel, with the resolution change factored out.
    std::array<Image, 6> env;
    for (auto& f : env)
    {
        f.width = 16; f.height = 16; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(16u * 16u * 8u, 0);
    }
    for (std::uint32_t y = 0; y < 16; ++y)
    {
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::uint16_t v = mosaic::app::float_to_half(((x / 4 + y / 4) % 2) ? 1.0f : 0.0f);
            const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 8;
            for (int c = 0; c < 3; ++c)
            {
                env[0].pixels[i + c * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                env[0].pixels[i + c * 2 + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            }
        }
    }
    auto variance = [](const Image& img) {
        double mean = 0, m2 = 0;
        const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x) { mean += red_of(img, x, y); }
        mean /= static_cast<double>(n);
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x)
            { const double d = red_of(img, x, y) - mean; m2 += d * d; }
        return m2 / static_cast<double>(n);
    };

    // Level 2 of a 5-level chain from a 16-pixel base: size 4, roughness 0.5.
    const auto chain = mosaic::app::bake_prefiltered(env, 16, 5);
    REQUIRE(chain[2][0].width == 4);
    // A 1-level chain at base size 4 IS the pure resample -- mip 0 is defined as one.
    const auto resample = mosaic::app::bake_prefiltered(env, 4, 1);
    REQUIRE(resample[0][0].width == 4);

    const double blurred = variance(chain[2][0]);
    const double sharp = variance(resample[0][0]);
    INFO("roughness 0.5 variance " << blurred << " vs resample variance " << sharp);
    REQUIRE(sharp > 1e-4);              // the fixture must actually have contrast to lose
    REQUIRE(blurred < sharp * 0.75);    // and the kernel must demonstrably remove some
}

TEST_CASE("prefiltered blur increases with the level, not merely with the resolution", "[asset]")
{
    // The test above compares level 2 of a 5-level chain, which is roughness 0.5 --
    // and INVERTING the roughness mapping leaves level 2 at 0.5, so it passed that
    // mutation too. Level 2 is exactly the symmetric point; measuring there can never
    // see the direction.
    //
    // So measure two ASYMMETRIC levels, each against its own pure resample, and compare
    // the ratios. Dividing by the resample removes the resolution change, which is the
    // confound that made the earlier variance test vacuous.
    std::array<Image, 6> env;
    for (auto& f : env)
    {
        f.width = 32; f.height = 32; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(32u * 32u * 8u, 0);
    }
    for (std::uint32_t y = 0; y < 32; ++y)
    {
        for (std::uint32_t x = 0; x < 32; ++x)
        {
            const std::uint16_t v = mosaic::app::float_to_half(((x / 8 + y / 8) % 2) ? 1.0f : 0.0f);
            const std::size_t i = (static_cast<std::size_t>(y) * 32 + x) * 8;
            for (int c = 0; c < 3; ++c)
            {
                env[0].pixels[i + c * 2 + 0] = static_cast<std::uint8_t>(v & 0xFF);
                env[0].pixels[i + c * 2 + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            }
        }
    }
    auto variance = [](const Image& img) {
        double mean = 0, m2 = 0;
        const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x) { mean += red_of(img, x, y); }
        mean /= static_cast<double>(n);
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x)
            { const double d = red_of(img, x, y) - mean; m2 += d * d; }
        return m2 / static_cast<double>(n);
    };

    const auto chain = mosaic::app::bake_prefiltered(env, 32, 5);
    // Level 1 is roughness 0.25 at size 16; level 3 is roughness 0.75 at size 4.
    REQUIRE(chain[1][0].width == 16);
    REQUIRE(chain[3][0].width == 4);
    const double r1 = variance(chain[1][0]) / variance(mosaic::app::bake_prefiltered(env, 16, 1)[0][0]);
    const double r3 = variance(chain[3][0]) / variance(mosaic::app::bake_prefiltered(env, 4, 1)[0][0]);
    INFO("blur ratio at roughness 0.25 = " << r1 << ", at 0.75 = " << r3);
    // The rougher level must retain LESS of its resample's contrast than the smoother
    // one. Inverting the mapping swaps these and fails here.
    REQUIRE(r3 < r1);
}
