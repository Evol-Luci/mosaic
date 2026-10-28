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
| Suite size | 1621 tests | `ctest --preset debug -N \| tail -1` (**build first** — see [landmines](landmines.md)) | 2026-10-27 |
| Suite status | all passing (1621/1621, 4.4 s local) | `ctest --preset debug` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-27 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

**No time-sensitive item is open.** The #307 GPU-test trial is **kept**: its two lavapipe steps are ordinary CI, so a red one is a finding. The Ubuntu-26 ICD pin re-check survives: backlog.

**U8, U9 and U11 are all COMPLETE.** The demo scene now authors a mid-right **scroll region** — the project's first `UiClipRect` — with clipping, scrolling, a button that scrolls out and a wrapped label. **U8d shipped a measurement and NO cache:** never rule on a debug timing (~17 ms debug vs ~0.7 ms release); the trigger is a counter (`engine/ui/ui_layout_stats.hpp`) above ~200k `visits`/frame. Tiers re-derived 2026-10-26 (#310-#318) and the 🟢/🟡 rows are all maintenance; queued are the `begin_frame` counter-reset seam (`🟡` row 5) and the duplicated `tests/gpu/` helpers (`🟢` row 4). [Tiers](deferred-work-backlog.md#effort-vs-impact).

**Eye checks DONE** — six items, all passed 2026-10-28 ([record](../record/2026-10.md)). The scroll offset is **deliberately unsnapped and a human kept it that way**: do not add pixel snapping from the argument alone, it lost to an observation. Still unverified, because nothing authors them: a `WorldLabel` containing `\n` (vertical centring known wrong) and whether `'?'` reads as an error.

## Landmines

**Moved to [`landmines.md`](landmines.md)** — read it before trusting memory. It is
the one part of this handoff that grows forever, so it has its own document rather
than being pruned for space; see `CLAUDE.md`, "Where documentation goes".

## Pointers

- **Traps that cost an hour:** [`landmines.md`](landmines.md) — read before trusting memory
- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
