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
#include <utility>

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
                // 0.5%, not the 2% this once had. The looser figure was almost
                // entirely consumed by a normalisation that used the NOMINAL step
                // measure instead of the actual one (1.85% error), which left this
                // unable to detect a genuine 1-2% mistake -- while being cited as the
                // check that justified the coarser step. The measure is correct now and
                // the residual is 0.2%, so the tolerance can do its job again.
                REQUIRE(red_of(irr[static_cast<std::size_t>(f)], x, y)
                        == Approx(1.0f).epsilon(0.005));
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
        REQUIRE(red_of(irr[0], 2, 2) == Approx(L).epsilon(0.005));
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

namespace
{
// (scale, bias) at a texel of the LUT.
std::pair<float, float> lut_at(const Image& lut, std::uint32_t x, std::uint32_t y)
{
    const std::size_t i = (static_cast<std::size_t>(y) * lut.width + x) * 8;
    return {mosaic::app::half_to_float(
                static_cast<std::uint16_t>(lut.pixels[i] | (lut.pixels[i + 1] << 8))),
            mosaic::app::half_to_float(
                static_cast<std::uint16_t>(lut.pixels[i + 2] | (lut.pixels[i + 3] << 8)))};
}
} // namespace

TEST_CASE("the BRDF LUT's terms stay within their analytic bounds", "[asset]")
{
    // Both split-sum terms are integrals of quantities in [0,1], so neither can leave it.
    // A value outside means the importance sampling or the Smith term is wrong, and the
    // visible result is a material that gains or loses energy with viewing angle --
    // which reads as a strange material rather than as a bug.
    const auto lut = mosaic::app::bake_brdf_lut(32);
    REQUIRE(lut.width == 32);
    REQUIRE(lut.height == 32);
    REQUIRE(lut.format == ImageFormat::Rgba16F);
    for (std::uint32_t y = 0; y < 32; ++y)
    {
        for (std::uint32_t x = 0; x < 32; ++x)
        {
            const auto [a, b] = lut_at(lut, x, y);
            INFO("texel " << x << "," << y << " scale " << a << " bias " << b);
            REQUIRE(a >= 0.0f);
            REQUIRE(a <= 1.0f);
            REQUIRE(b >= 0.0f);
            REQUIRE(b <= 1.0f);
        }
    }
}

TEST_CASE("the BRDF LUT approaches a pure Fresnel response at zero roughness", "[asset]")
{
    // The one corner of the domain with a known closed form, which makes it the honest
    // anchor for the whole table: at roughness 0 the distribution is a delta function, so
    // the scale goes to 1 and the bias to 0 and specular is exactly F0.
    const auto lut = mosaic::app::bake_brdf_lut(32);
    const auto [a, b] = lut_at(lut, 24, 0); // mid viewing angle, roughness 0
    INFO("scale " << a << " bias " << b);
    REQUIRE(a == Approx(1.0f).epsilon(0.05));
    REQUIRE(b == Approx(0.0f).margin(0.05));
}

TEST_CASE("the white furnace: measuring how much energy single-scatter GGX loses", "[asset]")
{
    // THE energy test, and the one the other furnace cases in this file cannot give:
    // those show the integrals are NORMALISED, not that the model conserves energy.
    //
    // Put a white surface (F0 = 1, every ray reflects) in a uniform environment of
    // radiance 1. The split-sum reconstruction is prefiltered * (F0 * scale + bias); the
    // prefiltered term is 1 by the furnace property, so the whole thing reduces to
    // (scale + bias). A surface that reflects everything and absorbs nothing must return
    // exactly 1.
    //
    // IT DOES NOT, AND THAT IS THE EXPECTED FINDING. Single-scatter GGX drops the light
    // that would have bounced between microfacets, and the loss grows with roughness.
    // The numbers below are MEASURED, not chosen to pass: they are what this
    // implementation actually returns, so when multi-scatter compensation is added (the
    // backlog carries it) the improvement is checkable against a recorded baseline rather
    // than against a tolerance someone widened.
    const auto lut = mosaic::app::bake_brdf_lut(64);

    // 1. Energy is never INVENTED. The integral's analytic bound is 1; bake_brdf_lut
    //    renormalises the grazing-angle overshoot that finite sampling produces.
    for (std::uint32_t y = 0; y < 64; ++y)
    {
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const auto [a, b] = lut_at(lut, x, y);
            INFO("texel " << x << "," << y << " scale " << a << " bias " << b);
            REQUIRE(static_cast<double>(a) + static_cast<double>(b) <= 1.001);
        }
    }

    // 2. A SMOOTH surface is essentially perfect. A shortfall here would be a defect in
    //    the integration rather than the known limitation, because at roughness 0 there
    //    are no microfacets to bounce between.
    const auto [a0, b0] = lut_at(lut, 32, 0);
    INFO("roughness 0: scale " << a0 << " bias " << b0);
    REQUIRE(static_cast<double>(a0) + static_cast<double>(b0) == Approx(1.0).epsilon(0.02));

    // 3. The loss grows monotonically with roughness, at a fixed viewing angle. This is
    //    the shape of the limitation, and it is what a multi-scatter term must flatten.
    double previous = 2.0;
    for (std::uint32_t y = 0; y < 64; y += 8)
    {
        const auto [a, b] = lut_at(lut, 40, y);
        const double total = static_cast<double>(a) + static_cast<double>(b);
        INFO("roughness " << (y / 63.0) << " returns " << total);
        REQUIRE(total <= previous + 0.02);
        previous = total;
    }

    // 4. The measured worst case, recorded so it can be beaten rather than forgotten.
    //    At roughness 1 a white surface returns about 0.31 of the light it receives --
    //    it loses roughly TWO THIRDS. That is the single-scatter energy loss, quantified.
    const auto [a1, b1] = lut_at(lut, 63, 63);
    const double worst = static_cast<double>(a1) + static_cast<double>(b1);
    INFO("roughness 1.0 at normal incidence returns " << worst);
    REQUIRE(worst > 0.25);
    REQUIRE(worst < 0.40);

    // 5. A MID-roughness value, pinned tightly, and the reason it has to be here.
    //
    // The Smith geometry term uses the IBL remap k = alpha/2, where alpha is roughness
    // squared. Substituting alpha^2/2 -- which is what shipped until the slice-4c review
    // -- is a silent energy error, and every assertion above passed with it, because the
    // two coincide at roughness 0 and at roughness 1 and those are exactly where the
    // other checks sit.
    //
    // They diverge in the middle: 0.786 with the CORRECT remap against 0.896 with the
    // wrong one. An earlier version of this case had those two labels the wrong way
    // round and pinned 0.896 -- so the test that existed to catch the bug was enforcing
    // it, and fixing the code would have produced a red test blaming the fix. Both
    // numbers below are measured against the corrected integral.
    const auto [am, bm] = lut_at(lut, 40, 32); // roughness ~0.51, mid viewing angle
    const double mid = static_cast<double>(am) + static_cast<double>(bm);
    INFO("roughness 0.51 returns " << mid << " (correct k = alpha/2 gives ~0.786, "
                                   << "the wrong k = alpha^2/2 gives ~0.896)");
    REQUIRE(mid == Approx(0.786).margin(0.02));

    // 6. And the whole table respects its analytic bound WITHOUT being clamped to it.
    //    The wrong remap pushed 21% of the grid above 1 and a renormalisation hid that;
    //    with the correct one the maximum is 1.0 exactly, reached only at roughness 0
    //    where it is analytically right. A maximum that is 1.0 across a BAND rather than
    //    at a point means something is being clamped again.
    int at_bound = 0;
    for (std::uint32_t y = 0; y < 64; ++y)
    {
        for (std::uint32_t x = 0; x < 64; ++x)
        {
            const auto [a, b] = lut_at(lut, x, y);
            if (static_cast<double>(a) + static_cast<double>(b) > 0.999) { ++at_bound; }
        }
    }
    INFO(at_bound << " of 4096 cells sit at the analytic bound");
    REQUIRE(at_bound < 200); // the roughness-0 row is 64; a clamped band is hundreds
}

TEST_CASE("an Environment round-trips through cook and uncook", "[asset]")
{
    mosaic::app::Environment env;
    for (auto& f : env.cubemap)
    {
        f.width = 4; f.height = 4; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(4u * 4u * 8u, 0x21);
    }
    for (auto& f : env.irradiance)
    {
        f.width = 2; f.height = 2; f.format = ImageFormat::Rgba16F;
        f.pixels.assign(2u * 2u * 8u, 0x33);
    }
    env.prefiltered.resize(3);
    for (std::size_t m = 0; m < 3; ++m)
    {
        const std::uint32_t sz = 4u >> m;
        for (auto& f : env.prefiltered[m])
        {
            f.width = sz; f.height = sz; f.format = ImageFormat::Rgba16F;
            f.pixels.assign(static_cast<std::size_t>(sz) * sz * 8u,
                            static_cast<std::uint8_t>(0x40 + m));
        }
    }

    const auto back = mosaic::app::uncook_environment(mosaic::app::cook_environment(env));
    REQUIRE(back.has_value());
    REQUIRE(back->cubemap[0].pixels == env.cubemap[0].pixels);
    REQUIRE(back->cubemap[5].width == 4);
    REQUIRE(back->irradiance[3].pixels == env.irradiance[3].pixels);
    REQUIRE(back->prefiltered.size() == 3);
    for (std::size_t m = 0; m < 3; ++m)
    {
        INFO("mip " << m);
        REQUIRE(back->prefiltered[m][0].width == (4u >> m));
        REQUIRE(back->prefiltered[m][5].pixels == env.prefiltered[m][5].pixels);
    }
}

TEST_CASE("uncook_environment rejects malformed blobs", "[asset]")
{
    REQUIRE_FALSE(mosaic::app::uncook_environment("").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_environment("not an environment").has_value());
    REQUIRE_FALSE(mosaic::app::uncook_environment("MOSAICENV1short").has_value());

    // A level count the payload cannot possibly hold. Trusting it would read far past
    // the end of the buffer -- and a cooked asset is file data, so it is exactly the
    // kind of input that arrives corrupt.
    std::string blob = "MOSAICENV1";
    auto put_u32 = [&](std::uint32_t v) {
        blob.push_back(static_cast<char>(v & 0xFF));
        blob.push_back(static_cast<char>((v >> 8) & 0xFF));
        blob.push_back(static_cast<char>((v >> 16) & 0xFF));
        blob.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(4);   // cubemap face size
    put_u32(2);   // irradiance face size
    put_u32(4);   // prefiltered base size
    put_u32(99);  // 99 levels, with no payload at all
    put_u32(static_cast<std::uint32_t>(ImageFormat::Rgba16F));
    REQUIRE_FALSE(mosaic::app::uncook_environment(blob).has_value());
}
