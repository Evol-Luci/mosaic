// The begin_frame() per-call counter resets, asserted on BEHAVIOUR.
//
// `particle_calls_`, `ribbon_calls_` and `world_text_calls_` are reset to 0 for the
// current frame-in-flight slot at the top of begin_frame(), right after the fence wait
// (vulkan_render_device.cpp, "its per-call staging chunks are free to be reused").
// Dropping one of those three lines leaks the previous frame's call count into this
// frame, so every draw on that path indexes past the chunks the slot already owns and
// `chunks.emplace_back()` grows the vector -- forever, one chunk per call per frame.
//
// NOTHING ABOUT THE IMAGE CHANGES. Each call still gets its own buffer, just a freshly
// grown one, so every pixel is identical and the three
// tests/gpu/offscreen_*_aliasing_test.cpp pixel tests stay green on the broken code.
// That is why the decisive assertion here is on chunk COUNTS, read through
// StagingChunkInspector, and not on pixels: a pixel assertion would be decorative.
//
// The counters themselves are not what is observed -- they are reset every frame either
// way. The observable residue is the SIZE of the chunk vectors: a vector that grew
// because a reset was missed stays grown.
//
// Shape of each case: drive kCallsPerFrame draws for one frame per in-flight slot (the
// warm-up, which is where every chunk the device legitimately needs gets allocated),
// record the counts, then drive kExtraFrames more identical frames and require the
// counts did not move. A healthy device reuses; a device missing a reset grows.
// Three separate cases, one per counter, because a single-path test would leave the
// other two resets uncovered while reading as done.

#include "modules/render_vulkan/vulkan_render_device.hpp"

#include "engine/render/render_device.hpp"
#include "foundation/math/mat4.hpp"
#include "foundation/math/vec3.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{

constexpr std::uint32_t kWidth = 64;
constexpr std::uint32_t kHeight = 64;

// Two calls per frame, not one: one call per frame would leave "the chunk vector holds
// at most one entry" indistinguishable from "it holds at most one entry per call".
constexpr std::size_t kCallsPerFrame = 2;

// One frame per frame-in-flight slot. Each slot allocates its own chunks on its first
// use, so the counts are only at their steady state once every slot has been through a
// frame -- comparing against a baseline taken earlier would compare against a device
// that was still legitimately growing.
constexpr int kWarmupFramesPerSlot = 1;

// Enough extra frames that each slot is reused several times. A missed reset grows by
// kCallsPerFrame per frame on the affected path, so even one extra frame per slot would
// catch it; six is cheap and makes the growth unmistakable in the failure message.
constexpr int kExtraFrames = 6;

// Two glyph-quad triangles spanning the given world-space rectangle at depth `z`.
std::vector<mosaic::engine::render::WorldGlyphVertex> make_world_glyph_quad(float x0,
                                                                           float y0,
                                                                           float x1,
                                                                           float y1,
                                                                           float z)
{
    using mosaic::engine::render::WorldGlyphVertex;
    using mosaic::foundation::math::Vec3;

    const Vec3 color{1.0f, 1.0f, 1.0f};
    const WorldGlyphVertex tl{Vec3{x0, y1, z}, 0.0f, 0.0f, color, 1.0f};
    const WorldGlyphVertex tr{Vec3{x1, y1, z}, 1.0f, 0.0f, color, 1.0f};
    const WorldGlyphVertex br{Vec3{x1, y0, z}, 1.0f, 1.0f, color, 1.0f};
    const WorldGlyphVertex bl{Vec3{x0, y0, z}, 0.0f, 1.0f, color, 1.0f};
    return {tl, tr, br, tl, br, bl};
}

} // namespace

TEST_CASE("gpu begin_frame resets particle_calls_ so the per-call chunk vector stops growing",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::ParticleDrawParams;
    using mosaic::engine::render::ParticleInstance;
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::StagingChunkCounts;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr Color kClearColor{0.0f, 0.0f, 0.0f, 1.0f};

    OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
    REQUIRE(offscreen.device != nullptr);
    REQUIRE(offscreen.chunks != nullptr);

    const ParticleDrawParams params{
        Mat4::identity(),
        Vec3{1.0f, 0.0f, 0.0f},
        Vec3{0.0f, 1.0f, 0.0f},
    };
    const std::vector<ParticleInstance> batch{
        ParticleInstance{Vec3{0.0f, 0.0f, 0.5f}, 0.25f, Vec3{1.0f, 1.0f, 1.0f}, 1.0f},
    };

    const auto run_frame = [&]() {
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->begin_hdr_scene_pass(kClearColor);
        for (std::size_t call = 0; call < kCallsPerFrame; ++call)
        {
            offscreen.device->draw_particles(batch, params);
        }
        offscreen.device->end_hdr_scene_pass();
        offscreen.device->draw_hdr_resolve(1.0f, 0.0f);
        offscreen.device->end_frame();
    };

    const StagingChunkCounts initial = offscreen.chunks->staging_chunk_counts();
    REQUIRE(initial.frames_in_flight > 0u);
    REQUIRE(initial.particle == 0u);

    const int warmup_frames =
        static_cast<int>(initial.frames_in_flight) * kWarmupFramesPerSlot;
    for (int frame = 0; frame < warmup_frames; ++frame) { run_frame(); }

    const StagingChunkCounts baseline = offscreen.chunks->staging_chunk_counts();
    INFO("after " << warmup_frames << " warm-up frames of " << kCallsPerFrame
                  << " draw_particles calls each: particle chunks = " << baseline.particle
                  << ", frames_in_flight = " << baseline.frames_in_flight);
    REQUIRE(baseline.particle == kCallsPerFrame * baseline.frames_in_flight);

    for (int frame = 0; frame < kExtraFrames; ++frame) { run_frame(); }

    const StagingChunkCounts after = offscreen.chunks->staging_chunk_counts();
    INFO("particle chunks after " << kExtraFrames << " further identical frames = "
                                  << after.particle << ", expected the warm-up total "
                                  << baseline.particle
                                  << " (a larger number means begin_frame did not reset "
                                     "particle_calls_, so every frame allocated "
                                     "kCallsPerFrame more chunks)");
    REQUIRE(after.particle == baseline.particle);
}

TEST_CASE("gpu begin_frame resets ribbon_calls_ so the per-call chunk vector stops growing",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::RibbonDrawParams;
    using mosaic::engine::render::RibbonVertex;
    using mosaic::foundation::math::Mat4;
    using mosaic::foundation::math::Vec3;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::StagingChunkCounts;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr Color kClearColor{0.0f, 0.0f, 0.0f, 1.0f};

    OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
    REQUIRE(offscreen.device != nullptr);
    REQUIRE(offscreen.chunks != nullptr);

    const RibbonDrawParams params{Mat4::identity()};
    const Vec3 color{1.0f, 1.0f, 1.0f};
    const std::vector<RibbonVertex> strip{
        RibbonVertex{Vec3{-0.4f, -0.1f, 0.5f}, color, 1.0f},
        RibbonVertex{Vec3{-0.4f, 0.1f, 0.5f}, color, 1.0f},
        RibbonVertex{Vec3{0.4f, -0.1f, 0.5f}, color, 1.0f},
        RibbonVertex{Vec3{0.4f, 0.1f, 0.5f}, color, 1.0f},
    };

    const auto run_frame = [&]() {
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->begin_hdr_scene_pass(kClearColor);
        for (std::size_t call = 0; call < kCallsPerFrame; ++call)
        {
            offscreen.device->draw_ribbons(strip, params);
        }
        offscreen.device->end_hdr_scene_pass();
        offscreen.device->draw_hdr_resolve(1.0f, 0.0f);
        offscreen.device->end_frame();
    };

    const StagingChunkCounts initial = offscreen.chunks->staging_chunk_counts();
    REQUIRE(initial.frames_in_flight > 0u);
    REQUIRE(initial.ribbon == 0u);

    const int warmup_frames =
        static_cast<int>(initial.frames_in_flight) * kWarmupFramesPerSlot;
    for (int frame = 0; frame < warmup_frames; ++frame) { run_frame(); }

    const StagingChunkCounts baseline = offscreen.chunks->staging_chunk_counts();
    INFO("after " << warmup_frames << " warm-up frames of " << kCallsPerFrame
                  << " draw_ribbons calls each: ribbon chunks = " << baseline.ribbon
                  << ", frames_in_flight = " << baseline.frames_in_flight);
    REQUIRE(baseline.ribbon == kCallsPerFrame * baseline.frames_in_flight);

    for (int frame = 0; frame < kExtraFrames; ++frame) { run_frame(); }

    const StagingChunkCounts after = offscreen.chunks->staging_chunk_counts();
    INFO("ribbon chunks after " << kExtraFrames << " further identical frames = "
                                << after.ribbon << ", expected the warm-up total "
                                << baseline.ribbon
                                << " (a larger number means begin_frame did not reset "
                                   "ribbon_calls_, so every frame allocated "
                                   "kCallsPerFrame more chunks)");
    REQUIRE(after.ribbon == baseline.ribbon);
}

TEST_CASE("gpu begin_frame resets world_text_calls_ so the per-call chunk vector stops growing",
          "[gpu][render][vulkan]")
{
    using mosaic::engine::render::Color;
    using mosaic::engine::render::Image;
    using mosaic::engine::render::ImageFormat;
    using mosaic::engine::render::TextureHandle;
    using mosaic::foundation::math::Mat4;
    using mosaic::render::vk::OffscreenDevice;
    using mosaic::render::vk::StagingChunkCounts;
    using mosaic::render::vk::create_offscreen_vulkan_render_device;

    constexpr Color kClearColor{0.0f, 0.0f, 0.0f, 1.0f};

    OffscreenDevice offscreen = create_offscreen_vulkan_render_device(kWidth, kHeight);
    REQUIRE(offscreen.device != nullptr);
    REQUIRE(offscreen.chunks != nullptr);

    // draw_world_text early-returns when textures_ is empty, which would make every
    // assertion below vacuously true, so the atlas is not optional scenery.
    Image solid_white;
    solid_white.width = 4;
    solid_white.height = 4;
    solid_white.format = ImageFormat::Rgba8;
    solid_white.pixels.assign(4u * 4u * 4u, 255u);
    const TextureHandle atlas = offscreen.device->create_texture(solid_white);
    REQUIRE(atlas != 0u);

    const std::vector<mosaic::engine::render::WorldGlyphVertex> quad =
        make_world_glyph_quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.5f);

    const auto run_frame = [&]() {
        offscreen.device->clear(kClearColor);
        offscreen.device->begin_frame();
        offscreen.device->begin_hdr_scene_pass(kClearColor);
        for (std::size_t call = 0; call < kCallsPerFrame; ++call)
        {
            offscreen.device->draw_world_text(quad, Mat4::identity(), atlas);
        }
        offscreen.device->end_hdr_scene_pass();
        offscreen.device->draw_hdr_resolve(1.0f, 0.0f);
        offscreen.device->end_frame();
    };

    const StagingChunkCounts initial = offscreen.chunks->staging_chunk_counts();
    REQUIRE(initial.frames_in_flight > 0u);
    REQUIRE(initial.world_text == 0u);

    const int warmup_frames =
        static_cast<int>(initial.frames_in_flight) * kWarmupFramesPerSlot;
    for (int frame = 0; frame < warmup_frames; ++frame) { run_frame(); }

    const StagingChunkCounts baseline = offscreen.chunks->staging_chunk_counts();
    INFO("after " << warmup_frames << " warm-up frames of " << kCallsPerFrame
                  << " draw_world_text calls each: world_text chunks = "
                  << baseline.world_text
                  << ", frames_in_flight = " << baseline.frames_in_flight);
    REQUIRE(baseline.world_text == kCallsPerFrame * baseline.frames_in_flight);

    for (int frame = 0; frame < kExtraFrames; ++frame) { run_frame(); }

    const StagingChunkCounts after = offscreen.chunks->staging_chunk_counts();
    INFO("world_text chunks after " << kExtraFrames << " further identical frames = "
                                    << after.world_text << ", expected the warm-up total "
                                    << baseline.world_text
                                    << " (a larger number means begin_frame did not reset "
                                       "world_text_calls_, so every frame allocated "
                                       "kCallsPerFrame more chunks)");
    REQUIRE(after.world_text == baseline.world_text);
}
