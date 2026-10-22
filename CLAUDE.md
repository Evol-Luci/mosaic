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

## Where documentation goes

The section above says where *deferred* work goes. This one says where everything
else goes. They are one policy: three documents, three jobs. Putting content in
the wrong one is how both the handoff and the backlog grew past the point of
being readable.

- **`docs/project/next-steps.md` — the handoff.** Pointers only, for an agent
  starting cold: current state with the command that verifies each claim, the
  next two or three slices with their gates, and the landmines. **Keep it under
  8 KB**; if an update would push it over, the content belongs in the record.
  Never append a dated narrative block here.
- **`docs/project/deferred-work-backlog.md` — pending work only.** One entry per
  separable follow-up, with why it was deferred and what it depends on. When an
  item ships, its narrative moves to the record and a **one-line stub** stays
  here, so "did we already consider this?" still answers from this file.
- **`docs/record/YYYY-MM.md` — what happened.** The postmortem for a shipped
  slice: what was decided, what was measured, which of its own predictions were
  wrong. Entries whose date was never recorded live in `docs/record/undated.md`.
  This is the one that grows forever, and that is correct.

`docs/updates/` is closed; do not add files there.

**Why the split exists.** Only the record legitimately grows forever. Any
document that mixes it with something else inherits unbounded growth, which is
how the handoff and the backlog both broke within a week of each other. Do not
fold narrative back into either of them.

**The tools exist, and are run deliberately** (not from a hook):
`tools/docs/archive_next_steps.py` rolls old handoff entries into the record, and
`tools/docs/archive_shipped_backlog.py` moves shipped backlog entries into the
record and leaves the stub behind.
