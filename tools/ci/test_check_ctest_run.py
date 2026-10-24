"""Tests for check_ctest_run.py against synthetic JUnit files."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_ctest_run import MAX_CASE_SECONDS, check_run


def junit(cases, **hdr):
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


class CheckRunTest(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="mosaic_ctestrun_test_"))

    def write(self, text):
        p = self.d / "j.xml"
        p.write_text(text)
        return p

    def test_clean(self):
        p = self.write(junit([("a", "ok"), ("b", "ok")]))
        self.assertEqual(check_run(p, "2"), [])

    def test_fewer_tests_than_discovered_is_red(self):
        p = self.write(junit([("a", "ok")]))
        self.assertTrue(any("O1" in x for x in check_run(p, "2")))

    def test_failure_is_red(self):
        p = self.write(junit([("a", "ok"), ("b", "fail")], failures=1))
        self.assertTrue(any("O2" in x for x in check_run(p, "2")))

    def test_empty_or_zero_or_garbage_n_is_red_not_defaulted(self):
        p = self.write(junit([("a", "ok")]))
        for bad in ("", "0", "unset", None):
            self.assertTrue(check_run(p, bad), bad)

    def test_missing_junit_is_red(self):
        self.assertTrue(check_run(self.d / "nope.xml", "1"))

    def test_cmake_cross_check(self):
        p = self.write(junit([("a", "ok"), ("b", "ok")]))
        c = self.d / "t.cmake"
        c.write_text("add_test( [==[a]==] x )\n")
        self.assertTrue(any("add_test" in x for x in check_run(p, "2", [c])))
        c.write_text("add_test( [==[a]==] x )\nadd_test( [==[b]==] x )\n")
        self.assertEqual(check_run(p, "2", [c]), [])

    def test_over_budget_case_is_red(self):
        p = self.write(junit([("fast", "ok", 0.05), ("slow case", "ok", 12.3)]))
        problems = check_run(p, "2")
        self.assertEqual(
            problems,
            ["O3: 'slow case' took 12.3s, over the 10.0s per-case budget"],
        )
        many = self.write(junit([(f"c{i}", "ok", 11.0 + i) for i in range(7)]))
        many_problems = check_run(many, "7")
        self.assertEqual(len(many_problems), 5)
        self.assertIn("'c6' took 17s", many_problems[0])
        self.assertIn("7 cases over budget in total", many_problems[0])

    def test_case_at_budget_boundary_is_clean(self):
        p = self.write(junit([("edge", "ok", MAX_CASE_SECONDS)]))
        self.assertEqual(check_run(p, "1"), [])

    def test_custom_max_case_seconds_is_honoured(self):
        p = self.write(junit([("a", "ok", 2.5), ("b", "ok", 0.5)]))
        self.assertEqual(check_run(p, "2", max_case_seconds=3.0), [])
        problems = check_run(p, "2", max_case_seconds=2.0)
        self.assertEqual(
            problems,
            ["O3: 'a' took 2.5s, over the 2.0s per-case budget"],
        )


if __name__ == "__main__":
    unittest.main()
