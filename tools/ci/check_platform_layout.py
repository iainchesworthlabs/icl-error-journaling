#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep the per-platform implementations of the bridge in step.

    python3 tools/ci/check_platform_layout.py [--root .]

Every `src/platform/ios/<name>.mm` needs a `<name>.cpp` in every sibling platform directory (desktop/, android/ and any
other) and the reverse, and a
`.mm` file may exist only under a `src/platform/ios` directory. Only a `src/platform` directory that holds both `ios/` and
`desktop/` is paired (the bridge); the storage library selects its per-OS files in its own CMake. A file named in INTERNAL is private to its platform
(a helper shared by that platform's implementations) and needs no sibling. See docs/architecture.md, "Platform code".
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Helpers private to one platform directory: top_view_controller is shared by the iOS share sheet and document picker.
INTERNAL = {"top_view_controller"}
# The directory of the platform that holds .mm files. Its siblings hold .cpp files with the same stems.
IOS = "ios"
DESKTOP = "desktop"
SOURCE_ROOTS = ("libs", "apps")
SKIP_PARTS = {"build", ".git", "vcpkg_installed", ".icl-infra"}


def platform_dirs(root: Path) -> list[Path]:
    """The `src/platform` directories laid out as ios/ plus desktop/ (the bridge). Other layouts, such as the storage
    library's per-OS directories with their own CMake selection, are not covered by this pairing rule."""
    found = []
    for top in SOURCE_ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("platform"):
            relative = path.relative_to(root).parts
            if not path.is_dir() or path.parent.name != "src" or not SKIP_PARTS.isdisjoint(relative):
                continue
            if (path / IOS).is_dir() and (path / DESKTOP).is_dir():
                found.append(path)
    return sorted(found)


def stems(directory: Path, suffix: str) -> set[str]:
    return {p.stem for p in directory.glob(f"*{suffix}") if p.is_file()} - INTERNAL


def check_platform_dir(platform: Path) -> list[str]:
    """Problems with one `src/platform` directory."""
    problems = []
    siblings = [d for d in sorted(platform.iterdir()) if d.is_dir() and d.name != IOS]
    ios = platform / IOS
    ios_stems = stems(ios, ".mm") if ios.is_dir() else set()
    for name in sorted(ios_stems):
        for sibling in siblings:
            if name not in stems(sibling, ".cpp"):
                problems.append(f"{ios / (name + '.mm')}: no {name}.cpp in {sibling}")
    for sibling in siblings:
        for name in sorted(stems(sibling, ".cpp") - ios_stems):
            problems.append(f"{sibling / (name + '.cpp')}: no {name}.mm in {ios}")
    return problems


def misplaced_mm(root: Path) -> list[str]:
    problems = []
    for top in SOURCE_ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*.mm"):
            parts = path.relative_to(root).parts
            if not SKIP_PARTS.isdisjoint(parts):
                continue
            if path.parent.name != IOS or path.parent.parent.name != "platform":
                problems.append(f"{path}: a .mm file belongs in src/platform/{IOS}/")
    return problems


def arc_covered(root: Path) -> list[str]:
    """Each library that has src/platform/ios must compile it with -fobjc-arc."""
    problems = []
    for platform in platform_dirs(root):
        if not (platform / IOS).is_dir():
            continue
        cmake = platform.parent.parent / "CMakeLists.txt"
        if not cmake.is_file() or "-fobjc-arc" not in cmake.read_text(encoding="utf-8"):
            problems.append(f"{cmake}: does not compile src/platform/{IOS} with -fobjc-arc")
    return problems


def run(root: Path) -> list[str]:
    problems = misplaced_mm(root)
    for platform in platform_dirs(root):
        problems += check_platform_dir(platform)
    problems += arc_covered(root)
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent.parent))
    args = parser.parse_args(argv)
    problems = run(Path(args.root))
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
