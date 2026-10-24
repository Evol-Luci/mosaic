# Re-derive the Effort vs. impact tiers — design

**Date:** 2026-10-23
**Status:** design. Docs-only slice; no code, no CI. The oracle and the predictions below were
written and committed BEFORE any file under `docs/project/` or `docs/record/` was changed.

## The problem

The "Effort vs. impact" section of `docs/project/deferred-work-backlog.md` (line 2176 to EOF,
~16.6 KB, 241 lines) is meant to answer *what is worth doing next*. It does not: 🟢 holds two
live rows, 🟡 holds none (its one row is shipped, #273), 🔴 holds nine mostly-unsized strategic
rows. The subsystem sections above hold 147 live top-level bullets the tiers do not cover. And the
section is a stratigraphy: six dated "Re-derived" notes stacked on each other, most of their tables
fully struck. A readable tier table cannot be written underneath that.

## The premise, as verified (2026-10-23, against `main` at `93e1d48`)

| Claim | Verdict |
|---|---|
| Last re-derived 2026-10-20 | **True** (the note at the section's line ~2246; its PR is #266). |
| "Twelve PRs have landed since" (`next-steps.md`) | **Stale, and so is the brief's 19.** `gh pr list --state merged` gives 18 merged in #265-#284 (#269 and #270 were closed unmerged: a throwaway probe and a superseded copy of #271). Since the re-derivation's own PR (#266) there are **16**: #267, #268, #271-#284. |
| ~150 live bullets | **147** top-level unstruck bullets above the section (203 total, 56 struck). |
| The standing rule | Found at the "Empty as of 2026-09-20" paragraph: *"Re-derive again once real new work has landed since the last pass -- don't immediately re-run a fourth time expecting different material from the same unchanged source docs."* Obeyed: material is the live bullets and record entries the 16 PRs created, not the specs' Non-goals. |
| `LiveLinkServer` unstruck duplicate at :2032 | **True, and it is one of NINE.** A title-match scan finds 9 live bullets sitting directly under their own struck one-line stub (:445, :1658, :1820, :1880, :1902, :1918, :2013, :2032, :2057). The pattern is older than #280: before it, the struck header line was followed by the unstruck original. |
| `ctest -j` (tier row) not struck but says shipped | **True**, and there is also a *bullet* of the same name in the CI section (:2085) whose title says "SHIPPED 2026-10-21 (#273)" and is unstruck. |
| (not in the brief) | Two more shipped-but-unstruck bullets: `enet_transport_test.cpp hard-codes six UDP ports too` (:2043; shipped by #284, "no fixed port left in that file") and `No way to apply an impulse or set a velocity` (:1502; its own prose says SHIPPED 2026-09-29). |

## Scope

(a) New tier tables that rank live work, each row traceable. (b) Collapse the section's six
re-derivation notes and struck tier rows into the record. (c) Fix the stale entries above, in two
commits so the reviewer can drop the second: **commit A** is the ruled pair (the `LiveLinkServer`
duplicate, `ctest -j`); **commit B** is the same defect found elsewhere (8 more duplicates, the
ENet and impulse leftovers). Out of scope: rewriting subsystem sections, code, CI.

## Method

Candidate set = every live bullet that did not exist before #266 (`git show 763cb63^` vs HEAD,
titles diffed): 3 Rendering, 6 Documentation-tooling, 13 CI-and-build bullets, plus the two
carried ones from the 2026-10-20 pass. Each is rated by effort (hours / one focused slice /
multi-slice) and by whether anything is currently *asking* for it. Items whose own text says
"declined", "accepted", "revisit if X" go to an explicit **decided: no action** table with their
trigger, so they are accounted for without being dressed up as work. The ~110 older bullets are
NOT re-ranked (see Non-goals): the standing rule says the material comes from what shipped.

## Oracle (chosen before measuring)

1. **Traceability.** Every tier row resolves to exactly one live bullet by its bold title (and I
   record `file:line`), or to a named PR / record heading. Untraceable rows are cut. Check:
   a script resolves each row.
2. **Conservation, with a negative control.** The tier rewrite (a)+(b) must leave the live-bullet
   count and the multiset of live titles unchanged. Only the deliberate fixes may change it.
   Pre-registered deltas: **commit A: 147 -> 145** (-1 `LiveLinkServer` duplicate, -1 `ctest -j`
   bullet; the tier row is not a bullet). **Commit B: 145 -> 135** (-8 duplicates, -1 ENet, -1
   impulse). The check must go red on a scratch copy with one live bullet deleted.
3. **Coverage, measured.** Live bullets cited by a tier row, before and after, strict (by title)
   and honest about what is not covered. If "about the same", say so.
4. **No dead anchors.** Every `](../record/...#anchor)` link in the backlog resolves to a heading
   that exists, checked programmatically with GitHub's slug rule, before and after.
5. **Nothing lost.** Every byte of narrative removed from the backlog is findable in the record
   (checked by a distinctive-substring search per moved block).
6. **Budgets.** `next-steps.md` <= 8,192 bytes.

## Predictions (written before implementing)

| Quantity | Predicted |
|---|---|
| Live bullets | 147 now; 145 after A; **135 after B** |
| Genuine new ranked tier items | **6** actionable (2-3 green, 3 yellow, 0-1 new red), plus ~14 in "decided: no action" |
| Strict coverage | 3 of 147 before (2%); after, >= 25 live bullets cited (about 19% of 135) |
| `deferred-work-backlog.md` | 177,448 B -> ~158,000 B (the section 16.6 KB -> ~9 KB; ~9 duplicate bodies out, ~14 KB) |
| `docs/record/2026-10.md` | 211,321 B -> ~228,000 B; `2026-09.md` roughly +3 KB |
| `next-steps.md` | 6,992 B -> <= 7,600 B |

## Non-goals (each is also in the backlog's Documentation-tooling section)

- Ranking the ~110 older live bullets in the subsystem sections. Why: the standing rule; they
  have no new evidence, and ranking them is per-subsystem judgement.
- Promoting this slice's checks (conservation, anchor, traceability) to `tools/docs/` + CI.
  Why: they were written for one pass; a standing check wants the design `next-steps.md` item 3 has.
- Teaching `archive_shipped_backlog.py` to detect an unstruck original sitting under its own stub,
  and to collapse prose "Re-derived" blocks. Why: found by this slice, fixed by hand here.
