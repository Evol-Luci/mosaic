# Lavapipe GPU Test Probe Findings

**Date:** 2026-10-24  
**Branch:** `chore/lavapipe-probe` (based on `main` at `7d007ab`)  
**Scope:** Probe-only investigation of whether Mesa's software Vulkan rasteriser (`lavapipe` / `llvmpipe`) can run the four opt-in GPU tests in `tests/gpu/` (`mosaic_gpu_tests`) as a standing CI guard on GPU-less runners. No CI workflows, build defaults, or source files were modified.

---

## Environment & Device-Selection Proof

### Host Environment (recorded beside all timings below)

| Property | Value |
|---|---|
| OS / Kernel | Omarchy 4.0.4 (Arch Linux derivative), `Linux 7.2.5-3-omarchy x86_64` |
| CPU | Intel(R) Core(TM) i9-10900K CPU @ 3.70GHz (10 cores / 20 threads, `nproc=20`) |
| Memory | 64,143 MiB total (~39,285 MiB available during runs) |
| Compiler / Build | `c++ (GCC) 16.2.1 20260810`, `cmake version 4.4.3`, Ninja generator, `debug` preset (`-DMOSAIC_ENABLE_GPU_TESTS=ON`) |
| Vulkan Loader & Validation | `vulkan-icd-loader 1.4.357.0-1`, `VK_LAYER_KHRONOS_validation 1.4.357` (`vulkan-validation-layers 1.4.357.0-1`) |
| Hardware GPU (Baseline) | `NVIDIA GeForce RTX 3080` (`DRIVER_ID_NVIDIA_PROPRIETARY`, driver `610.57.04`, Vulkan `1.4.341`) |
| Software Vulkan (Probe) | `llvmpipe (LLVM 22.1.8, 256 bits)` (`DRIVER_ID_MESA_LLVMPIPE`, `Mesa 26.2.2-arch1.1 (LLVM 22.1.8)`, Vulkan `1.4.354`) |

### User-Space Lavapipe Provisioning (No `sudo`)

The host did not have `/usr/share/vulkan/icd.d/lvp_icd*.json` installed system-wide (`pacman` package `vulkan-swrast` was not installed in `/usr`). However, every shared-library dependency of `vulkan-swrast 1:26.2.2-1` (`mesa 1:26.2.2-1`, `llvm-libs 22.1.8-2`, `spirv-tools 1:1.4.357.0-1`, etc.) was already installed on the host. We fetched `vulkan-swrast-1:26.2.2-1-x86_64.pkg.tar.zst` from the official Arch Linux archive into a user-space scratch directory without root privileges and pointed `VK_ICD_FILENAMES` at a user-local ICD manifest referencing the extracted `libvulkan_lvp.so`.

*Note on ICD filename:* Mesa 26 on Arch installs `/usr/share/vulkan/icd.d/lvp_icd.json` (without the `.x86_64` architecture infix). ~~whereas Ubuntu's `mesa-vulkan-drivers` package installs `/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`~~ — **WRONG, retracted 2026-10-24 by the CI trial (#307).** This was asserted without ever being observed on a runner. `mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4` on `ubuntu-latest` (noble) ships `lvp_icd.json`, with no architecture infix, exactly as Arch does. The first run of the trial workflow failed on `test -f .../lvp_icd.x86_64.json` and printed the directory listing, which is how this was caught; the workflow now globs `lvp_icd*.json`.

### Proof That Tests Executed on Lavapipe (Not Falling Back to NVIDIA)

Three independent checks confirm that `VK_ICD_FILENAMES` overrode the NVIDIA ICD completely:

1. **`vulkaninfo --summary` with `VK_ICD_FILENAMES` set** enumerates **only** `GPU0`:
   ```text
   GPU0:
       apiVersion         = 1.4.354
       driverVersion      = 26.2.2
       vendorID           = 0x10005
       deviceID           = 0x0000
       deviceType         = PHYSICAL_DEVICE_TYPE_CPU
       deviceName         = llvmpipe (LLVM 22.1.8, 256 bits)
       driverID           = DRIVER_ID_MESA_LLVMPIPE
       driverName         = llvmpipe
       driverInfo         = Mesa 26.2.2-arch1.1 (LLVM 22.1.8)
       conformanceVersion = 1.3.1.1
   ```
   (`maxPushConstantsSize = 256`, matching `VulkanRenderDevice`'s 216-byte `PushConstants` check at `modules/render_vulkan/vulkan_render_device.cpp:2942`.)

2. **Vulkan loader trace (`VK_LOADER_DEBUG=driver`) inside `mosaic_gpu_tests`** shows all 6 `vkCreateDevice` calls across the 4 test cases (2 in Test 1, 2 in Test 2, 1 in Test 3, 1 in Test 4) selecting `llvmpipe`:
   ```text
   [Vulkan Loader] INFO | DRIVER:       Sorted order:
   [Vulkan Loader] INFO | DRIVER:             [0] llvmpipe (LLVM 22.1.8, 256 bits)
   [Vulkan Loader] DRIVER:                Using "llvmpipe (LLVM 22.1.8, 256 bits)" with driver: ".../libvulkan_lvp.so"
   ```
   Without `VK_ICD_FILENAMES`, the same trace prints:
   ```text
   [Vulkan Loader] DRIVER:                Using "NVIDIA GeForce RTX 3080" with driver: "/usr/lib/libGLX_nvidia.so.610.57.04"
   ```

3. **Distinct 1-ULP sRGB rounding signatures in pixel readback** (detailed under Question 1): in `offscreen_ui_orientation_test.cpp`, NVIDIA renders the top-left quad green channel (`0.5` linear) as `188` and the bottom-right quad blue channel (`0.875` linear) as `241`, whereas lavapipe renders them as `187` and `240` respectively.

---

## Question 1: Conformance

> Do all four `tests/gpu/*` cases pass under lavapipe, assertion by assertion?

### Prediction vs. Achieved

- **Predicted:** All 4 test cases (35,402 assertions) would pass. Tests 1 and 2 (device lifecycle and `R8G8B8A8_UNORM` clear/readback) would match identically; Tests 3 and 4 (`B8G8R8A8_SRGB` clear and `draw_ui` solid-colour quads) would pass within their existing `<= 1` per-channel tolerance because neither test exercises texture filtering, mipmapping, or tonemapping.
- **Achieved:** **4 of 4 test cases passed (`35,402 / 35,402` assertions passed, 0 failures).**
- **Gap & Cause:** Zero test-outcome gap. At the byte level inside Test 4's `<= 1` tolerance, we observed a **1-ULP difference** (`187` vs `188` on lavapipe for linear `0.5`, and `241` vs `240` on NVIDIA for linear `0.875`). Both values lie within `0.03` of a half-integer rounding boundary (`187.516` and `240.471` in exact IEEE-754 float-to-sRGB8 conversion), confirming that the `<= 1` tolerance in `tests/gpu/offscreen_ui_orientation_test.cpp` is actively needed by both drivers on different channels.

### Per-Test Results and Exact Actual-vs-Expected Numbers

| # | Test Case (`tests/gpu/`) | Assertions | NVIDIA Result | Lavapipe Result | Pixel / State Comparison (Actual vs. Expected) |
|---|---|---:|---|---|---|
| 1 | `gpu headless Vulkan device creates and destroys cleanly twice in one process` (`headless_device_test.cpp`) | 14 | **PASS** (14/14) | **PASS** (14/14) | Both iterations: `instance != nullptr`, `physical_device != nullptr`, `device != nullptr`, `graphics_queue != nullptr`, `validation_enabled() == true`, `validation_message_count() == 0`, `total_validation_message_count() == 0`. |
| 2 | `gpu offscreen clear and readback asserts all 256 pixels twice in one process` (`offscreen_readback_test.cpp`) | 2,573 | **PASS** (2,573/2,573) | **PASS** (2,573/2,573) | 16×16 `VK_FORMAT_R8G8B8A8_UNORM` × 2 runs: all 256 pixels per run exactly equal expected `(0x12, 0x34, 0x56, 0x78)` = `(18, 52, 86, 120)` on both NVIDIA and lavapipe (0 delta). |
| 3 | `gpu offscreen VulkanRenderDevice clears and reads back 64x64 sRGB pixels across two frames` (`offscreen_render_device_test.cpp`) | 32,775 | **PASS** (32,775/32,775) | **PASS** (32,775/32,775) | 64×64 `VK_FORMAT_B8G8R8A8_SRGB` × 2 frames via render-pass `LOAD_OP_CLEAR`:<br>• Frame 0 linear `(0.25, 0.5, 0.75, 1.0)` → expected `(137, 188, 225, 255)`; actual on **both** NVIDIA and lavapipe = **`(137, 188, 225, 255)`** across all 4,096 pixels (0 delta).<br>• Frame 1 linear `(0.75, 0.125, 0.5, 1.0)` → expected `(225, 99, 188, 255)`; actual on **both** NVIDIA and lavapipe = **`(225, 99, 188, 255)`** across all 4,096 pixels (0 delta). |
| 4 | `gpu offscreen draw_ui places top-left origin quads in the expected corners` (`offscreen_ui_orientation_test.cpp`) | 40 | **PASS** (40/40) | **PASS** (40/40) | 128×128 `VK_FORMAT_B8G8R8A8_SRGB` across 2 frames (4 corner samples/frame, `±1` tolerance): see exact corner table below. |

#### Exact Corner Pixel Table for Test 4 (`offscreen_ui_orientation_test.cpp`)

| Frame & Sampled Pixel | Linear Input `(R, G, B, A)` | Unrounded `sRGB8` Float | CPU Reference `expected` | NVIDIA `actual` (Delta) | Lavapipe `actual` (Delta) |
|---|---|---|---|---|---|
| Frame 0 top-left `(8, 8)` (inside `top_left_quad`) | `(0.75, 0.5, 0.125, 1.0)` | `(225.430, 187.516, 99.406, 255.0)` | `(225, 188, 99, 255)` | `(225, 188, 99, 255)` (`0, 0, 0, 0`) | **`(225, 187, 99, 255)`** (`0, -1, 0, 0`) |
| Frame 0 bottom-left `(8, 119)` (clear) | `(0.125, 0.25, 0.5, 1.0)` | `(99.406, 136.968, 187.516, 255.0)` | `(99, 137, 188, 255)` | `(99, 137, 188, 255)` (`0, 0, 0, 0`) | `(99, 137, 188, 255)` (`0, 0, 0, 0`) |
| Frame 0 top-right `(119, 8)` (clear) | `(0.125, 0.25, 0.5, 1.0)` | `(99.406, 136.968, 187.516, 255.0)` | `(99, 137, 188, 255)` | `(99, 137, 188, 255)` (`0, 0, 0, 0`) | `(99, 137, 188, 255)` (`0, 0, 0, 0`) |
| Frame 0 bottom-right `(119, 119)` (clear) | `(0.125, 0.25, 0.5, 1.0)` | `(99.406, 136.968, 187.516, 255.0)` | `(99, 137, 188, 255)` | `(99, 137, 188, 255)` (`0, 0, 0, 0`) | `(99, 137, 188, 255)` (`0, 0, 0, 0`) |
| Frame 1 top-left `(8, 8)` (first `draw_ui` call) | `(0.75, 0.5, 0.125, 1.0)` | `(225.430, 187.516, 99.406, 255.0)` | `(225, 188, 99, 255)` | `(225, 188, 99, 255)` (`0, 0, 0, 0`) | **`(225, 187, 99, 255)`** (`0, -1, 0, 0`) |
| Frame 1 bottom-right `(119, 119)` (second `draw_ui` call) | `(0.25, 0.75, 0.875, 1.0)` | `(136.968, 225.430, 240.471, 255.0)` | `(137, 225, 240, 255)` | **`(137, 225, 241, 255)`** (`0, 0, +1, 0`) | `(137, 225, 240, 255)` (`0, 0, 0, 0`) |
| Frame 1 bottom-left `(8, 119)` (clear) | `(0.125, 0.25, 0.5, 1.0)` | `(99.406, 136.968, 187.516, 255.0)` | `(99, 137, 188, 255)` | `(99, 137, 188, 255)` (`0, 0, 0, 0`) | `(99, 137, 188, 255)` (`0, 0, 0, 0`) |
| Frame 1 top-right `(119, 8)` (clear) | `(0.125, 0.25, 0.5, 1.0)` | `(99.406, 136.968, 187.516, 255.0)` | `(99, 137, 188, 255)` | `(99, 137, 188, 255)` (`0, 0, 0, 0`) | `(99, 137, 188, 255)` (`0, 0, 0, 0`) |

---

## Question 2: Cost

> How long do they take under lavapipe, wall-clock, versus under the NVIDIA driver on the same machine? The CI job it would join is currently ~1m20s total, of which the test step is ~11-18s. State both numbers and the delta.

### Prediction vs. Achieved

- **Predicted:** Lavapipe would be 2x–5x *slower* than NVIDIA (`~0.5s – 3.0s` under lavapipe vs. `~0.3s – 1.0s` under NVIDIA) because `VulkanRenderDevice` constructs 13 graphics pipelines in Test 3 and again in Test 4, which we expected LLVM to JIT-compile eagerly.
- **Achieved:** Lavapipe is **faster** than the NVIDIA proprietary driver on this machine across every concurrency level and even with Mesa's on-disk shader cache disabled (`MESA_SHADER_CACHE_DISABLE=true`):
  - **Serial (`-j1`):** Lavapipe warm is **0.215s** median vs. NVIDIA **0.990s** median (delta **-0.775s**, **4.60x faster**). Lavapipe cold (`MESA_SHADER_CACHE_DISABLE=true`) is **0.389s** median (delta **-0.601s**, **2.54x faster**).
  - **Parallel (`-j4`, matching GitHub Actions `ubuntu-latest` vCPU count):** Lavapipe warm is **0.074s** median vs. NVIDIA **0.684s** median (delta **-0.610s**, **9.24x faster**). Lavapipe cold is **0.123s** median (delta **-0.561s**, **5.56x faster**).
  - **Parallel (`-j20`, matching host `nproc=20` used by CI's `-j"$(nproc)"`):** Lavapipe warm is **0.074s** median vs. NVIDIA **0.660s** median (delta **-0.586s**, **8.92x faster**). Lavapipe cold is **0.124s** median (delta **-0.536s**, **5.32x faster**).
- **Gap & Cause:** Two factors inverted our prediction:
  1. **Kernel driver bring-up vs. in-process user-space allocation:** These four tests execute tiny workloads (16×16, 64×64, 128×128) across 6 `VkInstance` + `VkDevice` lifecycles in 4 processes. On NVIDIA (`DRIVER_ID_NVIDIA_PROPRIETARY` 610.57.04), each process pays `/dev/nvidiactl` + `/dev/nvidia0` ioctl and kernel context creation/teardown overhead (~150–300ms serially, inflating to ~450–750ms per process when 4 processes contend on the kernel driver lock in parallel). Lavapipe initializes entirely in user-space RAM in ~20–45ms per device with zero kernel lock contention.
  2. **Lazy LLVM JIT compilation in lavapipe:** During `vkCreateGraphicsPipelines`, lavapipe translates SPIR-V to NIR and defers full LLVM x86_64 codegen to the draw variants actually bound and drawn (0 pipelines drawn in Test 3; 1 pipeline (`ui_pipeline_`) drawn in Test 4). Even with `MESA_SHADER_CACHE_DISABLE=true`, total CPU time across all 4 tests is only ~0.37s (`0.123s` wall-clock at `-j4`).

### Raw Timing Tables (5 Trials per Configuration)

All timings below were measured on the host environment recorded above (`Linux 7.2.5-3-omarchy x86_64`, Intel i9-10900K `nproc=20`, 64 GiB RAM, `build/debug` with `-DMOSAIC_ENABLE_GPU_TESTS=ON`) using Python `time.perf_counter()` around `ctest --test-dir build/debug -R gpu --output-on-failure` and reading per-case durations from `ctest --output-junit`.

#### Summary Wall-Clock Comparison (`ctest -R gpu`, 5 trials each)

| Mode | NVIDIA (`610.57.04`) Wall Median (Min–Max) | Lavapipe Warm (`Mesa 26.2.2`) Wall Median (Min–Max) | Delta (Warm − NVIDIA) | Lavapipe Cold (`MESA_SHADER_CACHE_DISABLE=true`) Wall Median (Min–Max) | Delta (Cold − NVIDIA) |
|---|---|---|---|---|---|
| `-j1` (serial default) | **0.990s** (`0.858s–1.566s`) | **0.215s** (`0.208s–0.221s`) | **-0.775s** (`0.22x` / 4.60x faster) | **0.389s** (`0.381s–0.416s`) | **-0.601s** (`0.39x` / 2.54x faster) |
| `-j4` (4 workers) | **0.684s** (`0.659s–0.778s`) | **0.074s** (`0.072s–0.101s`) | **-0.610s** (`0.11x` / 9.24x faster) | **0.123s** (`0.118s–0.134s`) | **-0.561s** (`0.18x` / 5.56x faster) |
| `-j20` (`nproc=20`) | **0.660s** (`0.656s–0.663s`) | **0.074s** (`0.072s–0.078s`) | **-0.586s** (`0.11x` / 8.92x faster) | **0.124s** (`0.118s–0.136s`) | **-0.536s** (`0.19x` / 5.32x faster) |

#### Per-Test CTest Durations (Serial `-j1`, Median [Min–Max] over 5 Trials)

| Test Case | NVIDIA (`-j1`) | Lavapipe Warm (`-j1`) | Delta (Warm − NVIDIA) | Lavapipe Cold (`-j1`) | Delta (Cold − NVIDIA) |
|---|---|---|---|---|---|
| 1. `gpu headless Vulkan device creates and destroys cleanly twice in one process` | `0.300s` `[0.268s–0.470s]` | `0.044s` `[0.041s–0.049s]` | `-0.256s` | `0.090s` `[0.089s–0.110s]` | `-0.210s` |
| 2. `gpu offscreen clear and readback asserts all 256 pixels twice in one process` | `0.328s` `[0.256s–0.607s]` | `0.046s` `[0.044s–0.050s]` | `-0.282s` | `0.093s` `[0.091s–0.100s]` | `-0.235s` |
| 3. `gpu offscreen VulkanRenderDevice clears and reads back 64x64 sRGB pixels across two frames` | `0.178s` `[0.153s–0.233s]` | `0.053s` `[0.052s–0.054s]` | `-0.125s` | `0.082s` `[0.079s–0.088s]` | `-0.096s` |
| 4. `gpu offscreen draw_ui places top-left origin quads in the expected corners` | `0.174s` `[0.151s–0.234s]` | `0.049s` `[0.047s–0.052s]` | `-0.125s` | `0.098s` `[0.097s–0.107s]` | `-0.076s` |
| **Sum of 4 CTest case durations** | **`0.967s`** `[0.837s–1.531s]` | **`0.194s`** `[0.188s–0.200s]` | **`-0.773s`** | **`0.367s`** `[0.360s–0.395s]` | **`-0.600s`** |

#### All 5 Raw Trial Runs per Configuration

- **NVIDIA (`-j1`):** `1.566s`, `1.179s`, `0.990s`, `0.883s`, `0.858s`
- **NVIDIA (`-j4`):** `0.778s`, `0.684s`, `0.726s`, `0.659s`, `0.675s`
- **NVIDIA (`-j20`):** `0.661s`, `0.660s`, `0.663s`, `0.656s`, `0.657s`
- **Lavapipe warm (`-j1`):** `0.215s`, `0.209s`, `0.215s`, `0.221s`, `0.208s`
- **Lavapipe warm (`-j4`):** `0.074s`, `0.073s`, `0.101s`, `0.077s`, `0.072s`
- **Lavapipe warm (`-j20`):** `0.074s`, `0.078s`, `0.077s`, `0.073s`, `0.072s`
- **Lavapipe cold (`-j1`):** `0.416s`, `0.390s`, `0.389s`, `0.381s`, `0.383s`
- **Lavapipe cold (`-j4`):** `0.134s`, `0.118s`, `0.121s`, `0.132s`, `0.123s`
- **Lavapipe cold (`-j20`):** `0.124s`, `0.136s`, `0.130s`, `0.121s`, `0.118s`

#### Full 1,466-Test Suite Simulation (`1462` unit + `4` GPU, `-j20`, Lavapipe)

Running the full suite (`ctest --preset debug --output-on-failure --no-tests=error -j20`) with `VK_ICD_FILENAMES` set to lavapipe completed all 1,466 tests in **3.380s** wall-clock (`sum_of_durations=53.3s`, longest case `1.11s` `PluginFileWatcher.poll detects a modified file's mtime`). Every GPU test case (~0.044s–0.114s) is two orders of magnitude below `tools/ci/check_ctest_run.py`'s O3 per-case budget (`MAX_CASE_SECONDS = 10.0s`).

Relative to the existing CI job (~1m20s total, ~11–18s `Unit tests` step), executing the 4 GPU tests under lavapipe adds **~0.1s–0.4s** of test execution time (<3% of the test step). However, see **What Could Not Be Determined Locally** below regarding `apt-get` package installation time on the runner.

---

## Question 3: False Failures (Validation Cleanliness)

> Does lavapipe report Vulkan validation warnings/errors that the NVIDIA driver does not?

### Prediction vs. Achieved

- **Predicted:** Zero validation warnings or errors on lavapipe under `VK_LAYER_KHRONOS_validation 1.4.357`.
- **Achieved:** **0 validation warnings and 0 validation errors** across all 4 GPU test cases (and all 6 `VkDevice` lifecycles) under both standard validation and synchronization validation (`MOSAIC_VK_SYNC_VALIDATION=1`).
- **Gap & Cause:** Zero gap.

### Evidence

1. **Test 1 (`headless_device_test.cpp`) and Test 2 (`offscreen_readback_test.cpp`):**
   - Both assert `REQUIRE(dev.validation_enabled())` (confirming `VK_LAYER_KHRONOS_validation` `1.4.357` was loaded and active, not silently skipped).
   - Both assert `REQUIRE(dev.validation_message_count() == 0u)` on every device instance and `REQUIRE(VulkanHeadlessDevice::total_validation_message_count() == baseline_total)` after teardown (`0 == 0`).
2. **Test 3 (`offscreen_render_device_test.cpp`) and Test 4 (`offscreen_ui_orientation_test.cpp`):**
   - Both install `ValidationCaptureSink` capturing all `Warn` and `Error` records on the `"vulkan"` log category during full `VulkanRenderDevice` construction (13 pipelines, IBL upload, bloom chain creation), multi-frame rendering, readback, and teardown.
   - Both assert `REQUIRE(validation_messages.empty())` (`true`, 0 messages).
3. **Additional probe with Synchronization Validation (`MOSAIC_VK_SYNC_VALIDATION=1`):**
   - Running all 4 tests under lavapipe with `MOSAIC_VK_SYNC_VALIDATION=1` logged `[INFO] vulkan: synchronization validation ENABLED (MOSAIC_VK_SYNC_VALIDATION); expect it to be slower` and passed all 35,402 assertions with **0** validation warnings or errors.

---

## What Could Not Be Determined Locally (Simulation Boundary)

Running on this workstation's user-space `vulkan-swrast 1:26.2.2-1` is a **local simulation**, not a GitHub Actions runner measurement. Four things could not be determined from this machine alone:

1. **Behavior of Ubuntu's older Mesa/LLVM stack on `ubuntu-latest` (24.04 `noble`) and upcoming Ubuntu 26:**
   - This workstation runs Mesa `26.2.2` / LLVM `22.1.8` and `VK_LAYER_KHRONOS_validation 1.4.357`. Ubuntu 24.04 `noble` ships an older `mesa-vulkan-drivers` (~Mesa 24.0/24.2) alongside LunarG's `noble` validation layers. Whether that combination triggers any driver-specific validation message or >1 ULP sRGB difference can only be proven on the runner itself.
2. **Runner `apt-get` installation time for `mesa-vulkan-drivers` and `vulkan-validationlayers`:**
   - `.github/workflows/ci.yml` (line 74) currently installs only `vulkan-headers libvulkan-dev shaderc`.
   - It does **not** install `mesa-vulkan-drivers` (which provides `/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` and `libvulkan_lvp.so` on Ubuntu) or `vulkan-validationlayers` (which provides `VK_LAYER_KHRONOS_validation`).
   - Because `headless_device_test.cpp:26,44` and `offscreen_readback_test.cpp:28,51` hard-assert `REQUIRE(dev.validation_enabled())`, running `mosaic_gpu_tests` on CI **without** `vulkan-validationlayers` installed will fail immediately on `dev.validation_enabled() == false`.
   - While executing the 4 tests takes only ~0.1–0.4s under lavapipe, downloading and unpacking `mesa-vulkan-drivers` + `vulkan-validationlayers` (and LLVM runtime dependencies if not already present on the runner image) during the `apt-get install` step will likely add **~3–10s** to the ~1m20s CI job; the exact `apt-get` wall-clock delta requires a runner measurement.
3. **Compilation time of the 4 `tests/gpu/*.cpp` TUs + `mosaic_gpu_tests` link step on CI:**
   - Locally with 20 threads, building `mosaic_gpu_tests` is hidden behind `mosaic_unit_tests`. On a 4-vCPU runner with `ccache` warm, the 4 TUs will hit `ccache` after the first `main` push, leaving only the `mosaic_gpu_tests` link step (~1–2s), which was not measured on a 4-vCPU runner.
4. **Two CI guard interactions proven locally that must be updated in lockstep if GPU tests join CI:**
   - **`tools/ci/check_ctest_run.py` O1 check (`.github/workflows/ci.yml:177`):** The workflow currently passes only `build/debug/tests/mosaic_unit_tests-*_tests.cmake` (1,462 `add_test` lines). With `-DMOSAIC_ENABLE_GPU_TESTS=ON`, `ctest -N` discovers `N=1466` tests while that single glob has `1462`, causing `check_ctest_run.py` to fail with:
     `check_ctest_run: O1: ctest -N reports 1466 but the generated cmake has 1462 add_test`
     Passing both generated files (`build/debug/tests/mosaic_*_tests-*_tests.cmake`) resolves this (`check_ctest_run: OK, 1466 of 1466 tests ran and passed`).
   - **`tools/docs/check_next_steps_claims.py` O2/O3 check (`.github/workflows/ci.yml:189`):** Reads `${RUNNER_TEMP}/ctest-junit.xml` and verifies that the testcase count matches `docs/project/next-steps.md`'s `Suite size` (`1462 tests`) and `Suite status` (`1462/1462`). If the main `ctest` invocation runs all 1,466 tests, `docs/project/next-steps.md` (and/or default `MOSAIC_ENABLE_GPU_TESTS`) must be updated in lockstep so local `ctest --preset debug -N` and CI agree on `1466`.

---

## Recommendation

**Proceed to a CI trial PR** enabling `mosaic_gpu_tests` under Mesa lavapipe (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd*.json` — glob it; see the retraction above).

### Why

1. **Conformance is 100%:** All 4 tests and all 35,402 assertions pass under lavapipe, with pixel values matching to 0 ULP on clears and `<= 1` ULP on shader-drawn sRGB UI quads (the exact same `±1` ULP boundary behaviour seen on NVIDIA).
2. **Test execution cost is negligible:** Lavapipe runs the 4 tests in **0.074s** warm / **0.123s** cold at `-j4` (**0.215s** warm / **0.389s** cold serially) — 2.5x to 9.2x *faster* than the NVIDIA proprietary driver on the same machine, and <3% of the existing 11–18s CI test step.
3. **Zero false validation failures:** Both default `VK_LAYER_KHRONOS_validation` and `MOSAIC_VK_SYNC_VALIDATION=1` produce 0 warnings and 0 errors under lavapipe.

### Condition That Would Change This Recommendation

Revert or abandon the CI change if, on the actual `ubuntu-latest` runner:
1. Ubuntu's `mesa-vulkan-drivers` (`lvp_icd.x86_64.json`) + LunarG `vulkan-validationlayers` emits driver-specific validation warnings/errors that do not occur on hardware drivers (violating Question 3's zero-false-failure requirement), **or**
2. The `apt-get install` wall-clock overhead of adding `mesa-vulkan-drivers` and `vulkan-validationlayers` inflates the ~1m20s CI job beyond what the human considers acceptable for 4 GPU tests.

---

## Reproducing this probe (added during supervision, 2026-10-24)

**The original probe environment was ephemeral and did not survive the session.** The
numbers above were produced against a lavapipe extracted into a scratch directory that no
longer exists; `/usr/share/vulkan/icd.d/` on this host still contains only
`nvidia_icd.json`, and `pacman -Q vulkan-swrast` reports the package is not installed. As
first written, this document recorded the measurements but not the provisioning steps, so
no one could re-run it — the evidence existed only as pasted output.

The steps below were **re-derived and re-executed independently** during review, which is
how the result was confirmed rather than merely re-read. They need no `sudo`.

```sh
# 1. Fetch and extract the ICD into an isolated directory (no root, nothing installed).
mkdir -p /tmp/lvp && cd /tmp/lvp
curl -sSLO "https://archive.archlinux.org/packages/v/vulkan-swrast/vulkan-swrast-1%3A26.2.2-1-x86_64.pkg.tar.zst"
tar --use-compress-program=unzstd -xf vulkan-swrast-1%3A26.2.2-1-x86_64.pkg.tar.zst

# 2. The shipped manifest uses a RELATIVE library_path, which the loader will not resolve
#    from an arbitrary directory. Rewrite it to an absolute path.
python3 - <<'PY'
import json
p = "/tmp/lvp/usr/share/vulkan/icd.d/lvp_icd.json"
d = json.load(open(p))
d["ICD"]["library_path"] = "/tmp/lvp/usr/lib/libvulkan_lvp.so"
json.dump(d, open("/tmp/lvp/lvp_abs.json", "w"), indent=2)
PY

# 3. Prove the override took effect BEFORE trusting any result. Every line must say
#    llvmpipe; a single NVIDIA line voids the run.
VK_ICD_FILENAMES=/tmp/lvp/lvp_abs.json VK_LOADER_DEBUG=driver \
  ./build/debug/tests/mosaic_gpu_tests 2>&1 | grep -i 'Using "' | sort | uniq -c

# 4. Run the tests.
VK_ICD_FILENAMES=/tmp/lvp/lvp_abs.json ctest --test-dir build/debug -R gpu --output-on-failure
```

**Independent re-run result (2026-10-24, same host, 5 GPU tests including the new
`draw_particles` aliasing case):** step 3 reported `7 llvmpipe, 0 NVIDIA` across all seven
`vkCreateDevice` calls; step 4 reported **5/5 passed, 0.25 s total**, against **1.05 s** for
the same binary on the NVIDIA driver — a 4.2x speedup, inside the 2.5x–9.2x range measured
above. The conformance and zero-false-failure findings reproduce.

**A caveat the headline numbers should carry:** the Arch package used here is Mesa 26.2.2.
`ubuntu-latest` ships an older `mesa-vulkan-drivers` (**measured by the trial: 25.2.8**, not
the ~24.0/24.2 guessed above), and the manifest is named `lvp_icd.json` there too, **not**
`lvp_icd.x86_64.json` as this document originally claimed. This probe is therefore evidence
about *lavapipe as an approach*, not a measurement of the runner. The runner's Mesa version
and its `apt-get` cost remain unmeasured, which is exactly what the trial PR is for.
