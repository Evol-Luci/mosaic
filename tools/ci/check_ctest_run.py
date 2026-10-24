#!/usr/bin/env python3
"""Standing guard for the CI ctest step: it must have RUN every test, and all must have passed.

A green ctest exit code does not say how many tests ran. A parallel (or filtered, or
mis-discovered) invocation that silently runs fewer than the suite has would be green and
protect nothing. This reads ctest's JUnit file and fails hard unless:

  O1  the number of testcases == N, where N is the discovered count, computed IN THE SAME JOB
      (`ctest -N`), and cross-checked against the add_test lines of the generated
      *_tests.cmake when those are passed. N is never a literal here: it would rot on the
      first added test and teach people to edit the check.
  O2  every testcase passed: no failure, error, skip, or not-run, and the suite header agrees.
  O3  no testcase exceeded MAX_CASE_SECONDS (10.0 s). `TIMEOUT 60` in `tests/CMakeLists.txt` is
      a hang backstop, not a performance budget: a case that quietly grows 40x still passes it
      (the HUD-underlay case reached 16.5 s locally / 23.4 s on CI and went unnoticed for weeks).
      10.0 s is chosen against measurement (8.8x the 1.13 s worst observed local case, 9.0x the
      1.11 s worst case measured on CI, and 1/6 of TIMEOUT 60), and fails the step rather than
      annotating because CI already carries an expected yellow warning annotation (duplicate-key
      ccache save) that would train a warning-only budget into noise.

There is deliberately NO fallback for a missing or zero N, and no "|| true" in the caller: a
fallback is where a silent failure hides, printing a complete-looking log that protects nothing.

Usage: check_ctest_run.py JUNIT_XML N [GENERATED_TESTS_CMAKE ...]
       check_ctest_run.py --report JUNIT_XML [--baseline SERIAL_JUNIT_XML] [--wall SECONDS]
The --report form is for the measurement campaign (outcome map diff, efficiency, longest case).
Exit 0 with a one-line summary when clean; exit 1 and every violation otherwise.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Measured 2026-10-24 (nproc=20, suite 1462, machine otherwise idle, 4 runs alternating
# -j20 and -j8, worst value per case taken):
#   median per-case 0.019 s | p95 0.13 s | p99 0.37 s | max 1.13 s
#   cases over 1 s: 1 ("PluginFileWatcher.poll detects a modified file's mtime", 1.13 s)
#   cases over 2 s: 0 | cases over 5 s: 0
# 10.0 s is 8.8x the worst observed local case (1.13 s) and 9.0x the worst case measured on CI
# (1.11 s, read per-case out of PR #287's CI log), and 1/6 of the TIMEOUT 60 hang backstop -- so
# a case that grows 9x trips the budget instead of needing 53x to trip the timeout. CI is NOT
# slower per case here: the slowest case waits on an mtime change rather than burning CPU, so a
# 2-core runner does not inflate it. Do not "restore" a safety margin for CI slowness.
MAX_CASE_SECONDS = 10.0


def parse_junit(path):
    """Return ({name: status}, {name: seconds}, suite_attrs)."""
    root = ET.parse(str(path)).getroot()
    suite = root if root.tag == "testsuite" else (root.find("testsuite") or root)
    outcomes, secs = {}, {}
    for tc in root.iter("testcase"):
        name = tc.get("name")
        status = tc.get("status", "")
        if tc.find("failure") is not None or tc.find("error") is not None:
            status = "fail"
        elif tc.find("skipped") is not None:
            status = "skipped"
        outcomes[name] = status
        secs[name] = float(tc.get("time", "0"))
    return outcomes, secs, dict(suite.attrib), sum(1 for _ in root.iter("testcase"))


def count_add_test(cmake_paths):
    total = 0
    for p in cmake_paths:
        total += len(re.findall(r"^add_test\(", Path(p).read_text(encoding="utf-8"), re.M))
    return total


def check_run(junit_path, n_text, cmake_paths=(), max_case_seconds=MAX_CASE_SECONDS):
    """Return a list of violation strings (empty == clean)."""
    problems = []
    try:
        n = int(str(n_text).strip())
    except (TypeError, ValueError):
        return [f"N is not an integer: {n_text!r} (discovery count missing; refusing to guess)"]
    if n <= 0:
        return [f"N is {n}: no tests discovered, so this run protects nothing"]
    if not Path(junit_path).is_file():
        return [f"JUnit file not found: {junit_path} (ctest did not produce it)"]
    outcomes, secs, attrs, ncases = parse_junit(junit_path)
    if ncases != n:
        problems.append(f"O1: JUnit has {ncases} testcases but {n} tests were discovered")
    if len(outcomes) != ncases:
        problems.append(f"O1: {ncases - len(outcomes)} duplicate test names in the JUnit file")
    if cmake_paths:
        c = count_add_test(cmake_paths)
        if c != n:
            problems.append(f"O1: ctest -N reports {n} but the generated cmake has {c} add_test")
    bad = sorted(k for k, v in outcomes.items() if v not in ("run", "passed"))
    if bad:
        problems.append(f"O2: {len(bad)} tests did not pass, e.g. {bad[:5]}")
    for key in ("failures", "errors", "skipped", "disabled"):
        if int(attrs.get(key, "0") or 0) != 0:
            problems.append(f"O2: suite header {key}={attrs.get(key)}")
    slow = sorted(
        ((t, k) for k, t in secs.items() if t > max_case_seconds),
        key=lambda x: (-x[0], x[1]),
    )
    for t, k in slow[:5]:
        suffix = f" ({len(slow)} cases over budget in total)" if len(slow) > 1 else ""
        problems.append(
            f"O3: {k!r} took {t:.2f}s, over the {max_case_seconds:.1f}s per-case budget{suffix}"
        )
    return problems


def report(junit_path, baseline=None, wall=None):
    outcomes, secs, attrs, _ = parse_junit(junit_path)
    total = sum(secs.values())
    longest = max(secs, key=secs.get)
    print(f"cases={len(outcomes)} sum_of_durations={total:.1f}s "
          f"longest={secs[longest]:.2f}s ({longest})")
    rc = 0
    if wall:
        print(f"wall={wall:.1f}s efficiency(sum/wall)={total / wall:.2f}  "
              f"floor_ok(wall>=longest)={wall >= secs[longest]}")
        if wall < secs[longest]:
            rc = 1
    if baseline:
        b_out, b_secs, _, _ = parse_junit(baseline)
        only_a = sorted(set(outcomes) - set(b_out))
        only_b = sorted(set(b_out) - set(outcomes))
        diff = sorted(k for k in set(outcomes) & set(b_out) if outcomes[k] != b_out[k])
        print(f"O3 vs baseline: only_here={len(only_a)} only_baseline={len(only_b)} "
              f"status_differs={len(diff)}")
        if only_a or only_b or diff:
            print("  O3 RED:", (only_a + only_b + diff)[:10])
            rc = 1
        if longest in b_secs:
            print(f"longest-case inflation vs baseline: {secs[longest] / b_secs[longest]:.2f}x")
    return rc


def main(argv):
    if len(argv) >= 3 and argv[1] == "--report":
        base = argv[argv.index("--baseline") + 1] if "--baseline" in argv else None
        wall = float(argv[argv.index("--wall") + 1]) if "--wall" in argv else None
        return report(argv[2], base, wall)
    if len(argv) < 3:
        print(__doc__)
        return 2
    problems = check_run(argv[1], argv[2], argv[3:])
    if problems:
        for p in problems:
            print(f"check_ctest_run: {p}")
        return 1
    print(f"check_ctest_run: OK, {argv[2].strip()} of {argv[2].strip()} tests ran and passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
