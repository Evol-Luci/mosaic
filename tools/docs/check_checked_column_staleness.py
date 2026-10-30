#!/usr/bin/env python3
"""Staleness guard for docs/project/next-steps.md's State table: a changed `Value` must carry a `Checked` date no older than the change.

`check_next_steps_claims.py` already asserts each State row's `Value` is *true*. Nothing
asserted that its `Checked` column was *honest*, and it drifted: #327 moved `Suite size`
1608 -> 1621 and `Suite status` to 1621/1621 on 2026-10-28 while leaving both rows'
`Checked` at 2026-10-27, so the table dated a number a day before it existed. #330 then
had to move both dates to 2026-10-29. A reader cannot tell a fresh row from a rotting one
if the date is decoration.

THE PROPERTY, stated exactly:

    For every `Claim` present in both the base and the new revision, if its `Value`
    changed, then its new `Checked` cell must be an ISO date NOT EARLIER than the date
    on which that `Value` change was authored.

FRESHNESS RELATIVE TO THE CHANGE, NOT MOVEMENT OF THE DATE. This is a ruling, recorded
here rather than only in a review thread because the obvious "tightening" is to demand
that the `Checked` cell *differ* whenever `Value` does -- and that rule is wrong. It has
a false positive this very document's history is full of: **same-day re-verification**,
where the suite is re-run and the number updated on the day the cell already names, so
there is nothing to edit and the date is already true. Of the 20 most recent commits to
touch the handoff, 8 change a suite `Value` while leaving `Checked` alone; in 7 the date
already equalled the commit's own date. A "must differ" rule reds all 8, finding one real
defect at the price of seven false ones -- and a guard that cries wolf seven times in
eight gets switched off. Do not reinstate it.

A `Checked` date NEWER than the change commit is likewise NOT a violation: a PR
legitimately spans days. There is exactly one failure mode here, `Checked < change date`.
(A date in the FUTURE relative to the newest commit examined is a different smell, and is
deliberately not checked; it is in `deferred-work-backlog.md` instead.)

How the change date is attributed (this is the `git log -S`-per-cell part):
the script walks every commit in `<base>..<new>` that touched the document, oldest
first, parses the State table at each, and records for each `Claim` the AUTHOR date of
the LAST snapshot in which that row's `Value` differed from the snapshot before it. Per
row, so an unrelated later commit to the document cannot re-date an honest row. In
working-tree mode an uncommitted edit is one more snapshot, dated today. If no commit
range is available (`--base-file`/`--new-file`, or two unrelated revisions), the
attribution falls back to `--as-of`, default today, and the message says so.

TIMEZONE: the author date reduced with `git log --date=short`, i.e. rendered in the
offset recorded IN the commit -- the author's own calendar date. Chosen over
`--date=short-local` (the reader's zone: the same commit would then attribute differently
on a UTC runner than on the author's machine, so CI and a local run could disagree about
the same history) and over UTC (which can sit a day away from the date the author actually
typed into the cell). The cell is written by a human reading their own calendar, so it is
judged against that same calendar. Uncommitted working-tree edits use `date.today()`, the
local date, for the same reason.

The checks:

  O1  Both sides contain a parseable `## State` table with a `Checked` column, the new
      side has at least one data row, and no `Claim` is duplicated on either side. An
      unparseable, column-less or empty table is RED, not a vacuous pass.
  O2  The property above, for every row whose `Value` changed. Every violation is
      reported, not just the first.
  O3  Every `Checked` cell on the new side is an ISO `YYYY-MM-DD` date. A cell reading
      `n/a` or `always` would make O2 unfalsifiable for that row.

Consequences worth stating, because they are the check's denominator:

  * A `Value` change is inequality of that cell's text after Markdown-cell normalisation
    (ends stripped, `\\|` unescaped, internal whitespace runs collapsed to one space). A
    pure reflow is NOT a change; any other edit to the cell is.
  * Rows are matched by `Claim`. A row whose `Claim` was RENAMED reads as one removal plus
    one addition and is NOT checked: there is no previous value under the new name. Same
    for a row added wholesale or deleted.
  * An edit to `Verify with`, or to any column other than `Value`, requires nothing of the
    date. #329 reworded a `Verify with` cell alone and must pass.
  * The first revision of a document has no base side. `--base` must resolve to a revision
    that has the file; an unresolvable base or a missing `--base-file` is RED, never a
    silent skip.
  * The comparison is base-vs-new, never per-commit. A branch that moves the value in one
    commit and the date in a later one PASSES: the branch as a whole is honest, and the
    date is compared against the value change's own author date. A per-commit check would
    reject it.

Usage:
    python3 tools/docs/check_checked_column_staleness.py --base origin/main
    python3 tools/docs/check_checked_column_staleness.py --base HEAD~1 --rev HEAD
    python3 tools/docs/check_checked_column_staleness.py --base-file OLD.md --new-file NEW.md
Exit 0 with a one-line summary when clean; exit 1 and print every violation otherwise.
"""
import argparse
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_next_steps_claims import parse_state_table  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_DOC = "docs/project/next-steps.md"
ISO_DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
WORKING_TREE = "the working tree"


def normalise_cell(text: str) -> str:
    """Cell text for comparison: whitespace runs collapsed, ends stripped."""
    return re.sub(r"\s+", " ", text).strip()


def _git(args: list[str], repo_root: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", *args], cwd=str(repo_root), capture_output=True, text=True
    )


def read_revision(rev: str, doc_path: str, repo_root: Path) -> tuple[str | None, str | None]:
    """Return (text, error) for `git show <rev>:<doc_path>`."""
    proc = _git(["show", f"{rev}:{doc_path}"], repo_root)
    if proc.returncode != 0:
        return None, (
            f"O1: could not read {doc_path!r} at revision {rev!r}: "
            f"{(proc.stderr or proc.stdout).strip()}"
        )
    return proc.stdout, None


def _index_rows(text: str, side: str) -> tuple[dict[str, dict[str, str]], list[str]]:
    """Parse one side's State table into {Claim: row}. Return (index, problems)."""
    rows, problems = parse_state_table(text)
    problems = [f"{p} [{side} side]" for p in problems]
    if problems:
        return {}, problems
    if rows and "Checked" not in rows[0]:
        return {}, [
            f"O1: '## State' table on the {side} side has no 'Checked' column "
            f"(columns: {sorted(rows[0])})"
        ]
    index: dict[str, dict[str, str]] = {}
    for row in rows:
        claim = row.get("Claim", "")
        if claim in index:
            problems.append(
                f"O1: duplicate State claim {claim!r} on the {side} side "
                "(rows are matched by Claim, so a duplicate is ambiguous)"
            )
            continue
        index[claim] = row
    return index, problems


def value_change_dates(
    snapshots: list[tuple[str, str, str]]
) -> dict[str, tuple[str, str]]:
    """Attribute each Claim's last `Value` change to a date.

    `snapshots` is (date, label, text), oldest first, starting with the base revision
    (whose own date is never used). Returns {Claim: (date, label)} for every Claim whose
    Value changed at least once across the sequence.
    """
    attributed: dict[str, tuple[str, str]] = {}
    previous: dict[str, str] | None = None
    for snap_date, label, text in snapshots:
        rows, problems = parse_state_table(text)
        if problems:
            # An intermediate snapshot that does not parse cannot attribute anything;
            # the base/new sides are validated separately by O1.
            continue
        current = {
            r.get("Claim", ""): normalise_cell(r.get("Value", "")) for r in rows
        }
        if previous is not None:
            for claim, value in current.items():
                if claim in previous and previous[claim] != value:
                    attributed[claim] = (snap_date, label)
        previous = current
    return attributed


def build_snapshots(
    base: str,
    rev: str | None,
    doc_path: str,
    repo_root: Path,
    base_text: str,
    new_text: str,
    today: str,
) -> list[tuple[str, str, str]]:
    """Base revision plus every commit in base..rev that touched the doc, oldest first."""
    head = rev or "HEAD"
    proc = _git(
        [
            "log",
            "--reverse",
            "--format=%H %ad",
            "--date=short",
            f"{base}..{head}",
            "--",
            doc_path,
        ],
        repo_root,
    )
    snapshots: list[tuple[str, str, str]] = [("", f"base {base}", base_text)]
    if proc.returncode == 0:
        for line in proc.stdout.splitlines():
            parts = line.split()
            if len(parts) != 2:
                continue
            sha, when = parts
            text, err = read_revision(sha, doc_path, repo_root)
            if err:
                continue
            snapshots.append((when, sha[:9], text))
    if normalise_cell(snapshots[-1][2]) != normalise_cell(new_text):
        # Uncommitted edits (or an unlistable range): one more snapshot, dated today.
        label = WORKING_TREE if rev is None else f"{rev} (unattributed)"
        snapshots.append((today, label, new_text))
    return snapshots


def check_staleness_texts(
    base_text: str,
    new_text: str,
    snapshots: list[tuple[str, str, str]] | None = None,
    as_of: str | None = None,
) -> tuple[list[str], int, int]:
    """Core check. Return (problems, rows_compared, value_changes_seen)."""
    today = as_of or date.today().isoformat()
    base_index, problems = _index_rows(base_text, "base")
    new_index, new_problems = _index_rows(new_text, "new")
    problems = problems + new_problems
    if problems:
        return problems, 0, 0

    if not new_index:
        return (
            [
                "O1: '## State' table on the new side has 0 data rows "
                "(refusing to pass vacuously)"
            ],
            0,
            0,
        )

    for claim in sorted(new_index):
        checked = normalise_cell(new_index[claim].get("Checked", ""))
        if not ISO_DATE.match(checked):
            problems.append(
                f"O3: State row {claim!r} has a non-ISO 'Checked' cell {checked!r} "
                "(expected YYYY-MM-DD; a non-date makes the staleness check "
                "unfalsifiable for that row)"
            )

    if snapshots is None:
        snapshots = [("", "base", base_text), (today, "as-of", new_text)]
    attributed = value_change_dates(snapshots)

    compared = 0
    value_changes = 0
    for claim in sorted(new_index):
        if claim not in base_index:
            continue  # added wholesale, or a renamed Claim: no previous value
        compared += 1
        old_value = normalise_cell(base_index[claim].get("Value", ""))
        new_value = normalise_cell(new_index[claim].get("Value", ""))
        if old_value == new_value:
            continue
        value_changes += 1
        checked = normalise_cell(new_index[claim].get("Checked", ""))
        if not ISO_DATE.match(checked):
            continue  # already reported by O3
        changed_on, where = attributed.get(claim, (today, "as-of (unattributed)"))
        if not ISO_DATE.match(changed_on):
            changed_on, where = today, "as-of (unattributed)"
        if date.fromisoformat(checked) < date.fromisoformat(changed_on):
            problems.append(
                f"O2: State row {claim!r} changed its Value ({old_value!r} -> "
                f"{new_value!r}) on {changed_on} ({where}), but 'Checked' reads "
                f"{checked}, which is EARLIER. Re-run that row's verify command and set "
                f"'Checked' to {changed_on} or later, or restore the old value."
            )

    return problems, compared, value_changes


def check_staleness(
    base: str | None = None,
    rev: str | None = None,
    doc_path: str = DEFAULT_DOC,
    repo_root: Path = REPO_ROOT,
    base_file: Path | None = None,
    new_file: Path | None = None,
    as_of: str | None = None,
) -> tuple[list[str], int, int]:
    """Resolve both sides (from git or from files), then check. Returns the core triple."""
    repo_root = Path(repo_root)
    today = as_of or date.today().isoformat()
    from_git = base_file is None and new_file is None

    if base_file is not None:
        p = Path(base_file)
        if not p.is_file():
            return [f"O1: base file not found: {p}"], 0, 0
        base_text = p.read_text(encoding="utf-8")
    else:
        if not base:
            return ["O1: no base side given (pass --base <rev> or --base-file <path>)"], 0, 0
        base_text, err = read_revision(base, doc_path, repo_root)
        if err:
            return [err], 0, 0

    if new_file is not None:
        p = Path(new_file)
        if not p.is_file():
            return [f"O1: new file not found: {p}"], 0, 0
        new_text = p.read_text(encoding="utf-8")
    elif rev:
        new_text, err = read_revision(rev, doc_path, repo_root)
        if err:
            return [err], 0, 0
    else:
        p = repo_root / doc_path
        if not p.is_file():
            return [f"O1: working-tree file not found: {p}"], 0, 0
        new_text = p.read_text(encoding="utf-8")

    snapshots = None
    if from_git:
        snapshots = build_snapshots(
            base, rev, doc_path, repo_root, base_text, new_text, today
        )
    return check_staleness_texts(base_text, new_text, snapshots=snapshots, as_of=today)


def main(argv: list[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", default=None, help="Base revision (e.g. origin/main)")
    ap.add_argument("--rev", default=None, help="New revision (default: the working-tree file)")
    ap.add_argument(
        "--doc",
        default=DEFAULT_DOC,
        help=f"Repo-relative path to the handoff (default: {DEFAULT_DOC})",
    )
    ap.add_argument("--repo-root", type=Path, default=REPO_ROOT, help="Repository root")
    ap.add_argument("--base-file", type=Path, default=None, help="Base side as a file, not a revision")
    ap.add_argument("--new-file", type=Path, default=None, help="New side as a file, not a revision")
    ap.add_argument(
        "--as-of",
        default=None,
        help="Date (YYYY-MM-DD) to attribute unattributable changes to (default: today)",
    )
    args = ap.parse_args(argv[1:])

    problems, compared, value_changes = check_staleness(
        base=args.base,
        rev=args.rev,
        doc_path=args.doc,
        repo_root=args.repo_root,
        base_file=args.base_file,
        new_file=args.new_file,
        as_of=args.as_of,
    )
    if problems:
        for p in problems:
            print(f"check_checked_column_staleness: {p}")
        return 1
    print(
        f"check_checked_column_staleness: OK, {compared} State row(s) present on both "
        f"sides ({value_changes} with a changed Value, each dated no earlier than its "
        "change)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
