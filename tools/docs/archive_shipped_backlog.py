#!/usr/bin/env python3
"""Move SHIPPED (struck-through) entries out of the deferred-work backlog into the record.

WHY. Half of docs/project/deferred-work-backlog.md -- about 111 KB of 226 KB, 55 struck
bullets and 14 struck tier rows -- is work already done. It is kept deliberately:
each entry records what actually happened and which of its own predictions were
wrong. That is the RECORD, not pending work, and it belongs in docs/record/.

A DATE-based split of this file would hide live work, which is why this one is
STATUS-based: a struck-through entry is by definition not live, so moving it cannot
hide anything pending. "Struck" means the entry STARTS with a struck title
(`- ~~` for a bullet, a leading `~~` cell for a table row). An entry that merely
mentions strikethrough somewhere in its body is live and must stay.

Every moved entry LEAVES A ONE-LINE STUB. "Did we already consider this?" is a real
job this file does, and it needs the entry discoverable where someone looks rather
than only where it was filed.
"""
import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Tuple

# An entry is struck only when its TITLE is struck: the bullet starts `- ~~`,
# or the table row's first cell starts `~~`.
STRUCK_BULLET = re.compile(r"^- ~~")
STRUCK_ROW = re.compile(r"^\|\s*~~")
# An explicit resolution marker followed (closely, possibly across a line break)
# by an ISO date. Anything
# without one is NOT guessed at -- it goes to the undated catch-all.
SHIPPED_DATE = re.compile(
    r"\b(SHIPPED|FIXED|DONE|COMPLETE|CLOSED|RESOLVED|CONFIRMED|ADDRESSED|WITHDRAWN|DESIGNED)\b"
    r"[^.]{0,30}?(20\d\d-\d\d-\d\d)",
    re.I)
TITLE = re.compile(r"~~(.+?)~~", re.S)
STUB_MARK = re.compile(r"see \[record\]\(\.\./record/")
UNDATED_FILE = "undated"
SECTION_HEADING = "# Shipped backlog entries"
MAX_TITLE = 160


class Item(NamedTuple):
    kind: str              # "bullet" | "row"
    struck: bool
    title: Optional[str]
    date: Optional[str]    # YYYY-MM-DD, or None when the entry carries no SHIPPED date
    text: str
    section: str
    line: int = 0          # 0-based index of the first line in the source
    nlines: int = 1
    header: str = ""       # for a row: the table header + separator lines

    @property
    def status(self) -> str:
        m = SHIPPED_DATE.search(self.text) or re.search(r"\b(SHIPPED|FIXED|DONE|COMPLETE|CLOSED|RESOLVED|CONFIRMED|ADDRESSED|WITHDRAWN|DESIGNED)\b", self.text, re.I)
        return m.group(1).upper() if m else "CLOSED"

    @property
    def month(self) -> str:
        return self.date[:7] if self.date else UNDATED_FILE


def _clean_title(raw: str) -> str:
    t = re.sub(r"\s+", " ", raw.replace("**", "")).strip()
    if len(t) > MAX_TITLE:
        t = t[:MAX_TITLE].rsplit(" ", 1)[0].rstrip(" ,;:-—") + " …"
    return t


def _mk(kind: str, text: str, section: str, line: int = 0, nlines: int = 1,
        header: str = "") -> Item:
    struck = bool((STRUCK_BULLET if kind == "bullet" else STRUCK_ROW).match(text))
    m = SHIPPED_DATE.search(text)
    t = TITLE.search(text) if struck else None
    return Item(kind, struck, _clean_title(t.group(1)) if t else None,
                m.group(2) if m else None, text, section, line, nlines, header)


def parse_items(text: str) -> Tuple[str, List[Item]]:
    """Split the backlog into (preamble, items). Top-level bullets and table rows."""
    lines = text.split("\n")
    items: List[Item] = []
    section = ""
    cur: Optional[List[str]] = None
    cur_start = 0
    first_item: Optional[int] = None
    table_pos = 0           # 0 = not in a table, 1 = saw header, 2 = saw separator
    header = ""

    def flush():
        nonlocal cur
        if cur:
            while cur and not cur[-1].strip():
                cur.pop()
            items.append(_mk("bullet", "\n".join(cur), section, cur_start, len(cur)))
            cur = None

    for i, l in enumerate(lines):
        if l.startswith("#"):
            flush()
            table_pos = 0
            if l.startswith("## "):
                section = l[3:]
            continue
        if l.startswith("|"):
            flush()
            if first_item is None:
                first_item = i
            if table_pos == 0:
                header, table_pos = l, 1
            elif table_pos == 1:
                header, table_pos = header + "\n" + l, 2
            else:
                items.append(_mk("row", l, section, i, 1, header))
            continue
        table_pos = 0
        if l.startswith("- "):
            flush()
            if first_item is None:
                first_item = i
            cur, cur_start = [l], i
        elif cur is not None and (l.startswith("  ") or not l.strip()):
            cur.append(l)
        else:
            flush()
    flush()
    return "\n".join(lines[: first_item or 0]), items


def plan(struck: List[Item]) -> Tuple[List[Item], Dict[str, List[Item]]]:
    """Group struck items by the month they shipped; undated go to a catch-all."""
    moved: Dict[str, List[Item]] = {}
    for it in struck:
        moved.setdefault(it.month, []).append(it)
    return [], moved


def slug(title: str) -> str:
    """GitHub's heading-anchor algorithm for a heading with markdown stripped:
    lowercase, drop everything but word chars, spaces and hyphens, spaces -> '-'.
    Backticks, slashes and colons in titles are dropped, not turned into dead links."""
    s = re.sub(r"[`*]", "", title).lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-") or "entry"


def heading_anchors(text: str) -> List[str]:
    """Anchor of every markdown heading in `text`, with GitHub's -1, -2 suffixes
    for repeats. Ignores headings inside code fences."""
    seen: Dict[str, int] = {}
    out: List[str] = []
    fenced = False
    for l in text.split("\n"):
        if l.startswith("```"):
            fenced = not fenced
        m = None if fenced else re.match(r"^#{1,6}\s+(.+?)\s*#*\s*$", l)
        if m:
            base = slug(m.group(1))
            n = seen.get(base, 0)
            seen[base] = n + 1
            out.append(base if n == 0 else f"{base}-{n}")
    return out


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


def stub_for(it: Item, record_name: str, anchor: Optional[str] = None) -> str:
    """The one line left where the entry used to be."""
    anchor = anchor or slug(it.title or "entry")
    link = f"../record/{record_name}.md#{anchor}"
    when = f" {it.date}" if it.date else ""
    # Say what the entry itself said (SHIPPED / FIXED / WITHDRAWN ...), not always "SHIPPED".
    word = it.status
    title = it.title or "entry"
    if it.kind == "row":
        cols = max(2, it.header.split("\n")[0].count("|") - 1) if it.header else 2
        cells = [f" ~~**{title}**~~ — {word}{when}, see [record]({link}). "]
        cells += [" — "] * (cols - 1)
        return "|" + "|".join(cells) + "|"
    return f"- ~~**{title}**~~ — {word}{when}, see [record]({link})."


def _is_stub(it: Item) -> bool:
    return bool(STUB_MARK.search(it.text))


def _body(e: Item) -> str:
    """What goes under the entry's heading. A row is given its table header so it
    renders as a table; the row text itself is untouched."""
    return (e.header + "\n" + e.text) if e.kind == "row" and e.header else e.text


def run(doc: Path, record_dir: Path, dry_run: bool = False) -> int:
    text = doc.read_text(encoding="utf-8")
    _, items = parse_items(text)
    # An entry already reduced to a stub has no body left to move.
    struck = [i for i in items if i.struck and not _is_stub(i)]
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

    nb = sum(1 for i in struck if i.kind == "bullet")
    nr = len(struck) - nb
    print(f"{doc.name}: moving {len(struck)} shipped entries ({nb} bullets + {nr} rows, "
          f"{sum(len(i.text.encode()) for i in struck) / 1024:.0f} KB)")
    for month in sorted(moved, reverse=True):
        print(f"  record/{month}.md  += {len(moved[month])}")
    print(f"  undated (no SHIPPED date, not guessed): {len(moved.get(UNDATED_FILE, []))}")
    if dry_run:
        print("(dry run -- nothing written)")
        return 0

    record_dir.mkdir(parents=True, exist_ok=True)
    stubs: Dict[int, str] = {}
    for month, entries in moved.items():
        path = record_dir / f"{month}.md"
        existing = path.read_text(encoding="utf-8") if path.exists() else \
            f"# {month if month != UNDATED_FILE else 'Undated'} — shipped work\n"
        taken: Dict[str, int] = {}
        for a in heading_anchors(existing + f"\n{SECTION_HEADING}\n"):
            taken[a] = 1
        blocks = []
        for e in entries:
            title = e.title or "entry"
            base = slug(title)
            a, n = base, 0
            while a in taken:
                n += 1
                a = f"{base}-{n}"
            taken[a] = 1
            # The heading's own GitHub anchor must equal `a`: a repeat of `base`
            # gets GitHub's -N suffix automatically, in file order.
            blocks.append(f"## {title}\n\n{_body(e)}")
            stubs[e.line] = stub_for(e, month, a)
        head = "" if SECTION_HEADING in existing else SECTION_HEADING + "\n\n"
        path.write_text(existing.rstrip("\n") + "\n\n" + head + "\n\n".join(blocks) + "\n",
                        encoding="utf-8")
    lines = text.split("\n")
    for it in sorted(struck, key=lambda i: -i.line):
        lines[it.line:it.line + it.nlines] = [stubs[it.line]]
    doc.write_text("\n".join(lines), encoding="utf-8")
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
