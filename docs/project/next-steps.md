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
| Suite size | 1621 tests | `ctest --preset debug -N \| tail -1` (**build first** — see [landmines](landmines.md)) | 2026-10-29 |
| Suite status | all passing (1621/1621, 4.6 s local) | `ctest --preset debug` (the preset carries `jobs: 8`; do NOT add `-j`) | 2026-10-29 |
| CI test step | ~11-18 s (#289: 18 s, #291: 11 s, #292: 16 s) | `gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name=="Unit tests")'` | 2026-10-24 |
| Build | debug preset | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | 2026-10-23 |

## Next work

**No time-sensitive item is open.** The #307 GPU-test trial is **kept**: its two lavapipe steps are ordinary CI, so a red one is a finding. The Ubuntu-26 ICD pin re-check survives: backlog.

**U8, U9 and U11 are all COMPLETE.** The demo scene now authors a mid-right **scroll region** — the project's first `UiClipRect` — with clipping, scrolling, a button that scrolls out and a wrapped label. **U8d shipped a measurement and NO cache:** never rule on a debug timing (~17 ms debug vs ~0.7 ms release); the trigger is a counter (`engine/ui/ui_layout_stats.hpp`) above ~200k `visits`/frame. Tiers re-derived 2026-10-26 (#310-#318) and the 🟢/🟡 rows are all maintenance. Beyond the three slices below, the next cheapest is the duplicated `tests/gpu/` helpers (`🟢` row 4). [Tiers](deferred-work-backlog.md#effort-vs-impact).

**Eye checks DONE** — six items, all passed 2026-10-28 ([record](../record/2026-10.md)). The scroll offset is **deliberately unsnapped and a human kept it that way**: do not add pixel snapping from the argument alone, it lost to an observation. Still unverified, because nothing authors them: a `WorldLabel` containing `\n` (vertical centring known wrong) and whether `'?'` reads as an error.

### The next three slices, in order, each with the gate that ends it

Ordered by how much they unblock, not by size. The first two are the same defect class — an
assertion that cannot fail — and the first one is why several "renderer gate identical" claims
in October's record mean less than they read.

1. **Make the renderer gate assert it reached a UI frame.** Today
   `timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ./build/debug/app/mosaic editor` executes **0**
   UI panel passes (0 at 12 s, 0 at 15 s, 4872 at 18 s), so a green gate cannot be told from a
   startup timeout. **Not just a bigger timeout.**
   **Gate:** forcing a bad scissor (an out-of-area *or* a negative offset in
   `clip_to_pixel_rect`) makes the gate **RED**. Both leave it green today — that is the
   control, and it must flip.
2. **Make `tests/gpu/`'s zero-validation assertion non-vacuous.** Eight assertion sites across
   seven GPU tests assert `validation_messages.empty()`, but the layer is enabled only
   `#ifndef NDEBUG` *and* only when `validation_layer_available()` agrees — so they pass for
   free wherever it is absent.
   **Gate:** the tests assert the layer was **actually enabled** before asserting no messages,
   and deliberately disabling it makes them **fail rather than pass**.
3. **Give the `begin_frame` per-call counter resets a behavioural test**
   (`particle_calls_`, `ribbon_calls_`, `world_text_calls_`, `vulkan_render_device.cpp:1931-1933`).
   Only a source-text contract test covers them; it is the last non-latent route back to the
   per-frame buffer-aliasing class, and omitting a reset grows buffers without changing a pixel.
   **Gate:** deleting one reset turns a test red. **Needs first:** a chunk-count inspection seam
   on `VulkanRenderDevice` or `OffscreenDevice` — that seam is the slice.

## Landmines

**Moved to [`landmines.md`](landmines.md)** — read it before trusting memory. It is
the one part of this handoff that grows forever, so it has its own document rather
than being pruned for space; see `CLAUDE.md`, "Where documentation goes".

## Pointers

- **Traps that cost an hour:** [`landmines.md`](landmines.md) — read before trusting memory
- Record of what happened: [`docs/record/`](../record/) (one file per month)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/); plans: [`docs/superpowers/plans/`](../superpowers/plans/)
