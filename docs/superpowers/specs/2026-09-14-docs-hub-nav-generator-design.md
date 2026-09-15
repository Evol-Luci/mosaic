# Docs Hub Nav Generator — Design

## Goal (Phase 5, Tool Ecosystem pillar — documentation tooling, part 1 of 2)

`docs/index.html` ("Mosaic — Documentation Hub") is a hand-built single-page
markdown browser: a hardcoded sidebar `<a onclick="loadFile(...)">` link per doc,
grouped under `<h3>` headers. It has drifted badly — the nav lists 84 files while
`docs/superpowers/specs/` + `docs/superpowers/plans/` alone already has 183, and
none of this session's specs/plans (or `docs/design/project-templates.md`, a
design doc) appear at all. This slice adds a script that regenerates the
append-only, directory-mapped parts of the nav from the actual `docs/` tree, so
it can't silently go stale again. Part 2 (reflection-driven API reference docs,
a separate later spec) will emit markdown this generator then picks up
automatically.

## Current state

- `docs/index.html`'s sidebar has 8 `<div class="nav-group">` blocks, each with an
  `<h3>` label and a flat list of `<a class="nav-link"
  onclick="loadFile('<path>', this)"><title></a>` entries. `loadFile` does a
  `fetch(path)` + renders the markdown client-side (marked.js) — the page must be
  served over `http://`, not opened as a `file://` URL (browsers block `fetch` of
  local files under `file://`).
- Diffed every entry in the existing "Design" group against each file's own
  content: **every title matches the file's first `# ` heading verbatim** — this
  nav was originally built by hand-copying each file's own title in. Confirmed
  the same "first line is `# <Title>`" convention holds with zero exceptions
  across every file checked in `docs/design/`, `docs/architecture/`,
  `docs/rfcs/`, and `docs/updates/`.
- The 8 groups split into two real categories:
  - **Curated, low-churn** — `General` (mixes root-level files like `README.md`
    with hand-picked docs, even has one duplicate entry — `project/advancement-
    plans-and-evaluation.md` appears in both `General` and `Project & Legal`) and
    `Project & Legal` (same mixing pattern). Not worth automating: small, edited
    rarely, genuinely wants human judgment about what belongs and how it's
    labeled. **Left alone — this generator never touches these two groups.**
  - **Directory-mapped, append-only** — `Updates & Summaries` (`docs/updates/`),
    `Architecture` (`docs/architecture/`), `Design` (`docs/design/`), `RFCs`
    (`docs/rfcs/`), `Design Specs` (`docs/superpowers/specs/`), `Implementation
    Plans` (`docs/superpowers/plans/`). Each maps 1:1 to a directory; this is
    exactly where the staleness lives, and exactly what this generator targets.
- **Accepted trade-off**: this project's oldest specs/plans predate a settled
  "clean one-line `# Title`" convention — e.g.
  `2026-08-15-mosaic-docs-hub.md`'s actual first line is `# Project Mosaic —
  Documentation Set & Docs Hub Implementation Plan`, hand-shortened in the
  current nav to "Docs Hub Setup". A pure H1-derived generator reproduces the
  verbose original instead. Accepted as the cost of full automation — a manual
  title-override system would reintroduce the exact staleness risk this slice
  exists to remove.
- **Sort order is not uniform today**, and the generator must preserve each
  group's existing convention rather than applying one rule everywhere:
  `Updates & Summaries` is newest-first (2026-08-23 listed before 2026-08-21);
  `Design Specs`/`Implementation Plans` are oldest-first (chronological reading
  order — specs build on each other narratively); `Architecture`/`Design`/`RFCs`
  are topic-alphabetical (their filenames carry no dates).

## Design

### Marker-based partial regeneration

Each of the 6 auto-generated groups gets an HTML comment marker pair around its
link list; the `<h3>` label stays hand-authored (renaming a group is a
deliberate, manual choice) and the generator only ever replaces what's between
its own group's markers:

```html
<div class="nav-group">
    <h3>Design</h3>
    <!-- AUTO-GENERATED:design -->
    <a class="nav-link" onclick="loadFile('design/asset-pipeline.md', this)">Asset Pipeline</a>
    <a class="nav-link" onclick="loadFile('design/automation-and-agents.md', this)">Automation &amp; Agents</a>
    ...
    <!-- END-AUTO-GENERATED -->
</div>
```

The `General` and `Project & Legal` groups get no markers and are never touched.

### `tools/docs/regenerate_nav.py`

A standalone Python script (stdlib only — `pathlib`, `re`; no new dependency),
run manually from the repo root: `python3 tools/docs/regenerate_nav.py`. Six
`(marker_key, docs_group, sort)` entries, each naming a directory relative to
`docs/` and a sort mode (`"newest-first"`, `"oldest-first"`, or `"alphabetical"`):

```python
GROUPS = [
    ("updates",  "updates",              "newest-first"),
    ("architecture", "architecture",     "alphabetical"),
    ("design",   "design",               "alphabetical"),
    ("rfcs",     "rfcs",                 "alphabetical"),
    ("specs",    "superpowers/specs",    "oldest-first"),
    ("plans",    "superpowers/plans",    "oldest-first"),
]
```

For each group: list every `*.md` file directly in that directory (non-recursive
— matches every directory's current flat layout), derive each file's title, sort
per the group's mode, render one `<a>` line per file, HTML-escape the title
(`&`, `<`, `>`), and splice the rendered block between that group's
`<!-- AUTO-GENERATED:<key> -->` / `<!-- END-AUTO-GENERATED -->` markers in
`docs/index.html`, overwriting the file in place. Idempotent — running it twice
in a row with no doc changes produces a byte-identical file.

**Title derivation**: read the file, find its first non-blank line. If it starts
with `# `, strip the prefix and any inline backticks (`` ` ``) — that's the
title verbatim, no further shortening (the accepted trade-off above). If the
file has no `# `-prefixed first line, fall back to a filename-derived title: strip
a leading `YYYY-MM-DD-` date prefix if present, strip a trailing `-design`/
`-plan` suffix if present, replace `-`/`_` with spaces, title-case each word.
Print a one-line warning to stderr for every file that hit the fallback path (a
missing H1 is a quality issue worth a human's attention, not a hard failure).

**Failure mode**: if `docs/index.html` is missing a marker pair for any of the 6
groups, the script exits non-zero with a clear error naming which marker is
missing and writes nothing — never partially regenerates the file.

### Path format

Emitted `loadFile(...)` paths are relative to `docs/` (matching every existing
entry, e.g. `design/scripting.md`, `superpowers/specs/2026-09-14-...-design.md`)
— the file's path relative to the `docs/` directory the generator scans from,
since `index.html` itself lives at `docs/index.html`.

## Testing

`tools/docs/test_regenerate_nav.py`, Python stdlib `unittest` (run manually:
`python3 -m unittest tools.docs.test_regenerate_nav` from the repo root — not
wired into `ctest`/CMake, since this is a pure-Python repo-maintenance script
with no engine/C++ dependency, same tier as the Blender addon under
`tools/blender_live_link/` which also has no C++ test coverage):

- Title derivation: a `# Title` first line extracts verbatim; inline backticks
  are stripped (`` # Engine `World` runtime — design `` → ``Engine World runtime
  — design``); a file with no H1 falls back to the filename heuristic (`2026-08-
  15-mosaic-docs-hub.md` → `"Mosaic Docs Hub"`); the fallback path prints a
  stderr warning.
- Sort modes: `newest-first` reverses filename order; `oldest-first` keeps
  filename order; `alphabetical` sorts non-date filenames correctly (`ARCHITECTURE.md`
  before `boundaries.md` before `data-model.md`).
- Marker splicing: given a small fixture HTML string with marker pairs, the
  correct block is replaced, everything outside the markers (including OTHER
  groups' marker pairs and content) is byte-identical before and after,
  re-running against the already-regenerated output is a no-op (idempotency).
- Missing-marker failure: a fixture HTML string missing one group's marker pair
  causes a non-zero exit and no file write, verified against a temp copy of the
  fixture (its content is unchanged after the failed run).
- End-to-end smoke test (not a mock): run the real generator against a temp copy
  of the real `docs/index.html` plus the real `docs/` tree, confirm every
  currently-existing file in the 6 auto-groups appears in the output with a
  non-empty title, and confirm running it twice back-to-back on the temp copy
  produces byte-identical output the second time.

## Files

- Create: `tools/docs/regenerate_nav.py`
- Create: `tools/docs/test_regenerate_nav.py`
- Modify: `docs/index.html` — add the 6 marker-comment pairs (one per
  auto-generated group) around each group's existing link list; the generator's
  first real run afterward will then re-render those 6 groups from the actual
  `docs/` tree, picking up every currently-missing spec/plan/design-doc.
- Create: `docs/design/documentation-tooling.md` — short doc explaining the
  marker convention, the 6 auto-groups vs. the 2 hand-curated ones, the sort-mode
  table, and how to run the regenerator (mirrors how `docs/design/project-
  templates.md` documents that mechanism).
- Modify: `docs/project/next-steps.md` — new dated entry.

## Non-goals / follow-ups

- **Reflection-driven API reference docs** (part 2 of this documentation-tooling
  sub-project) — a separate, later spec. Once it lands and emits markdown into a
  `docs/` subdirectory, adding that directory as a 7th auto-group here is a
  small follow-up to this generator, not a redesign.
- **Wiring the regenerator into CI or a pre-commit hook** (a `--check` mode that
  exits non-zero if regeneration would change the file, without writing) — v1
  is a manually-run script; automating the check is real but speculative future
  work, not built here.
- **Automating the `General`/`Project & Legal` groups** — deliberately out of
  scope; see the Current State section's reasoning.
- **Recursing into subdirectories of the 6 auto-mapped directories** — none of
  them currently have subdirectories; if one ever does, this is a small,
  separate follow-up.
