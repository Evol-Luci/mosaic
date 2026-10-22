"""Unit tests for tools/docs/archive_next_steps.py.

The load-bearing property is CONSERVATION: a split must move blocks, never lose,
truncate or duplicate one. `test_conservation_catches_a_dropped_block` is the
negative control for that check -- without it, `verify_conservation` could be a
function that always returns True and every other test here would still pass.
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import archive_next_steps as arc

HEADER = "# Next Steps — Build Handoff\n\nThis is the live \"you are here\".\n"

# The three real block-start forms in docs/project/next-steps.md, verified against
# the file on 2026-10-21: a plain one, the "Previously" form that carries its date
# BEFORE the colon, and one carrying a stray pilcrow.
B_OCT21 = "**Last updated:** 2026-10-21 (newest block.)"
B_OCT20 = "**Previously, last updated 2026-10-20:** (a block in the Previously form.)"
B_SEP19 = "¶ **Last updated:** 2026-09-19 (an older block with a pilcrow.)"
B_AUG02 = "**Last updated:** 2026-08-02 (an even older block.)"
CONT = "**A continuation paragraph** belonging to the block above it."


def doc(*blocks):
    return HEADER + "\n" + "\n\n".join(blocks) + "\n"


class ParseTest(unittest.TestCase):
    def test_finds_all_three_block_forms(self):
        header, blocks = arc.parse(doc(B_OCT21, B_OCT20, B_SEP19))
        self.assertEqual([b.date for b in blocks], ["2026-10-21", "2026-10-20", "2026-09-19"])
        self.assertIn("you are here", header)

    def test_continuation_paragraphs_stay_with_their_block(self):
        header, blocks = arc.parse(doc(B_OCT21, CONT, B_SEP19))
        self.assertEqual(len(blocks), 2)
        self.assertIn(CONT, blocks[0].text)
        self.assertNotIn(CONT, blocks[1].text)

    def test_header_is_everything_before_the_first_block(self):
        header, blocks = arc.parse(doc(B_OCT21))
        self.assertTrue(header.startswith("# Next Steps"))
        self.assertNotIn("2026-10-21", header)


class PlanTest(unittest.TestCase):
    def test_current_month_stays_older_is_archived(self):
        _, blocks = arc.parse(doc(B_OCT21, B_OCT20, B_SEP19, B_AUG02))
        live, archive = arc.plan(blocks, today="2026-10-21", ceiling=10**6)
        self.assertEqual([b.date for b in live], ["2026-10-21", "2026-10-20"])
        self.assertEqual(sorted(archive), ["2026-08", "2026-09"])

    def test_ceiling_archives_oldest_in_month_blocks_too(self):
        _, blocks = arc.parse(doc(B_OCT21, B_OCT20))
        # A ceiling below the two blocks combined must push the OLDER one out.
        live, archive = arc.plan(blocks, today="2026-10-21", ceiling=len(B_OCT21) + 10)
        self.assertEqual([b.date for b in live], ["2026-10-21"])
        self.assertIn("2026-10", archive)

    def test_ceiling_never_empties_the_live_file(self):
        _, blocks = arc.parse(doc(B_OCT21, B_OCT20))
        live, _ = arc.plan(blocks, today="2026-10-21", ceiling=1)
        self.assertEqual(len(live), 1, "the newest block must always stay")


class ConservationTest(unittest.TestCase):
    def test_every_block_survives_exactly_once(self):
        _, blocks = arc.parse(doc(B_OCT21, B_OCT20, B_SEP19, B_AUG02))
        live, archive = arc.plan(blocks, today="2026-10-21", ceiling=10**6)
        self.assertEqual(arc.verify_conservation(blocks, live, archive), [])

    def test_conservation_catches_a_dropped_block(self):
        _, blocks = arc.parse(doc(B_OCT21, B_OCT20, B_SEP19))
        live, archive = arc.plan(blocks, today="2026-10-21", ceiling=10**6)
        archive["2026-09"] = []  # simulate the split losing September
        problems = arc.verify_conservation(blocks, live, archive)
        self.assertTrue(problems, "a dropped block MUST be reported")
        self.assertIn("2026-09-19", " ".join(problems))

    def test_conservation_catches_a_duplicated_block(self):
        _, blocks = arc.parse(doc(B_OCT21, B_SEP19))
        live, archive = arc.plan(blocks, today="2026-10-21", ceiling=10**6)
        archive["2026-09"] = archive["2026-09"] * 2
        self.assertTrue(arc.verify_conservation(blocks, live, archive))

    def test_conservation_catches_a_mutated_block(self):
        _, blocks = arc.parse(doc(B_OCT21, B_SEP19))
        live, archive = arc.plan(blocks, today="2026-10-21", ceiling=10**6)
        edited = archive["2026-09"][0]._replace(text=archive["2026-09"][0].text + " tampered")
        archive["2026-09"] = [edited]
        self.assertTrue(arc.verify_conservation(blocks, live, archive))


class RunTest(unittest.TestCase):
    def _tree(self, d):
        src = Path(d) / "next-steps.md"
        src.write_text(doc(B_OCT21, B_OCT20, B_SEP19, B_AUG02), encoding="utf-8")
        return src, Path(d) / "updates"

    def test_writes_archive_files_and_shrinks_the_doc(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = self._tree(d)
            _, before_blocks = arc.parse(src.read_text(encoding="utf-8"))
            arc.run(src, out, today="2026-10-21", ceiling=10**6)
            self.assertIn(B_SEP19, (out / "2026-09.md").read_text(encoding="utf-8"))
            self.assertIn(B_AUG02, (out / "2026-08.md").read_text(encoding="utf-8"))
            after = src.read_text(encoding="utf-8")
            self.assertIn(B_OCT21, after)
            self.assertNotIn(B_SEP19, after)
            # Block count, not bytes: on a toy fixture the archive pointer outweighs the
            # four one-line blocks, so a byte comparison measures the pointer, not the
            # split. Fewer blocks live than before is the property that actually matters.
            _, after_blocks = arc.parse(after)
            self.assertLess(len(after_blocks), len(before_blocks))
            self.assertEqual(len(after_blocks), 2)

    def test_live_file_keeps_its_header_and_gains_a_pointer(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = self._tree(d)
            arc.run(src, out, today="2026-10-21", ceiling=10**6)
            after = src.read_text(encoding="utf-8")
            self.assertTrue(after.startswith("# Next Steps"))
            self.assertIn("docs/updates/", after)

    def test_is_idempotent(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = self._tree(d)
            arc.run(src, out, today="2026-10-21", ceiling=10**6)
            once = src.read_text(encoding="utf-8")
            sep_once = (out / "2026-09.md").read_text(encoding="utf-8")
            arc.run(src, out, today="2026-10-21", ceiling=10**6)
            self.assertEqual(src.read_text(encoding="utf-8"), once)
            self.assertEqual((out / "2026-09.md").read_text(encoding="utf-8"), sep_once)

    def test_appends_to_an_existing_archive_without_clobbering(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = self._tree(d)
            out.mkdir()
            (out / "2026-09.md").write_text(
                "# September 2026\n\n**Last updated:** 2026-09-01 (a pre-existing block.)\n",
                encoding="utf-8")
            arc.run(src, out, today="2026-10-21", ceiling=10**6)
            text = (out / "2026-09.md").read_text(encoding="utf-8")
            self.assertIn("a pre-existing block", text)
            self.assertIn(B_SEP19, text)


if __name__ == "__main__":
    unittest.main()
