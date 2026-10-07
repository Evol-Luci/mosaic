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
    // And the split-sum reconstruction itself. The single-scatter form
    // `prefiltered * (F0_amb * ab.x + ab.y)` this used to pin is now the FssEss term of
    // the multi-scatter reconstruction; the whole expression is pinned in its own case
    // below, because the GROUPING is what matters and a substring cannot see it.
    //
    // No type prefix in the search: pinning `vec3  FssEss` would pin the declaration's
    // alignment, which a reformat breaks while the math is untouched. That the local is
    // a vec3 is the compiler's job, not this test's.
    REQUIRE(frag.find("FssEss = F0_amb * ab.x + ab.y;") != std::string::npos);
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

TEST_CASE("cube.frag groups the multi-scatter term with the irradiance not the radiance",
          "[render]")
{
    // The assertion of the multi-scatter slice, and the only one that pins the GLSL
    // rather than its C++ twin.
    //
    // FmsEms is the energy that bounced between microfacets before escaping. Many
    // bounces randomize direction, so it is diffuse-like and belongs against the
    // irradiance, grouped with kD. Moving it into the FssEss group makes the
    // multi-scatter energy mirror the environment in every real scene.
    //
    // The C++ furnace tests DO catch that mutation -- three of them -- but only because
    // they assert the two weights SEPARATELY. An assertion on their sum cannot see it at
    // all: a uniform environment makes the irradiance and prefiltered maps identical by
    // construction, so the regrouping is invisible to the total. That distinction is why
    // this case pins the whole expression rather than any substring of it; a search for
    // "FmsEms" alone is satisfied by the broken form.
    const std::string frag = normalize_ws(code_only(read_shader("cube.frag")));
    INFO("If this fails, check the GROUPING before assuming the test is stale: "
         "FssEss multiplies prefiltered, and (FmsEms + kD) multiplies irradiance.");
    REQUIRE(frag.find("ambient = FssEss * prefiltered + (FmsEms + kD) * irradiance;")
            != std::string::npos);

    // And the statement that COMPUTES FmsEms -- the one line that is the multiple-
    // scattering series sum, and the one this case originally omitted.
    //
    // Found by the whole-branch review, and the omission is instructive: the five GLSL
    // mutation probes for this slice were derived FROM the assertion list above, so they
    // were structurally incapable of finding a statement nothing asserted. All five came
    // back red and the hole stayed invisible.
    //
    // `vec3 FmsEms = vec3(0.0);` keeps the identifier declared, leaves both pinned
    // expressions textually intact, and makes Favg and Ems unused locals -- which glslc
    // accepts without a diagnostic. It also disables the ENTIRE correction in the
    // shipped renderer, returning rough dielectrics to 0.31 at roughness 1, with every
    // test green. energy_compensate has no production caller, so this file is the only
    // thing standing behind the shader.
    REQUIRE(frag.find("FmsEms = Ems * FssEss * Favg / (vec3(1.0) - Favg * Ems);")
            != std::string::npos);

    // The average-Fresnel fit, whose divisor the furnace test cannot see -- at F0 = 1,
    // Favg is 1 whatever it is, and the total cancels to exactly 1 regardless.
    // ibl_bake_test's isolation case pins the value; this pins that the shader uses the
    // same one.
    REQUIRE(frag.find("Favg = F0_amb + (vec3(1.0) - F0_amb) / 21.0;") != std::string::npos);

    // Ems is built from the LUT's OWN two channels. A constant here -- the mutation that
    // slipped past four token searches in PBR slice 3 -- silently discards the whole
    // correction while every identifier above stays present and spelled correctly.
    REQUIRE(frag.find("Ems = 1.0 - (ab.x + ab.y);") != std::string::npos);

    // And kD comes from the metallic-weighted colour, not from raw albedo. This is the
    // one guard for a DARK conductor: the energy residual zeroes kD on its own only
    // where albedo is 1, so a metal with albedo 0.5 would grow a diffuse lobe. Both
    // lines are required, and the second is what ties them together -- a correctly
    // spelled diffuseColor sitting unused beside `kD = albedo * (...)` is the
    // "clamped-but-unused local" shape the slice-4c review found.
    REQUIRE(frag.find("diffuseColor = albedo * (1.0 - metallic);") != std::string::npos);
    REQUIRE(frag.find("kD = diffuseColor * (vec3(1.0) - FssEss - FmsEms);")
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

// No comma in this name -- see the note on the exposure case in
// shader_tonemap_contract_test.cpp.
TEST_CASE("the FrameLighting block member ORDER is pinned and not just its size", "[render]")
{
    // The gap the exposure slice found in the existing guards, stated plainly because it
    // is only half closable from here.
    //
    // The C++ side asserts `sizeof(FrameLightingUbo) == 208` -- the TOTAL SIZE, and
    // nothing about field offsets. So moving a member from the end of that struct into
    // the middle, without making the matching move in the GLSL, keeps the assertion green
    // and silently rebinds every member after the insertion point. With exposure that
    // would mean reading part of light_view_proj as the exposure scalar: a wrong shadow
    // matrix and a wrong brightness, from a change that compiles and whose size check
    // passes.
    //
    // THE HONEST LIMIT: this case pins the GLSL order only. No test here can read the C++
    // struct's layout -- it is a file-local type in a .cpp in the Vulkan backend, not a
    // header anything can include. What this buys is that the GLSL order cannot change
    // without someone editing this list, which makes a one-sided reorder a visible
    // decision rather than a silent one. Closing it properly needs the UBO layout
    // expressed once and generated into both, which is its own piece of work.
    const std::string frag = normalize_ws(code_only(read_shader("cube.frag")));
    const std::size_t begin = frag.find("uniform FrameLighting {");
    REQUIRE(begin != std::string::npos);
    const std::size_t end = frag.find("} fl;", begin);
    REQUIRE(end != std::string::npos);
    const std::string block = frag.substr(begin, end - begin);

    // The exact sequence, in order. std140 puts each vec4 on a 16-byte boundary and the
    // mat4 on the same, so this ordering IS the byte layout.
    REQUIRE(block == "uniform FrameLighting { vec4 light_direction; vec4 light_color; "
                     "vec4 point_position; vec4 point_color; vec4 spot_position; "
                     "vec4 spot_direction; vec4 spot_color; vec4 camera_position; "
                     "mat4 light_view_proj; vec4 exposure; ");
}
