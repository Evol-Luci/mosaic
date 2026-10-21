# CI parallel ctest — Implementation Plan

**Goal:** run the CI test step at `-j"$(nproc)"` without ever making CI randomly red, guarded
permanently by an O1/O2 count-and-pass check, and judged against the pre-registered oracle.

**Spec:** `docs/superpowers/specs/2026-10-21-ci-parallel-ctest-design.md` (its Rulings are binding).

## Global constraints

- Edit only `.github/workflows/ci.yml`, `tools/ci/`, the ENet test constants, the stale `TIMEOUT`
  comment, docs. No merge, no settings/secrets/permissions, nothing in `.claude/`. Do not bump the
  Node 20 `actions/*`. Do not raise `TIMEOUT`.
- Print image, compiler and core count beside every CI timing; never average across an
  `ImageVersion` change; LOCAL numbers are shape only.
- Classify every red run (known flake / environmental hazard / real finding) before it is data.
  O1-O3 red is never a flake.
- Commits end with the model trailers; the pre-commit hook aborts to regenerate docs: re-run the full
  commit, never `--amend`. Deferred work goes to the backlog in the same turn.

## Tasks

1. **Guard script (TDD).** `tools/ci/check_ctest_run.py` + `test_check_ctest_run.py`: count == N,
   all passed, cross-check against generated `add_test` count, hard failure on empty/zero/garbage N
   and on a missing JUnit file. Evidence: unit tests; run against a real serial JUnit (1455/1455).
2. **Negative controls (local).** `ctest -j4 -R ENet` makes the guard red (quote the line); empty N
   makes it red.
3. **ENet ports.** `34001-34006 -> 24101-24106`; confirm unprivileged, not in `/etc/services`, not
   in 32768-60999. Rebuild; the six cases pass.
4. **Stale comment.** Correct the `TIMEOUT` headroom comment in `tests/CMakeLists.txt`.
5. **Workflow.** `-j"$(nproc)"`, `--no-tests=error`, `--output-junit`, N from `ctest -N`, the guard,
   `free -m`, the guard's own unit tests as a lint step.
6. **LOCAL shape.** Serial baseline and 30 x `taskset -c 0-3 ctest -j4`: O1-O3 in all 30.
7. **CI campaign.** (a) A throwaway branch with a variant that runs serial then `-j"$(nproc)"` in the
   same job and diffs the JUnit maps (O3, same machine). (b) On the PR branch itself, at least 10
   consecutive runs with the real workflow (O1, O2, O4-O6). (c) A throwaway branch whose workflow
   filters with `-R` to show the guard red on CI, quoting the failing line.
8. **Results into the spec**: achieved beside predicted with the gap and its cause, and the
   breakdown of what a parallel run is now made of (the next lever).
9. **next-steps.md** rides this branch; **push, open the PR, do not merge.**
