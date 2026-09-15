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
