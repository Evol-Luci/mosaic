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
| Suite size | 1462 tests | `ctest --preset debug -N \| tail -1` (**build first** — see landmines) | 2026-10-24 |
| Suite status | all passing (1462/1462, 3.7 s local) | `ctest --preset debug` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-24 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

No single candidate is currently queued here. See the tier tables in [`deferred-work-backlog.md`](deferred-work-backlog.md#effort-vs-impact) (`🟡 Medium` and `🔴 Large`) for what is next.

## Landmines

Things that waste an hour and that reading the code will not reveal.

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate docs. `git add -A`, re-run the FULL commit. `--amend` silently rewrites the previous commit; `--no-verify --amend` hides that it did.
- **A GUI run under `timeout` passes with exit 124**, not 0. `mosaic run --window` has no natural exit.
- **A wrong CLI subcommand exits non-zero with ZERO validation errors**, which looks exactly like a clean run. Verify the subcommand exists before trusting a quiet failure.
- **The presets pin `Ninja` since 2026-10-24.** A `build/debug` left over from before that (Unix Makefiles) makes `cmake --preset debug` fail with "does not match the generator used previously". Delete `build/debug` once. The memory-pressure link failure in the next landmine is a LINK-step hazard only; it does not apply to `ctest -j`, whose heaviest case peaks at ~85 MB.
- **Four targets use a stdlib-only PCH since 2026-10-24** (`MOSAIC_PCH_HEADERS`, root `CMakeLists.txt`), and **CI deliberately builds WITHOUT it** (`-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON`). Do not remove that flag: ccache cannot cache a PCH-consuming compile, and leaving it on made 243/456 calls uncacheable and took CI from 1m38s to 6m49s. Never put a project header in the PCH — it would be invalidated by the edit it exists to make cheap. A missing `#include` can compile locally and fail in CI; that is intended, and CI is the guard. Reasoning: [record](../record/2026-10.md#reduce-header-fan-out--precompiled-headers--unity-builds).
- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under memory pressure. Grep for a real compiler error; if there is none, re-run the build.
- **A failed link leaves `ctest -N` reporting the PREVIOUS build's test count**, because the count comes from the discovery file regenerated after a successful link. This reads exactly like the State table being wrong: an unbuilt or half-built tree reported 1459 against a correct claim of 1461. Build successfully first, then trust the count.
- **The renderer HAS a regression gate, and it is not `ctest`.** `timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor` must exit **124** (no natural exit, so the timeout IS the pass) and print exactly three `[INFO]` lines — zero VUIDs, zero ERROR, zero WARN, with *synchronisation* validation on. `diff` the log against that baseline after any renderer change: it catches a broken object graph or a bad barrier, which the suite cannot. Used to verify the target-abstraction refactor was behaviour-preserving. Pixels are still eye-only; this is not.
- **The default suite creates no `VkDevice`,** so renderer behaviour is eye-only and green says nothing about pixels. Sync validation is off unless `MOSAIC_VK_SYNC_VALIDATION` is set. Since 2026-10-24 a headless device and a pixel readback DO exist, opt-in and off by default: `cmake -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMOSAIC_ENABLE_GPU_TESTS=ON && ctest --test-dir <dir> -R gpu`. Opt-in because CI has no GPU and a Catch2 `SKIP` trips `check_ctest_run.py`'s O2 — **do not loosen O2 to change that.** It asserts a *cleared* image, not the renderer's draw paths, so the Y-flip, buffer-aliasing and HUD-underlay classes below are still eye-only. Reasoning: [record](../record/2026-10.md#a-headless-vkdevice-for-tests-slice-2-offscreen-readback-and-the-first-asserted-pixel).
- **Editing `build_default_scene` breaks four app tests asserting entity counts:** `demo_scene_test.cpp:56` and `:71`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four and register any new component.
- **Adding a field to a component silently re-binds `app/run/demo_scene.hpp`'s positional brace initialisers.** gcc only catches it when the types differ.
- **`TEST_CASE` names:** must not start with `--`, must have balanced `[`, and a comma must be escaped `\,` when filtering from a script. CTest discovery breaks otherwise; CI lints it.
- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.
- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** Fixed for world text in #275; `draw_particles` and `draw_ribbons` are now chunked per call as well, so all three per-frame staging paths (`draw_particles`, `draw_ribbons`, `draw_world_text`) allocate a buffer per call and reset their call counters in `begin_frame`.
- **After a squash merge leaves local `main` diverged, `git reset --keep origin/main`**, never `--hard`.
- **Three sandbox-environment quirks** (no GUI screenshots, unpredictably slow cross-process loopback sockets, a broken Blender stdlib) and **two retired flakes that are NOT landmines** live in [environment quirks](../record/2026-10.md#environment-quirks-and-retired-landmines-moved-out-of-the-handoff-2026-10-24). Read that before re-diagnosing any of them as a project bug.
- **Screen-space shaders must compensate for the negative-height (Y-flipped) viewport.** `ui.vert` does so explicitly; `screen_effect.vert` does not, invisible there only because a vignette is radially symmetric. A non-MVP shader needs `clip.y = 1 - 2*ny` for a top-left origin, or it ships upside-down and no test can see it.

## Pointers

- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
- Closed, historical: [`docs/updates/`](../updates/)
- `next-steps.md` is a rewritten-in-place pointer, not a log. Put narrative in the record.
