#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when a release tag and the project version disagree.

    python3 tools/ci/check_release_version.py v0.1.0-rc.1

The part of the tag before any -suffix must equal the VERSION in project() in
CMakeLists.txt and the "version" in vcpkg.json, so the binary reports the version it
was released as. A tag such as v0.1.0-rc.1 is a release candidate of 0.1.0.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
TAG = re.compile(r"^v(\d+\.\d+\.\d+)(?:-[0-9A-Za-z.-]+)?$")
PROJECT_VERSION = re.compile(r"\bproject\s*\([^)]*?\bVERSION\s+(\d+\.\d+\.\d+)", re.S)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: check_release_version.py <tag>", file=sys.stderr)
        return 2
    tag = TAG.match(argv[1])
    if not tag:
        print(f"{argv[1]} is not vMAJOR.MINOR.PATCH[-suffix]", file=sys.stderr)
        return 1
    wanted = tag.group(1)
    cmake = PROJECT_VERSION.search((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    found = {
        "CMakeLists.txt project(VERSION)": cmake.group(1) if cmake else "(not found)",
        "vcpkg.json version": json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8")).get("version", "(not found)"),
    }
    bad = {name: value for name, value in found.items() if value != wanted}
    for name, value in bad.items():
        print(f"{argv[1]} releases {wanted}, but {name} is {value}", file=sys.stderr)
    if bad:
        print("Change the version in a pull request, or tag the version the project declares.", file=sys.stderr)
        return 1
    print(f"{argv[1]} matches the project version {wanted}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
