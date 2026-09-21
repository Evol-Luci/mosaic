"""Tests for check_dependency_rules.py against synthetic fixture trees (never the
real repo -- a real-repo pass/fail is exercised separately via an actual `python3
tools/ci/check_dependency_rules.py` run over the checked-out tree, which this file
doesn't duplicate).
"""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_dependency_rules import find_violations, TIERS


def write(root: Path, rel_path: str, includes: list[str]) -> None:
    path = root / rel_path
    path.parent.mkdir(parents=True, exist_ok=True)
    body = "\n".join(f'#include "{inc}"' for inc in includes)
    path.write_text(body + "\n", encoding="utf-8")


class CheckDependencyRulesTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="mosaic_deprule_test_"))

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def test_clean_tree_has_no_violations(self):
        write(self.root, "foundation/core/thing.hpp", [])
        write(self.root, "engine/ecs/world.hpp", ["foundation/core/thing.hpp"])
        write(self.root, "studio/studio_app.hpp", ["engine/ecs/world.hpp", "foundation/core/thing.hpp"])
        write(self.root, "app/main.cpp", ["engine/ecs/world.hpp", "studio/studio_app.hpp", "modules/render_vulkan/device.hpp"])
        write(self.root, "modules/render_vulkan/device.hpp", ["engine/ecs/world.hpp"])
        self.assertEqual(find_violations(self.root), [])

    def test_engine_including_studio_is_a_violation(self):
        write(self.root, "engine/render/render_system.hpp", ["studio/studio_app.hpp"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertIn("engine", violations[0].tier)
        self.assertIn("studio", violations[0].included_tier)

    def test_studio_including_app_is_a_violation(self):
        write(self.root, "studio/studio_app.hpp", ["app/project/project_io.hpp"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].tier, "studio")
        self.assertEqual(violations[0].included_tier, "app")

    def test_foundation_including_anything_above_is_a_violation(self):
        write(self.root, "foundation/core/thing.hpp", ["engine/ecs/world.hpp"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].tier, "foundation")

    def test_modules_including_studio_or_app_is_a_violation(self):
        write(self.root, "modules/render_vulkan/device.hpp", ["studio/studio_app.hpp"])
        write(self.root, "modules/render_vulkan/other.hpp", ["app/main.cpp"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 2)

    def test_app_may_freely_depend_on_engine_studio_and_modules(self):
        write(self.root, "app/main.cpp", [
            "engine/ecs/world.hpp",
            "studio/studio_app.hpp",
            "modules/render_vulkan/device.hpp",
            "foundation/core/thing.hpp",
        ])
        self.assertEqual(find_violations(self.root), [])

    def test_same_tier_and_system_includes_are_ignored(self):
        write(self.root, "engine/ecs/world.hpp", ["engine/ecs/entity.hpp"])
        path = self.root / "engine/ecs/world.hpp"
        path.write_text(path.read_text() + '#include <vector>\n#include "vulkan/vulkan.h"\n', encoding="utf-8")
        self.assertEqual(find_violations(self.root), [])

    def test_reports_file_path_and_line_number(self):
        write(self.root, "engine/foo.hpp", ["foundation/x.hpp", "studio/y.hpp"])
        violations = find_violations(self.root)
        self.assertEqual(len(violations), 1)
        self.assertTrue(str(violations[0].file).endswith("engine/foo.hpp"))
        self.assertEqual(violations[0].line, 2)

    def test_missing_tier_directory_is_not_an_error(self):
        # A fixture tree with only some tiers present (e.g. no modules/ dir) must
        # not crash -- mirrors a partial checkout or a future tier reorganization.
        write(self.root, "foundation/core/thing.hpp", [])
        self.assertEqual(find_violations(self.root), [])

    def test_tiers_constant_matches_the_documented_forbidden_edges(self):
        # Cheap guard against silently drifting from ARCHITECTURE.md's own table
        # if someone edits one without the other.
        self.assertEqual(set(TIERS), {"foundation", "engine", "studio", "app", "modules"})


if __name__ == "__main__":
    unittest.main()
