# Landmines

**Things that waste an hour and that reading the code will not reveal.** Not a style
guide and not a list of everything that can go wrong: each entry is a trap someone
here actually fell into, with the command or the `file:line` that gets you out.

**This has its own document because it is the one part of the handoff that grows
forever.** `next-steps.md` is a cold-start pointer sheet under a hard 8 KB ceiling,
and by 2026-10-28 this list was **5.6 KB of its 8.2 KB** — 68% of a document whose
job is to be short. The ceiling was fighting the one section nobody wanted to cut,
which is how a scar list ends up being pruned for space rather than because a scar
healed. See `CLAUDE.md`, "Where documentation goes".

**Adding one.** State the symptom first, because that is what a reader arrives with,
then the cause, then the escape. A landmine that reads "do not do X" without saying
what X *looks like* when it bites cannot be recognised in the moment. Keep it to a
few lines and put the narrative in `docs/record/`.

**Retiring one.** Delete it only when the trap is *gone*, never to save space, and
record the retirement in `docs/record/` — two flakes were retired in October and had
to be written down specifically to stop them being re-added. Deleting a live landmine
is the one edit here that costs an hour rather than saving one.

---

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate docs. `git add -A`, re-run the FULL commit. `--amend` silently rewrites the previous commit; `--no-verify --amend` hides that it did.

- **A PR with a merge CONFLICT gets NO check run** — not a red one, *none*: a `pull_request` run needs the merge ref, which cannot exist. Reads exactly like CI being broken; reopening and pushing do not help. Cost an hour (#321, 2026-10-26). Check `gh pr view <n> --json mergeable` for `CONFLICTING`, then rebase. `gh workflow run ci.yml --ref <branch>` gives evidence meanwhile.

- **A GUI run under `timeout` passes with exit 124**, not 0. `mosaic run --window` has no natural exit.

- **A wrong CLI subcommand exits non-zero with ZERO validation errors**, which looks exactly like a clean run. Verify the subcommand exists before trusting a quiet failure.

- **A stdlib-only PCH, and CI deliberately builds WITHOUT it** (`-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`). Never remove that flag (ccache cannot cache a PCH-consuming compile) and never put a project header in `MOSAIC_PCH_HEADERS`. A missing `#include` can compile locally and fail in CI; that is intended. [Record](../record/2026-10.md#reduce-header-fan-out--precompiled-headers--unity-builds).

- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under memory pressure: grep for a real compiler error, and if there is none just re-run the build. Until the link succeeds, `ctest -N` reports the PREVIOUS build's count (it comes from the discovery file, regenerated only on a successful link) — which reads exactly like the State table being wrong. Build clean first, then trust the count.

- **`ctest` sees nothing of the renderer: no `VkDevice` in the default suite, so pixels are eye-only.** **The GPU tests are what cover UI pixels**, on a real `VulkanRenderDevice`: `-DMOSAIC_ENABLE_GPU_TESTS=ON` then `ctest --test-dir <dir> -L gpu` (**13**). **Do not relax the local opt-in**; CI runs them on lavapipe, so the unit rows exclude them (`-LE gpu`). A fresh GPU dir needs `-DFETCHCONTENT_SOURCE_DIR_<DEP>=build/debug/_deps/<dep>-src` per dep.

- **The renderer gate reaches a UI draw only after ~12-25 s, and until 2026-10-30 it could not fail if it never got there.** Corrected, not retired — the trap changed shape. **The gate that asserts a UI frame is now `ctest --test-dir <dir> -L gpu`**, specifically `gpu offscreen the UI gate reaches a real UI draw ...`, which asserts `ui_draws > 0` *before* asserting every recorded scissor is in the render area. Prefer it; it needs no windowing system. **The GUI run is still useful but its pass condition CHANGED:** `timeout 25 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor` = exit **124**, **six** `[INFO]` lines, zero VUID/ERROR/WARN — and `first UI draw recorded` plus `first clipped UI draw recorded` must be PRESENT. At `timeout 12` they are absent and the run proves startup only; that is a FAILURE now, not the normal case. Never read a green GUI run as UI coverage on its own. Why the old one could not fail: at `timeout 12` a forced out-of-area *and* a forced negative scissor both stayed green, because no clipped UI draw had run. **And do not reach for the validation layer instead:** with the draw reached, the layer raised `VUID-vkCmdSetScissor-x-00595` for the negative offset and said **nothing** about the out-of-area extent (three consecutive runs). The out-of-area check is the project's own arithmetic, `scissor_in_render_area`. [Record](../record/2026-10.md#slice-1--the-renderer-gate-learns-to-assert-it-reached-a-ui-frame-2026-10-30).

- **In the editor, `ui_destination_extent()` is `1 x 1` while the viewport pass is open.** So every clipped UI draw the GUI gate exercises is computed against a degenerate one-pixel destination, and a scissor that is wrong against it can still be legal against the real render-pass area (which is why the validation layer stayed silent for an out-of-area extent). Do not read an editor-path clip result as covering a real viewport; 🟡 row 10's readback seam is the fix.

- **In a `-DMOSAIC_ENABLE_GPU_TESTS=ON` build dir, `python3 tools/docs/check_next_steps_claims.py` reports the handoff's Suite size as wrong by exactly 12** (1633 vs 1621), which reads as a stale `next-steps.md`. Its bare fallback is `ctest --preset debug -N` with **no `-LE gpu`**; CI never hits this because it passes `--junit` from the `-LE gpu` run. Run it the same way: `ctest --preset debug -LE gpu --output-junit J.xml && python3 tools/docs/check_next_steps_claims.py --junit J.xml`.

- **ALL 12 GPU tests red — 10 at `validation_messages_are_observed()` and 2 at `VulkanHeadlessDevice::validation_enabled()` — means the validation LAYER is missing, not a renderer regression.** **The 10/2 SPLIT IS EXPECTED, not a second bug:** since 2026-10-30 every GPU test asserts the layer was on before asserting it was silent, but through two different mechanisms — the ten offscreen cases call the process-global free function via `tests/gpu/validation_capture.hpp:100`, while `headless_device_test.cpp:26` and `offscreen_readback_test.cpp:28` construct a `VulkanHeadlessDevice` and read its own per-device accessor, which the shared helper never touches. That split is the visible face of the backlog's "the published VALUE is guarded by nothing" item. Check `vulkaninfo --summary | grep KHRONOS_validation` (and that nothing in the environment sets `VK_LOADER_LAYERS_DISABLE` or `VK_INSTANCE_LAYERS`); CI installs `vulkan-validationlayers` for this. An **NDEBUG** build does it too: the layer block in `vulkan_render_device.cpp` is `#ifndef NDEBUG`, so a release GPU build cannot validate and says so rather than passing vacuously.

- **The GLFW window user pointer HAS an owner:** `VulkanRenderDevice` sets it to `this` in its ctor (`vulkan_render_device.cpp:364`) and `framebuffer_resize_callback` (`:2785`) casts it back **unchecked**. A second `glfwSetWindowUserPointer` is therefore silent memory corruption on the first window resize, not a crash — reach a callback's state through a file-scope pointer (`g_wheel` beside `g_stop`). **No gate sees it:** no `VkDevice` in `ctest`, the renderer gate runs `editor` (no such callback), a smoke run that never resizes passes. Only `app/main.cpp` is pinned (#325).

- **Editing `build_default_scene` breaks four entity-count tests:** `demo_scene_test.cpp:59` and `:74`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four; register any new component. Three count **Transforms**; `:74` counts **scripts**.

- **Adding a field to a component silently re-binds a positional brace initialiser;** gcc only catches it when the types differ. Every UI-component init in `app/run/demo_scene.hpp` is designated since U8b, but `Bloom` is not.

- **`TEST_CASE` names:** no leading `--`, balanced `[`, a comma escaped `\,` when filtering from a script — and **unique across the whole suite**. A DUPLICATE NAME PASSES LOCALLY: `ctest` runs both, and only CI's JUnit guard sees it (`O1: N duplicate test names`). Reproduce before pushing: `ctest --preset debug -LE gpu --output-junit J.xml` then `python3 tools/ci/check_ctest_run.py J.xml <N> build/debug/tests/mosaic_unit_tests-*_tests.cmake`.

- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.

- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** All three staging paths allocate per call and reset in `begin_frame` (#275, #287), asserted in pixels by `tests/gpu/offscreen_*_aliasing_test.cpp`. Do not undo that.

- **After a squash merge leaves local `main` diverged, `git reset --keep origin/main`**, never `--hard`.

- **Four sandbox quirks** (no GUI screenshots, slow cross-process loopback sockets, a broken Blender stdlib, and **a full `/tmp` failing two `build_project` tests with `false == true`, never mentioning disk** — check `df -h /tmp`) plus **two retired flakes that are NOT landmines**: [environment quirks](../record/2026-10.md#environment-quirks-and-retired-landmines-moved-out-of-the-handoff-2026-10-24).

- **Screen-space (non-MVP) shaders must compensate for the negative-height (Y-flipped) viewport:** `clip.y = 1 - 2*ny` for a top-left origin. `ui.vert` does, guarded by `tests/gpu/offscreen_ui_orientation_test.cpp` (mutation-proven). `screen_effect.vert` does NOT — genuinely wrong but **not pixel-observable**, so read the backlog before "fixing" it.
