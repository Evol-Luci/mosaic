# Docs Hub Nav Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A script that regenerates the 6 append-only, directory-mapped nav
groups in `docs/index.html` from the actual `docs/` tree, so the sidebar can
never silently drift from what's actually in the repo again.

**Architecture:** Wrap each of the 6 target `<div class="nav-group">` blocks'
link lists in `<!-- AUTO-GENERATED:key --> ... <!-- END-AUTO-GENERATED -->`
markers (Task 1). A standalone Python script, `tools/docs/regenerate_nav.py`,
scans each mapped directory, derives a title per file (from its first `# `
heading, or a filename-derived fallback), sorts per the group's mode, and
splices the rendered `<a>` list between that group's markers, leaving
everything else in the file (including the 2 hand-curated groups) untouched
(Task 2).

**Tech Stack:** Python 3 (stdlib only: `pathlib`, `re`, `html`, `unittest`).

**Spec:** `docs/superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md`

## Global Constraints

- Only these 6 groups are auto-generated: `updates` (`docs/updates`, sort
  `newest-first`), `architecture` (`docs/architecture`, sort `alphabetical`),
  `design` (`docs/design`, sort `alphabetical`), `rfcs` (`docs/rfcs`, sort
  `alphabetical`), `specs` (`docs/superpowers/specs`, sort `oldest-first`),
  `plans` (`docs/superpowers/plans`, sort `oldest-first`). The `General` and
  `Project & Legal` groups are never touched by this plan.
- Title derivation: a file's first non-blank line, if it starts with `# `, is
  the title verbatim (backticks stripped, nothing else shortened) — this is a
  deliberate, accepted trade-off (some older docs' titles will be more verbose
  than their current hand-picked nav label). Otherwise, fall back to a
  filename-derived title (strip a `YYYY-MM-DD-` prefix and a `-design`/`-plan`
  suffix if present, replace `-`/`_` with spaces, title-case each word) and
  print a stderr warning naming the file.
- `alphabetical` sort is case-insensitive by filename; `oldest-first` sorts
  filenames ascending; `newest-first` sorts filenames descending. Files in
  every one of the 6 mapped directories are date- or number-prefixed, so
  filename sort order is meaningful.
- If `docs/index.html` is missing a marker pair for any of the 6 groups, the
  script exits non-zero with a clear error and writes nothing — never a
  partial regeneration.
- Not wired into CMake/`ctest`/CI — a manually-run script, same tier as
  `tools/blender_live_link/`'s addon (no C++ test coverage either).

---

### Task 1: Mark up `docs/index.html`'s 6 auto-generated groups; document the convention

**Files:**
- Modify: `docs/index.html`
- Create: `docs/design/documentation-tooling.md`

**Interfaces:**
- Consumes: nothing — pure markup/doc addition.
- Produces: the 6 `<!-- AUTO-GENERATED:key --> ... <!-- END-AUTO-GENERATED -->`
  marker pairs Task 2's script depends on finding. Exact keys: `updates`,
  `architecture`, `design`, `rfcs`, `specs`, `plans`.

- [ ] **Step 1: Add marker comments around each of the 6 groups**

This step only wraps EXISTING content in markers — it does not reorder,
retitle, or add/remove any `<a>` line. Task 2's real run is what actually
regenerates the content.

In `docs/index.html`, find the `Updates & Summaries` group:

```html
        <div class="nav-group">
            <h3>Updates &amp; Summaries</h3>
            <a class="nav-link" onclick="loadFile('updates/2026-08-23-summary.md', this)">2026-08-23 · Studio v0.1 Core &amp; Asset Browser</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-21-summary.md', this)">2026-08-21 · Camera &amp; View/Projection</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-18-summary.md', this)">2026-08-18 · Vulkan Rendering &amp; Mesh Draw</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-17-summary.md', this)">2026-08-17 · Foundation &amp; App Shell</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-16-summary.md', this)">2026-08-16 · Engine Core</a>
        </div>
```

and replace it with:

```html
        <div class="nav-group">
            <h3>Updates &amp; Summaries</h3>
            <!-- AUTO-GENERATED:updates -->
            <a class="nav-link" onclick="loadFile('updates/2026-08-23-summary.md', this)">2026-08-23 · Studio v0.1 Core &amp; Asset Browser</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-21-summary.md', this)">2026-08-21 · Camera &amp; View/Projection</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-18-summary.md', this)">2026-08-18 · Vulkan Rendering &amp; Mesh Draw</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-17-summary.md', this)">2026-08-17 · Foundation &amp; App Shell</a>
            <a class="nav-link" onclick="loadFile('updates/2026-08-16-summary.md', this)">2026-08-16 · Engine Core</a>
            <!-- END-AUTO-GENERATED -->
        </div>
```

Find the `Architecture` group:

```html
        <div class="nav-group">
            <h3>Architecture</h3>
            <a class="nav-link" onclick="loadFile('architecture/ARCHITECTURE.md', this)">Architecture</a>
            <a class="nav-link" onclick="loadFile('architecture/data-model.md', this)">Data Model</a>
            <a class="nav-link" onclick="loadFile('architecture/boundaries.md', this)">Boundaries</a>
        </div>
```

and replace it with:

```html
        <div class="nav-group">
            <h3>Architecture</h3>
            <!-- AUTO-GENERATED:architecture -->
            <a class="nav-link" onclick="loadFile('architecture/ARCHITECTURE.md', this)">Architecture</a>
            <a class="nav-link" onclick="loadFile('architecture/data-model.md', this)">Data Model</a>
            <a class="nav-link" onclick="loadFile('architecture/boundaries.md', this)">Boundaries</a>
            <!-- END-AUTO-GENERATED -->
        </div>
```

Find the `Design` group:

```html
        <div class="nav-group">
            <h3>Design</h3>
            <a class="nav-link" onclick="loadFile('design/reflection.md', this)">Reflection</a>
            <a class="nav-link" onclick="loadFile('design/command-event-system.md', this)">Commands &amp; Events</a>
            <a class="nav-link" onclick="loadFile('design/asset-pipeline.md', this)">Asset Pipeline</a>
            <a class="nav-link" onclick="loadFile('design/physics.md', this)">Physics</a>
            <a class="nav-link" onclick="loadFile('design/automation-and-agents.md', this)">Automation &amp; Agents</a>
            <a class="nav-link" onclick="loadFile('design/rendering.md', this)">Rendering</a>
            <a class="nav-link" onclick="loadFile('design/scripting.md', this)">Scripting</a>
        </div>
```

and replace it with:

```html
        <div class="nav-group">
            <h3>Design</h3>
            <!-- AUTO-GENERATED:design -->
            <a class="nav-link" onclick="loadFile('design/reflection.md', this)">Reflection</a>
            <a class="nav-link" onclick="loadFile('design/command-event-system.md', this)">Commands &amp; Events</a>
            <a class="nav-link" onclick="loadFile('design/asset-pipeline.md', this)">Asset Pipeline</a>
            <a class="nav-link" onclick="loadFile('design/physics.md', this)">Physics</a>
            <a class="nav-link" onclick="loadFile('design/automation-and-agents.md', this)">Automation &amp; Agents</a>
            <a class="nav-link" onclick="loadFile('design/rendering.md', this)">Rendering</a>
            <a class="nav-link" onclick="loadFile('design/scripting.md', this)">Scripting</a>
            <!-- END-AUTO-GENERATED -->
        </div>
```

Find the `RFCs` group:

```html
        <div class="nav-group">
            <h3>RFCs</h3>
            <a class="nav-link" onclick="loadFile('rfcs/0000-template.md', this)">RFC Template</a>
            <a class="nav-link" onclick="loadFile('rfcs/0001-core-data-model.md', this)">0001 · Core Data Model</a>
        </div>
```

and replace it with:

```html
        <div class="nav-group">
            <h3>RFCs</h3>
            <!-- AUTO-GENERATED:rfcs -->
            <a class="nav-link" onclick="loadFile('rfcs/0000-template.md', this)">RFC Template</a>
            <a class="nav-link" onclick="loadFile('rfcs/0001-core-data-model.md', this)">0001 · Core Data Model</a>
            <!-- END-AUTO-GENERATED -->
        </div>
```

Find the `Design Specs` group (starts with `<h3>Design Specs</h3>`, ends right
before `<div class="nav-group">` / `<h3>Implementation Plans</h3>`) — it has 24
`<a>` lines, from `superpowers/specs/2026-08-15-mosaic-design.md` through
`superpowers/specs/2026-08-23-studio-asset-browser-design.md`. Insert
`            <!-- AUTO-GENERATED:specs -->` as a new line immediately after its
`<h3>Design Specs</h3>` line, and `            <!-- END-AUTO-GENERATED -->` as
a new line immediately before that group's closing `</div>`. Do not change any
of the 24 existing `<a>` lines.

Find the `Implementation Plans` group (the file's last `nav-group`, 24 `<a>`
lines from `superpowers/plans/2026-08-15-mosaic-docs-hub.md` through
`superpowers/plans/2026-08-23-studio-asset-browser.md`, ending right before
`    </div>` which closes `#sidebar`). Insert
`            <!-- AUTO-GENERATED:plans -->` as a new line immediately after its
`<h3>Implementation Plans</h3>` line, and `            <!-- END-AUTO-GENERATED -->`
as a new line immediately before that group's own closing `</div>`. Do not
change any of the 24 existing `<a>` lines.

- [ ] **Step 2: Verify all 12 markers are present**

```bash
grep -c "AUTO-GENERATED" docs/index.html
```
Expected: `12` (6 start markers + 6 end markers).

```bash
grep -o "AUTO-GENERATED:[a-z]*" docs/index.html
```
Expected: exactly these 6 lines, in this order:
```
AUTO-GENERATED:updates
AUTO-GENERATED:architecture
AUTO-GENERATED:design
AUTO-GENERATED:rfcs
AUTO-GENERATED:specs
AUTO-GENERATED:plans
```

- [ ] **Step 3: Write the design doc**

Create `docs/design/documentation-tooling.md`:

```markdown
# Documentation tooling

`docs/index.html` ("Mosaic — Documentation Hub") is a hand-built single-page
markdown browser: a sidebar `<a onclick="loadFile(...)">` link per doc, grouped
under `<h3>` headers, rendered client-side via marked.js. Serve it over
`http://` (e.g. `python3 -m http.server` from `docs/`) — `fetch()` of local
files is blocked under `file://` in most browsers.

## Auto-generated vs. hand-curated groups

Two groups stay fully hand-maintained: `General` and `Project & Legal`. Both
mix root-level files (`README.md`, `CHANGELOG.md`) with hand-picked docs and
custom labels — low-churn, edited rarely, genuinely want human judgment about
what belongs and how it's titled. Nothing in this repo regenerates them.

The other 6 groups map 1:1 to a `docs/` subdirectory and are regenerated by
`tools/docs/regenerate_nav.py`:

| Group               | Directory                | Sort order    |
|---------------------|---------------------------|---------------|
| Updates & Summaries  | `docs/updates/`            | newest first  |
| Architecture         | `docs/architecture/`       | alphabetical  |
| Design               | `docs/design/`             | alphabetical  |
| RFCs                 | `docs/rfcs/`               | alphabetical  |
| Design Specs         | `docs/superpowers/specs/`  | oldest first  |
| Implementation Plans | `docs/superpowers/plans/`  | oldest first  |

Each group's link list in `docs/index.html` is wrapped in
`<!-- AUTO-GENERATED:key --> ... <!-- END-AUTO-GENERATED -->` markers; the
`<h3>` label itself is never touched by the script (renaming a group is a
deliberate manual edit). Adding a new file to one of the 6 mapped directories
is now enough on its own — no `index.html` edit required — as long as someone
reruns the generator afterward (see below).

## Running it

```bash
python3 tools/docs/regenerate_nav.py
```
Overwrites `docs/index.html` in place, regenerating only the 6 auto-groups.
Safe to run any time — idempotent (running it twice with no doc changes
produces a byte-identical file), and it never touches `General`/
`Project & Legal`. Not wired into the build or CI; rerun it by hand after
adding docs (this is real, acknowledged future work — see the design spec's
Non-goals).

## Title derivation

Each file's title comes from its own first `# ` heading (backticks stripped,
nothing else shortened) — most files in this repo already follow that
convention, so most titles come through verbatim. A file with no `# `-prefixed
first line falls back to a filename-derived title (strips a date prefix and a
`-design`/`-plan` suffix, title-cases the rest) and the generator prints a
warning naming it, since a missing heading is worth a human's attention.

**Accepted trade-off:** this project's oldest specs/plans predate a settled
"clean one-line title" convention, so a few of their auto-derived titles are
more verbose than the hand-picked labels the nav used to show for them (e.g.
`docs/superpowers/plans/2026-08-15-mosaic-docs-hub.md`'s real first line is
`# Project Mosaic — Documentation Set & Docs Hub Implementation Plan`, longer
than the old hand-shortened "Docs Hub Setup"). This is the accepted cost of
full automation — a manual title-override system would reintroduce the same
staleness risk this tool exists to remove.

Full design: `docs/superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md`.
```

- [ ] **Step 4: Commit**

```bash
git add docs/index.html docs/design/documentation-tooling.md
git commit -m "docs(tools): mark up index.html's auto-generated nav groups"
```

---

### Task 2: `tools/docs/regenerate_nav.py`

**Files:**
- Create: `tools/docs/regenerate_nav.py`
- Create: `tools/docs/test_regenerate_nav.py`
- Modify: `docs/index.html` (via running the script for real, not by hand)

**Interfaces:**
- Consumes: Task 1's 6 marker pairs in `docs/index.html`.
- Produces: nothing consumed elsewhere in this plan — Task 3 only adds docs
  and does a functional check, it doesn't import anything from this script.

- [ ] **Step 1: Write the failing tests**

Create `tools/docs/test_regenerate_nav.py`:

```python
"""Unit tests for tools/docs/regenerate_nav.py."""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regenerate_nav as nav


class TitleDerivationTest(unittest.TestCase):
    def test_extracts_h1_verbatim(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.md"
            p.write_text("# Asset Pipeline\n\nBody text.\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Asset Pipeline")
            self.assertFalse(used_fallback)

    def test_strips_inline_backticks(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.md"
            p.write_text("# Engine `World` runtime — design\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Engine World runtime — design")
            self.assertFalse(used_fallback)

    def test_falls_back_to_filename_when_no_heading(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "2026-08-15-mosaic-docs-hub.md"
            p.write_text("Just some prose, no heading.\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Mosaic Docs Hub")
            self.assertTrue(used_fallback)

    def test_filename_fallback_strips_design_suffix(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "2026-08-16-engine-world-runtime-design.md"
            p.write_text("no heading here\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Engine World Runtime")
            self.assertTrue(used_fallback)


class SortModeTest(unittest.TestCase):
    def _files(self, names):
        return [Path(n) for n in names]

    def test_newest_first_reverses_filename_order(self):
        files = self._files([
            "updates/2026-08-16-summary.md",
            "updates/2026-08-23-summary.md",
            "updates/2026-08-21-summary.md",
        ])
        result = [f.name for f in nav.sort_files(files, "newest-first")]
        self.assertEqual(result, [
            "2026-08-23-summary.md",
            "2026-08-21-summary.md",
            "2026-08-16-summary.md",
        ])

    def test_oldest_first_keeps_filename_order(self):
        files = self._files([
            "specs/2026-08-16-x-design.md",
            "specs/2026-08-15-y-design.md",
        ])
        result = [f.name for f in nav.sort_files(files, "oldest-first")]
        self.assertEqual(result, [
            "2026-08-15-y-design.md",
            "2026-08-16-x-design.md",
        ])

    def test_alphabetical_is_case_insensitive(self):
        files = self._files([
            "architecture/data-model.md",
            "architecture/ARCHITECTURE.md",
            "architecture/boundaries.md",
        ])
        result = [f.name for f in nav.sort_files(files, "alphabetical")]
        self.assertEqual(result, [
            "ARCHITECTURE.md",
            "boundaries.md",
            "data-model.md",
        ])


class SpliceGroupTest(unittest.TestCase):
    FIXTURE = (
        "<div class=\"nav-group\">\n"
        "    <h3>Design</h3>\n"
        "    <!-- AUTO-GENERATED:design -->\n"
        "    OLD CONTENT\n"
        "    <!-- END-AUTO-GENERATED -->\n"
        "</div>\n"
        "<div class=\"nav-group\">\n"
        "    <h3>Architecture</h3>\n"
        "    <!-- AUTO-GENERATED:architecture -->\n"
        "    OTHER OLD CONTENT\n"
        "    <!-- END-AUTO-GENERATED -->\n"
        "</div>\n"
    )

    def test_replaces_only_the_named_group(self):
        result = nav.splice_group(self.FIXTURE, "design", "NEW CONTENT")
        self.assertIn("NEW CONTENT", result)
        self.assertNotIn("OLD CONTENT\n    <!-- END", result)
        self.assertIn("OTHER OLD CONTENT", result)

    def test_missing_start_marker_raises(self):
        broken = self.FIXTURE.replace("<!-- AUTO-GENERATED:design -->\n", "")
        with self.assertRaises(RuntimeError):
            nav.splice_group(broken, "design", "NEW CONTENT")

    def test_missing_end_marker_raises(self):
        broken = self.FIXTURE.replace("<!-- END-AUTO-GENERATED -->\n</div>\n"
                                      "<div class=\"nav-group\">\n"
                                      "    <h3>Architecture</h3>",
                                      "</div>\n<div class=\"nav-group\">\n"
                                      "    <h3>Architecture</h3>", 1)
        with self.assertRaises(RuntimeError):
            nav.splice_group(broken, "design", "NEW CONTENT")


class EndToEndSmokeTest(unittest.TestCase):
    def test_regenerates_real_docs_tree_idempotently(self):
        repo_root = nav.REPO_ROOT
        with tempfile.TemporaryDirectory() as d:
            tmp_docs = Path(d) / "docs"
            shutil.copytree(repo_root / "docs", tmp_docs)

            original_docs_dir = nav.DOCS_DIR
            original_index = nav.INDEX_HTML
            nav.DOCS_DIR = tmp_docs
            nav.INDEX_HTML = tmp_docs / "index.html"
            try:
                nav.main()
                first_pass = nav.INDEX_HTML.read_text(encoding="utf-8")
                nav.main()
                second_pass = nav.INDEX_HTML.read_text(encoding="utf-8")
            finally:
                nav.DOCS_DIR = original_docs_dir
                nav.INDEX_HTML = original_index

            self.assertEqual(first_pass, second_pass)
            self.assertIn("design/scripting.md", first_pass)
            self.assertIn(
                "superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md",
                first_pass)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
python3 -m unittest tools.docs.test_regenerate_nav -v
```
Expected: `ModuleNotFoundError: No module named 'regenerate_nav'` (the module
doesn't exist yet).

- [ ] **Step 3: Implement**

Create `tools/docs/regenerate_nav.py`:

```python
#!/usr/bin/env python3
"""Regenerates the 6 auto-generated nav-group sections of docs/index.html from
the actual docs/ tree. See docs/design/documentation-tooling.md for the marker
convention and the group -> directory -> sort-mode table this drives from.

Run from the repo root: python3 tools/docs/regenerate_nav.py
"""
import html
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DOCS_DIR = REPO_ROOT / "docs"
INDEX_HTML = DOCS_DIR / "index.html"

# (marker_key, directory relative to docs/, sort_mode)
GROUPS = [
    ("updates", "updates", "newest-first"),
    ("architecture", "architecture", "alphabetical"),
    ("design", "design", "alphabetical"),
    ("rfcs", "rfcs", "alphabetical"),
    ("specs", "superpowers/specs", "oldest-first"),
    ("plans", "superpowers/plans", "oldest-first"),
]

DATE_PREFIX_RE = re.compile(r"^\d{4}-\d{2}-\d{2}-")


def derive_title(path):
    """Returns (title, used_fallback: bool)."""
    text = path.read_text(encoding="utf-8")
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("# "):
            title = stripped[2:].strip().replace("`", "")
            return title, False
        break  # first non-blank line exists but isn't a heading -- fall back

    stem = path.stem
    stem = DATE_PREFIX_RE.sub("", stem)
    if stem.endswith("-design"):
        stem = stem[: -len("-design")]
    elif stem.endswith("-plan"):
        stem = stem[: -len("-plan")]
    words = re.split(r"[-_]+", stem)
    title = " ".join(w.capitalize() for w in words if w)
    return title, True


def sort_files(files, mode):
    if mode == "alphabetical":
        return sorted(files, key=lambda p: p.name.lower())
    if mode == "oldest-first":
        return sorted(files, key=lambda p: p.name)
    if mode == "newest-first":
        return sorted(files, key=lambda p: p.name, reverse=True)
    raise ValueError(f"unknown sort mode: {mode}")


def render_group(marker_key, rel_dir, mode):
    directory = DOCS_DIR / rel_dir
    files = sort_files(list(directory.glob("*.md")), mode)

    lines = []
    for f in files:
        title, used_fallback = derive_title(f)
        if used_fallback:
            print(f"warning: {f.relative_to(DOCS_DIR)} has no '# ' first "
                 f"line; used filename-derived title '{title}'",
                 file=sys.stderr)
        rel_path = f.relative_to(DOCS_DIR).as_posix()
        escaped_title = html.escape(title)
        lines.append(
            f'            <a class="nav-link" '
            f"onclick=\"loadFile('{rel_path}', this)\">{escaped_title}</a>")
    return "\n".join(lines)


def splice_group(html_text, marker_key, rendered):
    start_marker = f"<!-- AUTO-GENERATED:{marker_key} -->"
    end_marker = "<!-- END-AUTO-GENERATED -->"
    start_idx = html_text.find(start_marker)
    if start_idx == -1:
        raise RuntimeError(f"missing marker '{start_marker}'")
    content_start = start_idx + len(start_marker)
    end_idx = html_text.find(end_marker, content_start)
    if end_idx == -1:
        raise RuntimeError(
            f"missing '{end_marker}' after '{start_marker}'")
    # If this group's own end marker was deleted, .find() above would happily
    # return the NEXT group's end marker instead -- detect that by checking
    # for another group's start marker inside what we're about to replace,
    # rather than silently splicing across a group boundary.
    between = html_text[content_start:end_idx]
    if "AUTO-GENERATED:" in between:
        raise RuntimeError(
            f"'{end_marker}' for '{start_marker}' not found before the next "
            f"group's marker -- likely missing")
    return (html_text[:content_start] + "\n" + rendered + "\n            "
           + html_text[end_idx:])


def main():
    html_text = INDEX_HTML.read_text(encoding="utf-8")
    for marker_key, rel_dir, mode in GROUPS:
        rendered = render_group(marker_key, rel_dir, mode)
        try:
            html_text = splice_group(html_text, marker_key, rendered)
        except RuntimeError as e:
            print(f"error: {e}", file=sys.stderr)
            sys.exit(1)
    INDEX_HTML.write_text(html_text, encoding="utf-8")
    print(f"regenerated {INDEX_HTML}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
python3 -m unittest tools.docs.test_regenerate_nav -v
```
Expected: all test cases pass, including `EndToEndSmokeTest`, which needs
Task 1's markers already present in the real `docs/index.html` — confirm
Task 1 landed first if this fails with a `RuntimeError: missing marker`.

- [ ] **Step 5: Run the generator for real, against the real docs/ tree**

```bash
python3 tools/docs/regenerate_nav.py
```
Expected: prints `regenerated <path>/docs/index.html` to stdout, plus any
`warning:` lines to stderr for files with no `# ` first line (expect none,
per Task 1's Global Constraints note that every file in the 6 mapped
directories was confirmed to already have one during this plan's design).
Inspect the diff:

```bash
git diff docs/index.html
```
Expected: the `Design`, `Design Specs`, and `Implementation Plans` groups gain
new entries (every spec/plan/design-doc created this session that was missing
before), `Architecture` and `RFCs`/`Updates` stay the same or reorder slightly
(per the spec's accepted sort-order trade-off), and nothing outside the 6
marker pairs changes at all.

- [ ] **Step 6: Commit**

```bash
git add tools/docs/regenerate_nav.py tools/docs/test_regenerate_nav.py docs/index.html
git commit -m "feat(tools): add docs hub nav generator; regenerate the 6 auto-groups"
```

---

### Task 3: Docs + functional verification

**Files:**
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: nothing new.
- Produces: nothing consumed elsewhere — final task.

- [ ] **Step 1: Update next-steps.md**

In `docs/project/next-steps.md`, find the `**Last updated:**` line at the top
of the file and prepend a new dated clause (following the same pattern used
for prior entries, keeping the ` ¶ (` separator convention) describing this
work: the 6 auto-generated nav groups and their sort-order table, the marker
convention, the title-derivation rule and its accepted verbose-old-titles
trade-off, that `General`/`Project & Legal` stay hand-curated, and that
reflection-driven API reference docs (part 2 of documentation tooling) remain
future work. Link this plan's spec
(`docs/superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md`).

- [ ] **Step 2: Functional verification (the docs hub needs an HTTP server, not a bare file:// open)**

```bash
cd docs && python3 -m http.server 8099 &
SERVER_PID=$!
sleep 1
curl -sf http://localhost:8099/index.html | grep -q "design/project-templates.md" \
    && echo "PASS: newly-added design doc appears in the regenerated nav" \
    || echo "FAIL: missing from nav"
curl -sf http://localhost:8099/superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md \
    > /dev/null && echo "PASS: the file the new nav entry points at is actually servable" \
    || echo "FAIL: not servable"
kill $SERVER_PID
cd ..
```
Expected: both `PASS` lines print. (This sandbox cannot screenshot or drive a
real browser, so this is a functional check — the page's own JS/marked.js
rendering path was already exercised manually in a real browser by the person
who built `docs/index.html` originally, and this plan doesn't touch that JS at
all, only the nav's static HTML content.)

- [ ] **Step 3: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "docs(next-steps): document the docs hub nav generator"
```
