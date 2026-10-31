# Next Steps — Build Handoff

**For an agent starting cold.** Kept small: narrative (why, what was
measured, which predictions were wrong) lives in [`docs/record/`](../record/), and
pending work in [`deferred-work-backlog.md`](deferred-work-backlog.md).

## State

Every claim carries the command that regenerates it; if they disagree, trust the
command and fix the text.

Rows must be slow-moving: do not re-add a `main` SHA row (#283). A `Value` you change must
carry a `Checked` date no older than your commit — `check_checked_column_staleness.py` fails
the PR otherwise. Re-verifying on the day the cell already names needs no edit.

| Claim | Value | Verify with | Checked |
|---|---|---|---|
| Suite size | 1629 tests (+ **13** `-L gpu`, opt-in) | `ctest --preset debug -N -LE gpu \| tail -1` (**build first** — see [landmines](landmines.md)) | 2026-10-30 |
| Suite status | all passing (1629/1629, 6.0 s local) | `ctest --preset debug -LE gpu` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-30 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

**No time-sensitive item is open.** The #307 GPU-test trial is **kept**: its two lavapipe steps are ordinary CI, so a red one is a finding. The Ubuntu-26 ICD pin re-check survives: backlog.

**U8, U9 and U11 are all COMPLETE.** The demo scene now authors a mid-right **scroll region** — the project's first `UiClipRect` — with clipping, scrolling, a button that scrolls out and a wrapped label. **U8d shipped a measurement and NO cache:** never rule on a debug timing (~17 ms debug vs ~0.7 ms release); the trigger is a counter (`engine/ui/ui_layout_stats.hpp`) above ~200k `visits`/frame. Tiers re-derived 2026-10-26 (#310-#318) and the 🟢/🟡 rows are all maintenance. The next cheapest is the duplicated `tests/gpu/` helpers (`🟢` row 4). [Tiers](deferred-work-backlog.md#effort-vs-impact).

**Eye checks DONE** — six items, all passed 2026-10-28 ([record](../record/2026-10.md)). The scroll offset is **deliberately unsnapped and a human kept it that way**: do not add pixel snapping from the argument alone, it lost to an observation. Still unverified, because nothing authors them: a `WorldLabel` containing `\n` (vertical centring known wrong) and whether `'?'` reads as an error.

### No slice is currently named

**All four slices the #330 handoff named SHIPPED 2026-10-30/31** (#331-#334), each with its
gate flipped and re-run by a reviewer against unmodified code. In order:

1. **The renderer gate asserts it reached a UI frame.** The gate is now
   `ctest --test-dir <dir> -L gpu`, not a GUI run — the GUI run's pass condition changed with
   it (see [landmines](landmines.md)). Killing the `draw_ui` path turns it red where the old
   gate stayed clean.
   [record](../record/2026-10.md#slice-1--the-renderer-gate-learns-to-assert-it-reached-a-ui-frame-2026-10-30)
2. **`tests/gpu/`'s zero-validation assertion is no longer vacuous.** Every GPU test that
   captures validation asserts the layer was ON before asserting it was silent.
   [record](../record/2026-10.md#making-the-gpu-tests-zero-validation-assertion-non-vacuous-2026-10-30)
3. **The `begin_frame` per-call counter resets have a behavioural test.** `StagingChunkInspector`
   on `OffscreenDevice`; deleting any one reset turns exactly its own case red, and the pixel
   tests driving the same paths stay green.
   [record](../record/2026-10.md#the-begin_frame-per-call-counter-resets-get-a-behavioural-test-2026-10-30)
4. **The `Checked` column has a staleness check.** `check_checked_column_staleness.py`, on
   `pull_request`. The property is freshness relative to the change, **not** that the date
   moved — re-verifying on the day the cell already names needs no edit, and the stricter rule
   the handoff asked for was a 7-false-positives-for-1-real-defect trade. Do not tighten it.
   [record](../record/2026-10.md#a-staleness-check-on-next-stepsmds-checked-column-and-the-stricter-rule-that-lost-to-its-own-history)

**Picking the next one is an open decision, not a backlog lookup.** Three candidates the four
slices themselves surfaced, each already carrying a gate: 🟡 row 11 (the value published into
`g_validation_observed` is guarded by no test — the vacuity defect one level up, and the one
thing slice 2 did *not* close), 🟡 row 10 (a viewport-texture readback seam, which is also what
would diagnose the `1 x 1` editor extent slice 1 found), and 🟢 row 4 (the remaining duplicated
`tests/gpu/` pixel helpers). [Tiers](deferred-work-backlog.md#effort-vs-impact).

## Landmines

**Moved to [`landmines.md`](landmines.md)** — read it before trusting memory. It is
the one part of this handoff that grows forever, so it has its own document rather
than being pruned for space; see `CLAUDE.md`, "Where documentation goes".

## Pointers

- **Traps that cost an hour:** [`landmines.md`](landmines.md) — read before trusting memory
- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
