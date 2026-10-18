// Textual contract tests for the bloom shaders.
//
// CI has no GPU, so the only way to pin a shader-side guard is to read the source and
// assert the guard is in it. These cases each name a specific way the chain goes wrong on
// a real device and nowhere else: a divide by zero, an upside-down sample, a composite on
// the wrong side of the tone curve.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::code_only;
using mosaic::tests::normalize_ws;
using mosaic::tests::read_shader;

TEST_CASE("the bloom prefilter floors the knee before dividing by it", "[render][bloom]")
{
    // The sharpest edge in this feature. The soft ramp divides by 4 * knee. knee is a
    // reflected float rendered by the Studio inspector as an unbounded DragFloat, so 0 is
    // one drag away -- and a zero there makes every bloomed pixel NaN, which the chain
    // then spreads across the whole image and the resolve adds in. NaN survives the tone
    // curve's clamp, so the result is not "too bright", it is garbage.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_prefilter.frag")));
    INFO("bloom_prefilter.frag must floor pc.knee before using it as a divisor. A zero "
         "knee must degrade to a hard threshold, not to NaN.");
    REQUIRE(frag.find("max(pc.knee") != std::string::npos);
    // And nothing divides by the raw value.
    REQUIRE(frag.find("/ (4.0 * pc.knee") == std::string::npos);
    REQUIRE(frag.find("/ pc.knee") == std::string::npos);
}

TEST_CASE("the bloom prefilter floors its input colour at zero", "[render][bloom]")
{
    // The same trap tonemap_aces documents: DirectionalLight::color is a reflected Vec3
    // with no clamp anywhere, and the inspector renders it as three unbounded DragFloats.
    // A negative colour reaching the brightness max() picks the least-negative channel and
    // thresholds a dark pixel against it.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_prefilter.frag")));
    INFO("bloom_prefilter.frag must floor the sampled colour at zero before computing "
         "brightness.");
    REQUIRE(frag.find("max(texture(hdrScene, uv).rgb, vec3(0.0))") != std::string::npos);
}

TEST_CASE("the bloom prefilter floors the threshold at zero", "[render][bloom]")
{
    // A negative threshold makes the contribution term (br + |t|) / br, which exceeds 1
    // and so AMPLIFIES rather than selecting -- a scene glowing brighter than it is lit,
    // from one typed minus sign.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_prefilter.frag")));
    REQUIRE(frag.find("max(pc.threshold, 0.0)") != std::string::npos);
}

TEST_CASE("every bloom shader derives its uv from gl_FragCoord", "[render][bloom]")
{
    // The Y-flip trap, which hdr_resolve.frag documents at length. A fullscreen triangle
    // that derives its UV from an interpolated clip position samples upside down under
    // this backend's negative-height viewports; screen_effect.vert has exactly that bug
    // and nobody noticed, because a vignette is radially symmetric. A bloom chain built
    // upside down would composite a mirrored glow onto the scene -- visible, but as "the
    // blur looks wrong" rather than as an obvious flip.
    for (const char* name : {"bloom_prefilter.frag", "bloom_blur.frag"})
    {
        const std::string frag = normalize_ws(code_only(read_shader(name)));
        INFO(std::string(name) + " must take uv from gl_FragCoord, not from an "
             "interpolated input.");
        REQUIRE(frag.find("vec2 uv = gl_FragCoord.xy * pc.inv_dest;") != std::string::npos);
        // No interpolated input at all, so it cannot drift back to one.
        REQUIRE(frag.find("layout(location = 0) in") == std::string::npos);
    }
}

TEST_CASE("the bloom blur offsets its taps by the source texel size", "[render][bloom]")
{
    // One shader serves both the downsample and the upsample, and this is why it can: the
    // taps are offset by a SOURCE texel, so against a source twice the destination's size
    // it is a box downsample, and against one half the size it is a tent upsample.
    // Offsetting by the DESTINATION texel instead would make the upsample a point resample
    // -- a visibly blocky glow, and the single easiest thing here to get backwards.
    const std::string frag = normalize_ws(code_only(read_shader("bloom_blur.frag")));
    INFO("bloom_blur.frag must offset its taps by pc.inv_src, never by pc.inv_dest.");
    REQUIRE(frag.find("vec2 t = pc.inv_src;") != std::string::npos);
    REQUIRE(frag.find("uv + vec2(-t.x, -t.y)") != std::string::npos);
    REQUIRE(frag.find("uv + vec2( t.x, t.y)") != std::string::npos);
    // The destination texel size is used ONLY for uv.
    REQUIRE(frag.find("vec2(-pc.inv_dest") == std::string::npos);
}

TEST_CASE("no bloom shader tone maps", "[render][bloom]")
{
    // hdr_resolve.frag holds the only copy of the curve in the tree, and the existing
    // tone-map contract sweeps the shader list to keep it that way. The two new ones join
    // that sweep as well as this case, so a reader of either file sees the rule.
    for (const char* name : {"bloom_prefilter.frag", "bloom_blur.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string(name) + " must not tone map -- the chain is linear light, and it "
             "is added INSIDE the curve by the resolve.");
        REQUIRE(frag.find("tonemap") == std::string::npos);
        REQUIRE(frag.find("2.2") == std::string::npos); // nor gamma-correct
    }
}

TEST_CASE("the resolve adds bloom inside the tone curve", "[render][bloom]")
{
    // The single most consequential line in the feature. Added AFTER the curve, bloom
    // would be untonemapped glow pasted onto a tone-mapped scene: it clips instantly and
    // reads as a white smear rather than as light. Exposure scales it too, because bloom
    // IS light -- an exposure change that moved the scene but not its glow would look like
    // a compositing error.
    const std::string frag = normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    INFO("hdr_resolve.frag must composite as "
         "tonemap_aces((hdr + bloom * pc.bloom_intensity) * max(pc.exposure, 0.0)).");
    REQUIRE(frag.find("tonemap_aces((hdr + bloom * pc.bloom_intensity) * "
                      "max(pc.exposure, 0.0))") != std::string::npos);
}

TEST_CASE("the resolve samples the bloom chain from its own descriptor set",
          "[render][bloom]")
{
    // Set 0 is the HDR scene, set 1 is the chain. Both use the backend's one
    // single-combined-image-sampler layout, so the two must differ by SET and not by
    // binding -- a second binding in set 0 would need a second layout, and
    // texture_set_layout_ is shared with every texture in the engine.
    const std::string frag = normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    REQUIRE(frag.find("layout(set = 0, binding = 0) uniform sampler2D hdrScene;")
            != std::string::npos);
    REQUIRE(frag.find("layout(set = 1, binding = 0) uniform sampler2D bloomChain;")
            != std::string::npos);
}

TEST_CASE("the resolve samples the chain unconditionally", "[render][bloom]")
{
    // No branch on bloom_intensity. A branch there would make the shader's behaviour
    // depend on a push constant, and the WHOLE skip-safety argument is that the multiply
    // by zero does the work -- which also means the chain must be in a readable layout on
    // every frame, including frame 1. That is what the backend's one-time clear and
    // transition is for.
    const std::string frag = normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    REQUIRE(frag.find("if (pc.bloom_intensity") == std::string::npos);
    REQUIRE(frag.find("vec3 bloom = texture(bloomChain, uv).rgb;") != std::string::npos);
}
