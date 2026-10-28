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
else goes. They are one policy: **four documents, four jobs.** Putting content in
the wrong one is how both the handoff and the backlog grew past the point of
being readable.

- **`docs/project/next-steps.md` — the handoff.** Pointers only, for an agent
  starting cold: current state with the command that verifies each claim, and
  the next two or three slices with their gates. **Keep it under 8 KB**; if an
  update would push it over, the content belongs in the record. Never append a
  dated narrative block here.
- **`docs/project/landmines.md` — the traps.** Things that waste an hour and that
  reading the code will not reveal, each with the command or `file:line` that gets
  you out. **It has its own document because it is the second thing here that
  grows forever** — one entry per scar, and scars do not stop happening. It lived
  in the handoff until 2026-10-28, by which point it was 5.6 KB of an 8.2 KB
  document and the ceiling was squeezing the one section nobody wanted to cut.
  Retire an entry only when the trap is *gone*, never for space, and record the
  retirement.
- **`docs/project/deferred-work-backlog.md` — pending work only.** One entry per
  separable follow-up, with why it was deferred and what it depends on. When an
  item ships, its narrative moves to the record and a **one-line stub** stays
  here, so "did we already consider this?" still answers from this file.
- **`docs/record/YYYY-MM.md` — what happened.** The postmortem for a shipped
  slice: what was decided, what was measured, which of its own predictions were
  wrong. Entries whose date was never recorded live in `docs/record/undated.md`.
  This is the one that grows forever, and that is correct.

`docs/updates/` is closed; do not add files there.

**Why the split exists.** The record and the landmine list are the two documents
that legitimately grow forever. Any document that mixes one of them with
something else inherits unbounded growth, which is how the handoff and the
backlog both broke within a week of each other, and how the handoff then
breached its ceiling a third time carrying landmines. Do not fold narrative back
into any of them.

**The tools exist, and are run deliberately** (not from a hook):
`tools/docs/archive_next_steps.py` rolls old handoff entries into the record, and
`tools/docs/archive_shipped_backlog.py` moves shipped backlog entries into the
record and leaves the stub behind.
