# Next Steps — Build Handoff

**For an agent starting cold.** Read this whole file; it is kept small on purpose.
Narrative (why a decision was made, what was measured, which predictions were
wrong) lives in [`docs/record/`](../record/), read when you work on that subsystem.
Pending work is in [`deferred-work-backlog.md`](deferred-work-backlog.md).

## State

Every claim carries the command that regenerates it. If a command disagrees with
the text, trust the command and fix the text.

Rows here must be slow-moving. A `main` SHA row was tried and removed the same day:
it rots on every commit, `git log` already answers it, and a row that is wrong more
often than right teaches a reader to distrust the ones that are stable. Do not re-add it.

| Claim | Value | Verify with | Checked |
|---|---|---|---|
| Suite size | 1515 tests | `ctest --preset debug -N \| tail -1` (**build first** — see landmines) | 2026-10-25 |
| Suite status | all passing (1515/1515, 4.0-4.5 s local) | `ctest --preset debug` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-25 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

**One time-sensitive item, NOT yet due.** The CI GPU tests are an explicit *trial* (#307), and a trial nobody revisits becomes permanent by default. Gate: ~10-20 `main` runs; **two** as of 2026-10-25, so do not decide yet. When due, check `gh run list --branch main --workflow ci.yml --limit 20` for red in the two lavapipe steps, then judge the ~4 s apt cost **on the principle** — runner variance dwarfs it, so a job total cannot settle it. Criteria: backlog.

**The tiers were re-derived 2026-10-25** (24 PRs of new evidence, #286-#309); the 🟢/🟡 rows are now all maintenance. Queued: the `begin_frame` counter-reset seam (`🟡` row 5 — the last non-latent way to reintroduce the aliasing class with every test green) and extracting the duplicated `tests/gpu/` helpers (`🟢` row 4). Tables: [tiers](deferred-work-backlog.md#effort-vs-impact).

**U8 layout containers is COMPLETE** (U8a-U8d, 2026-10-25; [spec](../superpowers/specs/2026-10-25-u8-layout-remainder-decomposition.md)). **U8d resolved with a measurement and NO cache:** a 256-element grid's whole layout pass is ~0.7 ms/frame in a **release** build (~17 ms in debug — never rule on a debug timing), and the shipped scene pays 306 component visits, so the trigger is a committed counter (`engine/ui/ui_layout_stats.hpp`, `tests/unit/ui/ui_layout_cost_test.cpp`: build the index above ~200k `visits`/frame). **So the tiers above, not U8, are the answer to "what is next".**

## Landmines

Things that waste an hour and that reading the code will not reveal.

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate docs. `git add -A`, re-run the FULL commit. `--amend` silently rewrites the previous commit; `--no-verify --amend` hides that it did.
- **A GUI run under `timeout` passes with exit 124**, not 0. `mosaic run --window` has no natural exit.
- **A wrong CLI subcommand exits non-zero with ZERO validation errors**, which looks exactly like a clean run. Verify the subcommand exists before trusting a quiet failure.
- **The presets pin `Ninja` since 2026-10-24.** A `build/debug` left over from before that (Unix Makefiles) makes `cmake --preset debug` fail with "does not match the generator used previously". Delete `build/debug` once.
- **Four targets use a stdlib-only PCH; CI deliberately builds WITHOUT it** (`-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`). Never remove that flag (ccache cannot cache a PCH-consuming compile) and never put a project header in `MOSAIC_PCH_HEADERS`. A missing `#include` can compile locally and fail in CI; that is intended. Measurements: [record](../record/2026-10.md#reduce-header-fan-out--precompiled-headers--unity-builds).
- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under memory pressure. Grep for a real compiler error; if there is none, re-run the build.
- **A failed link leaves `ctest -N` reporting the PREVIOUS build's test count** (the count comes from the discovery file, regenerated only after a successful link). This reads exactly like the State table being wrong. Build successfully first, then trust the count.
- **The renderer HAS a regression gate, and it is not `ctest`.** `timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor` must exit **124** (the timeout IS the pass), print three `[INFO]` lines and zero VUIDs/ERROR/WARN. Capture on unmodified `main`, `diff` after any renderer change: it catches a bad object graph or barrier, which the suite cannot. Full: [record](../record/2026-10.md#two-gates-defined-precisely-moved-out-of-the-handoff-2026-10-24).
- **The default suite creates no `VkDevice`,** so pixels are eye-only and a green suite says nothing about them; sync validation is off unless `MOSAIC_VK_SYNC_VALIDATION` is set. Opt-in GPU tests DO cover the real `VulkanRenderDevice` offscreen: `-DMOSAIC_ENABLE_GPU_TESTS=ON`, then `ctest --test-dir <dir> -L gpu` (8 tests). **Opt-in locally must not be relaxed**; CI runs them anyway in a Mesa-lavapipe step, which is why the rows above exclude them (its unit step filters `-LE gpu`). What is and is not asserted in pixels: [record](../record/2026-10.md#gpu-pixel-assertion-inventory-2026-10-25).
- **Editing `build_default_scene` breaks four app tests asserting entity counts:** `demo_scene_test.cpp:59` and `:74`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four and register any new component. Three count **Transforms** (a Transform-less entity is free); `:74` counts **scripts** — this line said all four counted Transforms until U8c checked it.
- **Adding a field to a component silently re-binds a positional brace initialiser;** gcc only catches it when the types differ. Every UI-component init in `app/run/demo_scene.hpp` is designated since U8b, but `Bloom` is not.
- **`TEST_CASE` names:** must not start with `--`, must have balanced `[`, and a comma must be escaped `\,` when filtering from a script. CTest discovery breaks otherwise; CI lints it.
- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.
- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** All three staging paths allocate per call and reset in `begin_frame` (#275, #287); all three are now asserted in pixels by `tests/gpu/offscreen_{particle,ribbon,world_text}_aliasing_test.cpp`. Do not undo that.
- **After a squash merge leaves local `main` diverged, `git reset --keep origin/main`**, never `--hard`.
- **Four sandbox quirks** (no GUI screenshots, slow cross-process loopback sockets, a broken Blender stdlib, and **a full `/tmp` failing two `build_project` tests with `false == true`, never mentioning disk** — check `df -h /tmp`) plus **two retired flakes that are NOT landmines**: [environment quirks](../record/2026-10.md#environment-quirks-and-retired-landmines-moved-out-of-the-handoff-2026-10-24). Read it before re-diagnosing any as a project bug.
- **Screen-space (non-MVP) shaders must compensate for the negative-height (Y-flipped) viewport:** `clip.y = 1 - 2*ny` for a top-left origin. `ui.vert` does, guarded by `tests/gpu/offscreen_ui_orientation_test.cpp` (mutation-proven). `screen_effect.vert` does NOT — genuinely wrong but **not pixel-observable** (uniform tint, symmetric vignette), so read the backlog before "fixing" it.

## Pointers

- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
- `next-steps.md` is a rewritten-in-place pointer, not a log. Put narrative in the record.
