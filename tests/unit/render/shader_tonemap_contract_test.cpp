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
