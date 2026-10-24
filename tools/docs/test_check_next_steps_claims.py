"""Unit tests for tools/docs/check_next_steps_claims.py against synthetic fixtures."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_next_steps_claims import (
    UNVERIFIABLE_CLAIMS,
    check_claims,
    main,
)


def make_junit(cases, **hdr):
    attrs = {"tests": str(len(cases)), "failures": "0", "disabled": "0", "skipped": "0"}
    attrs.update({k: str(v) for k, v in hdr.items()})
    head = " ".join(f'{k}="{v}"' for k, v in attrs.items())
    body = ""
    for case in cases:
        name, state = case[0], case[1]
        secs = case[2] if len(case) > 2 else 0.01
        inner = "<failure message='x'/>" if state == "fail" else ""
        st = "fail" if state == "fail" else "run"
        body += f'<testcase name="{name}" status="{st}" time="{secs}">{inner}</testcase>'
    return f'<?xml version="1.0"?><testsuite name="(empty)" {head}>{body}</testsuite>'


DEFAULT_ROWS = [
    (
        "Suite size",
        "3 tests",
        r"`ctest --preset debug -N \| tail -1` (**build first** — see landmines)",
        "2026-10-24",
    ),
    (
        "Suite status",
        "all passing (3/3, 2.7 s local)",
        '`ctest --preset debug -j"$(nproc)"`',
        "2026-10-24",
    ),
    (
        "CI test step",
        "~12-17 s (#284: 14.2 s, #285: 13.8 s)",
        r"`gh run view <id> --json jobs --jq '.jobs[].steps[] \| select(.name==\"Unit tests\")'`",
        "2026-10-23",
    ),
    (
        "Build",
        "debug preset",
        '`cmake --preset debug && cmake --build --preset debug -j"$(nproc)"`',
        "2026-10-23",
    ),
]


def make_doc(rows=DEFAULT_ROWS):
    lines = [
        "# Next Steps — Build Handoff",
        "",
        "## State",
        "",
        "| Claim | Value | Verify with | Checked |",
        "|---|---|---|---|",
    ]
    for claim, val, verify, checked in rows:
        lines.append(f"| {claim} | {val} | {verify} | {checked} |")
    lines += ["", "## Next work", "", "None.", ""]
    return "\n".join(lines)


class CheckNextStepsClaimsTest(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="mosaic_next_steps_claims_"))
        self.junit_3_ok = self.d / "junit_3_ok.xml"
        self.junit_3_ok.write_text(
            make_junit([("t1", "ok"), ("t2", "ok"), ("t3", "ok")]),
            encoding="utf-8",
        )
        self.presets = self.d / "CMakePresets.json"
        self.presets.write_text(
            json.dumps(
                {
                    "version": 3,
                    "configurePresets": [{"name": "debug"}, {"name": "release"}],
                    "buildPresets": [{"name": "debug"}, {"name": "release"}],
                }
            ),
            encoding="utf-8",
        )

    def write_doc(self, text: str) -> Path:
        p = self.d / "next-steps.md"
        p.write_text(text, encoding="utf-8")
        return p

    def test_clean_table_exits_0_and_reports_skipped_with_reason(self):
        doc = self.write_doc(make_doc(DEFAULT_ROWS))
        self.assertEqual(
            check_claims(doc, junit_path=self.junit_3_ok, presets_path=self.presets),
            [],
        )
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = main(
                [
                    "check_next_steps_claims.py",
                    str(doc),
                    "--junit",
                    str(self.junit_3_ok),
                    "--presets",
                    str(self.presets),
                ]
            )
        out = buf.getvalue()
        self.assertEqual(rc, 0)
        self.assertIn("OK, 4 State row(s) examined", out)
        self.assertIn("CI test step", out)
        self.assertIn(UNVERIFIABLE_CLAIMS["CI test step"], out)

    def test_wrong_suite_size_is_red_and_names_both_numbers(self):
        rows = list(DEFAULT_ROWS)
        rows[0] = ("Suite size", "1462 tests", rows[0][2], rows[0][3])
        doc = self.write_doc(make_doc(rows))
        problems = check_claims(doc, junit_path=self.junit_3_ok, presets_path=self.presets)
        self.assertEqual(len(problems), 1)
        self.assertIn("Suite size", problems[0])
        self.assertIn("1462", problems[0])
        self.assertIn("3", problems[0])

    def test_wrong_suite_status_count_is_red(self):
        rows = list(DEFAULT_ROWS)
        rows[1] = ("Suite status", "all passing (9999/9999, 2.7 s local)", rows[1][2], rows[1][3])
        doc = self.write_doc(make_doc(rows))
        problems = check_claims(doc, junit_path=self.junit_3_ok, presets_path=self.presets)
        self.assertEqual(len(problems), 1)
        self.assertIn("Suite status", problems[0])
        self.assertIn("9999/9999", problems[0])
        self.assertIn("3", problems[0])

    def test_failed_case_in_junit_makes_suite_status_red_even_when_counts_agree(self):
        junit_fail = self.d / "junit_3_fail.xml"
        junit_fail.write_text(
            make_junit([("t1", "ok"), ("t2", "fail"), ("t3", "ok")], failures=1),
            encoding="utf-8",
        )
        doc = self.write_doc(make_doc(DEFAULT_ROWS))
        problems = check_claims(doc, junit_path=junit_fail, presets_path=self.presets)
        self.assertTrue(problems)
        self.assertTrue(any("Suite status" in p and "t2" in p for p in problems), problems)

    def test_unrecognised_claim_is_red_and_names_the_claim(self):
        rows = list(DEFAULT_ROWS) + [
            ("Main branch SHA", "abc1234", "`git rev-parse --short HEAD`", "2026-10-24")
        ]
        doc = self.write_doc(make_doc(rows))
        problems = check_claims(doc, junit_path=self.junit_3_ok, presets_path=self.presets)
        self.assertEqual(len(problems), 1)
        self.assertIn("Main branch SHA", problems[0])

    def test_build_row_with_missing_preset_is_red(self):
        rows_cfg = list(DEFAULT_ROWS)
        rows_cfg[3] = (
            "Build",
            "missing preset",
            '`cmake --preset nonexistent_cfg && cmake --build --preset debug -j"$(nproc)"`',
            "2026-10-24",
        )
        doc_cfg = self.write_doc(make_doc(rows_cfg))
        problems_cfg = check_claims(doc_cfg, junit_path=self.junit_3_ok, presets_path=self.presets)
        self.assertEqual(len(problems_cfg), 1)
        self.assertIn("nonexistent_cfg", problems_cfg[0])

        rows_bld = list(DEFAULT_ROWS)
        rows_bld[3] = (
            "Build",
            "missing build preset",
            '`cmake --preset debug && cmake --build --preset nonexistent_bld -j"$(nproc)"`',
            "2026-10-24",
        )
        doc_bld = self.write_doc(make_doc(rows_bld))
        problems_bld = check_claims(doc_bld, junit_path=self.junit_3_ok, presets_path=self.presets)
        self.assertEqual(len(problems_bld), 1)
        self.assertIn("nonexistent_bld", problems_bld[0])

    def test_empty_state_table_or_missing_state_section_is_red(self):
        empty_table_doc = self.write_doc(make_doc([]))
        problems_empty = check_claims(
            empty_table_doc, junit_path=self.junit_3_ok, presets_path=self.presets
        )
        self.assertTrue(problems_empty, "a State table with no rows must not pass vacuously")

        no_state_doc = self.write_doc("# Next Steps\n\n## Next work\n\nNothing.\n")
        problems_missing = check_claims(
            no_state_doc, junit_path=self.junit_3_ok, presets_path=self.presets
        )
        self.assertTrue(problems_missing, "a file with no State table must not pass vacuously")


if __name__ == "__main__":
    unittest.main()
