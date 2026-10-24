#!/usr/bin/env python3
"""Check that every `docs/record/<file>.md#anchor` link in docs/ resolves to a real heading.

Run deliberately, like the other tools in this directory; it is NOT wired into CI yet
(see the "promote the corrected check" item in docs/project/deferred-work-backlog.md).

    python3 tools/docs/check_record_anchors.py            # repo root
    python3 tools/docs/check_record_anchors.py --self-test # prove it can fail

WHY THIS EXISTS, AND THE BUG IT IS BUILT AROUND
-----------------------------------------------
On 2026-10-23 a scratch script reported "ten record anchors are dead". They were not.
The script was never committed, so the claim could not be re-run and simply stood until
someone rebuilt the check. It was retracted 2026-10-24. Note the direction of the error:
it reported dead anchors that were alive, so nothing was ever actually broken.

Every falsely-flagged anchor contained a DOUBLE HYPHEN, which is the signature of the
bug. GitHub's slugger deletes punctuation and *then* maps each remaining space to one
hyphen, so a heading `Bulk refactoring / project health checks` becomes
`bulk-refactoring--project-health-checks` -- the deleted `/` leaves two spaces behind.
A checker that collapses whitespace runs instead (`re.sub(r"\\s+", "-", t)`) yields a
single hyphen and declares every such anchor dead.

Do not "simplify" slugify() back into a collapsing substitution. That is the bug.
"""

from __future__ import annotations

import argparse
import os
import re
import sys

HEADING_RE = re.compile(r"^#{1,6}\s+(.*?)\s*$")
# Only matches real links (they carry the `record/` path segment), never a bare
# `foo.md#bar` written inside backticks as prose -- those are examples, not links.
LINK_RE = re.compile(r"record/([\w.-]+\.md)#([\w-]+)")


def slugify(text: str) -> str:
    """Reproduce GitHub's heading-anchor algorithm closely enough for this repo."""
    text = text.replace("`", "")
    text = text.lower()
    text = re.sub(r"[^\w\s-]", "", text)  # drop punctuation, KEEP the spaces it leaves
    return text.replace(" ", "-")  # each space -> exactly one hyphen


def anchors_of(path: str) -> set[str]:
    counts: dict[str, int] = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            m = HEADING_RE.match(line)
            if m:
                s = slugify(m.group(1))
                counts[s] = counts.get(s, 0) + 1
    out: set[str] = set()
    for slug, n in counts.items():
        out.add(slug)
        for i in range(1, n):  # GitHub disambiguates repeats with -1, -2, ...
            out.add(f"{slug}-{i}")
    return out


def check(root: str) -> tuple[int, list[str]]:
    record_dir = os.path.join(root, "docs", "record")
    records = {
        fn: anchors_of(os.path.join(record_dir, fn))
        for fn in os.listdir(record_dir)
        if fn.endswith(".md")
    }

    dead: list[str] = []
    checked = 0
    for dirpath, _dirs, files in os.walk(os.path.join(root, "docs")):
        for fn in files:
            if not fn.endswith(".md"):
                continue
            path = os.path.join(dirpath, fn)
            with open(path, encoding="utf-8") as fh:
                body = fh.read()
            for m in LINK_RE.finditer(body):
                target, anchor = m.group(1), m.group(2)
                checked += 1
                if target not in records or anchor not in records[target]:
                    dead.append(f"{os.path.relpath(path, root)} -> {target}#{anchor}")
    return checked, sorted(set(dead))


def self_test() -> int:
    """Both controls. A negative control alone would not have caught the 2026-10-23 bug.

    POSITIVE control: known-good headings must slug to the anchors they really produce.
    This is the one that matters here -- the original bug reported *live* anchors as
    dead, a false positive, which a negative control passes straight through.

    NEGATIVE control: a genuinely absent anchor must still be reported, so that a run
    printing "0 dead" means something.
    """
    failures = 0

    print("positive control (known-good headings must slug correctly):")
    cases = [
        ("Bulk refactoring / project health checks", "bulk-refactoring--project-health-checks"),
        ("Issue #25 — Component TypeID densification", "issue-25--component-typeid-densification"),
        ("U8a — inherited alpha and stack layout", "u8a--inherited-alpha-and-stack-layout"),
        ("A headless `VkDevice` for tests", "a-headless-vkdevice-for-tests"),
    ]
    for heading, expected in cases:
        got = slugify(heading)
        ok = got == expected
        print(f"  {'ok  ' if ok else 'FAIL'} {heading!r} -> {got!r}")
        if not ok:
            print(f"       expected {expected!r}")
            failures += 1

    print("negative control (an absent anchor must be reported):")
    known = {"real-heading"}
    missing = "this-anchor-does-not-exist"
    if missing in known:
        print("  FAIL the negative control's own fixture is wrong")
        failures += 1
    else:
        print(f"  ok   {missing!r} correctly absent from {sorted(known)}")

    total = len(cases) + 1
    print(f"self-test: {total - failures} of {total} controls passed")
    return 1 if failures else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("root", nargs="?", default=".", help="repo root (default: cwd)")
    ap.add_argument("--self-test", action="store_true", help="check slugify, then exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    checked, dead = check(args.root)
    print(f"check_record_anchors: checked {checked} record anchors across docs/")
    if dead:
        print(f"check_record_anchors: {len(dead)} DEAD")
        for d in dead:
            print(f"  DEAD: {d}")
        return 1
    print("check_record_anchors: OK, 0 dead")
    return 0


if __name__ == "__main__":
    sys.exit(main())
