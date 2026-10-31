"""Unit tests for tools/docs/check_checked_column_staleness.py.

Structured as explicit controls, because the check is a new guard and the likeliest way it
ships broken is not "it cannot fail" but "it flags everything":

  * NEGATIVE controls (`test_negative_control_*`): a `Value` changed on a date later than
    its `Checked` cell -> the check must FAIL, naming the row, both values and both dates.
  * POSITIVE controls (`test_positive_control_*`): value and date both moved; a value
    changed on the same day the date already names; nothing changed at all; a row added
    wholesale; a row removed; a `Claim` renamed; only `Verify with` reworded; a
    whitespace-only reflow of the `Value` cell -> the check must PASS.

The same-day positive control is the one that distinguishes this check from the naive
"the `Checked` cell must differ" rule, which reds 8 of the 20 most recent commits to touch
the real handoff and is right about one of them.

Both sides are driven from synthetic fixtures. `StalenessGitResolutionTest` builds
throwaway git repositories in a temp dir -- never this repo's own history -- to exercise
`--base`/`--rev`, per-commit date attribution, and the multi-commit property.
"""
import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_checked_column_staleness import (  # noqa: E402
    check_staleness,
    check_staleness_texts,
    main,
    value_change_dates,
)

AS_OF = "2026-10-29"

BASE_ROWS = [
    (
        "Suite size",
        "1608 tests",
        r"`ctest --preset debug -N \| tail -1` (**build first**)",
        "2026-10-27",
    ),
    (
        "Suite status",
        "all passing (1608/1608, 4.9 s local)",
        "`ctest --preset debug`",
        "2026-10-27",
    ),
    ("Build", "debug preset", "`cmake --preset debug`", "2026-10-23"),
]


def make_doc(rows):
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


def replace_row(rows, index, **fields):
    out = list(rows)
    claim, val, verify, checked = out[index]
    out[index] = (
        fields.get("claim", claim),
        fields.get("value", val),
        fields.get("verify", verify),
        fields.get("checked", checked),
    )
    return out


class StalenessControlsTest(unittest.TestCase):
    """Text-mode controls: the change is attributed to `as_of`."""

    def setUp(self):
        self.base = make_doc(BASE_ROWS)

    def check(self, new_rows, as_of=AS_OF):
        return check_staleness_texts(self.base, make_doc(new_rows), as_of=as_of)

    # --- negative controls: it MUST be able to fail ----------------------------------
    def test_negative_control_value_moved_date_left_in_the_past_is_red(self):
        # The real #327 defect: 1608 -> 1621 authored on the 28th, Checked left at the 27th.
        problems, compared, changes = self.check(
            replace_row(BASE_ROWS, 0, value="1621 tests"), as_of="2026-10-28"
        )
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("O2", problems[0])
        self.assertIn("Suite size", problems[0])
        self.assertIn("1608 tests", problems[0])
        self.assertIn("1621 tests", problems[0])
        self.assertIn("2026-10-28", problems[0])
        self.assertIn("2026-10-27", problems[0])
        self.assertEqual((compared, changes), (3, 1))

    def test_negative_control_reports_every_offending_row_not_just_the_first(self):
        new_rows = replace_row(BASE_ROWS, 0, value="1621 tests")
        new_rows = replace_row(new_rows, 1, value="all passing (1621/1621, 4.6 s local)")
        problems, _, changes = check_staleness_texts(
            self.base, make_doc(new_rows), as_of=AS_OF
        )
        self.assertEqual(len(problems), 2, problems)
        self.assertTrue(any("Suite size" in p for p in problems))
        self.assertTrue(any("Suite status" in p for p in problems))
        self.assertEqual(changes, 2)

    def test_negative_control_date_moved_but_still_before_the_change_is_red(self):
        problems, _, _ = self.check(
            replace_row(BASE_ROWS, 0, value="1621 tests", checked="2026-10-28")
        )
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("2026-10-28", problems[0])
        self.assertIn("2026-10-29", problems[0])

    def test_negative_control_date_moved_backwards_is_red(self):
        problems, _, _ = self.check(
            replace_row(BASE_ROWS, 0, value="1621 tests", checked="2026-10-20")
        )
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("O2", problems[0])

    def test_non_iso_checked_cell_is_red(self):
        problems, _, _ = self.check(replace_row(BASE_ROWS, 0, checked="always"))
        self.assertTrue(any("O3" in p and "always" in p for p in problems), problems)

    def test_missing_or_empty_state_table_is_red(self):
        self.assertTrue(check_staleness_texts(self.base, make_doc([]), as_of=AS_OF)[0])
        self.assertTrue(check_staleness_texts(self.base, "# Doc\n\nno table\n", as_of=AS_OF)[0])
        self.assertTrue(check_staleness_texts("# Doc\n\nno table\n", self.base, as_of=AS_OF)[0])

    def test_table_without_a_checked_column_is_red(self):
        no_checked = "\n".join(
            [
                "# Doc",
                "",
                "## State",
                "",
                "| Claim | Value | Verify with |",
                "|---|---|---|",
                "| Suite size | 1608 tests | `ctest` |",
                "",
            ]
        )
        problems, _, _ = check_staleness_texts(no_checked, no_checked, as_of=AS_OF)
        self.assertTrue(any("Checked" in p for p in problems), problems)

    def test_duplicate_claim_is_red(self):
        problems, _, _ = self.check(BASE_ROWS + [BASE_ROWS[0]])
        self.assertTrue(any("duplicate" in p for p in problems), problems)

    # --- positive controls: it must NOT flag the legitimate cases ---------------------
    def test_positive_control_nothing_changed_is_green(self):
        problems, compared, changes = check_staleness_texts(
            self.base, self.base, as_of=AS_OF
        )
        self.assertEqual(problems, [])
        self.assertEqual((compared, changes), (3, 0))

    def test_positive_control_value_and_date_both_moved_is_green(self):
        problems, compared, changes = self.check(
            replace_row(BASE_ROWS, 0, value="1621 tests", checked=AS_OF)
        )
        self.assertEqual(problems, [])
        self.assertEqual((compared, changes), (3, 1))

    def test_positive_control_same_day_change_with_an_untouched_date_is_green(self):
        """The control the naive 'the cell must differ' rule fails: #316's shape."""
        base_rows = replace_row(BASE_ROWS, 0, checked="2026-10-29")
        problems, _, changes = check_staleness_texts(
            make_doc(base_rows),
            make_doc(replace_row(base_rows, 0, value="1621 tests")),
            as_of="2026-10-29",
        )
        self.assertEqual(problems, [], problems)
        self.assertEqual(changes, 1)

    def test_positive_control_date_already_ahead_of_the_change_is_green(self):
        base_rows = replace_row(BASE_ROWS, 0, checked="2026-10-30")
        problems, _, _ = check_staleness_texts(
            make_doc(base_rows),
            make_doc(replace_row(base_rows, 0, value="1621 tests")),
            as_of="2026-10-29",
        )
        self.assertEqual(problems, [], problems)

    def test_positive_control_row_added_wholesale_is_green(self):
        problems, compared, changes = self.check(
            BASE_ROWS + [("CI test step", "~11-18 s", "`gh run view <id>`", "2026-10-24")]
        )
        self.assertEqual(problems, [])
        self.assertEqual((compared, changes), (3, 0))

    def test_positive_control_row_removed_is_green(self):
        problems, compared, _ = self.check(BASE_ROWS[:2])
        self.assertEqual(problems, [])
        self.assertEqual(compared, 2)

    def test_positive_control_renamed_claim_is_green_and_uncompared(self):
        problems, compared, changes = self.check(
            replace_row(BASE_ROWS, 0, claim="Unit suite size", value="1621 tests")
        )
        self.assertEqual(problems, [])
        self.assertEqual((compared, changes), (2, 0))

    def test_positive_control_verify_with_reworded_alone_is_green(self):
        # #329's shape: a `Verify with` cell gains a link, nothing else moves.
        problems, _, changes = self.check(
            replace_row(
                BASE_ROWS,
                0,
                verify=r"`ctest --preset debug -N \| tail -1` (**build first** — see [landmines](landmines.md))",
            )
        )
        self.assertEqual(problems, [])
        self.assertEqual(changes, 0)

    def test_positive_control_whitespace_only_reflow_of_value_is_green(self):
        problems, _, changes = self.check(replace_row(BASE_ROWS, 0, value="1608    tests"))
        self.assertEqual(problems, [])
        self.assertEqual(changes, 0)

    def test_positive_control_date_moved_alone_is_green(self):
        problems, _, changes = self.check(replace_row(BASE_ROWS, 0, checked=AS_OF))
        self.assertEqual(problems, [])
        self.assertEqual(changes, 0)


class ValueChangeAttributionTest(unittest.TestCase):
    def test_last_change_wins_and_unchanged_rows_are_unattributed(self):
        a = make_doc(BASE_ROWS)
        b = make_doc(replace_row(BASE_ROWS, 0, value="1621 tests"))
        c = make_doc(replace_row(BASE_ROWS, 0, value="1630 tests"))
        attributed = value_change_dates(
            [("", "base", a), ("2026-10-28", "c1", b), ("2026-10-29", "c2", c)]
        )
        self.assertEqual(attributed["Suite size"], ("2026-10-29", "c2"))
        self.assertNotIn("Build", attributed)

    def test_unparseable_intermediate_snapshot_does_not_crash_attribution(self):
        a = make_doc(BASE_ROWS)
        c = make_doc(replace_row(BASE_ROWS, 0, value="1621 tests"))
        attributed = value_change_dates(
            [("", "base", a), ("2026-10-28", "junk", "# no table"), ("2026-10-29", "c2", c)]
        )
        self.assertEqual(attributed["Suite size"], ("2026-10-29", "c2"))


class StalenessGitResolutionTest(unittest.TestCase):
    """Drive --base/--rev against throwaway git repos, never this repo's own history."""

    def make_repo(self, revisions):
        d = Path(tempfile.mkdtemp(prefix="mosaic_staleness_git_"))

        def run(*a, env_date=None):
            env = None
            if env_date:
                import os

                env = dict(os.environ)
                env["GIT_AUTHOR_DATE"] = f"{env_date}T12:00:00"
                env["GIT_COMMITTER_DATE"] = f"{env_date}T12:00:00"
            return subprocess.run(
                ["git", *a], cwd=str(d), check=True, capture_output=True, text=True, env=env
            )

        run("init", "-q", "-b", "main")
        run("config", "user.email", "t@example.invalid")
        run("config", "user.name", "T")
        doc = d / "docs/project/next-steps.md"
        doc.parent.mkdir(parents=True, exist_ok=True)
        for text, message, when in revisions:
            doc.write_text(text, encoding="utf-8")
            run("add", "-A")
            run("commit", "-q", "-m", message, env_date=when)
        return d, doc

    def test_base_rev_resolution_catches_the_stale_commit_by_its_own_author_date(self):
        stale = make_doc(replace_row(BASE_ROWS, 0, value="1621 tests"))
        repo, _ = self.make_repo(
            [
                (make_doc(BASE_ROWS), "base", "2026-10-27"),
                (stale, "stale", "2026-10-28"),
            ]
        )
        problems, _, changes = check_staleness(base="HEAD~1", rev="HEAD", repo_root=repo)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("O2", problems[0])
        self.assertIn("2026-10-28", problems[0])
        self.assertEqual(changes, 1)
        # Default new side is the working tree, which holds the same text.
        self.assertEqual(len(check_staleness(base="HEAD~1", repo_root=repo)[0]), 1)

    def test_same_day_commit_with_a_matching_date_is_green_from_git(self):
        base_rows = replace_row(BASE_ROWS, 0, checked="2026-10-28")
        repo, _ = self.make_repo(
            [
                (make_doc(base_rows), "base", "2026-10-28"),
                (
                    make_doc(replace_row(base_rows, 0, value="1621 tests")),
                    "same day",
                    "2026-10-28",
                ),
            ]
        )
        problems, _, changes = check_staleness(base="HEAD~1", rev="HEAD", repo_root=repo)
        self.assertEqual(problems, [], problems)
        self.assertEqual(changes, 1)

    def test_value_and_date_moving_in_separate_commits_passes_across_the_branch(self):
        mid = make_doc(replace_row(BASE_ROWS, 0, value="1621 tests"))
        unrelated = make_doc(
            replace_row(BASE_ROWS, 0, value="1621 tests", verify="`ctest`")
        )
        tip = make_doc(replace_row(BASE_ROWS, 0, value="1621 tests", checked="2026-10-29"))
        repo, _ = self.make_repo(
            [
                (make_doc(BASE_ROWS), "base", "2026-10-27"),
                (mid, "value only", "2026-10-28"),
                (unrelated, "unrelated reword", "2026-10-29"),
                (tip, "date catches up", "2026-10-29"),
            ]
        )
        # Branch-wide, the way CI runs it: green.
        problems, _, changes = check_staleness(base="HEAD~3", rev="HEAD", repo_root=repo)
        self.assertEqual(problems, [], problems)
        self.assertEqual(changes, 1)
        # A per-commit check would have rejected the middle commit. Asserted, not claimed.
        self.assertTrue(check_staleness(base="HEAD~3", rev="HEAD~2", repo_root=repo)[0])

    def test_later_unrelated_doc_commit_does_not_re_date_an_honest_row(self):
        """Attribution is per row, so an unrelated later commit cannot make a row stale."""
        day1 = make_doc(replace_row(BASE_ROWS, 0, value="1621 tests", checked="2026-10-28"))
        day3 = day1.replace("None.", "Something else entirely.")
        repo, _ = self.make_repo(
            [
                (make_doc(BASE_ROWS), "base", "2026-10-27"),
                (day1, "value + date together", "2026-10-28"),
                (day3, "unrelated prose, two days later", "2026-10-30"),
            ]
        )
        problems, _, changes = check_staleness(base="HEAD~2", rev="HEAD", repo_root=repo)
        self.assertEqual(problems, [], problems)
        self.assertEqual(changes, 1)

    def test_unresolvable_base_is_red_not_a_silent_skip(self):
        repo, _ = self.make_repo([(make_doc(BASE_ROWS), "base", "2026-10-27")])
        problems, _, _ = check_staleness(base="HEAD~1", rev="HEAD", repo_root=repo)
        self.assertTrue(problems)
        self.assertIn("O1", problems[0])


class StalenessCliTest(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="mosaic_staleness_cli_"))
        self.base_file = self.d / "base.md"
        self.base_file.write_text(make_doc(BASE_ROWS), encoding="utf-8")

    def run_main(self, *args):
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = main(["check_checked_column_staleness.py", *args])
        return rc, buf.getvalue()

    def write_new(self, name, rows):
        p = self.d / name
        p.write_text(make_doc(rows), encoding="utf-8")
        return p

    def test_cli_exit_0_and_summary_when_clean(self):
        new_file = self.write_new(
            "ok.md", replace_row(BASE_ROWS, 0, value="1621 tests", checked=AS_OF)
        )
        rc, out = self.run_main(
            "--base-file", str(self.base_file), "--new-file", str(new_file), "--as-of", AS_OF
        )
        self.assertEqual(rc, 0, out)
        self.assertIn("OK, 3 State row(s)", out)
        self.assertIn("1 with a changed Value", out)

    def test_cli_exit_1_and_names_the_row_when_stale(self):
        new_file = self.write_new("stale.md", replace_row(BASE_ROWS, 0, value="1621 tests"))
        rc, out = self.run_main(
            "--base-file", str(self.base_file), "--new-file", str(new_file), "--as-of", AS_OF
        )
        self.assertEqual(rc, 1)
        self.assertIn("Suite size", out)
        self.assertIn("O2", out)

    def test_cli_missing_base_file_is_red(self):
        rc, out = self.run_main("--base-file", str(self.d / "nope.md"))
        self.assertEqual(rc, 1)
        self.assertIn("base file not found", out)

    def test_cli_with_no_base_at_all_is_red(self):
        rc, out = self.run_main()
        self.assertEqual(rc, 1)
        self.assertIn("no base side given", out)


if __name__ == "__main__":
    unittest.main()
