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

#include "engine/render/environment.hpp"
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

TEST_CASE("cube.frag samples the three IBL products from set 3", "[render]")
{
    // The lit pass reads irradiance, prefiltered specular and the BRDF LUT from their own
    // descriptor set -- see the backend's ibl_set_layout_ for why it is a separate set
    // rather than more bindings on the per-frame one.
    // normalize_ws so column alignment in the declarations is not a contract. The
    // backend contracts already do this; a test that breaks when someone lines up a
    // column is a test that will be deleted rather than fixed.
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("cube.frag")));
    REQUIRE(frag.find("set = 3, binding = 0") != std::string::npos);
    REQUIRE(frag.find("set = 3, binding = 1") != std::string::npos);
    REQUIRE(frag.find("set = 3, binding = 2") != std::string::npos);
    REQUIRE(frag.find("samplerCube irradianceMap") != std::string::npos);
    REQUIRE(frag.find("samplerCube prefilteredMap") != std::string::npos);
    REQUIRE(frag.find("sampler2D brdfLut") != std::string::npos);
}

TEST_CASE("cube.frag indexes the prefiltered chain with roughness times four", "[render]")
{
    // Review Focus 4. FIVE prefiltered levels hold roughness 0, .25, .5, .75, 1, so the
    // LOD is roughness * 4 and textureLod interpolates between them. A * 5.0, or a chain
    // built with a different level count, produces a material that merely looks too shiny
    // or too blurry -- which reads as an authoring choice rather than as a bug, and no
    // GPU in CI can see it.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("textureLod(prefilteredMap, R, roughness * 4.0)") != std::string::npos);
    // And the split-sum reconstruction itself...
    REQUIRE(frag.find("prefiltered * (F0_amb * ab.x + ab.y)") != std::string::npos);
    // ...and that `ab` actually comes FROM the LUT. Pinning only the reconstruction left
    // a mutation replacing the lookup with a constant vec2(1.0, 0.0) passing, because the
    // reconstruction reads the same either way -- and a constant there silently discards
    // the whole energy correction the LUT exists to apply.
    //
    // textureLod with an explicit 0, not texture(): the LUT's coordinates are TABLE
    // coordinates, so an implicit LOD from their screen-space derivatives is meaningless
    // and put a visible seam down every roughness-band boundary on the demo sphere.
    REQUIRE(frag.find("textureLod(brdfLut, vec2(max(dot(n, v), 1e-4), roughness), 0.0).rg")
            != std::string::npos);
}

TEST_CASE("cube.frag keeps the flat ambient as its no-environment fallback", "[render]")
{
    // Review Focus 3. Deleting this branch breaks every scene without an environment --
    // and nothing else would notice, because the demo always has one. It is not dead
    // code; it is the other half of a runtime choice.
    const std::string frag = code_only(read_shader("cube.frag"));
    REQUIRE(frag.find("albedo * 0.1 * kD_amb") != std::string::npos);
    // The flag that chooses between them rides in camera_position.w, which the UBO
    // documents as unused padding.
    REQUIRE(frag.find("fl.camera_position.w > 0.5") != std::string::npos);
}

TEST_CASE("the prefiltered LOD multiplier matches the level count the bakers use", "[render]")
{
    // The gap the slice-4c review found. Both bake sites pass kPrefilteredLevels, and
    // cube.frag indexes the chain with a literal multiplier -- but nothing connected the
    // two, so changing production to six levels shipped entirely green and made every
    // specular reflection one level too sharp.
    //
    // The multiplier must be levels - 1, because textureLod interpolates ACROSS the
    // chain: roughness 1 must land on the last level, not past it.
    static_assert(mosaic::engine::render::kPrefilteredLevels == 5,
                  "update cube.frag's roughness multiplier with this constant");
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("cube.frag")));
    const std::string expected =
        "roughness * " + std::to_string(mosaic::engine::render::kPrefilteredLevels - 1) + ".0";
    INFO("expecting the LOD to be " << expected);
    REQUIRE(frag.find(expected) != std::string::npos);
}

TEST_CASE("cube.frag reflects the view vector about the normal for the specular lookup", "[render]")
{
    // The other hole the review found, and the same shape as the CPU-side one already
    // fixed: every assertion pinned what happens to the sampled value, and none pinned
    // where the sample direction comes from.
    //
    // R = n makes the sphere reflect whatever is directly above each point; reflect(v, n)
    // points into the geometry. Both leave every other test in this file green, and both
    // look like a strange material rather than a bug.
    const std::string frag =
        mosaic::tests::normalize_ws(code_only(read_shader("cube.frag")));
    REQUIRE(frag.find("vec3 R = reflect(-v, n);") != std::string::npos);
}
