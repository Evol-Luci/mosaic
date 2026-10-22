#!/usr/bin/env python3
"""Roll old dated blocks out of docs/project/next-steps.md into docs/record/YYYY-MM.md.

WHY THIS EXISTS. `next-steps.md` is the live "you are here" handoff doc, and it is
append-only: every slice adds a dated block at the top. By 2026-10-21 it had reached
486 KB over 46 entries -- large enough that tooling refused to open it whole, which
means the document was silently failing its one job. Nothing warns you when that
happens; the file just gets slower to read until it cannot be read.

THE RULE (chosen 2026-10-21):
  * blocks dated in the CURRENT calendar month stay in next-steps.md;
  * everything older moves to docs/record/<YYYY-MM>.md, one file per month;
  * plus a ceiling -- if the live file is still over it after the month rule, the
    OLDEST in-month blocks move too, into that month's file. A busy month would
    otherwise reach ~139 KB before it rolled, which is how this started.

The ceiling is a backstop, not the primary rule: the month boundary does the work.

CONSERVATION IS THE CONTRACT. This script moves text between files and is therefore
the kind of tool that can quietly destroy history. Every run checks that each block
present before the split is present after -- byte-identical, exactly once, across
the live file and the archive together -- and refuses to write if that fails. The
check has its own negative control in test_archive_next_steps.py; a conservation
check that cannot fail would be worse than none, because it would license trust.

Usage:
    python3 tools/docs/archive_next_steps.py [--dry-run]
    python3 tools/docs/archive_next_steps.py --doc PATH --archive-dir PATH \
        --today 2026-10-21 --ceiling 122880
"""
import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Tuple

# The three block-start forms actually present in next-steps.md (verified 2026-10-21):
#   **Last updated:** 2026-10-21 (...
#   **Previously, last updated 2026-10-20:** (...        <- date BEFORE the colon
#   ¶ **Last updated:** 2026-09-19 (...                  <- stray pilcrow
# A looser "any bold line" rule would swallow continuation paragraphs, which begin
# with bold text too and must stay attached to the block above them.
BLOCK_START = re.compile(r"^(?:¶\s*)?\*\*(?:Last updated|Previously, last updated)\b")
DATE = re.compile(r"(20\d\d)-(\d\d)-(\d\d)")

POINTER_MARK = "<!-- archive-pointer -->"
DEFAULT_CEILING = 120 * 1024

MONTHS = ("January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December")


class Block(NamedTuple):
    date: str   # YYYY-MM-DD
    text: str   # the block verbatim, trailing blank lines stripped

    @property
    def month(self) -> str:
        return self.date[:7]


def parse(text: str) -> Tuple[str, List[Block]]:
    """Split a doc into (header, blocks). Header is everything before block one."""
    lines = text.split("\n")
    starts = [i for i, l in enumerate(lines) if BLOCK_START.match(l)]
    if not starts:
        return text, []
    header = "\n".join(lines[: starts[0]]).rstrip("\n")
    blocks: List[Block] = []
    for n, start in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(lines)
        body = "\n".join(lines[start:end]).rstrip("\n")
        m = DATE.search(lines[start][:90])
        if not m:
            raise ValueError(f"block at line {start + 1} carries no date: {lines[start][:80]!r}")
        blocks.append(Block(m.group(0), body))
    return header + "\n", blocks


def render_live(header: str, blocks: List[Block], archive_dir_name: str = "record") -> str:
    """The live doc: header, an archive pointer, then the kept blocks in order."""
    clean = [l for l in header.rstrip("\n").split("\n") if POINTER_MARK not in l]
    while clean and not clean[-1].strip():
        clean.pop()
    # ONE line, carrying the marker, so the strip above removes the whole pointer on a
    # re-run. A two-line pointer (marker, then the blockquote) left the quote behind and
    # appended a second one every run -- caught by test_is_idempotent.
    pointer = (
        f"> **Older entries are archived by month** in [`docs/{archive_dir_name}/`]"
        f"(../{archive_dir_name}/), one file per month. This file keeps the current "
        f"month; `tools/docs/archive_next_steps.py` rolls the rest over. Nothing is "
        f"deleted -- if an entry is not here, it is in the archive. {POINTER_MARK}"
    )
    parts = ["\n".join(clean), "", pointer, ""]
    parts += [b.text + "\n" for b in blocks]
    return "\n".join(parts).rstrip("\n") + "\n"


def render_archive(month: str, blocks: List[Block]) -> str:
    year, mon = month.split("-")
    title = f"# {MONTHS[int(mon) - 1]} {year} — the record"
    note = ("Dated entries rolled out of `docs/project/next-steps.md`, newest first. "
            "Moved verbatim; see `tools/docs/archive_next_steps.py`.")
    body = "\n\n".join(b.text for b in blocks)
    return f"{title}\n\n{note}\n\n{body}\n"


def plan(blocks: List[Block], today: str,
         ceiling: int = DEFAULT_CEILING) -> Tuple[List[Block], Dict[str, List[Block]]]:
    """Return (blocks kept live, {month: blocks archived})."""
    this_month = today[:7]
    live = [b for b in blocks if b.month == this_month]
    archive: Dict[str, List[Block]] = {}
    for b in blocks:
        if b.month != this_month:
            archive.setdefault(b.month, []).append(b)

    # Ceiling backstop: push the OLDEST in-month blocks out until the live file fits.
    # Never empty it -- a handoff doc with no entries is not a smaller doc, it is a
    # broken one, so the newest block always stays however large it is.
    while len(live) > 1 and len(render_live("", live).encode("utf-8")) > ceiling:
        moved = live.pop()
        archive.setdefault(moved.month, []).insert(0, moved)
    return live, archive


def verify_conservation(before: List[Block], live: List[Block],
                        archive: Dict[str, List[Block]]) -> List[str]:
    """Return a list of problems; empty means every block survived exactly once."""
    after: List[str] = [b.text for b in live]
    for blocks in archive.values():
        after += [b.text for b in blocks]
    problems: List[str] = []

    seen: Dict[str, int] = {}
    for t in after:
        seen[t] = seen.get(t, 0) + 1
    for b in before:
        n = seen.get(b.text, 0)
        if n == 0:
            problems.append(f"LOST: the {b.date} block is in neither the live file nor the archive")
        elif n > 1:
            problems.append(f"DUPLICATED x{n}: the {b.date} block")
    known = {b.text for b in before}
    for t in after:
        if t not in known:
            m = DATE.search(t[:90])
            problems.append(f"ALTERED or INVENTED: a block dated {m.group(0) if m else '?'} "
                            f"does not match anything in the original")
    return problems


def _merge(existing: List[Block], incoming: List[Block]) -> List[Block]:
    """Union by exact text, newest first, so a re-run cannot double up a month."""
    out: List[Block] = []
    for b in list(existing) + list(incoming):
        if all(b.text != o.text for o in out):
            out.append(b)
    return sorted(out, key=lambda b: b.date, reverse=True)


def run(doc: Path, archive_dir: Path, today: str,
        ceiling: int = DEFAULT_CEILING, dry_run: bool = False) -> int:
    text = doc.read_text(encoding="utf-8")
    header, blocks = parse(text)
    if not blocks:
        print(f"{doc}: no dated blocks found; nothing to do")
        return 0

    live, archive = plan(blocks, today, ceiling)
    problems = verify_conservation(blocks, live, archive)
    if problems:
        print("REFUSING TO WRITE -- the split would not conserve every block:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    moved = sum(len(v) for v in archive.values())
    new_text = render_live(header, live, archive_dir.name)
    print(f"{doc.name}: {len(blocks)} blocks -> {len(live)} live, {moved} archived "
          f"({len(text) / 1024:.0f} KB -> {len(new_text.encode('utf-8')) / 1024:.0f} KB)")
    for month in sorted(archive, reverse=True):
        print(f"  {archive_dir.name}/{month}.md  += {len(archive[month])} block(s)")
    if dry_run:
        print("(dry run -- nothing written)")
        return 0

    archive_dir.mkdir(parents=True, exist_ok=True)
    for month, incoming in archive.items():
        path = archive_dir / f"{month}.md"
        existing: List[Block] = []
        if path.exists():
            _, existing = parse(path.read_text(encoding="utf-8"))
        path.write_text(render_archive(month, _merge(existing, incoming)), encoding="utf-8")
    doc.write_text(new_text, encoding="utf-8")
    return 0


def main(argv: List[str]) -> int:
    root = Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--doc", type=Path, default=root / "docs/project/next-steps.md")
    ap.add_argument("--archive-dir", type=Path, default=root / "docs/record")
    ap.add_argument("--today", default=None, help="YYYY-MM-DD (default: the system date)")
    ap.add_argument("--ceiling", type=int, default=DEFAULT_CEILING,
                    help=f"live-file size backstop in bytes (default {DEFAULT_CEILING})")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    if a.today is None:
        from datetime import date
        a.today = date.today().isoformat()
    return run(a.doc, a.archive_dir, a.today, a.ceiling, a.dry_run)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
