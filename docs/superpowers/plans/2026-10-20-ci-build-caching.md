# CI build caching — Implementation Plan

> **Execution status:** not started. Waiting for the plan to be approved. Slice A is the only part
> that ships a repo change before the Slice B campaign; Slice B is gated on its own floor.

**Goal:** Shorten the wait for CI without ever serving a stale artifact: shallow dependency
clones (Slice A), then a `main`-seeded ccache (Slice B), judged against the floor in the spec.

**Spec:** `docs/superpowers/specs/2026-10-20-ci-build-caching-design.md`

**No unit test can prove any of this**, so each task names the evidence that stands in for the
test. The 1455-case suite must stay 1455/1455; it is run, and says nothing about caching.

## Global constraints

- First-party `actions/*` only (spec, Standing constraints). No third-party action, ever, with cache access.
- Nothing is pushed to `main` by me. Merging is the human's.
- **Runner-image drift.** Print `ImageOS`, `ImageVersion`, `gcc --version`, `ccache --version`,
  `nproc` in every run; record them beside every timing; never average across a change in any of
  them. If "Install Vulkan SDK components" fails, **stop and report**: it is the pre-existing
  `noble`-pinned time bomb, not this slice. Platform failure = zero steps and ~2 s.
- Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and the
  `Claude-Session:` line. The pre-commit hook aborts to regenerate docs: re-run the full commit,
  never `--amend`.
- Deferred work found along the way goes to `docs/project/deferred-work-backlog.md` in the same turn.

## Branches (one deviation from the approval, flagged)

| Branch | Purpose | Reaches `main`? |
|---|---|---|
| `ci/shallow-fetchcontent` | Slice A only | yes, own PR, first |
| `ci/build-caching-experiment` | the whole measurement campaign: probe steps, temporary save condition, scenario commits | **never** (deleted) |
| `ci/build-caching` | Slice B as it will merge: clean, final diff only | yes, own PR |

The approval said to delete probe material "in a commit before the PR is marked ready". Keeping
it on a separate disposable branch instead means it cannot reach `main` by construction, and the
final diff is the whole branch. If you prefer the single-branch shape, say so.
The spec, this plan and the backlog entries (currently commit a38caa9 on `ci/build-caching`) ride
**Slice A's PR**, as the first thing to land.

## Known unknown that changes the probe design

The spec says the scoping probe is a `workflow_dispatch`-only workflow. **I believe, but have not
verified, that GitHub will not dispatch a workflow file that exists only on a non-default
branch.** If true, a separate probe workflow cannot be run without putting it on `main`, which is
forbidden. So the probe is instead a gated block inside the experiment branch's own `ci.yml`
(which does exist on `main` and is dispatchable with `--ref`), triggered by a `workflow_dispatch`
input `probe`. Task P0 settles this in a minute before anything is built on it.

---

## Slice A — shallow FetchContent clones

### A1. Edit and verify locally

- [ ] Branch `ci/shallow-fetchcontent` off `main`.
- [ ] `GIT_SHALLOW TRUE` on the nine tag-pinned declarations (Catch2, tomlplusplus, nlohmann_json,
      CLI11, glfw, miniaudio, enet, imgui, pybind11). **Not** box3d; a comment on box3d's
      declaration says why (SHA pin: not shallow-fetchable in general).
- [ ] **Evidence (stands in for a test) — same sources.** Fresh build directory, fresh configure.
      For each dependency, `git rev-parse HEAD` and `git rev-parse 'HEAD^{tree}'` of the shallow
      checkout must equal those of the full-clone checkout from the current `main`. A differing
      tree hash is a bug in the change. Show the loop.
- [ ] **Evidence — local speed.** `time cmake --preset debug -G Ninja` from scratch, before vs
      after. Spec expectation: about 35-40 s less.
- [ ] Full build and `ctest --test-dir build/debug -j4` : 1455/1455.
- [ ] Check whether anything reads dependency git metadata (`git describe` in a dependency's
      CMake, version-from-tag); the tree-hash equality covers sources, not `.git`.

### A2. A permanent runner-environment step

- [ ] Add one step to `ci.yml` printing `ImageOS`, `ImageVersion`, `nproc`, `gcc --version`
      (and later `ccache --version`). Cost ~0 s. It is also what the campaign needs. Standing
      diagnostic; say so in the PR.

### A3. CI evidence and PR

- [ ] Push; run `ci.yml` at least 3 times (`gh workflow run ci.yml --ref ci/shallow-fetchcontent`
      and the PR run). Record Configure-step seconds with the A2 environment line beside each.
- [ ] Compare against the baseline of 66-78 s from the five existing runs **only if the image
      matches**; otherwise take a matching baseline run from `main` on the same image.
- [ ] **Floor: configure must drop at least 20 s on CI.** If not, do not open the PR as a win;
      report.
- [ ] Open the PR. Contents: the CMake change, the A2 step, the spec, this plan, the backlog entries.
      Next-steps update rides this PR (per the standing note). Hand over for review.

---

## Slice B — ccache

### P0. Settle the dispatch question (one minute, before building)

- [ ] On the experiment branch, a trivial workflow file with `workflow_dispatch`; try
      `gh workflow run <file> --ref ci/build-caching-experiment`. Expect: refused (not on default
      branch). Result decides the probe shape above. Delete the file in the same push.

### B1. The workflow change (on `ci/build-caching`, final form)

All first-party. In `ci.yml`:
- [ ] Add `ccache` to the existing apt install line.
- [ ] A step computing the key parts: `ImageOS`, `ImageVersion`, `gcc -dumpfullversion`; writes
      `CCACHE_DIR=${{ runner.temp }}/ccache` and the key prefix to `$GITHUB_ENV`.
- [ ] `actions/cache/restore` with `path: ${{ runner.temp }}/ccache`, key
      `ccache-v1-<ImageOS>-<ImageVersion>-gcc<ver>-<sha>`, restore-keys the same minus the sha.
- [ ] Guard step: fail if `ccache --show-config` `cache_dir` differs from the cached path; print
      the directory size after restore.
- [ ] Configure with `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache`
      added to the command line; `CCACHE_COMPILERCHECK=content`, `CCACHE_MAXSIZE=500M`.
- [ ] After build, `if: always()`: `ccache --show-stats`; print the directory size.
- [ ] `actions/cache/save`, `if: success() && github.event_name == 'push' && github.ref == 'refs/heads/main'`.
      Warn if the directory is empty at save time.
- [ ] Nothing added to `CMakeLists.txt` or the presets.

### B2. Experiment branch scaffolding (never merges)

- [ ] `ci/build-caching-experiment` = `ci/build-caching` plus: the save condition widened to the
      experiment ref; the `probe` input block; the stats and hash-printing steps below.
- [ ] A step that prints sha256 of every `.o`, `.a`, `mosaic`, `mosaic_unit_tests` (sorted).
      Used for parity; the diff of two runs' lists is done from `gh run view --log`.

### B3. Scenarios (effectiveness) — only the ones with no data

Cold is the existing five runs. On the experiment branch, one commit per scenario so the cache
from the previous commit is what is restored. Per scenario, at least 3 runs (5 for the single-file
scenario, which decides the floor), each with the environment line:

| # | Scenario | Change | Expected misses |
|---|---|---|---|
| 1 | seed | none | all (cold, saves) |
| 2 | fully warm | re-dispatch same commit | 0 |
| 3 | single file | declaration appended to one test `.cpp` | 1 |
| 4 | mid header | declaration in `engine/render/image.hpp` | 41 |
| 5 | wide header | declaration in `foundation/core/entity_id.hpp` | 171 |

- [ ] **Misses must equal the dependent-TU count.** Fewer = too loose (a correctness bug: stop).
      More = too tight (record).
- [ ] Record per scenario: per-step seconds, restore and save seconds, compressed size, hit rate.
- [ ] Report **achieved vs the spec's predictions and the gap with its cause**, per scenario.

### B4. Correctness probes (all must pass)

- [ ] **Parity on CI:** cold (`CCACHE_DISABLE=1`) vs 100%-hit cached build of the same commit:
      sha256 lists identical. First check CI cold is self-reproducible (two cold builds); if not,
      fall back to test results plus symbol/section content, and say so.
- [ ] **Invalidation:** compiler change (`CXX=clang++`: expect all miss); compile-flag change
      (expect all miss); a dependency tag bump (expect misses only in TUs that include it);
      header edits are covered by B3.
- [ ] **Fail-safe:** (a) empty cache (new epoch); (b) corrupt cache (store with truncated files
      restored; expect misses and a passing, parity-identical build); (c) service unavailable
      (restore step pointed at a dead cache URL with `continue-on-error`; **this simulates an
      outage, it does not prove behaviour in a real one**, and the report says so).
- [ ] **Save guard:** a deliberately failing run where saving is otherwise permitted saves nothing.

### B5. Scoping and eviction

- [ ] Sibling-branch isolation: branch A saves a key; branch B cannot restore it (expected miss).
- [ ] PR-ref scope: a **draft** PR from the experiment branch (`refs/pull/N/merge`) cannot see
      the experiment ref's cache except through base/default scope. *Asks approval to open a
      draft PR; it creates public noise.* Deleted after.
- [ ] `gh api repos/Evol-Luci/mosaic/actions/cache/usage` before, during and after; list caches
      with sizes (`gh api repos/Evol-Luci/mosaic/actions/caches`). Confirm retention and the
      ~10 GB pool from behaviour, since the limit endpoints return HTTP 402.
- [ ] Revisit `max_size` from measured restore+save time; lower if over ~40 s.

### B6. Decision gate

- [ ] Apply the spec's floor to the B3/B4 numbers. **Any failed correctness probe = do not ship.**
      If the floor is missed, write that result up and stop; do not tune the threshold afterwards.

### B7. Clean PR and post-merge verification

- [ ] Final diff on `ci/build-caching` contains only: B1, the results written into the spec
      (achieved, predicted, gap), the next-steps entry, and backlog updates. `git diff main --stat`
      is checked by hand for any probe, experiment condition or scenario edit. The experiment
      branch is deleted.
- [ ] Open the PR; the human merges.
- [ ] **After merge** (cannot be done earlier): the first `main` push is cold and seeds the cache;
      the next PR must restore it. Record that run's hit rate. This is the real proof of
      main-to-PR scoping.
- [ ] Weekly for a month: cache count and bytes, and a representative PR's hit rate, recorded in
      `next-steps.md`. Eviction shows up as a trend, not a point.

## Risks

- The image flips mid-campaign (see Global constraints): affected runs are excluded, not averaged.
- First merge is cold, and so is any run after an image or compiler change. Named in the spec.
- ccache on noble is an older version than the 4.14 measured; behaviour differences show up in B3 and B4.
- Actions cache limits are unreadable through the API; B5 infers them.
