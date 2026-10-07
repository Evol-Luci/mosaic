// Pins the four tone-mapping decisions a later reader is most likely to undo.
//
// There is no GPU in CI, so these are textual contracts against the shader source --
// the same idiom as shader_specular_contract_test.cpp. They pin the WIRING. They
// cannot show that the curve looks right; that is a human check against
// `mosaic run --window`, and the spec says so rather than letting a green suite
// imply more.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::code_only;
using mosaic::tests::count_assignments;
using mosaic::tests::read_shader;

TEST_CASE("cube.frag tone maps its output with the ACES approximation", "[render]")
{
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("tonemap_aces") != std::string::npos);

    // Review Focus 4: Reinhard (x / (1 + x)) is a plausible-looking "simplification"
    // that compiles, runs, and desaturates highlights.
    //
    // Pin each constant to its OWN named binding, and pin the expression that uses them.
    // Asserting the four values merely appear somewhere was not enough: swapping c's and
    // d's values (c = 0.59, d = 2.43) passed every assertion while producing a visibly
    // wrong curve, and b = 0.03 was unpinned entirely. Found in review 2026-10-04.
    REQUIRE(frag.find("a = 2.51") != std::string::npos);
    REQUIRE(frag.find("b = 0.03") != std::string::npos);
    REQUIRE(frag.find("c = 2.43") != std::string::npos);
    REQUIRE(frag.find("d = 0.59") != std::string::npos);
    REQUIRE(frag.find("e = 0.14") != std::string::npos);
    REQUIRE(frag.find("clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0)")
            != std::string::npos);
}

TEST_CASE("cube.frag writes only the tone-mapped colour to its attachment", "[render]")
{
    // Review Focus 1: the sharp edge of this slice. A second outColor write added
    // below the tone map bypasses it entirely -- tonemap_aces would still be present
    // and still be called, so a mere "is it applied?" check stays green while the
    // attachment receives unmapped values. Assert there is exactly ONE write, and
    // that it is the mapped one.
    const std::string frag = code_only(read_shader("cube.frag"));

    // Count every ASSIGNMENT to outColor, however spelled. Matching the literal
    // "outColor =" was evadable by exactly the edit this test guards against: a
    // component write such as `outColor.rgb = vec3(fragNormal);` left the count at 1 and
    // left the mapped write present, so the suite stayed green while the attachment
    // received unmapped values for those fragments. Verified in review 2026-10-04 -- the
    // probe passed. Mutation B had only shown that a bypass spelled like the original is
    // caught, which is a weaker claim than the plan made.
    //
    // So: find each `outColor`, skip whitespace and any `.rgb`/`.xyz`/`.r` swizzle, and
    // count it if what follows is a single `=` (not `==`).
    const std::size_t writes = count_assignments(frag, "outColor");
    INFO("cube.frag must assign outColor exactly once, and from tonemap_aces(). If you "
         "added a second write -- a debug visualisation, an early-out, or a component "
         "write like outColor.rgb = ... -- the tone map is being bypassed for those "
         "fragments.");
    REQUIRE(writes == 1);
    REQUIRE(frag.find("outColor = vec4(tonemap_aces(") != std::string::npos);
}

TEST_CASE("the unlit scene shaders are deliberately not tone mapped", "[render]")
{
    // Review Focus 2: decision 1 of the spec, and a judgement a later reader will
    // question. These four shaders output authored colour times alpha and cannot
    // exceed 1.0, so tone mapping them would DARKEN them -- the ACES curve maps 1.0
    // to about 0.8. A "consistency" edit here is a regression, so make it loud.
    for (const char* name : {"particle.frag", "ribbon.frag", "decal.frag", "world_text.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string("shader: ") + name);
        REQUIRE(frag.find("tonemap") == std::string::npos);
    }
}

TEST_CASE("no fragment shader gamma-corrects on top of the sRGB swapchain", "[render]")
{
    // Review Focus 3: the likeliest wrong "improvement" here, because tone mapping and
    // gamma are routinely conflated. The pipeline is ALREADY linear -- the swapchain is
    // B8G8R8A8_SRGB and textures are R8G8B8A8_SRGB, so the hardware converts at both
    // ends. A manual pow(c, 1.0/2.2) would double-correct and wash the image out.
    // All nine colour-producing fragment shaders. The three ui*.frag were missing from
    // this list until review 2026-10-04 pointed out they are the MOST likely place a
    // future reader adds gamma, being the ones the spec describes as display-space.
    for (const char* name : {"cube.frag", "particle.frag", "ribbon.frag", "decal.frag",
                             "world_text.frag", "screen_effect.frag", "ui.frag",
                             "ui_image.frag", "ui_text.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string("shader: ") + name);
        REQUIRE(frag.find("2.2") == std::string::npos);
        REQUIRE(frag.find("0.4545") == std::string::npos); // 1/2.2, the other spelling
    }
}

TEST_CASE("the skybox tone maps with the same curve as the lit pass", "[render]")
{
    // Slice 1 justified tone-mapping cube.frag ALONE on the grounds that every other
    // colour-producing shader is unlit, cannot exceed 1.0, and would merely be darkened
    // by a curve that maps 1.0 to about 0.8. A skybox sampling HDR radiance CAN exceed
    // 1.0 -- it is the first shader for which that reasoning does not hold, and "bright
    // regions roll off rather than clip" is a success criterion of this slice.
    //
    // The two copies must stay identical. A shared GLSL include would be better, but the
    // shader build has no include step, so this test is what holds them together.
    const std::string lit = code_only(read_shader("cube.frag"));
    const std::string sky = code_only(read_shader("skybox.frag"));
    auto body = [](const std::string& s) {
        const std::size_t b = s.find("vec3 tonemap_aces(vec3 x)");
        REQUIRE(b != std::string::npos);
        const std::size_t e = s.find('}', b);
        REQUIRE(e != std::string::npos);
        return mosaic::tests::normalize_ws(s.substr(b, e - b));
    };
    REQUIRE(body(lit) == body(sky));
    // ...and that the skybox actually CALLS it, not merely defines it.
    REQUIRE(sky.find("tonemap_aces(") != std::string::npos);
}

// No comma in this name. Catch2 treats a comma in a name filter as a test-spec SEPARATOR,
// so running this case by name would match nothing and exit non-zero -- indistinguishable
// from a failure. Cost a round of mutation probes on the multi-scatter slice; cost two
// false reds here before the pattern was recognised a second time.
TEST_CASE("skybox.frag writes only the tone-mapped colour to its attachment", "[render]")
{
    // The counterpart cube.frag has had since slice 1, which skybox.frag never got --
    // found by the exposure slice's branch review. Latent since the skybox shipped, and in
    // scope now because this slice makes skybox.frag an exposure consumer: the write it
    // guards is the one that applies BOTH the curve and the exposure scale.
    //
    // Concrete failure it catches: add a debug or early-out write below the real one,
    // e.g. `if (dir.y < 0.0) { outColor.rgb = vec3(0.1); }`. Every other skybox assertion
    // stays green -- tonemap_aces is still defined, still byte-identical to cube.frag's,
    // still called, and `pc.exposure` is still in the pinned expression -- while every
    // lower-hemisphere sky pixel silently ignores the tone map AND the exposure knob.
    //
    // count_assignments, not a literal search, for the reason the cube.frag case records:
    // `outColor.rgb = ...` and `outColor[0] = ...` both evade a literal "outColor =".
    const std::string sky = code_only(read_shader("skybox.frag"));
    const std::size_t writes = count_assignments(sky, "outColor");
    INFO("skybox.frag must assign outColor exactly once, from tonemap_aces(). A second "
         "write bypasses both the curve and the exposure scale for those fragments.");
    REQUIRE(writes == 1);
    REQUIRE(sky.find("outColor = vec4(tonemap_aces(") != std::string::npos);
}

TEST_CASE("exposure scales the colour entering the tone map in both shaders", "[render]")
{
    // The whole point of exposure, and the naive version of this test is worthless.
    //
    // Asserting that `exposure` merely APPEARS is satisfied by
    // `tonemap_aces(color) * fl.exposure.x`, which scales a value the curve has already
    // clamped to [0,1]. That form can darken but can NEVER brighten -- the exact thing a
    // user reaches for exposure to do -- and every token search for "exposure" stays
    // green while it ships. Hence the full expression, whitespace-normalised, in each
    // shader.
    const std::string lit = mosaic::tests::normalize_ws(code_only(read_shader("cube.frag")));
    const std::string sky = mosaic::tests::normalize_ws(code_only(read_shader("skybox.frag")));

    INFO("If this fails, check whether exposure moved OUTSIDE tonemap_aces(...) before "
         "assuming the test is stale. Outside the curve it can only darken.");
    REQUIRE(lit.find("outColor = vec4(tonemap_aces(color * max(fl.exposure.x, 0.0)), 1.0);")
            != std::string::npos);
    REQUIRE(sky.find("outColor = vec4(tonemap_aces(radiance * max(pc.exposure, 0.0)), 1.0);")
            != std::string::npos);
}

TEST_CASE("a negative exposure cannot brighten the image", "[render]")
{
    // Found by the exposure slice's branch review, and the arithmetic is the whole point.
    //
    // ACES is clamp((x*(2.51x + 0.03)) / (x*(2.43x + 0.59) + 0.14), 0, 1). The numerator
    // 2.51x^2 + 0.03x is POSITIVE for x < -0.012, and the denominator's discriminant is
    // 0.59^2 - 4*2.43*0.14 = -1.0127, so the denominator is strictly positive for every
    // real x. A negative input therefore produces a large POSITIVE ratio that clamps to
    // 1.0 -- pure white.
    //
    // Measured: x = -1 gives 2.48/1.98 = 1.2525 -> white. x = -5 -> white. But
    // x = -0.005 -> black and x = -0.1 -> 0.21. So the response is not even monotonic
    // across zero: in one frame, dim pixels go black while bright ones blow out to white.
    //
    // That makes `exposure = -2` where `2` was meant -- one character in a scene file --
    // produce a confident, plausible-looking, completely wrong image. Unlike an authored
    // NaN roughness, which the project deliberately lets through as "a garbage pixel, not
    // an error", this one looks like a deliberate choice.
    //
    // So it is clamped at the SHADING site, which is this project's established place for
    // it: Material does not clamp roughness either -- cube.frag does, with
    // clamp(roughness, 0.045, 1.0) -- precisely so an authored value round-trips
    // byte-for-byte while the shader stays well-defined. Camera keeps whatever was
    // authored; the shader refuses to brighten with it.
    //
    // NOT clamped at the top end, deliberately: a huge exposure overflows to NaN around
    // x = 1.2e19, which is the same garbage-in posture roughness already documents, and
    // bounding it would mean inventing a maximum nobody has asked for.
    const std::string lit = mosaic::tests::normalize_ws(code_only(read_shader("cube.frag")));
    const std::string sky = mosaic::tests::normalize_ws(code_only(read_shader("skybox.frag")));
    INFO("Both shaders must floor exposure at 0. Without it a negative exposure renders "
         "WHITE, not black, and non-monotonically.");
    REQUIRE(lit.find("max(fl.exposure.x, 0.0)") != std::string::npos);
    REQUIRE(sky.find("max(pc.exposure, 0.0)") != std::string::npos);
}

TEST_CASE("neither tone-mapping shader hard-codes an exposure value", "[render]")
{
    // The two shaders must scale by the SAME number, which they do by both reading it
    // from the camera rather than by agreeing on a literal. A constant substituted into
    // either one makes the sky and the lit geometry drift apart in brightness -- which
    // reads as "the sky doesn't match the scene", an authoring complaint rather than a
    // bug report, and no test that checks each shader in isolation would catch it.
    //
    // Pinned as the plumbing rather than the absence of a literal: `exposure` must come
    // from the per-frame block in the lit pass and from the push constant in the skybox,
    // because the skybox does not bind the UBO at all (set 0 is its cubemap).
    const std::string lit = mosaic::tests::normalize_ws(code_only(read_shader("cube.frag")));
    const std::string sky = mosaic::tests::normalize_ws(code_only(read_shader("skybox.frag")));
    REQUIRE(lit.find("vec4 exposure;") != std::string::npos);  // trailing UBO member
    REQUIRE(sky.find("float exposure;") != std::string::npos); // skybox push member
}
