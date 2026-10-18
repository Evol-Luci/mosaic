// Textual contract tests for the C++ half of the bloom chain.
//
// Nothing in the suite creates a VkDevice, and the validation layers check API legality
// rather than pixels -- so the gate this project trusts (ctest plus a validation-layer
// grep on both entry points) is silent on most of what follows. These cases pin the
// decisions whose failure mode is a freed handle, a wrong layout, or a leak: things that
// appear on a real device and in no test at all.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::normalize_ws;
using mosaic::tests::read_backend;

namespace
{
// The body of a named method, from its signature to the next method at the same
// indentation. Lets a case assert about ONE method rather than about the whole 4900-line
// file, where a string can be present for an entirely unrelated reason.
std::string method_body(const std::string& src, const std::string& signature)
{
    const std::size_t at = src.find(signature);
    REQUIRE(at != std::string::npos);
    const std::size_t end = src.find("\n    void ", at + signature.size());
    return src.substr(at, (end == std::string::npos ? src.size() : end) - at);
}
} // namespace

TEST_CASE("the bloom chain is created and destroyed with the HDR target", "[render][bloom]")
{
    // The chain is swapchain-derived, exactly as the HDR target is, so it has the same
    // lifecycle: built at startup, rebuilt on every resize. A chain left at the old size
    // would be sampled with the new size's uv, which is a scaled glow -- and in the editor
    // that mismatch is invisible to every check this project runs.
    const std::string rec = method_body(read_backend(), "void recreate_swapchain()");
    INFO("recreate_swapchain must destroy and recreate the bloom chain beside the HDR "
         "target.");
    REQUIRE(rec.find("destroy_bloom_chain()") != std::string::npos);
    REQUIRE(rec.find("create_bloom_chain(") != std::string::npos);
}

TEST_CASE("the bloom chain is cleared and made readable at creation", "[render][bloom]")
{
    // The layout hazard, and it is invisible to the build and to the whole headless suite.
    // hdr_resolve.frag samples the chain UNCONDITIONALLY, so on frame 1 -- and on any
    // frame following a skipped build -- it would otherwise read an image in UNDEFINED
    // layout. That is a validation error on a real device and nothing at all here.
    //
    // CLEARED as well as transitioned, which is not belt-and-braces: freshly allocated
    // device memory can read as NaN, and NaN * 0.0 is NaN, so the whole "intensity 0
    // contributes 0" argument needs the memory to actually be zero.
    const std::string body = method_body(read_backend(), "void create_bloom_chain(");
    INFO("create_bloom_chain must clear the image and leave every mip in "
         "SHADER_READ_ONLY_OPTIMAL.");
    REQUIRE(body.find("vkCmdClearColorImage") != std::string::npos);
    REQUIRE(body.find("VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL") != std::string::npos);
    // Every mip, not just mip 0 -- the bloom pass declares SHADER_READ_ONLY_OPTIMAL as its
    // initialLayout for all of them.
    REQUIRE(body.find("levelCount = bloom_levels_") != std::string::npos);
}

TEST_CASE("the bloom image carries TRANSFER_DST so it can be cleared", "[render][bloom]")
{
    // The usage flag the clear above needs. Omitting it is a validation error at
    // vkCmdClearColorImage, which is to say: at startup, on a real device, and never here.
    const std::string body = method_body(read_backend(), "void create_bloom_chain(");
    REQUIRE(body.find("VK_IMAGE_USAGE_TRANSFER_DST_BIT") != std::string::npos);
    REQUIRE(body.find("VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT") != std::string::npos);
    REQUIRE(body.find("VK_IMAGE_USAGE_SAMPLED_BIT") != std::string::npos);
    REQUIRE(body.find("VK_FORMAT_R16G16B16A16_SFLOAT") != std::string::npos);
}

TEST_CASE("the bloom chain is half the swapchain's resolution", "[render][bloom]")
{
    // Half, and derived rather than passed: create_bloom_chain takes the SWAPCHAIN's
    // extent so that one place owns the halving. Two call sites each halving for
    // themselves is a place for one of them to stop.
    const std::string body = method_body(read_backend(), "void create_bloom_chain(");
    REQUIRE(body.find("bloom_level_count(width, height)") != std::string::npos);
    REQUIRE(body.find("width / 2") != std::string::npos);
    REQUIRE(body.find("height / 2") != std::string::npos);
}

TEST_CASE("the bloom render pass loads and leaves its attachment readable",
          "[render][bloom]")
{
    // LOAD because the upsample ADDS INTO the level it targets -- a CLEAR there would
    // discard the downsampled content it is adding to, and the glow would be only the
    // smallest mip. SHADER_READ_ONLY at both ends because the next step samples what this
    // one wrote, and because the resolve samples mip 0 after the whole chain is done.
    const std::string body = method_body(read_backend(), "void create_bloom_render_pass()");
    INFO("The bloom pass must use LOAD_OP_LOAD with SHADER_READ_ONLY_OPTIMAL as both "
         "initial and final layout.");
    REQUIRE(body.find("VK_ATTACHMENT_LOAD_OP_LOAD") != std::string::npos);
    REQUIRE(body.find("VK_ATTACHMENT_LOAD_OP_CLEAR") == std::string::npos);
    REQUIRE(body.find("color.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL")
            != std::string::npos);
    REQUIRE(body.find("color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL")
            != std::string::npos);
}

TEST_CASE("destroy_bloom_chain releases every per-mip handle", "[render][bloom]")
{
    // One framebuffer and one view PER MIP, because a framebuffer binds a specific view.
    // Destroying only the image would leak up to six of each on every single resize, and a
    // resize is something a user does by dragging a window edge -- so "every resize" means
    // continuously.
    const std::string body = method_body(read_backend(), "void destroy_bloom_chain()");
    REQUIRE(body.find("vkDestroyFramebuffer") != std::string::npos);
    REQUIRE(body.find("vkDestroyImageView") != std::string::npos);
    REQUIRE(body.find("vkDestroyImage(") != std::string::npos);
    REQUIRE(body.find("vkFreeMemory") != std::string::npos);
    // And the vectors are emptied, or the next create appends to them.
    REQUIRE(body.find("bloom_views_.clear()") != std::string::npos);
    REQUIRE(body.find("bloom_fbs_.clear()") != std::string::npos);
    REQUIRE(body.find("bloom_levels_ = 0") != std::string::npos);
}
