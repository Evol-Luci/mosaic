// Textual contract tests for what the validation layers are asked to check.
//
// This file exists because of a specific lesson, recorded in the project's backlog: the
// gate this project trusts is "ctest, plus a validation-layer grep on both entry points",
// and on 2026-10-17 a whole-branch review pointed out that the grep had never been able to
// see a synchronisation error. The layer was enabled; SYNC validation, which is a separate
// opt-in on top of it, was not. Four render slices shipped under a check that was silent
// about every barrier in them.
//
// There is no way to assert this by running Vulkan here -- no test creates a VkDevice. So
// the request itself is pinned as text.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "shader_source.hpp"

using mosaic::tests::read_backend;

namespace
{
// create_instance's body, from its signature to the next method at the same indentation.
std::string instance_body(const std::string& src)
{
    const std::size_t at = src.find("void create_instance()");
    REQUIRE(at != std::string::npos);
    const std::size_t end = src.find("\n    void ", at + 24);
    return src.substr(at, (end == std::string::npos ? src.size() : end) - at);
}
} // namespace

TEST_CASE("the backend can ask for synchronization validation", "[render][validation]")
{
    // The feature the layer does NOT enable on its own. Without this, a wrong barrier, a
    // missing subpass dependency, or an intra-frame read-after-write on one image produces
    // no diagnostic at all -- the run is clean and says nothing.
    //
    // Bloom is what made this matter: it is the first feature in this renderer with a
    // multi-pass intra-frame dependency on a single image (eleven passes, each sampling
    // the mip the last one wrote), and its subpass dependency had to be widened on
    // reasoning alone because nothing automated could confirm it.
    const std::string body = instance_body(read_backend());
    INFO("create_instance must be able to request "
         "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT via "
         "VkValidationFeaturesEXT. Enabling VK_LAYER_KHRONOS_validation alone does NOT "
         "turn synchronisation checking on.");
    REQUIRE(body.find("VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT")
            != std::string::npos);
    REQUIRE(body.find("VkValidationFeaturesEXT") != std::string::npos);
}

TEST_CASE("synchronization validation is opt-in by environment variable",
          "[render][validation]")
{
    // Opt-in rather than always-on, because sync validation is genuinely expensive -- not
    // a free flag. The cost is why it is off by default; the env var is what makes it
    // REACHABLE, which it previously was not at any price.
    const std::string body = instance_body(read_backend());
    REQUIRE(body.find("MOSAIC_VK_SYNC_VALIDATION") != std::string::npos);
}

TEST_CASE("enabling sync validation does not unchain the debug messenger",
          "[render][validation]")
{
    // `ci.pNext` already carries VkDebugUtilsMessengerCreateInfoEXT, and chaining it is
    // what covers instance create/destroy. Adding VkValidationFeaturesEXT means a CHAIN,
    // not a replacement: overwrite ci.pNext and every message the messenger would have
    // reported during instance creation is silently lost -- which would make this change
    // a net reduction in what the gate sees, while appearing to add to it.
    const std::string body = instance_body(read_backend());
    INFO("VkValidationFeaturesEXT must be chained WITH the debug messenger, not over it: "
         "the messenger's pNext (or the features') must point at the other.");
    REQUIRE(body.find("features.pNext = &dbg;") != std::string::npos);
    REQUIRE(body.find("ci.pNext = &features;") != std::string::npos);
}

TEST_CASE("a run says in its own log whether sync validation was on",
          "[render][validation]")
{
    // THE POINT OF THE WHOLE SLICE. The failure this guards against is not a missing
    // check, it is a FALSE CLAIM about a check: "zero validation errors" was reported as
    // evidence about synchronisation when the feature was off. A log line at startup makes
    // that claim checkable from the same log the grep reads, instead of from someone's
    // memory of how the backend is configured.
    const std::string body = instance_body(read_backend());
    INFO("create_instance must log when synchronization validation is enabled, so a log "
         "proving a clean run also proves WHAT was being checked.");
    REQUIRE(body.find("synchronization validation") != std::string::npos);
}

namespace
{
// A named method's body, from its signature to the next method at the same indentation.
std::string pass_body(const std::string& src, const std::string& signature)
{
    const std::size_t at = src.find(signature);
    REQUIRE(at != std::string::npos);
    const std::size_t end = src.find("\n    void ", at + signature.size());
    return src.substr(at, (end == std::string::npos ? src.size() : end) - at);
}
} // namespace

TEST_CASE("every pass that clears depth orders the clear against its layout transition",
          "[render][validation]")
{
    // FOUND BY SYNCHRONIZATION VALIDATION, ten times per run, the first time it was ever
    // enabled on this renderer (2026-10-18):
    //
    //   WRITE_AFTER_WRITE hazard detected: attachment loadOp access is not synchronized
    //   with the attachment layout transition ... clears the depth aspect of attachment 1
    //   ... (loadOp VK_ATTACHMENT_LOAD_OP_CLEAR), which was previously written during an
    //   image layout transition initiated by the same command.
    //
    // vkCmdBeginRenderPass does two things to the depth attachment in order: the
    // UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL transition, which WRITES, and then the
    // loadOp CLEAR, which also writes. Ordering them requires the external dependency's
    // destination scope to name the DEPTH stage and access -- EARLY_FRAGMENT_TESTS and
    // DEPTH_STENCIL_ATTACHMENT_WRITE. A dependency naming only COLOR_ATTACHMENT_OUTPUT
    // covers the colour attachment and says nothing about depth.
    //
    // `create_render_pass` (the swapchain) already had this right and is the template the
    // other two now follow; it is asserted here too so the three cannot drift apart.
    const std::string src = read_backend();
    for (const char* sig : {"void create_render_pass()",
                            "void create_offscreen_render_pass()",
                            "void create_hdr_render_pass()"})
    {
        const std::string body = pass_body(src, sig);
        INFO(std::string(sig) + " clears a depth attachment, so its external dependency "
             "must name EARLY_FRAGMENT_TESTS and DEPTH_STENCIL_ATTACHMENT_WRITE in its "
             "DESTINATION scope. Re-check with MOSAIC_VK_SYNC_VALIDATION=1 if you change "
             "this.");
        // Only assert for passes that actually clear depth -- the premise of the case.
        REQUIRE(body.find("depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR") != std::string::npos);
        REQUIRE(body.find("VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT") != std::string::npos);
        REQUIRE(body.find("VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT") != std::string::npos);
    }
}
