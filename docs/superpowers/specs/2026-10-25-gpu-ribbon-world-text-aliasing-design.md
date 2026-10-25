# Offscreen GPU pixel assertions for `draw_ribbons` and `draw_world_text` per-call buffer aliasing — design

**Date:** 2026-10-25
**Status:** spec, written before running the tests or the revert probes.

## Premise verification

Before adding new tests, `tests/` was searched for existing coverage of `draw_ribbons` and `draw_world_text`:

1. **`tests/gpu/`** has six test files (`headless_device_test.cpp`, `offscreen_readback_test.cpp`, `offscreen_render_device_test.cpp`, `offscreen_ui_orientation_test.cpp`, `offscreen_particle_aliasing_test.cpp`, `offscreen_hud_underlay_test.cpp`). None calls `draw_ribbons` or `draw_world_text`.
2. **`tests/unit/render/render_system_test.cpp`** defines `FakeRenderDevice::draw_ribbons` (`:160`) and `FakeRenderDevice::draw_world_text` (`:210`), and tests that `RenderSystem` groups `WorldLabel` entities sharing a font into one `draw_world_text` call (`:1478`) and emits one call per distinct font atlas (`:1504`). This is **not sufficient** to guard the Vulkan backend's per-call buffer chunking: `FakeRenderDevice` runs on the CPU with no `VkDevice`, no `VkBuffer`, and no pixel output. Worse, `FakeRenderDevice::draw_world_text` (`:222-224`) deliberately models the *unfixed* single-buffer overwrite (`world_text_buffer` overwritten at offset 0) to test `RenderSystem`'s same-atlas batching, and the multi-font test (`:1504-1539`) inspects `device.world_text_draws` (recorded call arguments) rather than executed GPU draws.
3. **`tests/unit/render/texture_colorspace_contract_test.cpp`** (`every per-frame staging buffer is chunked per call`, `:345-392`) inspects `vulkan_render_device.cpp` as normalised source text for `chunks.emplace_back();` and `_calls_[frame_]++]`. That pins the spelling of the C++ source without executing Vulkan command recording, buffer binding, or GPU submission.

Therefore, `draw_ribbons` and `draw_world_text` remain unasserted in pixels.

### Latency caveat for `draw_ribbons`

`draw_world_text` can be called multiple times per frame through `RenderSystem` today whenever a scene uses more than one font atlas (`engine/render/render_system.hpp:470-473`). By contrast, `draw_ribbons` aliasing is **latent** at the engine level today: `RenderSystem` accumulates one `std::vector<RibbonVertex>` across every `TrailRenderer` and calls `device.draw_ribbons` once outside the `for_each` (`engine/render/render_system.hpp:387-398`). We test `draw_ribbons` directly at the `RenderDevice` level anyway — exactly as `offscreen_particle_aliasing_test.cpp` does for `draw_particles` — to guard the backend contract against a second caller or a future per-trail submission path, and record that latency in the test comment.

## Design

Add two opt-in GPU tests in `tests/gpu/`, registered in `tests/CMakeLists.txt` under `if(MOSAIC_ENABLE_GPU_TESTS)` on the `mosaic_gpu_tests` target (`LABELS gpu`):

1. **`tests/gpu/offscreen_ribbon_aliasing_test.cpp`**
   - Creates a 128x128 offscreen `VulkanRenderDevice` with `ValidationCaptureSink` installed.
   - Constructs two disjoint 6-vertex (`VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`) `RibbonVertex` quads in NDC (`view_proj = Mat4::identity()`, `z = 0.5`):
     - Quad A covers `[-0.75, -0.25] x [+0.25, +0.75]` -> framebuffer `[16, 48] x [16, 48]`, centred at `P_A = (32, 32)`, colour `{1.0, 0.25, 0.125}`, alpha `1.0`.
     - Quad B covers `[+0.25, +0.75] x [-0.75, -0.25]` -> framebuffer `[80, 112] x [80, 112]`, centred at `P_B = (96, 96)`, colour `{0.125, 0.75, 1.0}`, alpha `1.0`.
     - Control point `P_control = (32, 96)` in the bottom-left quadrant outside both quads.
   - Records `clear` -> `begin_frame` -> `begin_hdr_scene_pass` -> `draw_ribbons(batch_a, params)` -> `draw_ribbons(batch_b, params)` -> `end_hdr_scene_pass` -> `draw_hdr_resolve(1.0f, 0.0f)` -> `end_frame`.

2. **`tests/gpu/offscreen_world_text_aliasing_test.cpp`**
   - Same 128x128 offscreen `VulkanRenderDevice`, `ValidationCaptureSink`, and NDC quad coordinates (`P_A = (32, 32)`, `P_B = (96, 96)`, `P_control = (32, 96)`), using 6-vertex `WorldGlyphVertex` batches.
   - Uploads a 4x4 RGBA8 solid-white texture (`255` in all channels) via `offscreen.device->create_texture(solid_white)` first, because `draw_world_text` early-returns when `textures_.empty()` (`vulkan_render_device.cpp:1650`) and `world_text.frag` multiplies `v_color.a` by `texture(atlas, v_uv).a`. A solid-white atlas makes UV coordinates irrelevant so the assertion is purely positional.
   - Records `clear` -> `begin_frame` -> `begin_hdr_scene_pass` -> `draw_world_text(batch_a, Mat4::identity(), atlas)` -> `draw_world_text(batch_b, Mat4::identity(), atlas)` -> `end_hdr_scene_pass` -> `draw_hdr_resolve(1.0f, 0.0f)` -> `end_frame`.

## Oracle and disproving observation (chosen before measuring)

For both tests, on unmodified production code:
- `P_A = (32, 32)` differs from the cleared black background `(0, 0, 0, 255)` by `max_rgb_delta > 64`.
- `P_B = (96, 96)` differs from `(0, 0, 0, 255)` by `max_rgb_delta > 64`.
- `P_control = (32, 96)` is within `±1` per channel of `(0, 0, 0, 255)`.
- `validation_messages` is empty.

**Disproving observation (revert probe):**
When the per-call chunk selection in `vulkan_render_device.cpp` is replaced with a single shared buffer per frame (`chunks[0]`, emplaced once when empty) in `draw_ribbons` (`:1611-1613`) or `draw_world_text` (`:1660-1662`):
- Both recorded draws bind `chunks[0].buf`, whose host-visible memory is overwritten at offset 0 by the second call before `end_frame()` submits the command buffer.
- **Predicted failure:** the test goes RED specifically on the **first** assertion — `require_pixel_differs_from_background` for **`P_A` at `(32, 32)`** — with `P_A` reading back as the background colour `(0, 0, 0, 255)` (`max_rgb_delta = 0`, failing `0 > 64`), while `P_B` at `(96, 96)` remains lit with batch B's tone-mapped colour and `P_control` at `(32, 96)` remains `(0, 0, 0, 255)`.
- Restoring `vulkan_render_device.cpp` (and touching its mtime so Ninja rebuilds) returns both tests to GREEN.

**Suite-count invariance:**
- Default build (`MOSAIC_ENABLE_GPU_TESTS` OFF): `ctest --preset debug -N` discovers **1462** tests, all passing.
- GPU build (`-DMOSAIC_ENABLE_GPU_TESTS=ON`): `ctest -L gpu -N` discovers **8** tests (up from 6), all passing.

## Non-goals

- **Asserting the `begin_frame()` counter resets (`particle_calls_`, `ribbon_calls_`, `world_text_calls_` at `vulkan_render_device.cpp:1931-1933`).** Omitting a counter reset allocates a new chunk on every call across frames rather than reusing chunks from index 0, which is an unbounded memory growth leak rather than intra-frame pixel aliasing and needs a chunk-count inspection seam rather than pixel readback. Tracked in `docs/project/deferred-work-backlog.md`.
- **Changing `RenderSystem` to issue multiple `draw_ribbons` calls per frame.** `RenderSystem` intentionally batches all `TrailRenderer` ribbons into one vector per frame; the new test exercises `RenderDevice::draw_ribbons` directly. This is not deferred work but a declined change: recorded with its reopening trigger in the `Decided: no action` table of `docs/project/deferred-work-backlog.md`.
- **Extracting `ValidationCaptureSink` and the pixel-comparison helpers into a shared `tests/gpu/` header.** Each GPU test file keeps its own anonymous-namespace helpers matching `offscreen_particle_aliasing_test.cpp`. Tracked in `docs/project/deferred-work-backlog.md`.
