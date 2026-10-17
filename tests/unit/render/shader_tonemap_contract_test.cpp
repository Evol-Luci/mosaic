// Pins the tone-mapping decisions a later reader is most likely to undo.
//
// REWRITTEN 2026-10-16, when tone mapping moved out of the object shaders and into a
// fullscreen resolve. Before that it ran per OBJECT -- cube.frag and skybox.frag each
// carried their own copy of the ACES curve, and decals, particles, ribbons and world text
// carried none, so they alpha-blended against a buffer already through it. These cases
// used to hold the two copies byte-identical. There is one copy now, and the contract
// that matters is the opposite one: that it exists in exactly one place and that nothing
// in world space maps anything.
//
// There is no GPU in CI, so these are textual contracts against the shader source -- the
// same idiom as shader_specular_contract_test.cpp. They pin the WIRING. They cannot show
// that the curve looks right; that is a human check against `mosaic run --window`, and
// the spec says so rather than letting a green suite imply more.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::code_only;
using mosaic::tests::count_assignments;
using mosaic::tests::read_shader;

TEST_CASE("hdr_resolve.frag tone maps with the ACES approximation", "[render]")
{
    const std::string frag = code_only(read_shader("hdr_resolve.frag"));
    REQUIRE(frag.find("tonemap_aces") != std::string::npos);

    // Reinhard (x / (1 + x)) is a plausible-looking "simplification" that compiles, runs,
    // and desaturates highlights.
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

TEST_CASE("the resolve writes only the tone-mapped colour to its attachment", "[render]")
{
    // The sharp edge of the resolve. A second outColor write added below the tone map
    // bypasses it entirely -- tonemap_aces would still be present and still be called, so
    // a mere "is it applied?" check stays green while the attachment receives unmapped
    // values. Assert there is exactly ONE write, and that it is the mapped one.
    const std::string frag = code_only(read_shader("hdr_resolve.frag"));

    // Count every ASSIGNMENT to outColor, however spelled. Matching the literal
    // "outColor =" was evadable by exactly the edit this guards against: a component write
    // such as `outColor.rgb = vec3(hdr);` left the count at 1 and left the mapped write
    // present, so the suite stayed green while the attachment received unmapped values for
    // those fragments. Verified by probe in review 2026-10-04.
    const std::size_t writes = count_assignments(frag, "outColor");
    INFO("hdr_resolve.frag must assign outColor exactly once, and from tonemap_aces(). If "
         "you added a second write -- a debug visualisation, an early-out, or a component "
         "write like outColor.rgb = ... -- the tone map is being bypassed for those "
         "fragments.");
    REQUIRE(writes == 1);
    REQUIRE(frag.find("outColor = vec4(tonemap_aces(") != std::string::npos);
}

TEST_CASE("exactly one shader in the tree maps anything", "[render]")
{
    // THE CONTRACT THIS SLICE EXISTS FOR, and the inverse of what these cases pinned
    // before it. Tone mapping belongs to the frame, not to an object: every world-space
    // shader writes LINEAR HDR into the R16G16B16A16_SFLOAT scene target and
    // hdr_resolve.frag maps the whole image once.
    //
    // A "consistency" edit that reintroduces the curve in cube.frag or skybox.frag --
    // which is what they both looked like until 2026-10-16, so it will look like a
    // restoration rather than a regression -- double-maps those pixels: mapped once at the
    // object, then again at the resolve. It is also precisely what makes bloom impossible,
    // because a value clamped to 1.0 at the attachment has no headroom left for a bright
    // pass to find.
    for (const char* name : {"cube.frag", "skybox.frag", "particle.frag", "ribbon.frag",
                             "decal.frag", "world_text.frag", "screen_effect.frag",
                             "ui.frag", "ui_image.frag", "ui_text.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string("shader: ") + name
             + " -- tone mapping lives in hdr_resolve.frag alone. Mapping here as well "
               "double-maps these pixels and clamps away the headroom bloom needs.");
        REQUIRE(frag.find("tonemap") == std::string::npos);
    }
}

TEST_CASE("the lit pass and the skybox write their colour exactly once", "[render]")
{
    // Kept from the per-object era, with its reason intact. These two compute a colour
    // through a long function; a debug or early-out write added below the real one, e.g.
    // `if (dir.y < 0.0) { outColor.rgb = vec3(0.1); }`, silently replaces the shading for
    // those fragments while every other assertion stays green. What changed is only what
    // the single write now contains: linear HDR rather than mapped colour.
    for (const char* name : {"cube.frag", "skybox.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string("shader: ") + name + " must assign outColor exactly once.");
        REQUIRE(count_assignments(frag, "outColor") == 1);
    }
}

TEST_CASE("no fragment shader gamma-corrects on top of the sRGB destination", "[render]")
{
    // The likeliest wrong "improvement" here, because tone mapping and gamma are routinely
    // conflated. The pipeline is ALREADY linear -- the swapchain is B8G8R8A8_SRGB and
    // textures are R8G8B8A8_SRGB, so the hardware converts at both ends. A manual
    // pow(c, 1.0/2.2) would double-correct and wash the image out.
    //
    // hdr_resolve.frag joined this list the moment it became the one shader writing
    // display colour, which makes it the most likely place for exactly this edit.
    for (const char* name : {"cube.frag", "skybox.frag", "particle.frag", "ribbon.frag",
                             "decal.frag", "world_text.frag", "screen_effect.frag",
                             "hdr_resolve.frag", "ui.frag", "ui_image.frag", "ui_text.frag"})
    {
        const std::string frag = code_only(read_shader(name));
        INFO(std::string("shader: ") + name);
        REQUIRE(frag.find("2.2") == std::string::npos);
        REQUIRE(frag.find("0.4545") == std::string::npos); // 1/2.2, the other spelling
    }
}

TEST_CASE("exposure scales the colour entering the tone map", "[render]")
{
    // The whole point of exposure, and the naive version of this test is worthless.
    //
    // Asserting that `exposure` merely APPEARS is satisfied by
    // `tonemap_aces(hdr) * pc.exposure`, which scales a value the curve has already
    // clamped to [0,1]. That form can darken but can NEVER brighten -- the exact thing a
    // user reaches for exposure to do -- and every token search for "exposure" stays green
    // while it ships. Hence the full expression, whitespace-normalised.
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("hdr_resolve.frag")));

    INFO("If this fails, check whether exposure moved OUTSIDE tonemap_aces(...) before "
         "assuming the test is stale. Outside the curve it can only darken.");
    REQUIRE(frag.find("outColor = vec4(tonemap_aces(hdr * max(pc.exposure, 0.0)), 1.0);")
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
    // NOT clamped at the top end, deliberately: a huge exposure overflows to NaN around
    // x = 1.2e19, which is the same garbage-in posture roughness already documents, and
    // bounding it would mean inventing a maximum nobody has asked for.
    //
    // One shader floors it now rather than two. That is a strictly smaller surface for the
    // bug, and it is also why the second guard below still earns its place.
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    INFO("The resolve must floor exposure at 0. Without it a negative exposure renders "
         "WHITE, not black, and non-monotonically.");
    REQUIRE(frag.find("max(pc.exposure, 0.0)") != std::string::npos);

    // AND the curve floors its own input, which is a second guard on a different input
    // rather than a duplicate of the first. The branch review's observation: the colour
    // itself can go negative, because DirectionalLight::color is a reflected Vec3 with no
    // clamp anywhere and the inspector renders it as three unbounded DragFloats. A light
    // colour of (-1,-1,-1) reproduces the identical white-screen pathology with exposure
    // sitting at a perfectly sane 1.0, and the call-site floor cannot see it. That path is
    // if anything wider now: the resolve receives the SUM of every world-space pass, so a
    // negative contribution from any of them arrives here.
    //
    // Flooring exposure at the call site still earns its place: it catches a negative knob
    // even when the colour is ALSO negative, where flooring only the product would
    // multiply two negatives into a positive and brighten.
    REQUIRE(frag.find("x = max(x, vec3(0.0));") != std::string::npos);
}

TEST_CASE("the resolve takes exposure from its own push constant", "[render]")
{
    // Pinned as the plumbing rather than as the absence of a literal: the guard against a
    // hard-coded `tonemap_aces(hdr * 2.0)` is the full-expression pin above. What this
    // adds is that the value is an INPUT at all.
    //
    // It is a push constant rather than a read of the per-frame lighting UBO deliberately:
    // exposure is a property of the resolve now, and a parameter to draw_hdr_resolve is
    // testable -- render_system_test asserts the active camera's exposure reaches it --
    // where a UBO field read inside the backend is not.
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    REQUIRE(frag.find("layout(push_constant)") != std::string::npos);
    REQUIRE(frag.find("float exposure;") != std::string::npos);
}

TEST_CASE("the resolve samples the scene in framebuffer space", "[render]")
{
    // The Y-flip trap, and the reason hdr_resolve.vert exists instead of a reuse of
    // screen_effect.vert.
    //
    // Every render pass in the Vulkan backend sets a NEGATIVE-HEIGHT viewport, so a
    // fullscreen triangle that derives its UV from the clip position inherits that flip
    // and samples the scene UPSIDE DOWN. screen_effect.vert does exactly that, and nobody
    // noticed for two slices, because a vignette is radially symmetric. A photograph of
    // the scene is not.
    //
    // gl_FragCoord is framebuffer space, so taking the UV from it is correct by
    // construction. No headless test can see an upside-down image; this pins the
    // construction that makes it impossible instead.
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("hdr_resolve.frag")));
    const std::string vert =
        mosaic::tests::normalize_ws(code_only(read_shader("hdr_resolve.vert")));
    INFO("The resolve must derive its UV from gl_FragCoord, not from an interpolated clip "
         "position -- the viewport is Y-flipped and an interpolated UV inherits that.");
    REQUIRE(frag.find("gl_FragCoord.xy / vec2(textureSize(hdrScene, 0))") != std::string::npos);
    // ...and the vertex shader must not offer one, so the fragment shader cannot drift
    // back to it. No `out` at all is the enforceable form of that.
    REQUIRE(vert.find("out vec2") == std::string::npos);
}
