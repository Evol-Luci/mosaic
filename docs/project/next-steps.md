# Next Steps — Build Handoff

**For an agent starting cold.** Kept small: narrative (why, what was
measured, which predictions were wrong) lives in [`docs/record/`](../record/), and
pending work in [`deferred-work-backlog.md`](deferred-work-backlog.md).

## State

Every claim carries the command that regenerates it; if they disagree, trust the
command and fix the text.

Rows must be slow-moving: do not re-add a `main` SHA row (#283).

| Claim | Value | Verify with | Checked |
|---|---|---|---|
| Suite size | 1621 tests | `ctest --preset debug -N \| tail -1` (**build first** — see landmines) | 2026-10-27 |
| Suite status | all passing (1621/1621, 4.4 s local) | `ctest --preset debug` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-27 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

**No time-sensitive item is open.** The #307 GPU-test trial is **kept**: its two lavapipe steps are ordinary CI, so a red one is a finding. The Ubuntu-26 ICD pin re-check survives: backlog.

**U8, U9 and U11 are all COMPLETE.** The demo scene now authors a mid-right **scroll region** — the project's first `UiClipRect` — with clipping, scrolling, a button that scrolls out and a wrapped label. **U8d shipped a measurement and NO cache:** never rule on a debug timing (~17 ms debug vs ~0.7 ms release); the trigger is a counter (`engine/ui/ui_layout_stats.hpp`) above ~200k `visits`/frame. Tiers re-derived 2026-10-26 (#310-#318) and the 🟢/🟡 rows are all maintenance; queued are the `begin_frame` counter-reset seam (`🟡` row 5) and the duplicated `tests/gpu/` helpers (`🟢` row 4). [Tiers](deferred-work-backlog.md#effort-vs-impact).

**Eye checks DONE** — six items, all passed 2026-10-28 ([record](../record/2026-10.md)). The scroll offset is **deliberately unsnapped and a human kept it that way**: do not add pixel snapping from the argument alone, it lost to an observation. Still unverified, because nothing authors them: a `WorldLabel` containing `\n` (vertical centring known wrong) and whether `'?'` reads as an error.

## Landmines

Things that waste an hour and that reading the code will not reveal.

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate docs. `git add -A`, re-run the FULL commit. `--amend` silently rewrites the previous commit; `--no-verify --amend` hides that it did.
- **A PR with a merge CONFLICT gets NO check run** — not a red one, *none*: a `pull_request` run needs the merge ref, which cannot exist. Reads exactly like CI being broken; reopening and pushing do not help. Cost an hour (#321, 2026-10-26). Check `gh pr view <n> --json mergeable` for `CONFLICTING`, then rebase. `gh workflow run ci.yml --ref <branch>` gives evidence meanwhile.
- **A GUI run under `timeout` passes with exit 124**, not 0. `mosaic run --window` has no natural exit.
- **A wrong CLI subcommand exits non-zero with ZERO validation errors**, which looks exactly like a clean run. Verify the subcommand exists before trusting a quiet failure.
- **A stdlib-only PCH, and CI deliberately builds WITHOUT it** (`-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`). Never remove that flag (ccache cannot cache a PCH-consuming compile) and never put a project header in `MOSAIC_PCH_HEADERS`. A missing `#include` can compile locally and fail in CI; that is intended. [Record](../record/2026-10.md#reduce-header-fan-out--precompiled-headers--unity-builds).
- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under memory pressure: grep for a real compiler error, and if there is none just re-run the build. Until the link succeeds, `ctest -N` reports the PREVIOUS build's count (it comes from the discovery file, regenerated only on a successful link) — which reads exactly like the State table being wrong. Build clean first, then trust the count.
- **`ctest` sees nothing of the renderer, AND THE GATE NEVER REACHES A UI DRAW.** No `VkDevice` in the default suite: pixels are eye-only. **The gate:** `timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor` = exit **124**, three `[INFO]`, zero VUID/ERROR/WARN; capture on `main`, `diff` after. **It proves STARTUP ONLY** (2026-10-27: **0** UI panel passes at 12 s, 0 at 15 s, 4872 at 18 s — a forced out-of-area *and* a negative scissor both stayed GREEN). A pass cannot be told from a startup timeout; never read it as UI coverage. Fix backlogged (assert it reached a frame, not a bigger timeout). **GPU tests are what cover UI pixels**, on a real `VulkanRenderDevice`: `-DMOSAIC_ENABLE_GPU_TESTS=ON` then `ctest --test-dir <dir> -L gpu` (**12**). **Do not relax the local opt-in**; CI runs them on lavapipe, so the rows above exclude them (`-LE gpu`). A fresh GPU dir needs `-DFETCHCONTENT_SOURCE_DIR_<DEP>=build/debug/_deps/<dep>-src` per dep.
- **The GLFW window user pointer HAS an owner:** `VulkanRenderDevice` sets it to `this` in its ctor (`vulkan_render_device.cpp:364`) and `framebuffer_resize_callback` (`:2785`) casts it back **unchecked**. A second `glfwSetWindowUserPointer` is therefore silent memory corruption on the first window resize, not a crash — reach a callback's state through a file-scope pointer (`g_wheel` beside `g_stop`). **No gate sees it:** no `VkDevice` in `ctest`, the renderer gate runs `editor` (no such callback), a smoke run that never resizes passes. Only `app/main.cpp` is pinned (#325).
- **Editing `build_default_scene` breaks four entity-count tests:** `demo_scene_test.cpp:59` and `:74`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four; register any new component. Three count **Transforms**; `:74` counts **scripts**.
- **Adding a field to a component silently re-binds a positional brace initialiser;** gcc only catches it when the types differ. Every UI-component init in `app/run/demo_scene.hpp` is designated since U8b, but `Bloom` is not.
- **`TEST_CASE` names:** no leading `--`, balanced `[`, a comma escaped `\,` when filtering from a script — and **unique across the whole suite**. A DUPLICATE NAME PASSES LOCALLY: `ctest` runs both, and only CI's JUnit guard sees it (`O1: N duplicate test names`). Reproduce before pushing: `ctest --preset debug -LE gpu --output-junit J.xml` then `python3 tools/ci/check_ctest_run.py J.xml <N> build/debug/tests/mosaic_unit_tests-*_tests.cmake`.
- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.
- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** All three staging paths allocate per call and reset in `begin_frame` (#275, #287), asserted in pixels by `tests/gpu/offscreen_*_aliasing_test.cpp`. Do not undo that.
- **After a squash merge leaves local `main` diverged, `git reset --keep origin/main`**, never `--hard`.
- **Four sandbox quirks** (no GUI screenshots, slow cross-process loopback sockets, a broken Blender stdlib, and **a full `/tmp` failing two `build_project` tests with `false == true`, never mentioning disk** — check `df -h /tmp`) plus **two retired flakes that are NOT landmines**: [environment quirks](../record/2026-10.md#environment-quirks-and-retired-landmines-moved-out-of-the-handoff-2026-10-24).
- **Screen-space (non-MVP) shaders must compensate for the negative-height (Y-flipped) viewport:** `clip.y = 1 - 2*ny` for a top-left origin. `ui.vert` does, guarded by `tests/gpu/offscreen_ui_orientation_test.cpp` (mutation-proven). `screen_effect.vert` does NOT — genuinely wrong but **not pixel-observable**, so read the backlog before "fixing" it.

## Pointers

- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
