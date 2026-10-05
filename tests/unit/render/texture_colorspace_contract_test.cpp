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
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/render_device.hpp"
#include "shader_source.hpp"

using mosaic::engine::render::TextureColorSpace;
using mosaic::tests::normalize_ws;

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

namespace
{
// Records what colour space each create_texture call actually received, so the DEFAULT
// ARGUMENT can be observed rather than assumed.
class SpaceRecordingDevice : public mosaic::engine::render::RenderDevice
{
public:
    std::vector<TextureColorSpace> seen;

    void begin_frame() override {}
    void clear(const mosaic::engine::render::Color&) override {}
    void end_frame() override {}
    mosaic::engine::render::MeshHandle create_mesh(
        const std::vector<mosaic::engine::render::Vertex>&,
        const std::vector<std::uint32_t>&) override
    {
        return 1;
    }
    mosaic::engine::render::TextureHandle create_texture(
        const mosaic::engine::render::Image&, TextureColorSpace space) override
    {
        seen.push_back(space);
        return 1;
    }
    void draw(mosaic::engine::render::MeshHandle, mosaic::engine::render::TextureHandle,
              const mosaic::engine::render::DrawParams&) override
    {
    }
};
} // namespace

TEST_CASE("create_texture's default argument is Srgb", "[render]")
{
    // The default is load-bearing: the font atlas, the default white, UI images and
    // decals all rely on it, and flipping it to Linear would re-colour every one of
    // them at once.
    //
    // The first version of this case asserted TextureColorSpace{} == Srgb, which only
    // tests which enumerator is written first -- changing the DEFAULT ARGUMENT on
    // render_device.hpp left it green. Call the function with one argument and look at
    // what it received, which is the thing that actually matters. Found in review
    // 2026-10-04.
    SpaceRecordingDevice device;
    mosaic::engine::render::Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {255, 255, 255, 255};
    // Through a RenderDevice& ON PURPOSE. Default arguments are not inherited, so the
    // default that matters is the one on the BASE declaration -- which is also why the
    // overrides must not restate it: whichever default applies would then depend on the
    // static type of the reference you happen to hold.
    mosaic::engine::render::RenderDevice& as_base = device;
    as_base.create_texture(img); // deliberately one argument
    REQUIRE(device.seen.size() == 1);
    REQUIRE(device.seen[0] == TextureColorSpace::Srgb);
}

TEST_CASE("the Vulkan backend maps Linear to UNORM and Srgb to SRGB", "[render]")
{
    // Review Focus 1. Four independent substring searches for UNORM, SRGB,
    // TextureColorSpace::Linear and tex_format were all satisfied by SWAPPING THE
    // TERNARY ARMS -- which ships every roughness map as sRGB and every base colour as
    // UNORM, precisely the silent failure this file exists to prevent. Pin the
    // ASSOCIATION, not the tokens. Found in review 2026-10-04.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    INFO("A metallic/roughness map uploaded as R8G8B8A8_SRGB has its values distorted "
         "by the sRGB EOTF: an authored 0.5 arrives as ~0.21, and the image merely "
         "looks like a different material.");
    REQUIRE(src.find("TextureColorSpace::Linear) ? VK_FORMAT_R8G8B8A8_UNORM "
                     ": VK_FORMAT_R8G8B8A8_SRGB")
            != std::string::npos);
    // ...and that the single computed format is what both the image and its view use,
    // since two independent ternaries is how they come to disagree.
    REQUIRE(src.find("img.format = tex_format;") != std::string::npos);
    REQUIRE(src.find("view.format = tex_format;") != std::string::npos);
}

TEST_CASE("the descriptor pool is sized for both kinds of set", "[render]")
{
    // Plan Review Focus 5 had no test at all: the pool feeds kMaxTextures
    // single-binding sets AND kMaxMaterialSets two-binding ones, and bumping either cap
    // without bumping descriptorCount ships green and fails at runtime, mid-frame, with
    // VK_ERROR_OUT_OF_POOL_MEMORY. The arithmetic is source-checkable the same way the
    // formats are. Found in review 2026-10-04.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("pool_size.descriptorCount = kMaxTextures + kMaxMaterialSets * 2;")
            != std::string::npos);
    REQUIRE(src.find("pool.maxSets = kMaxTextures + kMaxMaterialSets;") != std::string::npos);
    // And that create_texture is bounded, so exceeding the texture cap fails at upload
    // time with a message about textures, rather than later as a material-set
    // allocation failure that blames the wrong thing.
    REQUIRE(src.find("textures_.size() >= kMaxTextures") != std::string::npos);
}

TEST_CASE("the backend generates a full mip chain and samples it", "[render]")
{
    // Three things must agree or minification is broken, and dropping any ONE of them
    // leaves the other two compiling cleanly:
    //   - TRANSFER_SRC usage, because each level is the blit SOURCE for the next;
    //   - the blit loop that actually fills levels 1..n-1;
    //   - the sampler's maxLod, without which the chain exists but is never sampled.
    // The failure is a black or undefined minified texture, not an error, and there is
    // no GPU in CI -- so pin the source.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("VK_IMAGE_USAGE_TRANSFER_SRC_BIT") != std::string::npos);
    REQUIRE(src.find("vkCmdBlitImage") != std::string::npos);
    REQUIRE(src.find("img.mipLevels = levels;") != std::string::npos);
    // "all levels", however spelled -- but emphatically not 0, which is the regression.
    REQUIRE(src.find("sampler.maxLod = VK_LOD_CLAMP_NONE;") != std::string::npos);
    // The LEADING SPACE is load-bearing. Without it this also matched
    // `cube_sampler.maxLod = 0.0f;`, which is correct -- a cubemap has one level -- so
    // the guard failed on correct code. normalize_ws puts exactly one space between
    // tokens, and `cube_sampler` is preceded by `_`, so the space pins the 2D sampler
    // alone. Found while adding create_cubemap, 2026-10-05.
    REQUIRE(src.find(" sampler.maxLod = 0.0f;") == std::string::npos);
    // The two single-token edits that break the LAYOUT LIFECYCLE while every other
    // assertion here still passes, and which no test held: the initial transition must
    // cover the whole chain (otherwise levels 1..n-1 are blitted while in UNDEFINED),
    // and the final barrier must retire the LAST level (otherwise level n-1 is never
    // transitioned and levels 0..n-2 are transitioned twice). Both produce undefined
    // sampling plus a validation error, on a GPU, which CI does not have.
    // Review 2026-10-05.
    REQUIRE(src.find("to_transfer.subresourceRange.levelCount = levels;") != std::string::npos);
    REQUIRE(src.find("to_read.subresourceRange.baseMipLevel = levels - 1;") != std::string::npos);
    // Linear-filter blits need the format to advertise the feature. It is near-universal
    // for R8G8B8A8 but it is a device query, not a guarantee, and skipping the check
    // produces undefined results rather than a validation error.
    // All THREE features the blit requires. Pinning only the filter bit left the gate's
    // own regression -- reverting it to one feature -- passing: the filter bit is still
    // present either way. My own probe caught that after adding the fix.
    // Review 2026-10-05.
    REQUIRE(src.find("VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT "
                     "| VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT")
            != std::string::npos);
    // ...and that the test is a SUBSET check, not an equality one: == kBlitFeatures is
    // the difference between "has all of them" and "has any of them".
    REQUIRE(src.find("& kBlitFeatures) == kBlitFeatures") != std::string::npos);
}

TEST_CASE("the backend creates cubemaps with the cube flags and view type", "[render]")
{
    // Review Focus 4. A cubemap shares TextureHandle and the texture table with 2D
    // images, so the only things distinguishing it are the create flag, the layer count
    // and the view type -- and all three must agree. Two out of three is a validation
    // error at best and a wrong binding at worst, and CI has no GPU to catch either.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT") != std::string::npos);
    REQUIRE(src.find("VK_IMAGE_VIEW_TYPE_CUBE") != std::string::npos);
    REQUIRE(src.find("cube.arrayLayers = 6;") != std::string::npos);
    REQUIRE(src.find("cube_view.subresourceRange.layerCount = 6;") != std::string::npos);
    // All six faces must be transitioned together, or whichever layers were missed are
    // sampled in the wrong layout -- undefined contents plus a validation error.
    REQUIRE(src.find("cube_barrier.subresourceRange.layerCount = 6;") != std::string::npos);
    // Cube sampling must clamp on every axis, or the faces show seams at their edges.
    REQUIRE(src.find("cube_sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;")
            != std::string::npos);
}
