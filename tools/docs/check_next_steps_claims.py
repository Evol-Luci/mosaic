#!/usr/bin/env python3
"""Standing guard for docs/project/next-steps.md's State table: every verifiable claim must hold.

`docs/project/next-steps.md` opens with a `## State` table whose stated contract is:
"Every claim carries the command that regenerates it. If a command disagrees with the
text, trust the command and fix the text." Without a check in CI, a gate stayed stale
for six weeks and the handoff's own verify command (`cmake --preset debug -G Ninja`)
failed against an already-configured tree until #288 pinned the generator.

This script parses the `## State` table and fails hard unless:

  O1  The document contains a `## State` section with a non-empty Markdown table, and
      every row's `Claim` is either handled in `HANDLERS` or explicitly declared in
      `UNVERIFIABLE_CLAIMS` with its reason. An UNRECOGNISED CLAIM IS RED: there is
      deliberately NO fallback and no skip-by-default. A new State row must arrive with
      a handler or an explicit unverifiable marker, and a missing or empty table cannot
      pass vacuously.
  O2  `Suite size` — the claimed integer test count in `Value` (e.g. `1462 tests`)
      equals the real discovered count: the `<testcase>` count in `--junit` when given,
      or `Total Tests:` from `ctest --preset debug -N` otherwise.
  O3  `Suite status` — every testcase in the suite passed (no failure, error, skip, or
      disabled case in the JUnit file or suite header), AND both numbers of the `N/N`
      figure in `Value` (e.g. `all passing (1462/1462, 2.7 s local)`) equal the real
      testcase count. With `--junit`, reads the file; without it, runs the suite.
  O4  `Build` — does NOT run a build. Asserts instead that every `--preset <name>` in
      that row's `Verify with` cell resolves to a preset that actually exists in
      `CMakePresets.json` (`configurePresets` for `cmake --preset`, `buildPresets` for
      `cmake --build --preset`). This catches a renamed or deleted preset, which is the
      way that row realistically rots.

DELIBERATELY NOT CHECKED: the timing figures (`2.7 s local` in `Suite status`,
`~12-17 s` in `CI test step`). They are environment-dependent and asserting them would
make the check flaky on a different machine or runner -- a flaky guard is worse than
none. Do not add this later.

Usage:
    python3 tools/docs/check_next_steps_claims.py [NEXT_STEPS_MD] [--junit JUNIT_XML] [--presets CMAKE_PRESETS_JSON]
Exit 0 with a one-line summary (naming examined and skipped-with-reason rows) when clean;
exit 1 and print every violation otherwise.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_DOC = REPO_ROOT / "docs/project/next-steps.md"
DEFAULT_PRESETS = REPO_ROOT / "CMakePresets.json"

# Claims that cannot be verified by this tool, mapped to the explicit reason they are
# skipped. Any row whose Claim is in neither HANDLERS nor UNVERIFIABLE_CLAIMS fails O1.
UNVERIFIABLE_CLAIMS = {
    "CI test step": "needs `gh` and a specific run id (network + authenticated token)",
}


def parse_junit(path):
    """Return ({name: status}, suite_attrs, testcase_count)."""
    root = ET.parse(str(path)).getroot()
    suite = root if root.tag == "testsuite" else (root.find("testsuite") or root)
    outcomes = {}
    count = 0
    for tc in root.iter("testcase"):
        count += 1
        name = tc.get("name", f"<unnamed-{count}>")
        status = tc.get("status", "")
        if tc.find("failure") is not None or tc.find("error") is not None:
            status = "fail"
        elif tc.find("skipped") is not None:
            status = "skipped"
        outcomes[name] = status
    return outcomes, dict(suite.attrib), count


def _split_md_row(line: str) -> list[str]:
    """Split a Markdown table row on unescaped `|` and unescape `\\|` in each cell."""
    stripped = line.strip()
    if stripped.startswith("|"):
        stripped = stripped[1:]
    if stripped.endswith("|") and not stripped.endswith(r"\|"):
        stripped = stripped[:-1]
    parts = re.split(r"(?<!\\)\|", stripped)
    return [p.replace(r"\|", "|").strip() for p in parts]


def _is_separator_row(cells: list[str]) -> bool:
    return bool(cells) and all(re.match(r"^:?-{3,}:?$", c) for c in cells)


def parse_state_table(text: str) -> tuple[list[dict[str, str]], list[str]]:
    """Extract rows from the `## State` Markdown table. Return (rows, O1_problems)."""
    m = re.search(r"^##\s+State\s*$", text, re.M)
    if not m:
        return [], ["O1: no '## State' section found"]
    rest = text[m.end() :]
    next_h2 = re.search(r"^##\s+", rest, re.M)
    section = rest[: next_h2.start()] if next_h2 else rest

    table_lines = [
        line.strip()
        for line in section.splitlines()
        if line.strip().startswith("|")
    ]
    if len(table_lines) < 2:
        return [], ["O1: no Markdown table found in '## State' section"]

    headers = _split_md_row(table_lines[0])
    if "Claim" not in headers or "Value" not in headers or "Verify with" not in headers:
        return [], [
            f"O1: '## State' table header {headers!r} is missing required columns "
            "('Claim', 'Value', 'Verify with')"
        ]
    if not _is_separator_row(_split_md_row(table_lines[1])):
        return [], ["O1: '## State' table is missing its separator row"]

    rows: list[dict[str, str]] = []
    problems: list[str] = []
    for raw in table_lines[2:]:
        cells = _split_md_row(raw)
        if len(cells) != len(headers):
            problems.append(
                f"O1: '## State' row has {len(cells)} cell(s), expected {len(headers)}: {raw!r}"
            )
            continue
        rows.append(dict(zip(headers, cells)))

    if not rows and not problems:
        problems.append("O1: '## State' table has 0 data rows (refusing to pass vacuously)")
    return rows, problems


def _discover_count_via_ctest(cwd: Path) -> tuple[int | None, str | None]:
    proc = subprocess.run(
        ["ctest", "--preset", "debug", "-N"],
        cwd=str(cwd),
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        return None, (
            f"O2 (Suite size): `ctest --preset debug -N` exited {proc.returncode}: "
            f"{(proc.stderr or proc.stdout).strip()}"
        )
    m = re.search(r"^Total Tests:\s*(\d+)\s*$", proc.stdout, re.M)
    if not m:
        return None, "O2 (Suite size): `ctest --preset debug -N` output had no 'Total Tests:' line"
    return int(m.group(1)), None


def check_suite_size(row: dict[str, str], ctx: dict) -> list[str]:
    value = row.get("Value", "")
    m = re.search(r"\b(\d+)\b", value)
    if not m:
        return [f"O2 (Suite size): could not parse integer test count from Value {value!r}"]
    claimed = int(m.group(1))

    junit_path = ctx.get("junit_path")
    if junit_path is not None:
        p = Path(junit_path)
        if not p.is_file():
            return [f"O2 (Suite size): JUnit file not found: {p}"]
        _, _, actual = parse_junit(p)
        source = "JUnit"
    else:
        actual, err = _discover_count_via_ctest(ctx["repo_root"])
        if err:
            return [err]
        source = "ctest -N"

    if actual <= 0:
        return [f"O2 (Suite size): {source} reported {actual} tests (refusing to trust 0)"]
    if claimed != actual:
        return [
            f"O2 (Suite size): claims {claimed} tests, but {source} has {actual}"
        ]
    return []


def _load_or_run_junit(ctx: dict) -> tuple[tuple[dict, dict, int] | None, str | None]:
    if "junit_parsed" in ctx:
        return ctx["junit_parsed"], None
    junit_path = ctx.get("junit_path")
    if junit_path is not None:
        p = Path(junit_path)
        if not p.is_file():
            return None, f"O3 (Suite status): JUnit file not found: {p}"
        ctx["junit_parsed"] = parse_junit(p)
        return ctx["junit_parsed"], None

    with tempfile.TemporaryDirectory(prefix="mosaic_claims_junit_") as tmp:
        out_xml = Path(tmp) / "ctest-junit.xml"
        proc = subprocess.run(
            [
                "ctest",
                "--preset",
                "debug",
                "--output-on-failure",
                "--no-tests=error",
                "--output-junit",
                str(out_xml),
            ],
            cwd=str(ctx["repo_root"]),
            capture_output=True,
            text=True,
        )
        if not out_xml.is_file():
            return None, (
                f"O3 (Suite status): `ctest --preset debug` did not produce JUnit XML "
                f"(exit {proc.returncode}): {(proc.stderr or proc.stdout).strip()}"
            )
        ctx["junit_parsed"] = parse_junit(out_xml)
        return ctx["junit_parsed"], None


def check_suite_status(row: dict[str, str], ctx: dict) -> list[str]:
    problems: list[str] = []
    value = row.get("Value", "")
    if "all passing" not in value.lower():
        problems.append(
            f"O3 (Suite status): Value {value!r} does not state 'all passing'"
        )
    m = re.search(r"\b(\d+)/(\d+)\b", value)
    if not m:
        problems.append(
            f"O3 (Suite status): Value {value!r} does not contain an N/N count"
        )
        return problems

    claimed_pass, claimed_total = int(m.group(1)), int(m.group(2))
    parsed, err = _load_or_run_junit(ctx)
    if err:
        problems.append(err)
        return problems
    outcomes, attrs, actual_total = parsed

    if actual_total <= 0:
        problems.append("O3 (Suite status): JUnit has 0 testcases")
        return problems

    if claimed_pass != actual_total or claimed_total != actual_total:
        problems.append(
            f"O3 (Suite status): claims {claimed_pass}/{claimed_total} passing, "
            f"but suite has {actual_total} testcases"
        )

    bad = sorted(k for k, v in outcomes.items() if v not in ("run", "passed"))
    if bad:
        problems.append(
            f"O3 (Suite status): {len(bad)} testcase(s) did not pass, e.g. {bad[:5]}"
        )
    for key in ("failures", "errors", "skipped", "disabled"):
        if int(attrs.get(key, "0") or 0) != 0 and not bad:
            problems.append(f"O3 (Suite status): JUnit suite header has {key}={attrs.get(key)}")
    return problems


def check_build(row: dict[str, str], ctx: dict) -> list[str]:
    presets_path = Path(ctx["presets_path"])
    if not presets_path.is_file():
        return [f"O4 (Build): CMakePresets.json not found at {presets_path}"]
    try:
        data = json.loads(presets_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"O4 (Build): failed to parse {presets_path}: {exc}"]

    configure_presets = {
        p["name"] for p in data.get("configurePresets", []) if isinstance(p, dict) and "name" in p
    }
    build_presets = {
        p["name"] for p in data.get("buildPresets", []) if isinstance(p, dict) and "name" in p
    }

    verify_cell = row.get("Verify with", "").strip().strip("`")
    clauses = [c.strip() for c in re.split(r"&&|\|\||;|\|", verify_cell) if c.strip()]
    found_any = False
    problems: list[str] = []

    for clause in clauses:
        matches = list(re.finditer(r"--preset(?:=|\s+)([A-Za-z0-9_.-]+)", clause))
        if not matches:
            continue
        is_build = bool(re.search(r"(?:^|\s)--build(?:\s|$)", clause))
        for match in matches:
            found_any = True
            preset_name = match.group(1)
            if is_build:
                if preset_name not in build_presets:
                    problems.append(
                        f"O4 (Build): build preset {preset_name!r} (in {clause!r}) "
                        f"not found in {presets_path.name} buildPresets "
                        f"(known: {sorted(build_presets)})"
                    )
            else:
                if preset_name not in configure_presets:
                    problems.append(
                        f"O4 (Build): configure preset {preset_name!r} (in {clause!r}) "
                        f"not found in {presets_path.name} configurePresets "
                        f"(known: {sorted(configure_presets)})"
                    )

    if not found_any:
        problems.append(
            f"O4 (Build): no '--preset <name>' found in Verify with cell {row.get('Verify with', '')!r}"
        )
    return problems


HANDLERS = {
    "Suite size": check_suite_size,
    "Suite status": check_suite_status,
    "Build": check_build,
}


def _evaluate_claims(
    doc_path: Path = DEFAULT_DOC,
    junit_path: Path | None = None,
    presets_path: Path = DEFAULT_PRESETS,
    repo_root: Path = REPO_ROOT,
) -> tuple[list[str], int, int, list[tuple[str, str]]]:
    """Return (problems, total_rows, verified_rows, skipped_rows_with_reasons)."""
    doc_path = Path(doc_path)
    if not doc_path.is_file():
        return [f"O1: file not found: {doc_path}"], 0, 0, []

    rows, problems = parse_state_table(doc_path.read_text(encoding="utf-8"))
    if problems:
        return problems, len(rows), 0, []

    ctx = {
        "junit_path": Path(junit_path) if junit_path is not None else None,
        "presets_path": Path(presets_path),
        "repo_root": Path(repo_root),
    }

    verified = 0
    skipped: list[tuple[str, str]] = []
    for row in rows:
        claim = row.get("Claim", "")
        if claim in HANDLERS:
            verified += 1
            problems.extend(HANDLERS[claim](row, ctx))
        elif claim in UNVERIFIABLE_CLAIMS:
            skipped.append((claim, UNVERIFIABLE_CLAIMS[claim]))
        else:
            problems.append(
                f"O1: unrecognised State claim {claim!r} "
                "(no handler in HANDLERS and not declared in UNVERIFIABLE_CLAIMS)"
            )

    if verified == 0 and not problems:
        problems.append("O1: 0 verifiable rows in '## State' table (refusing to pass vacuously)")
    return problems, len(rows), verified, skipped


def check_claims(
    doc_path: Path = DEFAULT_DOC,
    junit_path: Path | None = None,
    presets_path: Path = DEFAULT_PRESETS,
    repo_root: Path = REPO_ROOT,
) -> list[str]:
    """Pure check returning a list of violation strings (empty == clean)."""
    problems, _, _, _ = _evaluate_claims(
        doc_path=doc_path,
        junit_path=junit_path,
        presets_path=presets_path,
        repo_root=repo_root,
    )
    return problems


def main(argv: list[str] | None = None) -> int:
    if argv is None:
        argv = sys.argv
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=DEFAULT_DOC,
        help="Path to next-steps.md (default: docs/project/next-steps.md)",
    )
    ap.add_argument(
        "--junit",
        type=Path,
        default=None,
        help="Read an existing ctest JUnit XML file instead of running ctest",
    )
    ap.add_argument(
        "--presets",
        type=Path,
        default=DEFAULT_PRESETS,
        help="Path to CMakePresets.json (default: repo root CMakePresets.json)",
    )
    args = ap.parse_args(argv[1:])

    problems, total_rows, verified, skipped = _evaluate_claims(
        doc_path=args.path,
        junit_path=args.junit,
        presets_path=args.presets,
    )
    if problems:
        for p in problems:
            print(f"check_next_steps_claims: {p}")
        return 1

    if skipped:
        skip_detail = "; ".join(f"{name}: {reason}" for name, reason in skipped)
        print(
            f"check_next_steps_claims: OK, {total_rows} State row(s) examined "
            f"({verified} verified, {len(skipped)} skipped [{skip_detail}])"
        )
    else:
        print(
            f"check_next_steps_claims: OK, {total_rows} State row(s) examined "
            f"({verified} verified, 0 skipped)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
