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

TEST_CASE("the bloom descriptors are rewritten on resize", "[render][bloom]")
{
    // The trap, and it is exactly the defect PR #256 shipped: recreate_swapchain rebuilt
    // the HDR target and left the resolve's descriptor pointing at the freed view. The fix
    // there -- write_hdr_descriptor, called from recreate_swapchain -- is the pattern, and
    // bloom has N+1 sets rather than one, so there are N+1 chances to forget.
    const std::string rec = method_body(read_backend(), "void recreate_swapchain()");
    INFO("recreate_swapchain must call write_bloom_descriptors() after rebuilding the "
         "chain -- every set points at a view destroy_bloom_chain just freed.");
    REQUIRE(rec.find("write_bloom_descriptors()") != std::string::npos);
    // And after, not before: the views it writes do not exist until create_bloom_chain ran.
    REQUIRE(rec.find("create_bloom_chain(") < rec.find("write_bloom_descriptors()"));
}

TEST_CASE("there is one bloom descriptor set per distinct source", "[render][bloom]")
{
    // N + 1 sets: the HDR view, plus EVERY mip 0..N-1. Not N -- the downsample reads mips
    // 0..N-2 and the upsample reads mips 1..N-1, so between them every level is a source at
    // some point, including the smallest, which nothing downsamples FROM.
    //
    // Allocated for the MAXIMUM chain, so a resize that grows the level count never has to
    // reallocate the pool.
    const std::string body = method_body(read_backend(), "void create_bloom_pipelines()");
    INFO("The bloom descriptor pool must be sized for kBloomMaxLevels + 1 sets.");
    REQUIRE(body.find("kBloomMaxLevels + 1") != std::string::npos);
    // Its OWN pool, not descriptor_pool_, whose maxSets is budgeted exactly for textures,
    // materials and IBL -- borrowing there would starve the last texture at some scene size
    // rather than failing here.
    REQUIRE(body.find("bloom_descriptor_pool_") != std::string::npos);
}

TEST_CASE("the bloom upsample pipeline blends additively", "[render][bloom]")
{
    // The one difference between the downsample and the upsample, since they share a
    // shader. Without (ONE, ONE) the upsample REPLACES the level it targets instead of
    // adding to it, and the finished glow is only the smallest mip scaled up -- a soft,
    // uniform wash with none of the chain's detail. It looks deliberate, which is what
    // makes it hard to spot.
    const std::string body = method_body(read_backend(), "void create_bloom_pipelines()");
    REQUIRE(body.find("VK_BLEND_FACTOR_ONE") != std::string::npos);
    REQUIRE(body.find("bloom_up_pipeline_") != std::string::npos);
    REQUIRE(body.find("bloom_down_pipeline_") != std::string::npos);
    REQUIRE(body.find("bloom_prefilter_pipeline_") != std::string::npos);
}

TEST_CASE("build_bloom runs outside every render pass", "[render][bloom]")
{
    // It opens bloom_levels_ render passes of its own, and vkCmdBeginRenderPass inside a
    // pass is a validation error. RenderSystem calls it between end_hdr_scene_pass and
    // draw_hdr_resolve, a window where nothing is recording -- but the viewport pass is
    // LAZILY opened, so "nothing is recording" is a property of today's call order rather
    // than of the API, and cheap to guarantee here.
    const std::string body = method_body(read_backend(), "void build_bloom(");
    INFO("build_bloom must bail out if any pass is already recording.");
    REQUIRE(body.find("in_hdr_pass_") != std::string::npos);
    REQUIRE(body.find("swapchain_pass_open_") != std::string::npos);
    REQUIRE(body.find("viewport_pass_open_") != std::string::npos);
    REQUIRE(body.find("frame_skipped_") != std::string::npos);
}

TEST_CASE("build_bloom's loops are written so a one-level chain is a no-op",
          "[render][bloom]")
{
    // At N = 1 the chain is prefilter only and both loops run N-1 = 0 times. bloom_levels_
    // is UNSIGNED, so `for (i = bloom_levels_ - 2; i >= 0; --i)` would not terminate -- it
    // would wrap to 4294967294 and index past every vector in the class. Written with
    // `i + 1 < bloom_levels_` ascending and `i-- > 0` descending, both correct at 1 without
    // a special case.
    const std::string body = normalize_ws(method_body(read_backend(), "void build_bloom("));
    INFO("Neither loop may compute `bloom_levels_ - 2` as an unsigned loop bound.");
    REQUIRE(body.find("bloom_levels_ - 2") == std::string::npos);
    REQUIRE(body.find("i + 1 < bloom_levels_") != std::string::npos);
    REQUIRE(body.find("i-- > 0") != std::string::npos);
}

TEST_CASE("the resolve binds a set to slot 1 even with no bloom chain", "[render][bloom]")
{
    // A minimised window gives bloom_levels_ == 0 and no chain at all, but hdr_resolve.frag
    // still declares set 1 and samples it -- an unbound set is a validation error, and on
    // some drivers a crash. The HDR set is bound there as a stand-in; the intensity is 0 on
    // that path anyway, so what it reads is multiplied away.
    const std::string body = method_body(read_backend(), "void draw_hdr_resolve(");
    INFO("draw_hdr_resolve must bind two descriptor sets, falling back to hdr_set_ for "
         "slot 1 when the chain does not exist.");
    REQUIRE(body.find("bloom_sets_.empty()") != std::string::npos);
    REQUIRE(body.find("0, 2, sets") != std::string::npos);
}

TEST_CASE("the resolve's push block matches hdr_resolve.frag", "[render][bloom]")
{
    // The two layouts are shared byte for byte, and a mismatch is silent: the shader reads
    // whatever bytes land at its offsets, so a wrong order shows up as an exposure that
    // behaves like an intensity. vec2 first because it has the larger alignment.
    const std::string src = read_backend();
    const std::size_t at  = src.find("struct ResolvePush");
    REQUIRE(at != std::string::npos);
    const std::string decl = normalize_ws(src.substr(at, 220));
    REQUIRE(decl.find("float inv_dest[2]; float exposure; float bloom_intensity;")
            != std::string::npos);
}

TEST_CASE("the bloom chain is created after the command pool", "[render][bloom]")
{
    // FOUND BY A REAL-DEVICE RUN, not by this suite: create_bloom_chain clears the image
    // through begin_single_time_commands, which needs the command pool. Built before the
    // pool exists it segfaults with "commandPool must be a valid VkCommandPool handle" --
    // and the constructor ALREADY carries that exact lesson for
    // create_default_ibl_resources, which is the comment this ordering now sits beside.
    //
    // Nothing headless can see it: no test creates a VkDevice, so the constructor never
    // runs here. Pinned as text instead, because the next person to add a swapchain-sized
    // resource will reach for the create_hdr_target line as the template and that line is
    // ABOVE the pool.
    const std::string src  = read_backend();
    const std::size_t pool = src.find("create_command_pool_and_buffers();");
    const std::size_t make = src.find("create_bloom_chain(extent_.width, extent_.height);");
    REQUIRE(pool != std::string::npos);
    REQUIRE(make != std::string::npos);
    INFO("create_bloom_chain must appear AFTER create_command_pool_and_buffers in the "
         "constructor -- it submits a one-time command buffer to clear the chain.");
    REQUIRE(make > pool);
}

TEST_CASE("the bloom descriptors are written after the chain exists", "[render][bloom]")
{
    // The consequence of the ordering above. create_bloom_pipelines allocates the N+1 sets
    // and writes whatever views exist AT THAT MOMENT -- which, with the chain built later,
    // is only the HDR view. Without a second write the per-mip sets stay empty and every
    // bloom pass samples an unwritten descriptor.
    //
    // Same shape as write_hdr_descriptor: harmless before the resource exists, called again
    // once it does.
    // Scoped to the CONSTRUCTOR, between the chain's creation and the constructor's last
    // statement. An unscoped search finds the call in recreate_swapchain and passes while
    // the startup path is still broken -- which is exactly what it did when first written.
    const std::string src  = read_backend();
    const std::size_t make = src.find("create_bloom_chain(extent_.width, extent_.height);");
    REQUIRE(make != std::string::npos);
    const std::size_t ctor_end = src.find("glfwSetWindowUserPointer(window_, this);", make);
    REQUIRE(ctor_end != std::string::npos);
    const std::size_t write = src.find("write_bloom_descriptors();", make);
    INFO("A write_bloom_descriptors() call must follow create_bloom_chain inside the "
         "constructor, or the per-mip sets are never pointed at the mip views.");
    REQUIRE(write != std::string::npos);
    REQUIRE(write < ctor_end);
}
