# Project docs: separate the record from the handoff and the backlog — design

**Date:** 2026-10-21
**Status:** design, approved in conversation; implementation decomposed into four slices below.

## The problem

Two project documents broke the same way within a week of each other, and the
measurements say they broke for the same reason.

`docs/project/next-steps.md` reached **487 KB across 79 dated entries** — large enough
that tooling refused to open it whole. It was fixed on 2026-10-21 (PR #277) by rolling
old entries into a monthly archive, which took it to 118 KB.

`docs/project/deferred-work-backlog.md` is **226 KB**, and the split is the finding:

| | items | size |
|---|---|---|
| Live, pending | 148 | ~101 KB |
| Shipped, struck through | 55 bullets + 14 tier rows | **~111 KB** |

Half the backlog is work that is already done. It is concentrated: Rendering carries
**50.5 KB of struck content in 5 shipped entries** — about 10 KB of retrospective
narrative each — and the "Effort vs. impact" tier tables are 78% struck by weight. The
live half averages ~700 bytes an item and is healthy.

**The common cause.** Three kinds of content are mixed across three documents, and only
one of the three kinds legitimately grows forever. Any document that mixes it with
something else inherits unbounded growth.

| Type | What it is | Growth | Where it lives today |
|---|---|---|---|
| **A — Pointer / state** | Where we are, what to do next | Bounded; rewritten in place | A few lines of `next-steps.md` |
| **B — Pending work** | Not done, why deferred, what it depends on | Bounded; items leave when done | `deferred-work-backlog.md` |
| **C — The record** | What happened, what was decided, which predictions were wrong | **Unbounded, by design** | **Scattered across all three** |

C is the valuable one and the dangerous one. The house style deliberately records what
each shipped item actually did *and which of its own predictions were wrong*; that is
unusually good institutional memory and none of it should be lost. But it is currently
written into a handoff doc, a backlog, and `docs/updates/`, so every one of those
documents grows without limit and two of them have already failed.

**A duplication this exposes.** The struck Rendering entries narrate the same slices as
the `next-steps.md` blocks about HDR and bloom. Two accounts of the same work, in two
files, free to drift. That is the hazard the project explicitly avoided when it declined
to resume `docs/updates/` as a parallel narrative — and it has been paying it anyway,
in two other files.

**Why the earlier framing was wrong, recorded so it is not repeated.** This backlog
problem was first judged unsolvable because "a date-based archive would hide live work."
That is true, and it is the wrong axis. A **status-based** split cannot hide live work by
construction: a struck-through item is by definition not live. The difficulty was in the
proposed solution, not in the problem.

## Target architecture

Three documents, one content type each.

### A — `docs/project/next-steps.md`: the handoff, for agents

**Decided:** this file's primary reader is an agent starting cold, not a human browsing.
It is a pointer document. No narrative, no postmortems, no measurements beyond the ones
an agent needs to start work.

Target size **5–8 KB** — small enough to read whole. This is a correctness property, not
tidiness: at 120 KB a reader greps, sees fragments, and misses the one line that mattered,
which is how a stale gate survived in this repo for six weeks (see below).

Six requirements, each derived from an observed failure or an observed success during the
2026-10-21 session:

1. **Every claim is verifiable in one command.** "Suite: 1459 tests
   (`ctest --preset debug -N`)" rather than "the test suite is comprehensive." A checkable
   claim fails loudly when it rots; prose rots silently.
2. **Dates per claim, not per file.** A single `Last updated` header lets a stale line
   inherit a fresh timestamp. The claim that misled a reader on 2026-10-21 looked current
   because the file around it was.
3. **A landmine list with `file:line`.** The highest-value content in the document, and
   the part no amount of reading the code reveals: flaky tests by name, `git commit
   --amend` being forbidden after a pre-commit abort, a GUI run under `timeout` passing
   with exit 124, a wrong CLI subcommand exiting non-zero with zero validation errors and
   looking like a pass.
4. **Paths, not descriptions.** `tests/unit/physics/physics_system_test.cpp:601` is worth
   a paragraph of prose to a reader that greps.
5. **Next work with its gate attached** — what, where, what would prove it wrong, and what
   is still unverified. Three candidate slices, not thirty.
6. **No narrative.** Why a decision was made and which predictions were wrong belong to
   the record, read when working on that subsystem. Loading it on every cold start is pure
   cost.

**The evidence for 3.** During the 2026-10-21 session, every landmine briefed to a
delegated subagent came from the agent's own memory directory, *not* from `next-steps.md`.
Those briefs were the difference between a slice that worked and a slice that chased
ghosts. If this file is for agents, it should hold what actually goes into those briefs.

**The evidence for 1 and 2.** The same session opened with a stored claim that the
parallel-`ctest` slice was "gated on two flaky tests." One had been fixed six weeks
earlier in a merged PR; the other could not occur on that path at all. Both facts were in
the repo the whole time. The cost of that handoff was not missing information — it was
confident information that had rotted with nothing to mark it.

### B — `docs/project/deferred-work-backlog.md`: pending work only

Live items stay. Shipped items move to the record **and leave a one-line stub**:

```markdown
- ~~**Exposure control**~~ — SHIPPED 2026-10-07, see [record](../record/2026-10.md#exposure-control).
```

**The stub is required, not cosmetic.** "Did we already consider this?" is a real job the
backlog does, and it needs the entry discoverable *where someone looks* rather than only
where it was filed. 69 stubs (55 bullets + 14 tier rows) at ~100 bytes costs ~7 KB to keep
that working. Moving entries wholesale would silently break a job the document is
currently doing well.

Expected result: **~226 KB → ~120 KB** (226 less the 111 KB of struck bodies, plus ~7 KB of
stubs; the ~115 KB remainder is the 101 KB of live items plus section headers, preamble and
the live tier rows). Nothing lost, nothing live hidden.

### C — `docs/record/YYYY-MM.md`: the record

**New directory, deliberately named.** It holds both streams of C: the dated handoff
entries already archived from `next-steps.md`, and the shipped backlog entries moved out
of the backlog.

`docs/updates/` keeps its eight 2026-08/09 development summaries and **stops being
written to**. It gets a short header saying it is historical and pointing at
`docs/record/`, so nobody appends there by accident. Rewriting those eight files into the
new convention is explicitly not worth the churn.

The record grows forever, and that is correct — it is the only one of the three types
that should.

## Migration, decomposed

Four slices. Each is independently useful and independently revertible; none depends on a
later one.

**Slice 1 — stand up `docs/record/`.** Create the directory, move the two monthly archive
files from `docs/updates/` (`2026-09.md`, `2026-10.md`), add the historical header to
`docs/updates/`, repoint `tools/docs/archive_next_steps.py`'s default archive directory,
and update the doc-hub nav. Conservation check as in PR #277: every entry survives
byte-identical exactly once.

**Slice 2 — move shipped backlog entries into the record.** A script, following
`archive_next_steps.py`: identify struck-through top-level items and struck tier rows,
move the bodies into the matching month file of the record, leave a one-line stub with an
anchor link. Same conservation contract, same negative control — the test must prove the
check reports a dropped, duplicated and mutated entry, not merely that it passes on a good
run. Dating: an entry's `SHIPPED <date>` marker gives the month; entries with no date go
to a dated catch-all rather than being guessed at.

**Slice 3 — rewrite `next-steps.md` to the agent-facing shape.** The largest judgement
call and the one with no script: the current 118 KB of blocks move to the record, and a
5–8 KB pointer document replaces them, built to the six requirements. Worth doing after
slices 1 and 2 so the record already exists to point at.

**Slice 4 — update the house rules.** `CLAUDE.md` currently tells every slice to record
deferred work in the backlog; it must also say where a *shipped* item's narrative goes,
and that `next-steps.md` takes pointers rather than prose. Without this the documents
regrow and slices 1–3 are a one-time cleanup rather than a fix.

**Slice 4 is the one that matters most and is easiest to skip.** Slices 1–3 treat the
symptom; the house rules are the cause. `next-steps.md` accumulated ~10 KB a day because
the convention told people to write detail there.

## Acceptance

- `next-steps.md` ≤ 8 KB and read whole without grepping.
- `deferred-work-backlog.md` contains no non-stub struck entries; size roughly halved.
- Every entry that existed before migration exists exactly once afterwards, byte-identical,
  verified by a conservation check that has a negative control.
- A grep of `docs/project/` still answers "did we consider X?" for every shipped item
  (the stubs).
- `CLAUDE.md` names the destination for each of the three content types.

## Non-goals

Each is recorded in `docs/project/deferred-work-backlog.md` under Documentation tooling.

1. **Rewriting the eight `docs/updates/*-summary.md` files.** A different document type
   (retrospective narrative with PR links); converting them is churn for no reader benefit.
2. **Automating any of this in the pre-commit hook.** That hook already aborts commits to
   regenerate docs; adding steps that move content between files to the same path is a far
   larger surface for surprise, and a file that grows slowly is not urgent.
3. **A tool that verifies the checkable claims in `next-steps.md`.** Requirement 1 makes
   claims verifiable by hand; a checker that runs them and fails on drift is a genuine
   follow-up, and the right time to build it is after the shape has settled.
4. **Splitting the record by subsystem rather than by month.** Monthly matches the existing
   archive and needs no judgement call per entry. Revisit only if reading the record by
   subsystem becomes a real need.
5. **Resolving the overlap between this file and the agent's memory directory.** The
   landmine list duplicates what memory already holds. Stated here as the intended rule —
   *the repo file is authoritative, memory holds only what is personal to how an agent
   works* — but actually reconciling the two is its own piece of work, and doing it badly
   recreates the duplication problem one layer up.
6. **Backfilling a record entry for work that predates the convention.** The record starts
   with what the archive already holds.

## Risks

- **Slice 3 is unscriptable judgement.** Condensing 118 KB of blocks into 8 KB of pointers
  means deciding what a cold reader needs. Mitigation: nothing is deleted — the blocks move
  to the record first (slice 1), so slice 3 can only get the *summary* wrong, never lose
  content.
- **Stub anchors can rot.** A stub links into a record file by anchor; a later edit to the
  record could break it. Mitigation: generate anchors from the entry title at migration and
  have the conservation check verify each stub's target resolves.
- **The house rules may not take.** If slice 4 lands but future slices keep writing
  narrative into `next-steps.md`, this repeats. The ≤ 8 KB acceptance figure is the thing
  to re-measure in a month.
