// Pins the one decision in PBR slice 3 that fails silently.
//
// create_texture uploaded every texture as R8G8B8A8_SRGB. That is correct for base
// colour -- the hardware applies the sRGB EOTF on sample, so the shader receives linear
// values, which is what the BRDF needs. A metallic/roughness map is DATA, not colour:
// uploaded as sRGB, an authored roughness of 0.5 (byte 128) arrives as roughly 0.21.
// Nothing crashes, no validation layer complains, and the result looks like a plausible
// material -- just the wrong one.
//
// There is no GPU in CI, so the backend half is a source contract, the same idiom as
// shader_specular_contract_test.cpp.

#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/render_device.hpp"
#include "shader_source.hpp"

using mosaic::engine::render::TextureColorSpace;

namespace
{
std::string read_backend()
{
    const std::string path =
        std::string(MOSAIC_TEST_DIR) + "/../modules/render_vulkan/vulkan_render_device.cpp";
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

TEST_CASE("the colour space default keeps every existing call site on sRGB", "[render]")
{
    // The default is load-bearing: the font atlas, the default white, and every
    // base-colour texture rely on it. A value-initialised TextureColorSpace must be
    // Srgb, or adding the parameter silently re-colours all of them.
    REQUIRE(TextureColorSpace{} == TextureColorSpace::Srgb);
}

TEST_CASE("the Vulkan backend selects UNORM for data and SRGB for colour", "[render]")
{
    // Review Focus 1. Both formats must be present AND chosen from the parameter
    // rather than hardcoded. If this fails after a refactor, confirm a data map still
    // reaches the GPU as UNORM before relaxing it -- the whole point is that nothing
    // downstream can tell you when it does not.
    const std::string src = mosaic::tests::code_only(read_backend());
    INFO("A metallic/roughness map uploaded as R8G8B8A8_SRGB has its values distorted "
         "by the sRGB EOTF: an authored 0.5 arrives as ~0.21, and the image merely "
         "looks like a different material.");
    REQUIRE(src.find("VK_FORMAT_R8G8B8A8_UNORM") != std::string::npos);
    REQUIRE(src.find("VK_FORMAT_R8G8B8A8_SRGB") != std::string::npos);
    REQUIRE(src.find("TextureColorSpace::Linear") != std::string::npos);
    // The image and its view must agree. Two independent ternaries is how they come
    // to disagree, so pin that the format is computed once into a named local.
    REQUIRE(src.find("tex_format") != std::string::npos);
}
