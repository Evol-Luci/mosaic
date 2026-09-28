// Pins the shader half of a contract whose engine half is aggressively tested.
//
// Material::shininess is deliberately NOT clamped by the engine: three tests
// guarantee that a zero or negative exponent authored in a scene file reaches the
// GPU unmodified, so a scene round-trips byte-for-byte. That decision is only safe
// because cube.frag clamps the exponent to >= 1 before pow(). Nothing else pins
// that clamp -- the C++ static_asserts cannot see shader source -- so dropping it
// would make pow(x, -4.0) evaluate to +inf near the terminator and blow the
// highlight to white, with the whole suite still green and zero Vulkan validation
// errors. This test makes that regression loud instead of invisible.

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

TEST_CASE("cube.frag clamps the specular exponent because the engine deliberately does not", "[render]")
{
    const std::string frag = read_shader("cube.frag");
    INFO("If this fails, either restore the clamp in blinn_spec() or move the clamp "
         "into the engine AND relax the tests that guarantee a negative shininess "
         "reaches the GPU unchanged. The two halves must not disagree.");
    REQUIRE(frag.find("max(shininess, 1.0)") != std::string::npos);
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
