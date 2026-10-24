"""Tests for check_dependency_pins.py against synthetic fixture trees (never the
real repo -- a real-repo pass/fail is exercised separately via an actual `python3
tools/ci/check_dependency_pins.py` run over the checked-out tree, which this file
doesn't duplicate).
"""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_dependency_pins import check_git_tag, find_violations


def write_cmake(root: Path, content: str, rel_path: str = "CMakeLists.txt") -> None:
    path = root / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


class CheckDependencyPinsTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="mosaic_deppin_test_"))

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_full_lowercase_sha_with_version_comment_is_clean(self):
        write_cmake(
            self.root,
            """
            include(FetchContent)
            FetchContent_Declare(
              CLI11
              GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
              GIT_TAG        6c7b07a878ad834957b98d0f9ce1dbe0cb204fc9  # v2.4.2
              GIT_SHALLOW    TRUE
            )
            """,
        )
        self.assertEqual(find_violations(self.root), [])

    def test_mutable_tag_is_a_violation(self):
        write_cmake(
            self.root,
            """
            FetchContent_Declare(
              Catch2
              GIT_REPOSITORY https://github.com/catchorg/Catch2.git
              GIT_TAG        v3.5.2
              GIT_SHALLOW    TRUE
            )
            """,
        )
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].dependency, "Catch2")
        self.assertEqual(violations[0].git_tag, "v3.5.2")
        self.assertIn("mutable tag", violations[0].reason)
        self.assertEqual(violations[0].line, 5)

    def test_uppercase_sha_is_a_violation(self):
        write_cmake(
            self.root,
            """
            FetchContent_Declare(
              box3d
              GIT_REPOSITORY https://github.com/erincatto/box3d.git
              GIT_TAG        30C67B5E6D0A3A66F0F506C69CE9E9E0587E3B7C
            )
            """,
        )
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].dependency, "box3d")
        self.assertEqual(violations[0].git_tag, "30C67B5E6D0A3A66F0F506C69CE9E9E0587E3B7C")
        self.assertIn("uppercase", violations[0].reason)

    def test_truncated_sha_is_a_violation(self):
        write_cmake(
            self.root,
            """
            FetchContent_Declare(
              box3d
              GIT_REPOSITORY https://github.com/erincatto/box3d.git
              GIT_TAG        30c67b5e6d0a
            )
            """,
        )
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].dependency, "box3d")
        self.assertEqual(violations[0].git_tag, "30c67b5e6d0a")
        self.assertIn("12-character hex prefix", violations[0].reason)

    def test_file_with_no_declarations_is_a_violation_not_vacuously_clean(self):
        write_cmake(
            self.root,
            """
            cmake_minimum_required(VERSION 3.20)
            project(mosaic LANGUAGES CXX)
            """,
        )
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].dependency, "<none>")
        self.assertIn("no FetchContent_Declare", violations[0].reason)

    def test_missing_cmake_file_is_a_violation_not_vacuously_clean(self):
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].dependency, "<none>")

    def test_missing_git_tag_in_declaration_is_a_violation(self):
        write_cmake(
            self.root,
            """
            FetchContent_Declare(
              broken_dep
              GIT_REPOSITORY https://github.com/example/broken.git
            )
            """,
        )
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].dependency, "broken_dep")
        self.assertEqual(violations[0].git_tag, "<missing>")

    def test_check_git_tag_direct(self):
        self.assertEqual(check_git_tag("6c7b07a878ad834957b98d0f9ce1dbe0cb204fc9"), "")
        self.assertNotEqual(check_git_tag("v2.4.2"), "")
        self.assertNotEqual(check_git_tag("6c7b07a"), "")
        self.assertNotEqual(check_git_tag("6C7B07A878AD834957B98D0F9CE1DBE0CB204FC9"), "")


if __name__ == "__main__":
    unittest.main()
