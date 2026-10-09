"""Tests for check_test_case_names.py against synthetic fixture trees (never the real
repo -- a real-repo pass is exercised separately by an actual `python3
tools/ci/check_test_case_names.py` run, which this file doesn't duplicate).
"""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_test_case_names import check_name, find_violations


def write_test_file(root: Path, rel_path: str, names: list[str]) -> None:
    path = root / "tests" / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    body = "\n".join(f'TEST_CASE("{n}", "[tag]")\n{{\n}}\n' for n in names)
    path.write_text(body, encoding="utf-8")


class CheckTestCaseNamesTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="mosaic_testname_test_"))

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_ordinary_names_are_clean(self):
        write_test_file(self.root, "unit/a_test.cpp",
                        ["a plain name", "one with (parens) and a colon: fine"])
        self.assertEqual(find_violations(self.root), [])

    def test_a_comma_is_NOT_a_violation(self):
        # The deliberate non-rule, and the reason it is a test rather than an omission:
        # CatchAddTests.cmake escapes commas when generating add_test, so these run fine
        # under ctest. A comma only breaks an UNESCAPED manual filter, which is a bug in
        # the caller. Flagging it here would have meant renaming 89 names in this repo
        # for nothing.
        write_test_file(self.root, "unit/b_test.cpp",
                        ["slots recycle, invalidating old handles",
                         "Quat default and identity are (0,0,0,1)"])
        self.assertEqual(find_violations(self.root), [])

    def test_leading_double_dash_is_a_violation(self):
        write_test_file(self.root, "unit/c_test.cpp", ["--help is not a test name"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertIn("begins with `--`", violations[0].reason)
        self.assertEqual(violations[0].line, 1)

    def test_a_single_leading_dash_is_fine(self):
        # Only `--` is parsed as a flag; a single dash is an ordinary character, and
        # rejecting it would ban names like "-1 clamps to zero".
        write_test_file(self.root, "unit/d_test.cpp", ["-1 clamps to zero"])
        self.assertEqual(find_violations(self.root), [])

    def test_unbalanced_open_bracket_is_a_violation(self):
        write_test_file(self.root, "unit/e_test.cpp", ["operator[ is unbalanced"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertIn("unbalanced brackets", violations[0].reason)

    def test_unbalanced_close_bracket_is_also_a_violation(self):
        # Caught by the same count comparison rather than by a separate rule, and worth
        # its own case so a rule rewritten as "count('[') > count(']')" fails here.
        write_test_file(self.root, "unit/f_test.cpp", ["operator] is unbalanced"])
        self.assertEqual(len(find_violations(self.root)), 1)

    def test_balanced_brackets_in_a_name_are_fine(self):
        write_test_file(self.root, "unit/g_test.cpp",
                        ["over-life helpers clamp t to [0,1] (no extrapolation)"])
        self.assertEqual(find_violations(self.root), [])

    def test_template_test_case_and_scenario_are_scanned_too(self):
        path = self.root / "tests" / "unit" / "h_test.cpp"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            'TEMPLATE_TEST_CASE("--templated", "[t]", int)\n{\n}\n'
            'SCENARIO("--scenario", "[s]")\n{\n}\n',
            encoding="utf-8")
        self.assertEqual(len(find_violations(self.root)), 2)

    def test_an_escaped_quote_does_not_truncate_the_name(self):
        # The regex is escape-aware so a name containing \" is read whole. If it were
        # not, the name below would be read as `say \` and the unbalanced bracket after
        # the quote would be missed.
        path = self.root / "tests" / "unit" / "i_test.cpp"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('TEST_CASE("say \\"hi\\" to [unbalanced", "[t]")\n{\n}\n',
                        encoding="utf-8")
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertIn("unbalanced brackets", violations[0].reason)

    def test_reports_the_right_line_number(self):
        write_test_file(self.root, "unit/j_test.cpp",
                        ["fine one", "also fine", "--bad one"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 9)  # 4 lines per generated case

    def test_check_name_is_usable_directly(self):
        self.assertEqual(check_name("ordinary"), "")
        self.assertEqual(check_name("with, a comma"), "")
        self.assertNotEqual(check_name("--flag"), "")
        self.assertNotEqual(check_name("un[balanced"), "")

    def test_a_tree_with_no_tests_dir_is_clean(self):
        self.assertEqual(find_violations(self.root), [])


if __name__ == "__main__":
    unittest.main()
