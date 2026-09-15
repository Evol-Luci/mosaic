"""Unit tests for tools/docs/regenerate_nav.py."""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regenerate_nav as nav


class TitleDerivationTest(unittest.TestCase):
    def test_extracts_h1_verbatim(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.md"
            p.write_text("# Asset Pipeline\n\nBody text.\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Asset Pipeline")
            self.assertFalse(used_fallback)

    def test_strips_inline_backticks(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.md"
            p.write_text("# Engine `World` runtime — design\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Engine World runtime — design")
            self.assertFalse(used_fallback)

    def test_falls_back_to_filename_when_no_heading(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "2026-08-15-mosaic-docs-hub.md"
            p.write_text("Just some prose, no heading.\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Mosaic Docs Hub")
            self.assertTrue(used_fallback)

    def test_filename_fallback_strips_design_suffix(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "2026-08-16-engine-world-runtime-design.md"
            p.write_text("no heading here\n")
            title, used_fallback = nav.derive_title(p)
            self.assertEqual(title, "Engine World Runtime")
            self.assertTrue(used_fallback)


class SortModeTest(unittest.TestCase):
    def _files(self, names):
        return [Path(n) for n in names]

    def test_newest_first_reverses_filename_order(self):
        files = self._files([
            "updates/2026-08-16-summary.md",
            "updates/2026-08-23-summary.md",
            "updates/2026-08-21-summary.md",
        ])
        result = [f.name for f in nav.sort_files(files, "newest-first")]
        self.assertEqual(result, [
            "2026-08-23-summary.md",
            "2026-08-21-summary.md",
            "2026-08-16-summary.md",
        ])

    def test_oldest_first_keeps_filename_order(self):
        files = self._files([
            "specs/2026-08-16-x-design.md",
            "specs/2026-08-15-y-design.md",
        ])
        result = [f.name for f in nav.sort_files(files, "oldest-first")]
        self.assertEqual(result, [
            "2026-08-15-y-design.md",
            "2026-08-16-x-design.md",
        ])

    def test_alphabetical_is_case_insensitive(self):
        files = self._files([
            "architecture/data-model.md",
            "architecture/ARCHITECTURE.md",
            "architecture/boundaries.md",
        ])
        result = [f.name for f in nav.sort_files(files, "alphabetical")]
        self.assertEqual(result, [
            "ARCHITECTURE.md",
            "boundaries.md",
            "data-model.md",
        ])


class SpliceGroupTest(unittest.TestCase):
    FIXTURE = (
        "<div class=\"nav-group\">\n"
        "    <h3>Design</h3>\n"
        "    <!-- AUTO-GENERATED:design -->\n"
        "    OLD CONTENT\n"
        "    <!-- END-AUTO-GENERATED -->\n"
        "</div>\n"
        "<div class=\"nav-group\">\n"
        "    <h3>Architecture</h3>\n"
        "    <!-- AUTO-GENERATED:architecture -->\n"
        "    OTHER OLD CONTENT\n"
        "    <!-- END-AUTO-GENERATED -->\n"
        "</div>\n"
    )

    def test_replaces_only_the_named_group(self):
        result = nav.splice_group(self.FIXTURE, "design", "NEW CONTENT")
        self.assertIn("NEW CONTENT", result)
        # Anchored to design's own start marker specifically -- a bare
        # "OLD CONTENT\n    <!-- END" substring check would also match inside
        # the untouched Architecture group's "OTHER OLD CONTENT\n    <!-- END
        # -->" text, since "OLD CONTENT" is itself a substring of "OTHER OLD
        # CONTENT". This was a real bug in this test, not in splice_group.
        self.assertNotIn(":design -->\n    OLD CONTENT", result)
        self.assertIn("OTHER OLD CONTENT", result)

    def test_missing_start_marker_raises(self):
        broken = self.FIXTURE.replace("<!-- AUTO-GENERATED:design -->\n", "")
        with self.assertRaises(RuntimeError):
            nav.splice_group(broken, "design", "NEW CONTENT")

    def test_missing_end_marker_raises(self):
        broken = self.FIXTURE.replace("<!-- END-AUTO-GENERATED -->\n</div>\n"
                                      "<div class=\"nav-group\">\n"
                                      "    <h3>Architecture</h3>",
                                      "</div>\n<div class=\"nav-group\">\n"
                                      "    <h3>Architecture</h3>", 1)
        with self.assertRaises(RuntimeError):
            nav.splice_group(broken, "design", "NEW CONTENT")


class EndToEndSmokeTest(unittest.TestCase):
    def test_regenerates_real_docs_tree_idempotently(self):
        repo_root = nav.REPO_ROOT
        with tempfile.TemporaryDirectory() as d:
            tmp_docs = Path(d) / "docs"
            shutil.copytree(repo_root / "docs", tmp_docs)

            original_docs_dir = nav.DOCS_DIR
            original_index = nav.INDEX_HTML
            nav.DOCS_DIR = tmp_docs
            nav.INDEX_HTML = tmp_docs / "index.html"
            try:
                nav.main()
                first_pass = nav.INDEX_HTML.read_text(encoding="utf-8")
                nav.main()
                second_pass = nav.INDEX_HTML.read_text(encoding="utf-8")
            finally:
                nav.DOCS_DIR = original_docs_dir
                nav.INDEX_HTML = original_index

            self.assertEqual(first_pass, second_pass)
            self.assertIn("design/scripting.md", first_pass)
            self.assertIn(
                "superpowers/specs/2026-09-14-docs-hub-nav-generator-design.md",
                first_pass)


if __name__ == "__main__":
    unittest.main()
