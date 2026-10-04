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

#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace
{
std::string read_shader(const std::string& name)
{
    const std::string path = std::string(MOSAIC_TEST_DIR) + "/../modules/render_vulkan/shaders/" + name;
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("cube.frag clamps roughness because the engine deliberately does not", "[render]")
{
    // Review Focus 1. Same contract as the shininess clamp this replaces: Material
    // round-trips byte-for-byte, so a roughness of 0 authored in a scene file reaches
    // the GPU unchanged. alpha = roughness * roughness, and alpha == 0 makes the GGX
    // denominator (NdotH*NdotH*(alpha2-1) + 1) zero at NdotH == 1 -- so D is inf and
    // the highlight centre blows to white. Nothing else pins this: the C++ cannot see
    // shader source, and inf is not a crash.
    const std::string frag = read_shader("cube.frag");
    INFO("If this fails, either restore the roughness clamp in cube.frag or move it "
         "into the engine AND relax the tests that guarantee an authored roughness "
         "reaches the GPU unchanged. The two halves must not disagree.");
    REQUIRE(frag.find("max(roughness, 0.045)") != std::string::npos);
}

TEST_CASE("cube.frag guards the specular denominator against grazing angles", "[render]")
{
    // Review Focus 2: NdotL and NdotV appear in 4 * NdotL * NdotV. At a silhouette
    // edge NdotV approaches zero, which happens on every frame of every scene, so this
    // is the ordinary case rather than an edge case.
    const std::string frag = read_shader("cube.frag");
    REQUIRE(frag.find("max(dot(n, v), 1e-4)") != std::string::npos);
    REQUIRE(frag.find("max(dot(n, L), 1e-4)") != std::string::npos);
}

TEST_CASE("cube.frag gives a fully metallic surface no diffuse", "[render]")
{
    // Review Focus 3, and the single assertion that separates "metal" from "shiny
    // plastic". kD = (1 - F) * (1 - metallic): drop the (1 - metallic) factor and every
    // metal gains a diffuse lobe, which is the exact failure this slice exists to
    // prevent. It is one token wide.
    const std::string frag = read_shader("cube.frag");
    REQUIRE(frag.find("(1.0 - metallic)") != std::string::npos);
    // And the dielectric F0 constant, which is physical rather than tunable.
    REQUIRE(frag.find("mix(vec3(0.04), albedo, metallic)") != std::string::npos);
}

TEST_CASE("cube.vert and cube.frag declare byte-identical FrameLighting blocks", "[render]")
{
    // The UBO block is bound to both stages, so the two declarations must agree
    // exactly. A mismatch does not crash -- it renders subtly wrong, which is the
    // failure mode the spec named as this slice's main risk.
    auto extract_block = [](const std::string& src) {
        const std::size_t begin = src.find("uniform FrameLighting {");
        REQUIRE(begin != std::string::npos);
        const std::size_t end = src.find("} fl;", begin);
        REQUIRE(end != std::string::npos);
        return src.substr(begin, end - begin);
    };
    REQUIRE(extract_block(read_shader("cube.vert")) == extract_block(read_shader("cube.frag")));
}
