# CLAUDE.md — Mosaic

## Deferred-work capture during planning

When brainstorming, scoping, or designing anything (a spec, a feature slice, an
architecture decision), the moment a design conversation settles on something to
**not** build now — a v1 simplification, an adjacent feature the user agrees to
skip, a "real but unrelated" fix noticed along the way — write it to
`docs/project/deferred-work-backlog.md` immediately, in the same turn. Don't wait
until the whole design is approved, and don't rely on a later manual re-survey or
the user having to ask for it.

- One entry per genuinely separable follow-up, not a vague catch-all.
- Record *why* it was deferred (time/complexity/missing prerequisite/low priority
  today), not just "later" — and what it depends on, if anything.
- Match the backlog's existing tiered format (`## <subsystem>` sections, plus the
  "Effort vs. impact" 🟢/🟡/🔴 tiers near the bottom) rather than inventing a new
  structure.
- Tell the user where it landed (e.g. "added to the Rendering section of
  `deferred-work-backlog.md`") so it's visibly tracked, not silently dropped.
- When a spec is written, cross-check: everything the spec's own Non-goals
  section names should also appear in the backlog. If the spec has one the live
  capture missed, add it then.

This applies on top of whatever brainstorming skill is active — it isn't
conditional on a specific skill version.
