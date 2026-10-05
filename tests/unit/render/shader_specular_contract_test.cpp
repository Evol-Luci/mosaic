// Pins the shader half of contracts whose engine half is aggressively tested.
//
// Material does not clamp its shading parameters: tests guarantee that whatever a
// scene file authors reaches the GPU unmodified, so a scene round-trips byte-for-byte.
// That decision is only safe because cube.frag clamps at shading time. Nothing else
// pins those clamps -- the C++ static_asserts cannot see shader source -- and every
// one of them fails by producing inf or a divide-by-zero rather than a crash, so the
// whole suite stays green and Vulkan reports no validation errors while the image is
// wrong. These tests make that class of regression loud instead of invisible.
//
// Previously this file pinned max(shininess, 1.0) for the Blinn-Phong exponent. That
// parameter no longer exists; the clamps below are its metallic/roughness successors.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::code_only;
using mosaic::tests::normalize_ws;
using mosaic::tests::read_shader;

TEST_CASE("cube.frag clamps roughness because the engine deliberately does not", "[render]")
{
    // Review Focus 1. Same contract as the shininess clamp this replaces: Material
    // round-trips byte-for-byte, so a roughness of 0 authored in a scene file reaches
    // the GPU unchanged. alpha = roughness * roughness, and alpha == 0 makes the GGX
    // denominator (NdotH*NdotH*(alpha2-1) + 1) zero at NdotH == 1 -- so D is inf and
    // the highlight centre blows to white. Nothing else pins this: the C++ cannot see
    // shader source, and inf is not a crash.
    const std::string frag = code_only(read_shader("cube.frag"));
    INFO("If this fails, either restore the roughness clamp in cube.frag or move it "
         "into the engine AND relax the tests that guarantee an authored roughness "
         "reaches the GPU unchanged. The two halves must not disagree.");
    REQUIRE(frag.find("alpha = clamp(roughness, 0.045, 1.0)") != std::string::npos);
    // ...and that it is the CLAMPED value that gets squared. Nothing tied the two
    // together: `alpha = roughness * roughness` below a clamped-but-unused local would
    // have passed. Review 2026-10-04.
    REQUIRE(frag.find("alpha = alpha * alpha") != std::string::npos);
}

TEST_CASE("cube.frag guards the specular denominator against grazing angles", "[render]")
{
    // Review Focus 2: NdotL and NdotV appear in 4 * NdotL * NdotV. At a silhouette
    // edge NdotV approaches zero, which happens on every frame of every scene, so this
    // is the ordinary case rather than an edge case.
    //
    // Asserting the two clamps merely APPEAR was not enough, and the case's own title
    // said so: rewriting the specular line as
    //     / max(4.0 * dot(n, v) * dot(n, L), 1e-7)
    // leaves the clamped locals defined and used by G, keeps both strings present, and
    // reinstates the divide-by-~0 at every silhouette edge with this test green. Found
    // in review 2026-10-04. So pin the denominator expression itself -- including its
    // own 1e-7 floor, which was asserted nowhere -- and pin that alpha is squared from
    // the clamped value rather than from raw roughness.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("max(dot(n, v), 1e-4)") != std::string::npos);
    REQUIRE(frag.find("max(dot(n, L), 1e-4)") != std::string::npos);
    REQUIRE(frag.find("max(4.0 * NdotV * NdotL, 1e-7)") != std::string::npos);
}

TEST_CASE("cube.frag gives a fully metallic surface no diffuse", "[render]")
{
    // Review Focus 3, and the single assertion that separates "metal" from "shiny
    // plastic". kD = (1 - F) * (1 - metallic): drop the (1 - metallic) factor and every
    // metal gains a diffuse lobe, which is the exact failure this slice exists to
    // prevent. It is one token wide.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("(1.0 - metallic)") != std::string::npos);
    // And the dielectric F0 constant, which is physical rather than tunable.
    REQUIRE(frag.find("mix(vec3(0.04), albedo, metallic)") != std::string::npos);
}

TEST_CASE("cube.vert and cube.frag declare matching FrameLighting blocks", "[render]")
{
    // The UBO block is bound to both stages, so the two declarations must agree
    // exactly. A mismatch does not crash -- it renders subtly wrong, which is the
    // failure mode the spec named as this slice's main risk.
    auto extract_block = [](const std::string& src) {
        const std::size_t begin = src.find("uniform FrameLighting {");
        REQUIRE(begin != std::string::npos);
        const std::size_t end = src.find("} fl;", begin);
        REQUIRE(end != std::string::npos);
        return normalize_ws(src.substr(begin, end - begin));
    };
    REQUIRE(extract_block(code_only(read_shader("cube.vert"))) == extract_block(code_only(read_shader("cube.frag"))));
}

TEST_CASE("cube.vert and cube.frag declare matching Push blocks", "[render]")
{
    // The FrameLighting case below has guarded the UBO since the lighting slice; the
    // push-constant block had no counterpart, and the two declarations had in fact
    // drifted -- cube.vert still named the fields shininess/specular_strength after
    // this slice renamed them in cube.frag. That particular drift was harmless, because
    // GLSL member names are debug info and the offsets matched, which is exactly why
    // nothing caught it. The next drift need not be harmless: insert a field mid-block
    // in one stage only and every field after it mis-binds, silently, with
    // static_assert(sizeof(PushConstants) == 216) still passing. Found in review
    // 2026-10-04.
    auto extract_block = [](const std::string& src) {
        const std::size_t begin = src.find("uniform Push {");
        REQUIRE(begin != std::string::npos);
        const std::size_t end = src.find("} pc;", begin);
        REQUIRE(end != std::string::npos);
        return normalize_ws(src.substr(begin, end - begin));
    };
    REQUIRE(extract_block(code_only(read_shader("cube.vert")))
            == extract_block(code_only(read_shader("cube.frag"))));
}

TEST_CASE("cube.frag samples metallic/roughness with the glTF channel packing", "[render]")
{
    // Review Focus 2, and the subtlest way this slice fails. G = roughness,
    // B = metallic is a CONVENTION, not a derivation -- there is nothing about the data
    // that makes roughness green. Read them backwards and a rough metal becomes a
    // smooth dielectric with metallic variation: both are materials, neither looks like
    // a bug, and with no GPU in CI nothing else can tell. Every authoring tool exports
    // this packing, so getting it wrong makes every imported asset subtly wrong.
    const std::string frag = code_only(read_shader("cube.frag"));
    // Pin the ASSIGNMENTS, not merely that both swizzles appear somewhere: the whole
    // failure mode is the two being crossed, and both strings survive a swap.
    REQUIRE(frag.find("pc.roughness * mr.g") != std::string::npos);
    REQUIRE(frag.find("pc.metallic * mr.b") != std::string::npos);
}

TEST_CASE("cube.frag declares the metallic/roughness sampler at set 0 binding 1", "[render]")
{
    // The base colour sampler has been set 0 binding 0 since textures existed. A second
    // binding in the SAME set is what lets one bind serve a whole material; moving it to
    // another set would silently cost a descriptor slot per future map type.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("set = 0, binding = 1") != std::string::npos);
}
