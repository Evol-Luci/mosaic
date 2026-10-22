"""Unit tests for tools/docs/archive_shipped_backlog.py.

Conservation is the contract; test_conservation_catches_* are its negative
control. Without them verify_conservation could `return []` unconditionally and
every other test here would still pass.

Conservation does NOT prove the RIGHT entries moved, so classification has its
own tests: a live entry that merely mentions strikethrough must stay live.
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import archive_shipped_backlog as arc

LIVE = "- **A live item** that has not shipped.\n  More detail on a second line."
SHIPPED = ("- ~~**Exposure control**~~ — **SHIPPED 2026-10-07.** The framing was wrong;\n"
           "  the UBO grew by a vec4 instead.")
UNDATED = "- ~~**An old idea**~~ — SHIPPED, but nobody wrote down when."
ROW = ("| ~~**Pointer input from the viewport**~~ — **SHIPPED 2026-10-07**, play-mode only. "
       "| Not the one-liner it resembles. |")
ROW_LIVE = "| **`ctest -j` in CI** | The serial step is the largest fixed cost. |"
ROW_MENTIONS = "| **A live row** | It once said ~~this~~ and was corrected. |"

# A REAL live entry, verbatim from the backlog: it contains `~~` in its body
# (two struck spans) but its title is not struck, so it is pending work.
REAL_LIVE_WITH_STRIKETHROUGH_IN_BODY = """- **No way to apply an impulse or set a velocity.** `PhysicsBackend`'s
  only write path into the simulation is `set_body_transform`, which
  teleports a body *and resets its velocity*, and `BodyDef` has no
  initial-velocity field. So a body can be positioned but never pushed —
  nothing in the engine can make an object move under its own momentum.
  This blocks click-to-nudge, any gameplay verb (throwing, knockback,
  jumping), and even a scripted "roll this ball" one-liner. The real
  design question inside it: an impulse applied **at a point** induces
  torque and makes a sphere *roll*, while a center-of-mass impulse makes
  it *slide* — the demo needs the former, so the API must take a world
  point, not just a direction. ~~**SHIPPED 2026-09-29**~~ --
  `PhysicsBackend::apply_impulse(BodyHandle, impulse, world_point)` plus an
  entity-level `PhysicsSystem::apply_impulse` and a `ClickNudgeSystem` that
  turns a click into an impulse along the camera ray. See
  `docs/project/next-steps.md` for the full entry, including the Critical
  dead-code bug it shipped past a fully green suite."""

DOC = f"""# Deferred work

## Rendering

{LIVE}
{SHIPPED}
{UNDATED}

### Tier table

| Item | Why |
|---|---|
{ROW}
{ROW_LIVE}
{ROW_MENTIONS}
"""


class ParseTest(unittest.TestCase):
    def test_finds_struck_bullets_and_rows(self):
        _, items = arc.parse_items(DOC)
        self.assertEqual(len([i for i in items if i.struck]), 3)
        self.assertEqual(len([i for i in items if i.kind == "row" and i.struck]), 1)

    def test_a_live_bullet_is_not_struck(self):
        _, items = arc.parse_items(DOC)
        live = [i for i in items if not i.struck]
        self.assertTrue(any("A live item" in i.text for i in live))

    def test_multiline_bullet_keeps_its_continuation(self):
        _, items = arc.parse_items(DOC)
        exposure = next(i for i in items if "Exposure control" in i.text)
        self.assertIn("UBO grew by a vec4", exposure.text)


class ClassificationTest(unittest.TestCase):
    def test_live_bullet_that_mentions_strikethrough_is_not_struck(self):
        fx = REAL_LIVE_WITH_STRIKETHROUGH_IN_BODY
        self.assertIn("~~", fx)                      # the fixture really has it
        self.assertFalse(fx.startswith("- ~~"))      # ... but not as its title
        _, items = arc.parse_items("## Physics\n\n" + fx + "\n")
        self.assertEqual(len(items), 1)
        self.assertFalse(items[0].struck)

    def test_live_row_that_mentions_strikethrough_is_not_struck(self):
        _, items = arc.parse_items(DOC)
        row = next(i for i in items if "A live row" in i.text)
        self.assertEqual(row.kind, "row")
        self.assertFalse(row.struck)

    def test_such_an_entry_is_never_moved(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            src = "## Physics\n\n" + REAL_LIVE_WITH_STRIKETHROUGH_IN_BODY + "\n\n" + SHIPPED + "\n"
            doc.write_text(src, encoding="utf-8")
            arc.run(doc, Path(d) / "record", dry_run=False)
            self.assertIn(REAL_LIVE_WITH_STRIKETHROUGH_IN_BODY, doc.read_text(encoding="utf-8"))
            blob = "".join(p.read_text(encoding="utf-8") for p in (Path(d) / "record").glob("*.md"))
            self.assertNotIn("No way to apply an impulse", blob)


class DateTest(unittest.TestCase):
    def test_shipped_date_decides_the_month(self):
        _, items = arc.parse_items(DOC)
        exposure = next(i for i in items if "Exposure control" in i.text)
        self.assertEqual(exposure.date, "2026-10-07")

    def test_undated_shipped_entry_is_not_guessed(self):
        _, items = arc.parse_items(DOC)
        old = next(i for i in items if "An old idea" in i.text)
        self.assertIsNone(old.date)

    def test_undated_entries_go_to_a_catch_all_month(self):
        _, items = arc.parse_items(DOC)
        _, moved = arc.plan([i for i in items if i.struck])
        self.assertIn(arc.UNDATED_FILE, moved)
        self.assertEqual(len(moved[arc.UNDATED_FILE]), 1)

    def test_fixed_and_done_markers_date_an_entry_too(self):
        _, items = arc.parse_items("- ~~**X**~~ — **FIXED 2026-10-12**, done.\n")
        self.assertEqual(items[0].date, "2026-10-12")


class WrappedDateTest(unittest.TestCase):
    def test_date_on_the_next_line_still_counts(self):
        _, items = arc.parse_items("- ~~**X**~~ — **SHIPPED\n  2026-10-02.** body.\n")
        self.assertEqual(items[0].date, "2026-10-02")

    def test_a_date_in_a_later_sentence_is_not_taken(self):
        _, items = arc.parse_items("- ~~**X**~~ — SHIPPED. Filed 2026-09-01 originally.\n")
        self.assertIsNone(items[0].date)


class ConservationTest(unittest.TestCase):
    def _split(self):
        _, items = arc.parse_items(DOC)
        struck = [i for i in items if i.struck]
        _, moved = arc.plan(struck)
        return struck, moved

    def test_clean_split_reports_no_problems(self):
        struck, moved = self._split()
        self.assertEqual(arc.verify_conservation(struck, moved), [])

    def test_conservation_catches_a_dropped_entry(self):
        struck, moved = self._split()
        moved[next(iter(moved))] = []
        self.assertTrue(arc.verify_conservation(struck, moved))

    def test_conservation_catches_a_duplicated_entry(self):
        struck, moved = self._split()
        k = next(iter(moved))
        moved[k] = moved[k] * 2
        self.assertTrue(arc.verify_conservation(struck, moved))

    def test_conservation_catches_a_mutated_entry(self):
        struck, moved = self._split()
        k = next(iter(moved))
        moved[k] = [moved[k][0]._replace(text=moved[k][0].text + " tampered")]
        self.assertTrue(arc.verify_conservation(struck, moved))


class SlugTest(unittest.TestCase):
    def test_backticks_and_punctuation_do_not_break_the_anchor(self):
        self.assertEqual(arc.slug("`ctest -j` in CI"), "ctest--j-in-ci")
        self.assertEqual(arc.slug("UI/UX: the **thing**"), "uiux-the-thing")

    def test_stub_anchor_matches_the_heading_run_writes(self):
        # Compare both sides: the stub's anchor against the anchor GitHub would
        # compute for the heading text run() writes.
        _, items = arc.parse_items(DOC)
        it = next(i for i in items if i.struck and i.title)
        heading_anchor = arc.heading_anchors(f"## {it.title}\n")[0]
        self.assertIn(f"#{heading_anchor})", arc.stub_for(it, "2026-10"))

    def test_a_differing_heading_would_be_caught(self):
        _, items = arc.parse_items(DOC)
        it = next(i for i in items if i.struck and i.title)
        self.assertNotIn(f"#{arc.slug(it.title + ' changed')})",
                         arc.stub_for(it, "2026-10"))

    def test_repeated_titles_get_distinct_resolving_anchors(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            doc.write_text("## S\n\n- ~~**Same**~~ — SHIPPED 2026-10-01, a.\n"
                           "- ~~**Same**~~ — SHIPPED 2026-10-02, b.\n", encoding="utf-8")
            rec = Path(d) / "record"
            arc.run(doc, rec, dry_run=False)
            anchors = set(arc.heading_anchors((rec / "2026-10.md").read_text(encoding="utf-8")))
            import re
            links = re.findall(r"\.md#([^)]+)\)", doc.read_text(encoding="utf-8"))
            self.assertEqual(len(set(links)), 2)
            self.assertTrue(set(links) <= anchors)


class StubTest(unittest.TestCase):
    def test_row_stub_is_still_a_table_row_with_matching_columns(self):
        _, items = arc.parse_items(DOC)
        row = next(i for i in items if i.kind == "row" and i.struck)
        stub = arc.stub_for(row, "2026-10")
        self.assertTrue(stub.startswith("| ~~") and stub.endswith("|"))
        self.assertEqual(stub.count("|"), row.header.split("\n")[0].count("|"))


class StatusWordTest(unittest.TestCase):
    def test_stub_keeps_the_entrys_own_status_word(self):
        _, items = arc.parse_items("- ~~**X**~~ — **WITHDRAWN 2026-10-09**, premise wrong.\n")
        self.assertIn("WITHDRAWN 2026-10-09", arc.stub_for(items[0], "2026-10"))
        self.assertNotIn("SHIPPED", arc.stub_for(items[0], "2026-10"))


class RunTest(unittest.TestCase):
    def test_is_idempotent(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            doc.write_text(DOC, encoding="utf-8")
            rec = Path(d) / "record"
            arc.run(doc, rec, dry_run=False)
            once = doc.read_text(encoding="utf-8")
            files_once = {p.name: p.read_text(encoding="utf-8") for p in rec.glob("*.md")}
            arc.run(doc, rec, dry_run=False)
            self.assertEqual(doc.read_text(encoding="utf-8"), once)
            self.assertEqual({p.name: p.read_text(encoding="utf-8")
                              for p in rec.glob("*.md")}, files_once)

    def test_a_stub_replaces_each_moved_entry(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            doc.write_text(DOC, encoding="utf-8")
            arc.run(doc, Path(d) / "record", dry_run=False)
            after = doc.read_text(encoding="utf-8")
            self.assertIn("Exposure control", after)          # still findable here
            self.assertNotIn("UBO grew by a vec4", after)      # body has moved
            self.assertIn("A live item", after)                # live untouched
            self.assertIn("A live row", after)

    def test_existing_record_content_survives_byte_identical(self):
        with tempfile.TemporaryDirectory() as d:
            doc = Path(d) / "backlog.md"
            doc.write_text(DOC, encoding="utf-8")
            rec = Path(d) / "record"
            rec.mkdir()
            prior = "# October 2026 — the record\n\n## Where we are\n\nhandoff text\n"
            (rec / "2026-10.md").write_text(prior, encoding="utf-8")
            arc.run(doc, rec, dry_run=False)
            self.assertTrue((rec / "2026-10.md").read_text(encoding="utf-8").startswith(prior))


if __name__ == "__main__":
    unittest.main()
