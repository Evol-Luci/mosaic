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

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/render/render_device.hpp"
#include "shader_source.hpp"

using mosaic::engine::render::TextureColorSpace;
using mosaic::tests::normalize_ws;
using mosaic::tests::read_backend;
using mosaic::tests::read_shader;

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
    // Updated when IBL added a third kind of set. The point of pinning the exact
    // expression is that adding a cap without adding its descriptors ships green and
    // fails at runtime, mid-frame -- so each new kind must come through here.
    REQUIRE(src.find("pool_size.descriptorCount = kMaxTextures + kMaxMaterialSets * 2 "
                     "+ kMaxIblSets * 3;")
            != std::string::npos);
    REQUIRE(src.find("pool.maxSets = kMaxTextures + kMaxMaterialSets + kMaxIblSets;")
            != std::string::npos);
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

TEST_CASE("the skybox draws behind the scene and does not write depth", "[render]")
{
    // Review Focus 5. depthWrite on would let the sky occlude everything drawn after it;
    // a LESS compare at the far plane rejects every fragment and shows no sky at all.
    // Both are one-token mistakes with a total visual effect, and CI has no GPU.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("sky_depth.depthWriteEnable = VK_FALSE;") != std::string::npos);
    REQUIRE(src.find("sky_depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;")
            != std::string::npos);
    // The fullscreen triangle sits exactly on the far plane, so it fills only pixels
    // nothing else wrote.
    const std::string vert =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(mosaic::tests::read_shader("skybox.vert")));
    REQUIRE(vert.find("gl_Position = vec4(pos, 1.0, 1.0);") != std::string::npos);
}

TEST_CASE("every world-space recording method guards the pass and the skipped frame",
          "[render]")
{
    // Began as the slice-4b review's Critical finding about draw_skybox alone, which had
    // neither guard while every other recording method had both.
    //
    // Without the frame_skipped_ guard: resize or minimise the window, begin_frame takes
    // the VK_ERROR_OUT_OF_DATE_KHR path, sets frame_skipped_ and returns BEFORE
    // vkBeginCommandBuffer -- and the method then records commands into a command buffer
    // that is not in the recording state. That is undefined behaviour, not a visual
    // artefact, and it diverges by driver.
    //
    // The pass guard changed shape on 2026-10-16 and got STRICTER with it. It used to be
    // `if (!viewport_pass_open_) { ensure_swapchain_pass(); }` -- open the destination if
    // nothing has. World space no longer draws into the destination at all: it draws into
    // the HDR scene pass, which RenderSystem opens, and `if (!in_hdr_pass_) { return; }`
    // is a refusal rather than a fallback. A world-space draw that reached the
    // destination pass would be composited over an image the resolve had already
    // tone-mapped, which is the incoherence this slice removed.
    //
    // Widened from draw_skybox to all seven, because the guard is now a shared invariant
    // rather than one method's missing prologue -- and because collapsing the pipeline
    // pairs touched every one of them.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    for (const char* method : {"void draw(", "void draw_particles(", "void draw_ribbons(",
                               "void draw_world_text(", "void draw_decals(",
                               "void draw_skybox(", "void draw_screen_effect("})
    {
        INFO(std::string("method: ") + method);
        const std::size_t at = src.find(method);
        REQUIRE(at != std::string::npos);
        const std::string prologue = src.substr(at, 700);
        REQUIRE(prologue.find("if (!in_hdr_pass_) { return; }") != std::string::npos);
        REQUIRE(prologue.find("frame_skipped_") != std::string::npos);
    }
}

TEST_CASE("the three UI draws record into the destination pass, not the HDR one",
          "[render]")
{
    // The other half of the boundary, and the half a "consistency" edit would break:
    // having just read that world space guards on in_hdr_pass_, the obvious next move is
    // to make the UI do the same. That would put the HUD inside the tone map.
    //
    // A UiPanel's colour is AUTHORED, not captured -- {0.1, 0.6, 0.9} is a value someone
    // typed and expects to see. Through ACES, what appears on screen becomes a function
    // of how bright the scene behind it is, so the HUD would dim whenever the world got
    // bright. ensure_destination_pass() is what keeps them out of it.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    for (const char* method : {"void draw_ui(", "void draw_ui_image(", "void draw_ui_text("})
    {
        INFO(std::string("method: ") + method);
        const std::size_t at = src.find(method);
        REQUIRE(at != std::string::npos);
        const std::string prologue = src.substr(at, 400);
        REQUIRE(prologue.find("ensure_destination_pass();") != std::string::npos);
        REQUIRE(prologue.find("in_hdr_pass_") == std::string::npos);
    }
}

TEST_CASE("the skybox depth TEST is enabled, not merely its write disabled", "[render]")
{
    // The slice-4b review's other test gap. VkPipelineDepthStencilStateCreateInfo is
    // zero-initialised, so deleting the depthTestEnable line silently yields VK_FALSE:
    // every skybox fragment then passes, and with blending disabled the sky paints over
    // the ENTIRE scene. The sibling test pinned depthWriteEnable and the compare op but
    // not this, so the one state that loses the whole image was unguarded.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("sky_depth.depthTestEnable = VK_TRUE;") != std::string::npos);
}

TEST_CASE("the IBL descriptor set is allocated fresh, never rewritten in place", "[render]")
{
    // Review Focus 2, and the structural reason this is its own set rather than three
    // more bindings on the per-frame set.
    //
    // The environment resolves DURING render(), between begin_frame and end_frame, with
    // two frames in flight. Writing IBL descriptors into an existing per-frame set would
    // mutate one the other in-flight frame may still be reading -- undefined behaviour
    // that a single-threaded local run would almost never reproduce. Allocating a new set
    // and swapping the handle is safe, because the old set stays valid for any command
    // buffer still referencing it.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("ibl_set_layout_") != std::string::npos);
    REQUIRE(src.find("vkAllocateDescriptorSets(device_, &ibl_alloc, &fresh_set)")
            != std::string::npos);
    // Four sets: material, shadow, frame UBO, IBL. Vulkan guarantees at least four bound
    // sets, so this sits exactly at the floor and must not grow without a device check.
    REQUIRE(src.find("std::array<VkDescriptorSetLayout, 4> set_layouts") != std::string::npos);
}

TEST_CASE("a cubemap mip chain is uploaded, not generated", "[render]")
{
    // Review Focus 5. The prefiltered chain's levels are CONVOLUTIONS at increasing
    // roughness, not downsamples -- blitting level 0 down would silently replace the
    // slice's entire specular result with a blur, and it would look plausible. Sub-slice
    // 4a's ladder must not be reused here.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    const std::size_t at = src.find("create_cubemap(const std::vector<std::array<");
    REQUIRE(at != std::string::npos);
    const std::string body = src.substr(at, 4000);
    REQUIRE(body.find("vkCmdBlitImage") == std::string::npos);
    REQUIRE(body.find("chain.mipLevels = static_cast<std::uint32_t>(levels.size());")
            != std::string::npos);
    // Every level's size must be checked against the base, or a chain with a wrong level
    // size overruns the staging buffer -- which validation does not reliably catch.
    REQUIRE(body.find("mip_extent_for") != std::string::npos);
}

TEST_CASE("the backend copies exposure into both of its GPU payloads", "[render]")
{
    // The last leg of exposure's journey, and the only one no behavioural test reaches:
    // the copy out of the engine-neutral structs into the two GPU payloads. CI has no GPU,
    // so this is a textual pin on the backend, the same instrument the draw_skybox guard
    // above uses and for the same reason.
    //
    // Both halves are needed and they are separate code paths. The lit pass takes exposure
    // through the per-frame UBO; the skybox cannot, because its pipeline does not bind that
    // descriptor set, so it takes a push constant instead. Replace either assignment with
    // a literal 1.0 and every other test in the suite stays green -- the RenderSystem
    // tests check what the DEVICE was handed, not what the device then did with it, and
    // the shader contract tests check the GLSL, which still reads its input correctly.
    // The symptom would be exposure doing nothing at all, or working on the geometry while
    // the sky ignored it.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));
    REQUIRE(src.find("ubo.exposure[0] = fl.exposure;") != std::string::npos);
    REQUIRE(src.find("push.exposure = params.exposure;") != std::string::npos);
}

TEST_CASE("every per-frame staging buffer is chunked per call", "[render]")
{
    // Latent-shape removal following #275 (where two draw_world_text calls in one frame
    // shared one buffer at offset 0 and the second overwrote the first before submit).
    //
    // Why a source-level contract test is the gate here: the default suite creates no
    // VkDevice, so it cannot execute the Vulkan backend, and RenderSystem already batches
    // all emitters into one draw_particles call and all trails into one draw_ribbons call
    // per frame, so there is no engine-side batching bug to make fail behaviourally.
    //
    // THE BEGIN_FRAME RESETS ARE NO LONGER ASSERTED HERE. They were, as three literal
    // string searches, until a behavioural test took them over:
    // tests/gpu/offscreen_staging_chunk_reset_test.cpp drives real frames on a real
    // offscreen VulkanRenderDevice and requires the per-call chunk vectors stop growing,
    // and deleting any one of the three resets turns exactly its own case red. The string
    // searches were removed rather than kept alongside it, because they passed on a
    // renamed counter and failed on a reformat -- a second assertion that is wrong in
    // both directions is a liability, not a belt and braces. What is still asserted below
    // is the part no GPU test covers behaviourally, since RenderSystem makes exactly one
    // call per path per frame: that the chunked FORM is what the source has.
    const std::string src =
        mosaic::tests::normalize_ws(mosaic::tests::code_only(read_backend()));

    // 1. Each of the three draws takes a chunk, not a shared buffer.
    for (const char* method :
         {"void draw_particles(", "void draw_ribbons(", "void draw_world_text("})
    {
        INFO(std::string("method: ") + method);
        const std::size_t at = src.find(method);
        REQUIRE(at != std::string::npos);
        // Bound the window at the NEXT method, not at a fixed character count. Measured on
        // the pre-change source, draw_ribbons sat 1319 normalised characters from
        // draw_world_text, so a fixed 900 left only 419 characters of margin: shortening
        // draw_ribbons would have slid world text's own chunking into the ribbon window and
        // made this assertion pass on an unchunked ribbon path. A probe that cannot fail is
        // worse than no probe, so the window ends where the method does.
        const std::size_t next = src.find("void draw", at + 1);
        const std::string body =
            src.substr(at, (next == std::string::npos) ? std::string::npos : next - at);
        REQUIRE(body.find("chunks.emplace_back();") != std::string::npos);
        REQUIRE(body.find("_calls_[frame_]++]") != std::string::npos);
    }

    // 2. The single-buffer form is gone.
    REQUIRE(src.find("InstanceBuffer& ib = particle_instances_[frame_];") == std::string::npos);
    REQUIRE(src.find("InstanceBuffer& vb = ribbon_vertices_[frame_];") == std::string::npos);

    // 3. (The per-frame counter resets: see the note at the top of this case --
    //     tests/gpu/offscreen_staging_chunk_reset_test.cpp owns them now.)

    // 4. The members are declared as chunk lists.
    REQUIRE(src.find("std::array<std::vector<InstanceBuffer>, kFramesInFlight> particle_instances_")
            != std::string::npos);
    REQUIRE(src.find("std::array<std::vector<InstanceBuffer>, kFramesInFlight> ribbon_vertices_")
            != std::string::npos);
}
