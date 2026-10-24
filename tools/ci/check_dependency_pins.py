#!/usr/bin/env python3
"""Enforces that every CMake FetchContent_Declare in the repository pins GIT_TAG
to an immutable 40-character lowercase hex commit SHA rather than a mutable tag
or branch name.

An upstream git tag can be moved or deleted in place, silently changing what the
project builds. Because every FetchContent dependency here is hosted on GitHub
(which permits `git fetch --depth 1 origin <40-hex-sha>`), pinning to full commit
SHAs works alongside `GIT_SHALLOW TRUE`.

Also fails if no FetchContent_Declare blocks are found at all, so a broken regex
or moved file cannot pass vacuously.

Run from anywhere: python3 tools/ci/check_dependency_pins.py
Exits 0 with no output if clean; exits 1 and prints every violation otherwise.
"""
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

DECLARE_RE = re.compile(r"\bFetchContent_Declare\s*\(([^)]*)\)", re.DOTALL)
GIT_TAG_RE = re.compile(r"\bGIT_TAG\s+(\S+)")
SHA40_LOWER_RE = re.compile(r"^[0-9a-f]{40}$")
HEX_RE = re.compile(r"^[0-9a-fA-F]+$")


@dataclass
class Violation:
    file: Path
    line: int
    dependency: str
    git_tag: str
    reason: str


def _strip_cmake_comments(text: str) -> str:
    """Replace `# ...` comments with same-length spaces to preserve offsets and lines."""
    return re.sub(r"#[^\n]*", lambda m: " " * len(m.group(0)), text)


def check_git_tag(git_tag: str) -> str:
    """Return a non-empty reason if `git_tag` is not a 40-char lowercase hex SHA."""
    if SHA40_LOWER_RE.match(git_tag):
        return ""
    if HEX_RE.match(git_tag) and len(git_tag) == 40:
        return "contains uppercase hex; must be a 40-character lowercase commit SHA"
    if HEX_RE.match(git_tag):
        return (
            f"is a {len(git_tag)}-character hex prefix; "
            "must be a full 40-character lowercase commit SHA"
        )
    return "is a mutable tag or ref; must be a 40-character lowercase commit SHA"


def find_violations(root: Path) -> list[Violation]:
    candidates = [
        p
        for p in (root / "CMakeLists.txt", root / "cmake" / "dependencies.cmake")
        if p.is_file()
    ]
    if not candidates:
        return [
            Violation(
                file=root / "CMakeLists.txt",
                line=1,
                dependency="<none>",
                git_tag="<none>",
                reason="no CMake dependency file found (vacuous-pass guard)",
            )
        ]

    violations: list[Violation] = []
    total_declarations = 0

    for path in candidates:
        raw = path.read_text(encoding="utf-8", errors="replace")
        stripped = _strip_cmake_comments(raw)
        for match in DECLARE_RE.finditer(stripped):
            total_declarations += 1
            body = match.group(1)
            tokens = body.split()
            dep_name = tokens[0] if tokens else "<unknown>"
            declare_line = stripped.count("\n", 0, match.start()) + 1

            tag_match = GIT_TAG_RE.search(body)
            if not tag_match:
                violations.append(
                    Violation(
                        file=path,
                        line=declare_line,
                        dependency=dep_name,
                        git_tag="<missing>",
                        reason="missing GIT_TAG in FetchContent_Declare",
                    )
                )
                continue

            git_tag = tag_match.group(1).strip('"')
            tag_offset = match.start(1) + tag_match.start()
            tag_line = stripped.count("\n", 0, tag_offset) + 1
            reason = check_git_tag(git_tag)
            if reason:
                violations.append(
                    Violation(
                        file=path,
                        line=tag_line,
                        dependency=dep_name,
                        git_tag=git_tag,
                        reason=reason,
                    )
                )

    if total_declarations == 0:
        violations.append(
            Violation(
                file=candidates[0],
                line=1,
                dependency="<none>",
                git_tag="<none>",
                reason="no FetchContent_Declare blocks found (vacuous-pass guard)",
            )
        )

    return violations


def main() -> int:
    violations = find_violations(REPO_ROOT)
    if not violations:
        return 0
    print(
        "Unpinned FetchContent dependencies found "
        "(every GIT_TAG must be a 40-character lowercase commit SHA):"
    )
    for v in violations:
        rel = v.file.relative_to(REPO_ROOT)
        print(f"  {rel}:{v.line}: `{v.dependency}` has GIT_TAG `{v.git_tag}` ({v.reason})")
    print(f"\n{len(violations)} violation(s).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
