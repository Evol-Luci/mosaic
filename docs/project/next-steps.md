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
| Suite status | all passing (1462/1462, 2.7 s local) | `ctest --preset debug -j"$(nproc)"` | 2026-10-24 |
| CI test step | ~12-17 s (#284: 14.2 s, #285: 13.8 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-23 |
| Build | debug preset | `cmake --preset debug -G Ninja && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

Two candidates, most ready first. The ranking behind them is the "Effort vs. impact" section of the backlog, re-derived 2026-10-23.

### 1. A per-test duration budget in the CI ctest check (green, hours)
- **What:** `TIMEOUT 60` is a hang backstop; a case that quietly grows 40x passes it (the HUD-underlay case did, unseen, for weeks). Fail or annotate any case above a stated fraction of the timeout.
- **Where:** `tools/ci/check_ctest_run.py` already parses per-case seconds from the JUnit file (`parse_junit`, and `--report`); add one threshold there and wire it in the "Unit tests" step of `.github/workflows/ci.yml`. Backlog entry: "A per-test duration budget" (CI and build section).
- **Gate:** red against a synthetic JUnit with one case over the budget, plus the existing `tools/ci/test_check_ctest_run.py` still green.
- **Unverified:** the threshold. Slowest case is ~1.1 s since #274, so anything from 5 s to 15 s works; pick from a few CI runs, not from one.

### 2. A checker for this file's verifiable claims (deferred non-goal of the docs-record spec)
- **What:** run every command in the State table and fail on drift. It would have caught the gate that stayed stale for six weeks.
- **Where:** new, under `tools/docs/`; wire it beside the other docs checks in `.github/workflows/ci.yml`.
- **Gate:** it must go red against a deliberately wrong claim.
- **Unverified:** how to make the CI-step and `main` rows machine-checkable.

## Landmines

Things that waste an hour and that reading the code will not reveal.

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate docs. `git add -A`, re-run the FULL commit. `--amend` silently rewrites the previous commit; `--no-verify --amend` hides that it did.
- **A GUI run under `timeout` passes with exit 124**, not 0. `mosaic run --window` has no natural exit.
- **A wrong CLI subcommand exits non-zero with ZERO validation errors**, which looks exactly like a clean run. Verify the subcommand exists before trusting a quiet failure.
- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under memory pressure. Grep for a real compiler error; if there is none, re-run the build.
- **A failed link leaves `ctest -N` reporting the PREVIOUS build's test count**, because the count comes from the discovery file regenerated after a successful link. This reads exactly like the State table being wrong: an unbuilt or half-built tree reported 1459 against a correct claim of 1461. Build successfully first, then trust the count.
- **Nothing in the suite creates a `VkDevice`.** Renderer-backend behaviour is checkable only by eye; green says nothing about the pixels. Sync validation is never enabled either, so the gate cannot see synchronisation bugs.
- **Editing `build_default_scene` breaks four app tests asserting entity counts:** `demo_scene_test.cpp:56` and `:71`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four and register any new component.
- **Adding a field to a component silently re-binds `app/run/demo_scene.hpp`'s positional brace initialisers.** gcc only catches it when the types differ.
- **`TEST_CASE` names:** must not start with `--`, must have balanced `[`, and a comma must be escaped `\,` when filtering from a script. CTest discovery breaks otherwise; CI lints it.
- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.
- **The sandbox cannot screenshot the GUI.** Verify a windowed run functionally (exit 124 under `timeout`, silent stderr); do not chase `hyprctl`/`grim`.
- **Cross-process loopback sockets here are unpredictably slow** (seconds to 20+ s for a first packet). A sandbox property, not an engine bug.
- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** Fixed for world text in #275; `draw_particles` and `draw_ribbons` are now chunked per call as well, so all three per-frame staging paths (`draw_particles`, `draw_ribbons`, `draw_world_text`) allocate a buffer per call and reset their call counters in `begin_frame`.
- **After a squash merge leaves local `main` diverged, `git reset --keep origin/main`**, never `--hard`.
- **Screen-space shaders must compensate for the negative-height (Y-flipped) viewport.** `ui.vert` does so explicitly; `screen_effect.vert` does not, invisible there only because a vignette is radially symmetric. A non-MVP shader needs `clip.y = 1 - 2*ny` for a top-left origin, or it ships upside-down and no test can see it.
- **`blender --background` cannot import the stdlib here** (`math`, `subprocess` fail even with a clean env). A broken Blender install, not a project or script bug; do not re-diagnose it as one.

Two threaded-test flakes recorded in older notes are **not** landmines: the `PhysicsSystem::raycast` cross-thread case was fixed in #230, and the threaded-`ScriptSystem` SIGSEGV cannot occur under `ctest` (one process per case). If either goes red, it is a real finding.

## Pointers

- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
- Closed, historical: [`docs/updates/`](../updates/)
- `next-steps.md` is a rewritten-in-place pointer, not a log. Put narrative in the record.
