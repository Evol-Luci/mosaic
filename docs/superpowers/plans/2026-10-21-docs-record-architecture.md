# Docs record architecture — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each of the project's three documentation content types its own home, so that the one type which legitimately grows forever stops dragging the other two down with it.

**Architecture:** A new `docs/record/YYYY-MM.md` holds the record (what happened, what was decided, which predictions were wrong). `deferred-work-backlog.md` keeps only live items, with a one-line stub where each shipped entry used to be. `next-steps.md` becomes a 5–8 KB agent-facing pointer document. `CLAUDE.md` is updated so new work lands in the right file and the documents do not regrow.

**Tech Stack:** Python 3 (stdlib only, matching `tools/docs/regenerate_nav.py` and `tools/docs/archive_next_steps.py`), `unittest`, markdown.

**Spec:** `docs/superpowers/specs/2026-10-21-docs-record-architecture-design.md`

## Global Constraints

- **Conservation is the contract.** Any script that moves text between files must verify every entry survives byte-identical exactly once across source-plus-destination, and refuse to write otherwise. The check must have a **negative control** in its tests — proving it reports a dropped, a duplicated and a mutated entry — because a conservation check that cannot fail is worse than none.
- **Nothing is deleted.** Every migration moves content; no task removes an entry outright.
- **No pre-commit hook automation.** Scripts are run deliberately and reviewed in a PR.
- **The pre-commit hook aborts to regenerate docs.** When it does, re-run the *full* commit. Never `git commit --amend`, never `--no-verify --amend`.
- **Stdlib only.** No new dependencies for any tool here.
- **Tools live in `tools/docs/` with a `test_<name>.py` beside them**, matching `test_regenerate_nav.py` and `test_archive_next_steps.py`.
- Run tests with `python3 -m unittest tools/docs/test_<name>.py` from the repo root.

## Review Focus

Five conditions the spec implies that no task's happy path exercises. Each has a test in the task that owns it.

1. **A struck entry with no `SHIPPED <date>` marker.** 55 struck bullets exist; the spec does not promise all are dated. Guessing a date silently files history in the wrong month. Expected: it goes to a dated catch-all, and the run says how many did. *(Task 2, Step 3.)*
2. **A struck entry whose title contains markdown or punctuation that breaks an anchor** (backticks, `/`, `:`). The stub links by anchor; a mangled anchor is a dead link that looks fine. Expected: anchors are slugified deterministically and every stub target resolves. *(Task 2, Step 7.)*
3. **A struck tier-table row.** 14 of them, and they are table rows rather than bullets, so a bullet-only parser silently leaves 8.4 KB behind while reporting success. Expected: rows are migrated too, and the stub remains a valid table row. *(Task 2, Step 5.)*
4. **A non-date-prefixed file in a `newest-first` nav group.** `docs/updates/README.md` has no `YYYY-MM-DD-` prefix; `sort_files` may place it unpredictably or the title deriver may fall back. Expected: the nav regenerates without error and the file appears. *(Task 1, Step 5.)*
5. **Running a migration twice.** Both scripts will be re-run; a second pass must be a no-op rather than double-stubbing or re-appending. Expected: byte-identical output. *(Task 2, Step 8.)*

---

### Task 1: Stand up `docs/record/`

**Files:**
- Create: `docs/record/2026-09.md`, `docs/record/2026-10.md` (moved, via `git mv`)
- Create: `docs/updates/README.md`
- Modify: `tools/docs/archive_next_steps.py` (default archive dir, docstring, pointer text)
- Modify: `tools/docs/regenerate_nav.py:18-26` (add the `record` group)
- Modify: `docs/index.html` (add the `record` marker block by hand)
- Test: `tools/docs/test_archive_next_steps.py` (existing — must stay green)

**Interfaces:**
- Consumes: nothing.
- Produces: `docs/record/` exists and `archive_next_steps.py` writes there by default. Tasks 2 and 3 write into `docs/record/<YYYY-MM>.md`.

- [ ] **Step 1: Move the two monthly archives into the new directory**

```bash
mkdir -p docs/record
git mv docs/updates/2026-09.md docs/record/2026-09.md
git mv docs/updates/2026-10.md docs/record/2026-10.md
ls docs/updates/   # should now show only the eight *-summary.md files
```

- [ ] **Step 2: Mark `docs/updates/` historical**

Create `docs/updates/README.md`:

```markdown
# Historical development summaries (August–September 2026)

**This directory is closed. Do not add files here.**

It holds eight dated development summaries written between 2026-08-16 and
2026-09-10 — retrospective narratives with PR links, in a format no longer used.
They are kept as-is because converting them would be churn for no reader benefit.

The project's record of what happened, what was decided, and which predictions
were wrong now lives in [`docs/record/`](../record/), one file per month.
```

- [ ] **Step 3: Repoint the archive script**

In `tools/docs/archive_next_steps.py`, change the default archive directory and the strings that name it:

```python
# main(), the --archive-dir default:
ap.add_argument("--archive-dir", type=Path, default=root / "docs/record")

# render_live(), the parameter default:
def render_live(header: str, blocks: List[Block], archive_dir_name: str = "record") -> str:
```

Also update the two docstring lines that say `docs/updates/YYYY-MM.md` to say `docs/record/YYYY-MM.md`.

- [ ] **Step 4: Run the existing tests to confirm nothing broke**

Run: `python3 -m unittest tools/docs/test_archive_next_steps.py`
Expected: `OK`, 14 tests. The tests pass an explicit `archive_dir`, so they exercise the function rather than the default — which is why Step 5 checks the default by hand.

- [ ] **Step 5: Add the nav group and verify the regenerated nav (Review Focus 4)**

In `tools/docs/regenerate_nav.py`, add to `GROUPS` above the `updates` line:

```python
GROUPS = [
    ("record", "record", "newest-first"),
    ("updates", "updates", "newest-first"),
    ...
]
```

In `docs/index.html`, add a nav group for the record beside the existing updates group — copy the shape of the updates group, with its own heading and marker pair:

```html
<div class="nav-group">
    <div class="nav-group-title">Record</div>
    <!-- AUTO-GENERATED:record -->
    <!-- END-AUTO-GENERATED -->
</div>
```

`splice_group` raises `RuntimeError: missing marker` if that marker pair is absent, so the regen will fail loudly rather than silently skip.

Run: `python3 tools/docs/regenerate_nav.py && git diff --stat docs/index.html`
Expected: the Record group lists `2026-10.md` then `2026-09.md`; the Updates group lists the eight summaries **and** `README.md`. Confirm `README.md` actually appears and the command exits 0 — it has no `YYYY-MM-DD-` prefix, so if `sort_files` or `derive_title` mishandles it, this is where it shows. If it is dropped or the run errors, give the file a date prefix (`2026-10-21-historical-notice.md`) and re-run rather than changing the sorter.

- [ ] **Step 6: Verify the archive script still round-trips with the new default**

Run: `python3 tools/docs/archive_next_steps.py --today 2026-10-21 --dry-run`
Expected: `0 archived` (everything live is already current-month and under the ceiling), and the paths it names are under `record/`, not `updates/`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "docs: stand up docs/record/ and close docs/updates/

The record -- what happened, what was decided, which predictions were
wrong -- is the one content type that legitimately grows forever, and it
now has its own directory instead of sharing one with eight historical
dev summaries in a different format.

docs/updates/ keeps those eight files and is closed to new writes."
```

---

### Task 2: Move shipped backlog entries into the record

**Files:**
- Create: `tools/docs/archive_shipped_backlog.py`
- Create: `tools/docs/test_archive_shipped_backlog.py`
- Modify: `docs/project/deferred-work-backlog.md` (by running the tool)
- Modify: `docs/record/2026-*.md` (by running the tool)

**Interfaces:**
- Consumes: `docs/record/<YYYY-MM>.md` exists (Task 1).
- Produces: `parse_items(text) -> (preamble, List[Item])`, `plan(struck) -> ([], {month: List[Item]})`, `verify_conservation(before, moved) -> List[str]`, `slug(title) -> str`, `stub_for(item, record_name) -> str`, `run(doc, record_dir, dry_run) -> int`. `Item` is `NamedTuple(kind, struck, title, date, text, section)` where `kind` is `"bullet"` or `"row"`, `date` is `None` when the entry carries no SHIPPED date, and `.month` yields `YYYY-MM` or `arc.UNDATED_FILE`.

- [ ] **Step 1: Write the failing test for parsing struck bullets and rows**

Create `tools/docs/test_archive_shipped_backlog.py`:

```python
"""Unit tests for tools/docs/archive_shipped_backlog.py.

Conservation is the contract; test_conservation_catches_* are its negative
control. Without them verify_conservation could `return []` unconditionally and
every other test here would still pass.
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import archive_shipped_backlog as arc

LIVE = "- **A live item** that has not shipped.\n  More detail on a second line."
SHIPPED = ("- ~~**Exposure control**~~ — **SHIPPED 2026-10-07.** The framing was wrong;\n"
           "  the UBO grew by a vec4 instead.")
UNDATED = "- ~~**An old idea**~~ — SHIPPED, but nobody wrote down when."
ROW = ("| ~~**Pointer input from the viewport**~~ — **SHIPPED 2026-10-07**, play-mode only. "
       "| Not the one-liner it resembles. |")
ROW_LIVE = "| **`ctest -j` in CI** | The serial step is the largest fixed cost. |"

DOC = f"""# Deferred work

## Rendering

{LIVE}
{SHIPPED}
{UNDATED}

### Tier table

| Item | Why |
|---|---|
{ROW}
{ROW_LIVE}
"""


class ParseTest(unittest.TestCase):
    def test_finds_struck_bullets_and_rows(self):
        _, items = arc.parse_items(DOC)
        self.assertEqual(len([i for i in items if i.struck]), 3)
        self.assertEqual(len([i for i in items if i.kind == "row" and i.struck]), 1)

    def test_a_live_bullet_is_not_struck(self):
        _, items = arc.parse_items(DOC)
        live = [i for i in items if not i.struck]
        self.assertTrue(any("A live item" in i.text for i in live))

    def test_multiline_bullet_keeps_its_continuation(self):
        _, items = arc.parse_items(DOC)
        exposure = next(i for i in items if "Exposure control" in i.text)
        self.assertIn("UBO grew by a vec4", exposure.text)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python3 -m unittest tools/docs/test_archive_shipped_backlog.py`
Expected: FAIL — `ModuleNotFoundError: No module named 'archive_shipped_backlog'`

- [ ] **Step 3: Add the date test, including the undated case (Review Focus 1)**

Append to the test file:

```python
class DateTest(unittest.TestCase):
    def test_shipped_date_decides_the_month(self):
        _, items = arc.parse_items(DOC)
        exposure = next(i for i in items if "Exposure control" in i.text)
        self.assertEqual(exposure.date, "2026-10-07")

    def test_undated_shipped_entry_is_not_guessed(self):
        _, items = arc.parse_items(DOC)
        old = next(i for i in items if "An old idea" in i.text)
        self.assertIsNone(old.date)

    def test_undated_entries_go_to_a_catch_all_month(self):
        _, items = arc.parse_items(DOC)
        _, moved = arc.plan([i for i in items if i.struck])
        self.assertIn(arc.UNDATED_FILE, moved)
        self.assertEqual(len(moved[arc.UNDATED_FILE]), 1)
```

- [ ] **Step 4: Write the parser and the planner**

Create `tools/docs/archive_shipped_backlog.py`:

```python
#!/usr/bin/env python3
"""Move SHIPPED (struck-through) entries out of the deferred-work backlog into the record.

WHY. Half of docs/project/deferred-work-backlog.md -- 111 KB of 226 KB, 55 struck
bullets and 14 struck tier rows -- is work already done. It is kept deliberately:
each entry records what actually happened and which of its own predictions were
wrong. That is the RECORD, not pending work, and it belongs in docs/record/.

A DATE-based split of this file would hide live work, which is why this one is
STATUS-based: a struck-through entry is by definition not live, so moving it cannot
hide anything pending.

Every moved entry LEAVES A ONE-LINE STUB. "Did we already consider this?" is a real
job this file does, and it needs the entry discoverable where someone looks rather
than only where it was filed.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Tuple

STRUCK = re.compile(r"~~")
SHIPPED_DATE = re.compile(r"SHIPPED\s+(20\d\d-\d\d-\d\d)")
TITLE = re.compile(r"~~\*\*(.+?)\*\*~~")
UNDATED_FILE = "undated"


class Item(NamedTuple):
    kind: str              # "bullet" | "row"
    struck: bool
    title: Optional[str]
    date: Optional[str]    # YYYY-MM-DD, or None when the entry carries no SHIPPED date
    text: str
    section: str

    @property
    def month(self) -> str:
        return self.date[:7] if self.date else UNDATED_FILE


def _mk(kind: str, text: str, section: str) -> Item:
    struck = bool(STRUCK.search(text))
    m = SHIPPED_DATE.search(text)
    t = TITLE.search(text)
    return Item(kind, struck, t.group(1) if t else None,
                m.group(1) if m else None, text, section)


def parse_items(text: str) -> Tuple[str, List[Item]]:
    """Split the backlog into (preamble, items). Bullets and table rows only."""
    lines = text.split("\n")
    items: List[Item] = []
    section = ""
    cur: Optional[List[str]] = None
    preamble_end = None

    def flush():
        nonlocal cur
        if cur:
            items.append(_mk("bullet", "\n".join(cur).rstrip(), section))
            cur = None

    for i, l in enumerate(lines):
        if l.startswith("## "):
            flush()
            section = l[3:]
            continue
        if l.startswith("|"):
            flush()
            if not re.match(r"^\|\s*-+", l) and "| Item |" not in l:
                items.append(_mk("row", l, section))
            continue
        if l.startswith("- "):
            flush()
            if preamble_end is None:
                preamble_end = i
            cur = [l]
        elif cur is not None and (l.startswith("  ") or not l.strip()):
            cur.append(l)
        else:
            flush()
    flush()
    return "\n".join(lines[: preamble_end or 0]), items


def plan(struck: List[Item]) -> Tuple[List[Item], Dict[str, List[Item]]]:
    """Group struck items by the month they shipped; undated go to a catch-all."""
    moved: Dict[str, List[Item]] = {}
    for it in struck:
        moved.setdefault(it.month, []).append(it)
    return [], moved
```

- [ ] **Step 5: Run the parse and date tests**

Run: `python3 -m unittest tools/docs/test_archive_shipped_backlog.py -v`
Expected: the `ParseTest` and `DateTest` cases PASS. Review Focus 3 is covered by `test_finds_struck_bullets_and_rows` asserting exactly one struck **row** — a bullet-only parser fails it.

- [ ] **Step 6: Write the failing conservation tests (the negative control)**

Append:

```python
class ConservationTest(unittest.TestCase):
    def _split(self):
        _, items = arc.parse_items(DOC)
        struck = [i for i in items if i.struck]
        _, moved = arc.plan(struck)
        return struck, moved

    def test_clean_split_reports_no_problems(self):
        struck, moved = self._split()
        self.assertEqual(arc.verify_conservation(struck, moved), [])

    def test_conservation_catches_a_dropped_entry(self):
        struck, moved = self._split()
        moved[next(iter(moved))] = []
        self.assertTrue(arc.verify_conservation(struck, moved))

    def test_conservation_catches_a_duplicated_entry(self):
        struck, moved = self._split()
        k = next(iter(moved))
        moved[k] = moved[k] * 2
        self.assertTrue(arc.verify_conservation(struck, moved))

    def test_conservation_catches_a_mutated_entry(self):
        struck, moved = self._split()
        k = next(iter(moved))
        moved[k] = [moved[k][0]._replace(text=moved[k][0].text + " tampered")]
        self.assertTrue(arc.verify_conservation(struck, moved))
```

Run: `python3 -m unittest tools/docs/test_archive_shipped_backlog.py`
Expected: FAIL — `AttributeError: module has no attribute 'verify_conservation'`

- [ ] **Step 7: Implement conservation, slugs and stubs (Review Focus 2)**

Append to `archive_shipped_backlog.py`:

```python
def slug(title: str) -> str:
    """Deterministic anchor from an entry title. Backticks, slashes and colons in
    titles would otherwise produce anchors that look fine and resolve nowhere."""
    s = re.sub(r"[`*_]", "", title.lower())
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s or "entry"


def verify_conservation(before: List[Item], moved: Dict[str, List[Item]]) -> List[str]:
    after = [i.text for v in moved.values() for i in v]
    problems: List[str] = []
    seen: Dict[str, int] = {}
    for t in after:
        seen[t] = seen.get(t, 0) + 1
    for it in before:
        n = seen.get(it.text, 0)
        if n == 0:
            problems.append(f"LOST: {it.title or it.text[:50]!r}")
        elif n > 1:
            problems.append(f"DUPLICATED x{n}: {it.title or it.text[:50]!r}")
    known = {i.text for i in before}
    for t in after:
        if t not in known:
            problems.append(f"ALTERED or INVENTED: {t[:60]!r}")
    return problems


def stub_for(it: Item, record_name: str) -> str:
    """The one line left where the entry used to be."""
    anchor = f"../record/{record_name}.md#{slug(it.title or 'entry')}"
    when = f" {it.date}" if it.date else ""
    title = it.title or "entry"
    if it.kind == "row":
        return f"| ~~**{title}**~~ — SHIPPED{when}, see [record]({anchor}). | — |"
    return f"- ~~**{title}**~~ — SHIPPED{when}, see [record]({anchor})."
```

And the anchor test:

```python
class SlugTest(unittest.TestCase):
    def test_backticks_and_punctuation_do_not_break_the_anchor(self):
        self.assertEqual(arc.slug("`ctest -j` in CI"), "ctest-j-in-ci")
        self.assertEqual(arc.slug("UI/UX: the **thing**"), "ui-ux-the-thing")

    def test_stub_anchor_matches_the_heading_run_writes(self):
        # run() writes each entry under "## <title>"; the stub links to
        # slug(title). These must agree, or every stub is a dead link that
        # looks correct. Asserting the stub merely CONTAINS a slug would pass
        # even if the two were computed differently -- so compare both sides.
        _, items = arc.parse_items(DOC)
        it = next(i for i in items if i.struck and i.title)
        heading_anchor = arc.slug(it.title)
        self.assertIn(f"#{heading_anchor})", arc.stub_for(it, "2026-10"))

    def test_a_differing_heading_would_be_caught(self):
        # Negative control for the test above: if run() ever wrote a heading
        # that slugged differently, the anchor check must not still pass.
        _, items = arc.parse_items(DOC)
        it = next(i for i in items if i.struck and i.title)
        self.assertNotIn(f"#{arc.slug(it.title + ' changed')})",
                         arc.stub_for(it, "2026-10"))
```

Run: `python3 -m unittest tools/docs/test_archive_shipped_backlog.py`
Expected: all PASS.

- [ ] **Step 8: Implement `run`, with an idempotency test (Review Focus 5)**

Append the test:

```python
class RunTest(unittest.TestCase):
    def test_is_idempotent(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            doc.write_text(DOC, encoding="utf-8")
            rec = Path(d) / "record"
            arc.run(doc, rec, dry_run=False)
            once = doc.read_text(encoding="utf-8")
            files_once = {p.name: p.read_text(encoding="utf-8") for p in rec.glob("*.md")}
            arc.run(doc, rec, dry_run=False)
            self.assertEqual(doc.read_text(encoding="utf-8"), once)
            self.assertEqual({p.name: p.read_text(encoding="utf-8")
                              for p in rec.glob("*.md")}, files_once)

    def test_a_stub_replaces_each_moved_entry(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            doc.write_text(DOC, encoding="utf-8")
            arc.run(doc, Path(d) / "record", dry_run=False)
            after = doc.read_text(encoding="utf-8")
            self.assertIn("Exposure control", after)          # still findable here
            self.assertNotIn("UBO grew by a vec4", after)      # body has moved
            self.assertIn("A live item", after)                # live untouched
```

Then the implementation:

```python
def run(doc: Path, record_dir: Path, dry_run: bool = False) -> int:
    text = doc.read_text(encoding="utf-8")
    _, items = parse_items(text)
    # An entry already reduced to a stub has no body left to move.
    struck = [i for i in items if i.struck and "see [record](" not in i.text]
    if not struck:
        print(f"{doc.name}: nothing to move")
        return 0
    _, moved = plan(struck)
    problems = verify_conservation(struck, moved)
    if problems:
        print("REFUSING TO WRITE -- the split would not conserve every entry:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print(f"{doc.name}: moving {len(struck)} shipped entries "
          f"({sum(len(i.text) for i in struck) / 1024:.0f} KB)")
    for month in sorted(moved, reverse=True):
        print(f"  record/{month}.md  += {len(moved[month])}")
    if dry_run:
        print("(dry run -- nothing written)")
        return 0

    record_dir.mkdir(parents=True, exist_ok=True)
    for month, entries in moved.items():
        path = record_dir / f"{month}.md"
        existing = path.read_text(encoding="utf-8") if path.exists() else \
            f"# {month} — shipped work\n"
        add = "\n\n".join(f"## {e.title}\n\n{e.text}" for e in entries
                          if f"## {e.title}" not in existing)
        if add:
            path.write_text(existing.rstrip("\n") + "\n\n" + add + "\n", encoding="utf-8")
    new = text
    for it in struck:
        new = new.replace(it.text, stub_for(it, it.month), 1)
    doc.write_text(new, encoding="utf-8")
    return 0


def main(argv: List[str]) -> int:
    root = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--doc", type=Path, default=root / "docs/project/deferred-work-backlog.md")
    ap.add_argument("--record-dir", type=Path, default=root / "docs/record")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    return run(a.doc, a.record_dir, a.dry_run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

Run: `python3 -m unittest tools/docs/test_archive_shipped_backlog.py`
Expected: all PASS.

- [ ] **Step 9: Dry-run against the real backlog and read the output**

Run: `python3 tools/docs/archive_shipped_backlog.py --dry-run`
Expected: it reports moving **about 69 entries** (55 bullets + 14 rows) totalling **about 111 KB**. If the bullet count is near 55 but the row count is 0, the table branch is not firing — stop and fix, do not proceed.

- [ ] **Step 10: Run it, then verify conservation independently of the script**

```bash
git show HEAD:docs/project/deferred-work-backlog.md > /tmp/backlog-before.md
python3 tools/docs/archive_shipped_backlog.py
python3 - <<'PY'
import sys, hashlib
from pathlib import Path
sys.path.insert(0, 'tools/docs'); import archive_shipped_backlog as arc
_, before = arc.parse_items(Path('/tmp/backlog-before.md').read_text(encoding='utf-8'))
struck_before = [i for i in before if i.struck]
after_texts = []
for p in Path('docs/record').glob('*.md'):
    after_texts.append(p.read_text(encoding='utf-8'))
blob = "\n".join(after_texts)
missing = [i.title for i in struck_before if i.text not in blob]
print(f"struck before: {len(struck_before)}   not found in record: {len(missing)}")
print("missing:", missing[:5])
PY
```

Expected: `not found in record: 0`. A non-zero count means history was lost — revert with `git checkout -- docs/` and fix before continuing.

- [ ] **Step 11: Check every stub anchor resolves**

```bash
python3 - <<'PY'
import re
from pathlib import Path
bad = []
for m in re.finditer(r'\[record\]\(\.\./record/([^)#]+)#([^)]+)\)',
                     Path('docs/project/deferred-work-backlog.md').read_text(encoding='utf-8')):
    f, anchor = m.group(1), m.group(2)
    p = Path('docs/record') / f
    if not p.exists():
        bad.append(f"missing file {f}"); continue
    import sys; sys.path.insert(0, 'tools/docs'); import archive_shipped_backlog as arc
    heads = {arc.slug(h) for h in re.findall(r'^## (.+)$', p.read_text(encoding='utf-8'), re.M)}
    if anchor not in heads:
        bad.append(f"{f}#{anchor}")
print(f"dead stub anchors: {len(bad)}"); print(bad[:5])
PY
```

Expected: `dead stub anchors: 0`.

- [ ] **Step 12: Commit**

```bash
git add -A
git commit -m "docs: move shipped backlog entries into the record

Half the backlog (111 KB of 226 KB, 55 struck bullets and 14 struck tier
rows) was work already done. Status-based, not date-based: a struck entry
is by definition not live, so moving it cannot hide pending work.

Each moved entry leaves a one-line stub, because 'did we already consider
this?' is a real job this file does and needs the entry discoverable
where someone looks."
```

---

### Task 3: Rewrite `next-steps.md` to the agent-facing shape

**Files:**
- Modify: `docs/project/next-steps.md` (replace its 28 blocks with a pointer document)
- Modify: `docs/record/2026-10.md` (receives those blocks)

**Interfaces:**
- Consumes: `docs/record/` (Task 1), and a backlog whose live items are now easy to read (Task 2).
- Produces: a ≤ 8 KB `next-steps.md`. Task 4 documents the rule it embodies.

**No script.** This is the judgement task. Nothing is deleted: the blocks move first, so the worst failure mode is a summary that is thin, never lost content.

- [ ] **Step 1: Move every remaining block into the record**

```bash
python3 tools/docs/archive_next_steps.py --today 2026-11-01 --ceiling 1
```

Using a `--today` in the following month makes every October block older-than-current, so ALL of them archive: expect `28 blocks -> 0 live, 28 archived`. (The script's "never empty the live file" guard only applies to the ceiling backstop, which never runs here because the month rule has already emptied it — so do not expect one block to remain.) The `--ceiling 1` is belt-and-braces and changes nothing in this case.

- [ ] **Step 2: Verify conservation, then empty the live file of blocks**

Run the same independent check as Task 2 Step 10, against `/tmp/next-steps-before.md`.
Expected: every block present in `docs/record/`.

- [ ] **Step 3: Write the new document**

Replace the whole of `docs/project/next-steps.md` with this shape. Fill every bracketed value from the repo as it stands — do not copy the examples.

```markdown
# Next Steps — Build Handoff

**For an agent starting cold.** Read this whole file; it is kept small on purpose.
Narrative — why a decision was made, what was measured, which predictions were
wrong — lives in [`docs/record/`](../record/). Pending work is in
[`deferred-work-backlog.md`](deferred-work-backlog.md).

## State

Every claim here carries the command that regenerates it. If a command disagrees
with the text, trust the command and fix the text.

| Claim | Value | Verify with | Checked |
|---|---|---|---|
| Suite size | 1459 tests | `ctest --preset debug -N` | 2026-10-21 |
| Suite status | all passing | `ctest --preset debug -j"$(nproc)"` | 2026-10-21 |
| CI test step | ~11 s | latest run on `main` | 2026-10-21 |
| Build | `cmake --preset debug && cmake --build --preset debug -j"$(nproc)"` | — | 2026-10-21 |

## Next work

Three candidates, most ready first. Each names where it lives and what would
prove it wrong.

### 1. [slice name]
- **What:** [one or two sentences]
- **Where:** `path/to/file.ext:LINE`
- **Gate:** [the observation that would prove it wrong]
- **Unverified:** [what is still unknown, or "nothing"]

## Landmines

Things that waste an hour if you do not know them. Each is a fact about this
repo, not an opinion.

- **`git commit --amend` after a pre-commit abort.** The hook aborts to regenerate
  docs. Re-run the FULL commit; `--amend` silently rewrites the previous commit.
- **A GUI run under `timeout` passes with exit 124.** `mosaic run --window` has no
  natural exit. Exit 0 is not the success signal.
- **A wrong CLI subcommand exits non-zero with zero validation errors**, which
  looks exactly like a clean run. Verify the subcommand exists.
- **`mosaic_unit_tests` (~170 MB) intermittently fails at the LINK step** under
  memory pressure. Grep for a real compiler error; if there is none, re-run.
- **Nothing in the suite creates a `VkDevice`.** Renderer-backend behaviour is
  checkable only by eye; a green suite says nothing about the pixels.
- **Sync validation is never enabled.** The gate cannot see synchronisation bugs.
- [add any other standing trap, with `file:line`]

## Pointers

- Record of what happened: [`docs/record/`](../record/)
- Pending work and tiers: [`deferred-work-backlog.md`](deferred-work-backlog.md)
- Designs: [`docs/superpowers/specs/`](../superpowers/specs/)
- Plans: [`docs/superpowers/plans/`](../superpowers/plans/)
- Closed, historical: [`docs/updates/`](../updates/)
```

- [ ] **Step 4: Check the size budget**

Run: `wc -c docs/project/next-steps.md`
Expected: **under 8192**. If it is over, the narrative has crept back in — move it to the record rather than raising the budget.

- [ ] **Step 5: Verify every command in the State table actually runs**

Run each `Verify with` command and confirm the value matches. A handoff whose verification commands do not work is worse than one with no commands, because it teaches the next reader to stop checking.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "docs: next-steps.md becomes an agent-facing pointer document

Its primary reader starts cold and greps. Claims carry the command that
regenerates them, dates are per claim rather than per file, landmines
carry file:line, and the narrative moved to docs/record/.

A single file-level 'Last updated' let a stale line inherit a fresh
timestamp -- which is how a gate stayed wrong for six weeks."
```

---

### Task 4: Update the house rules

**Files:**
- Modify: `CLAUDE.md` (project instructions at the repo root)

**Interfaces:**
- Consumes: the three documents now have distinct jobs (Tasks 1–3).
- Produces: nothing code-level. This is the task that stops the regrowth.

**This is the task that matters most and is easiest to skip.** Tasks 1–3 are a one-time cleanup. `next-steps.md` grew ~10 KB a day because the conventions told people to write detail there; without this, it regrows.

- [ ] **Step 1: Add the routing rule to `CLAUDE.md`**

`CLAUDE.md` currently has a "Deferred-work capture during planning" section pointing at the backlog. Add a section beside it:

```markdown
## Where documentation goes

Three documents, three jobs. Putting content in the wrong one is how both the
handoff and the backlog grew past the point of being readable.

- **`docs/project/next-steps.md` — the handoff.** Pointers only, for an agent
  starting cold: current state with the command that verifies each claim, the next
  two or three slices with their gates, and the landmines. **Keep it under 8 KB**;
  if an update would push it over, the content belongs in the record. Never append
  a dated narrative block here.
- **`docs/project/deferred-work-backlog.md` — pending work only.** One entry per
  separable follow-up, with why it was deferred and what it depends on. When an
  item ships, its narrative moves to the record and a **one-line stub** stays here,
  so "did we already consider this?" still answers from this file.
- **`docs/record/YYYY-MM.md` — what happened.** The postmortem for a shipped
  slice: what was decided, what was measured, which of its own predictions were
  wrong. This is the one that grows forever, and that is correct.

`docs/updates/` is closed; do not add files there.
```

- [ ] **Step 2: Verify the rule matches what the repo now looks like**

Read the three files and confirm each matches its description. A house rule that
describes a state the repo is not in will be ignored.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: house rule for which document receives what

Tasks 1-3 were the cleanup; this is the fix. next-steps.md grew ~10 KB a
day because the conventions told every slice to write its narrative
there."
```

---

## Self-review notes

- **Spec coverage.** Slice 1 → Task 1; slice 2 → Task 2; slice 3 → Task 3; slice 4 → Task 4. Acceptance criteria map to Task 3 Step 4 (≤ 8 KB), Task 2 Steps 10–11 (conservation, stubs resolve), Task 4 Step 1 (`CLAUDE.md` names all three).
- **The spec's risk "stub anchors can rot"** is covered by Task 2 Step 11 and the `SlugTest` cases.
- **The spec's risk "slice 3 is unscriptable judgement"** is mitigated structurally: Task 3 Step 1 moves the blocks before any summarising, so the task cannot lose content.
- **Not covered by any task, deliberately:** the six non-goals in the spec, each already recorded in the backlog's Documentation tooling section.
