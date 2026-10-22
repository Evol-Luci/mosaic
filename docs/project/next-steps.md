# Next Steps — Build Handoff

**For an agent starting cold.** Read this whole file; it is kept small on purpose.
Narrative (why a decision was made, what was measured, which predictions were
wrong) lives in [`docs/record/`](../record/), read when you work on that subsystem.
Pending work is in [`deferred-work-backlog.md`](deferred-work-backlog.md).

## State

Every claim carries the command that regenerates it. If a command disagrees with
the text, trust the command and fix the text.

| Claim | Value | Verify with | Checked |
|---|---|---|---|
| Suite size | 1459 tests | `ctest --preset debug -N \| tail -1` | 2026-10-21 |
| Suite status | all passing | `ctest --preset debug -j"$(nproc)"` | 2026-10-21 |
| CI test step | ~12-17 s (#278: 12 s, #279: 17 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-21 |
| `main` | `9e689ac` | `git rev-parse --short origin/main` | 2026-10-21 |
| Build | debug preset | `cmake --preset debug -G Ninja && cmake --build --preset debug -j"$(nproc)"` | 2026-10-21 |

## Next work

Three candidates, most ready first.

### 1. Guard `draw_particles` / `draw_ribbons` against a second call per frame (green, hours)
- **What:** both stage into one per-frame buffer, so a second call in a frame aliases the first. Same latent class as the world-text bug fixed in #275.
- **Where:** `modules/render_vulkan/vulkan_render_device.cpp:1505` and `:1554`. Copy the fix pattern from `draw_world_text` (`:1613`): a per-call chunk list, reset in `begin_frame` (`:1883`) after the fence wait.
- **Gate:** a test that fails on the old behaviour. Nothing creates a `VkDevice`, so only engine-side batching is testable; the backend change is checkable by eye.
- **Unverified:** `RenderSystem` makes exactly one call of each per frame (`engine/render/render_system.hpp:380`, `:397`), so this bites only with a second caller or a second pass.

### 2. ENet servers: bind 0 and report the port (yellow)
- **What:** test ports were moved below the ephemeral range in #273; the structural fix is an ENet transport API change, following `LiveLinkServer::port()`.
- **Where:** `tests/unit/studio/live_link_server_test.cpp:185` (the precedent; records a real CI failure on port 34906).
- **Gate:** a test binding 0 and asserting the reported port is non-zero and matches.
- **Unverified:** how many ENet call sites hard-code a port.

### 3. A checker for this file's verifiable claims (deferred non-goal of the docs-record spec)
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
- **Nothing in the suite creates a `VkDevice`.** Renderer-backend behaviour is checkable only by eye; green says nothing about the pixels. Sync validation is never enabled either, so the gate cannot see synchronisation bugs.
- **Editing `build_default_scene` breaks four app tests asserting entity counts:** `demo_scene_test.cpp:56` and `:71`, `populate_world_test.cpp:52`, `scene_save_roundtrip_test.cpp:40`. Fix all four and register any new component.
- **Adding a field to a component silently re-binds `app/run/demo_scene.hpp`'s positional brace initialisers.** gcc only catches it when the types differ.
- **`TEST_CASE` names:** must not start with `--`, must have balanced `[`, and a comma must be escaped `\,` when filtering from a script. CTest discovery breaks otherwise; CI lints it.
- **The editor opens stopped.** No ticks, so lazily-created state (physics bodies, particles, trails) does not exist until Play. A trail records nothing for something that is not moving.
- **The sandbox cannot screenshot the GUI.** Verify a windowed run functionally (exit 124 under `timeout`, silent stderr); do not chase `hyprctl`/`grim`.
- **Cross-process loopback sockets here are unpredictably slow** (seconds to 20+ s for a first packet). A sandbox property, not an engine bug.
- **A backend draw path called once per entity that reuses one per-frame buffer aliases itself.** Fixed for world text in #275; `draw_particles` and `draw_ribbons` still have the shape and are correct only because each is called once per frame.
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
