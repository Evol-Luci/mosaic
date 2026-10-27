# Next Steps — Build Handoff

**For an agent starting cold.** Read this whole file; it is kept small on purpose.
Narrative (why a decision was made, what was measured, which predictions were
wrong) lives in [`docs/record/`](../record/), read when you work on that subsystem.
Pending work is in [`deferred-work-backlog.md`](deferred-work-backlog.md).

## State

Every claim carries the command that regenerates it. If a command disagrees with
the text, trust the command and fix the text.

Rows here must be slow-moving. A `main` SHA row was tried and removed the same day (#283):
a row that is wrong more often than right teaches a reader to distrust the stable ones. Do
not re-add it.

| Claim | Value | Verify with | Checked |
|---|---|---|---|
| Suite size | 1546 tests | `ctest --preset debug -N \| tail -1` (**build first** — see landmines) | 2026-10-26 |
| Suite status | all passing (1546/1546, 3.8 s local) | `ctest --preset debug` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-26 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

**No time-sensitive item is open.** The CI GPU-test trial (#307) was **decided 2026-10-26: it stays** (zero flakes in 10 `main` runs; ~4 s of apt). Its two lavapipe steps are ordinary CI now, so a red one is a finding. The Ubuntu-26 ICD pin re-check survives: backlog.

**The tiers were re-derived 2026-10-26** (#310-#318); the 🟢/🟡 rows are all maintenance. Queued: the `begin_frame` counter-reset seam (`🟡` row 5 — the last non-latent way to reintroduce the aliasing class with every test green) and the duplicated `tests/gpu/` helpers (`🟢` row 4). [Tiers](deferred-work-backlog.md#effort-vs-impact).

**U8 layout containers is COMPLETE** (U8a-U8e, [spec](../superpowers/specs/2026-10-25-u8-layout-remainder-decomposition.md)). **U8d shipped a measurement and NO cache:** never rule on a debug timing (~17 ms vs ~0.7 ms release); the trigger is a counter (`engine/ui/ui_layout_stats.hpp`) above ~200k `visits`/frame.

**NEXT: U9 clipping and scroll regions** ([spec](../superpowers/specs/2026-10-26-u9-clipping-scroll-decomposition.md)), chosen 2026-10-26 over U11: **U9a** a clip rect on `RenderDevice` (pixel-asserted, SHIPPED), **U9b** `UiClip` down the hierarchy for draw AND hit-test, **U9c** a wheel delta on `PointerState`, **U9d** `UiScroll`. **U11 runs in parallel** ([spec](../superpowers/specs/2026-10-26-u11-text-quality-decomposition.md)); U11a shipped the UTF-8 decode.

**Owed: three eye checks, in ONE pass** — the uncovered "GLOW", the flow block at upper-centre (U8e), the `field_bool` checkbox. Sharpest: toggle `visible` on a selected `UiPanel`; no gate reaches it.

## Landmines

Things that waste an hour and that reading the code will not reveal.

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate docs. `git add -A`, re-run the FULL commit. `--amend` silently rewrites the previous commit; `--no-verify --amend` hides that it did.
- **A PR with a merge CONFLICT gets NO check run** — not a red one, *none*: a `pull_request` run needs the merge ref, which cannot exist. Reads exactly like CI being broken; reopening and pushing do not help. Cost an hour (#321, 2026-10-26). Check `gh pr view <n> --json mergeable` for `CONFLICTING`, then rebase. `gh workflow run ci.yml --ref <branch>` gives evidence meanwhile.
- **A GUI run under `timeout` passes with exit 124**, not 0. `mosaic run --window` has no natural exit.
- **A wrong CLI subcommand exits non-zero with ZERO validation errors**, which looks exactly like a clean run. Verify the subcommand exists before trusting a quiet failure.
- **Four targets use a stdlib-only PCH; CI deliberately builds WITHOUT it** (`-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`). Never remove that flag (ccache cannot cache a PCH-consuming compile) and never put a project header in `MOSAIC_PCH_HEADERS`. A missing `#include` can compile locally and fail in CI; that is intended. Measurements: [record](../record/2026-10.md#reduce-header-fan-out--precompiled-headers--unity-builds).
- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under memory pressure. Grep for a real compiler error; if there is none, re-run the build.
- **A failed link leaves `ctest -N` reporting the PREVIOUS build's test count** (the count comes from the discovery file, regenerated only after a successful link). This reads exactly like the State table being wrong. Build successfully first, then trust the count.
- **The renderer HAS a regression gate, and it is not `ctest`.** `timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor` must exit **124** (the timeout IS the pass), print three `[INFO]` lines and zero VUIDs/ERROR/WARN. Capture on unmodified `main`, `diff` after any renderer change: it catches a bad object graph or barrier, which the suite cannot. Full: [record](../record/2026-10.md#two-gates-defined-precisely-moved-out-of-the-handoff-2026-10-24).
- **The default suite creates no `VkDevice`,** so pixels are eye-only and a green suite says nothing about them; sync validation is off unless `MOSAIC_VK_SYNC_VALIDATION` is set. Opt-in GPU tests DO cover the real `VulkanRenderDevice` offscreen: `-DMOSAIC_ENABLE_GPU_TESTS=ON`, then `ctest --test-dir <dir> -L gpu` (8 tests). **Do not relax the local opt-in**; CI runs them in a Mesa-lavapipe step, so the rows above exclude them (its unit step filters `-LE gpu`). Pixel inventory: [record](../record/2026-10.md#gpu-pixel-assertion-inventory-2026-10-25).
- **Editing `build_default_scene` breaks four app tests asserting entity counts:** `demo_scene_test.cpp:59` and `:74`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four; register any new component. Three count **Transforms** (a Transform-less entity is free); `:74` counts **scripts**.
- **Adding a field to a component silently re-binds a positional brace initialiser;** gcc only catches it when the types differ. Every UI-component init in `app/run/demo_scene.hpp` is designated since U8b, but `Bloom` is not.
- **`TEST_CASE` names:** must not start with `--`, must have balanced `[`, and a comma must be escaped `\,` when filtering from a script. CTest discovery breaks otherwise; CI lints it.
- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.
- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** All three staging paths allocate per call and reset in `begin_frame` (#275, #287); all three are asserted in pixels by `tests/gpu/offscreen_*_aliasing_test.cpp`. Do not undo that.
- **After a squash merge leaves local `main` diverged, `git reset --keep origin/main`**, never `--hard`.
- **Four sandbox quirks** (no GUI screenshots, slow cross-process loopback sockets, a broken Blender stdlib, and **a full `/tmp` failing two `build_project` tests with `false == true`, never mentioning disk** — check `df -h /tmp`) plus **two retired flakes that are NOT landmines**: [environment quirks](../record/2026-10.md#environment-quirks-and-retired-landmines-moved-out-of-the-handoff-2026-10-24). Read it before re-diagnosing any as a project bug.
- **Screen-space (non-MVP) shaders must compensate for the negative-height (Y-flipped) viewport:** `clip.y = 1 - 2*ny` for a top-left origin. `ui.vert` does, guarded by `tests/gpu/offscreen_ui_orientation_test.cpp` (mutation-proven). `screen_effect.vert` does NOT — genuinely wrong but **not pixel-observable**, so read the backlog before "fixing" it.

## Pointers

- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
