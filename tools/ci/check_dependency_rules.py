#!/usr/bin/env python3
"""Enforces Mosaic's directory-ownership dependency rules (docs/architecture/
ARCHITECTURE.md's "Dependency rules" section): a file's `#include` graph must
never cross a forbidden tier edge. Run from anywhere; scans the real repo root
by default, or a fixture tree when imported by test_check_dependency_rules.py.

Run from the repo root: python3 tools/ci/check_dependency_rules.py
Exits 0 with no output if clean; exits 1 and prints every violation otherwise.
"""
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# Real top-level directories that participate in the tier system (tests/, tools/,
# templates/, packages/, docs/ etc. sit outside it entirely and are not scanned).
TIERS = ("foundation", "engine", "studio", "app", "modules")

# tier -> set of tiers it may depend on (besides itself). Mirrors ARCHITECTURE.md's
# "Allowed" list exactly -- see that doc for the real #include-direction evidence
# this mapping is based on.
ALLOWED = {
    "foundation": set(),
    "engine": {"foundation"},
    "studio": {"engine", "foundation"},
    "app": {"engine", "studio", "modules", "foundation"},
    "modules": {"engine", "foundation"},
}

INCLUDE_RE = re.compile(r'^\s*#include\s*"([^"]+)"')


@dataclass
class Violation:
    file: Path
    line: int
    tier: str
    included_tier: str
    included_path: str


def _tier_of(rel_path: str) -> str | None:
    first_segment = rel_path.split("/", 1)[0]
    return first_segment if first_segment in TIERS else None


def find_violations(root: Path) -> list[Violation]:
    violations: list[Violation] = []
    for tier in TIERS:
        tier_dir = root / tier
        if not tier_dir.is_dir():
            continue
        for path in sorted(tier_dir.rglob("*")):
            if path.suffix not in (".hpp", ".cpp", ".h", ".cc", ".inl"):
                continue
            allowed = ALLOWED.get(tier, set())
            for lineno, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
            ):
                match = INCLUDE_RE.match(line)
                if not match:
                    continue
                included_path = match.group(1)
                included_tier = _tier_of(included_path)
                if included_tier is None or included_tier == tier:
                    continue  # not a tier-owned header (system/third-party), or same-tier
                if included_tier not in allowed:
                    violations.append(
                        Violation(path, lineno, tier, included_tier, included_path)
                    )
    return violations


def main() -> int:
    violations = find_violations(REPO_ROOT)
    if not violations:
        return 0
    print("Dependency rule violations found (see docs/architecture/ARCHITECTURE.md):")
    for v in violations:
        rel = v.file.relative_to(REPO_ROOT)
        print(f"  {rel}:{v.line}: `{v.tier}` must not depend on `{v.included_tier}`"
              f' (#include "{v.included_path}")')
    print(f"\n{len(violations)} violation(s).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
