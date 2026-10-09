#!/usr/bin/env python3
"""Rejects Catch2 TEST_CASE names that break ctest's test DISCOVERY.

Two constraints, both learned the hard way in this repo, both silent when violated --
the suite still builds, and the damage shows up as tests that do not exist rather than
as tests that fail:

  1. A name beginning with `--` is parsed as a command-line flag by
     catch_discover_tests / ctest rather than as a test spec.
  2. A name containing an unbalanced `[` confuses CatchAddTests.cmake's tag parsing,
     because tags are bracketed and the parser cannot tell an unclosed tag from a
     literal bracket in the name.

DELIBERATELY NOT CHECKED: commas. A comma IS a test-spec separator in a Catch2 name
filter, but `CatchAddTests.cmake` escapes commas when it generates each `add_test`, so
comma-containing names run correctly under ctest (verified 2026-10-09 against the
generated *_tests.cmake). What a comma breaks is an UNESCAPED per-name invocation from a
script -- a tooling bug, fixed by escaping `\\,` in the script, not by renaming tests.
An earlier version of this project's notes had that backwards and would have had 89
well-written names renamed for nothing.

Run from anywhere: python3 tools/ci/check_test_case_names.py
Exits 0 with no output if clean; exits 1 and prints every violation otherwise.
"""
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
TESTS_DIR = "tests"

# The first string literal of a TEST_CASE / TEMPLATE_TEST_CASE / SCENARIO. Non-greedy and
# escape-aware so a name containing \" does not end the match early.
NAME_RE = re.compile(
    r'\b(?:TEST_CASE|TEMPLATE_TEST_CASE|SCENARIO)\s*\(\s*"((?:[^"\\]|\\.)*)"'
)


@dataclass
class Violation:
    file: Path
    line: int
    name: str
    reason: str


def check_name(name: str) -> str:
    """Return a reason string if `name` breaks discovery, else ''."""
    if name.startswith("--"):
        return "begins with `--`, which ctest parses as a flag rather than a test spec"
    if name.count("[") != name.count("]"):
        return (
            f"has unbalanced brackets ({name.count('[')} `[` vs {name.count(']')} `]`), "
            "which breaks CatchAddTests.cmake's tag parsing"
        )
    return ""


def find_violations(root: Path) -> list[Violation]:
    violations: list[Violation] = []
    tests = root / TESTS_DIR
    if not tests.is_dir():
        return violations
    for path in sorted(tests.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in NAME_RE.finditer(text):
            reason = check_name(match.group(1))
            if reason:
                line = text.count("\n", 0, match.start()) + 1
                violations.append(Violation(path, line, match.group(1), reason))
    return violations


def main() -> int:
    violations = find_violations(REPO_ROOT)
    if not violations:
        return 0
    print("TEST_CASE names that break ctest discovery:")
    for v in violations:
        rel = v.file.relative_to(REPO_ROOT)
        print(f'  {rel}:{v.line}: "{v.name}"')
        print(f"      {v.reason}")
    print(f"\n{len(violations)} violation(s).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
