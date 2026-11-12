#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when the guide's pictures, their list and the screens the app can render disagree.

    python3 tools/ci/check_guide_screenshots.py [--root .]

Stage 10, package 10.7 (docs/plan/stage-10.md). Needs no build and no Pillow. It holds these to each other:

- docs/guide/screenshots.json: every picture names a state and a mode that tools/ej_screenshots/screens.json renders, has a
  file name of lower-case letters, digits and dashes, and is listed once;
- docs/guide/img/: one PNG for every picture of the list, no other file, each of the list's width;
- the pages of docs/guide/: every `![alt](img/<file>.png)` names a listed picture and has alt text that says what the
  screen shows (at least 20 characters), and every listed picture is used by some page.

Regenerate the pictures with tools/docs/guide_screenshots.py. The confidential-terms check already reads these pages.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

LIST = "docs/guide/screenshots.json"
SCREENS = "tools/ej_screenshots/screens.json"
GUIDE = "docs/guide"
IMG = "docs/guide/img"
PNG = b"\x89PNG\r\n\x1a\n"
MIN_ALT = 20


def png_width(path: Path) -> int | None:
    data = path.read_bytes()[:24]
    if data[:8] != PNG or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">I", data[16:20])[0]


def check(root: Path) -> list[str]:
    problems: list[str] = []
    plan = json.loads((root / LIST).read_text(encoding="utf-8"))
    screens = json.loads((root / SCREENS).read_text(encoding="utf-8"))
    states = {state["name"] for flow in screens["flows"] for state in flow["states"]}
    modes = {mode["name"] for mode in screens["modes"]}
    width = plan["width"]

    listed: dict[str, dict] = {}
    for item in plan["images"]:
        name = item["file"]
        if name in listed:
            problems.append(f"{LIST}: {name} is listed twice")
        listed[name] = item
        if not re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)*", name):
            problems.append(f"{LIST}: the file name {name!r} must be lower-case letters, digits and dashes")
        if item["state"] not in states:
            problems.append(f"{LIST}: {name} names the state {item['state']!r}, which {SCREENS} does not render")
        if item["mode"] not in modes:
            problems.append(f"{LIST}: {name} names the mode {item['mode']!r}, which {SCREENS} does not render")

    folder = root / IMG
    files = {path.name: path for path in folder.glob("*") if path.is_file()} if folder.is_dir() else {}
    for name in sorted(listed):
        path = files.get(f"{name}.png")
        if path is None:
            problems.append(f"{IMG}/{name}.png: missing; run tools/docs/guide_screenshots.py")
        else:
            actual = png_width(path)
            if actual is None:
                problems.append(f"{IMG}/{name}.png: not a PNG")
            elif actual != width:
                problems.append(f"{IMG}/{name}.png: {actual} pixels wide, the list says {width}")
    for name in sorted(set(files) - {f"{n}.png" for n in listed}):
        problems.append(f"{IMG}/{name}: not in {LIST}; list it or delete it")

    used: set[str] = set()
    for page in sorted((root / GUIDE).glob("*.md")):
        rel = page.relative_to(root).as_posix()
        for alt, target in re.findall(r"!\[([^\]]*)\]\(([^)\s]+)", page.read_text(encoding="utf-8")):
            found = re.fullmatch(r"img/([^/]+)\.png", target)
            if not found:
                problems.append(f"{rel}: the image {target!r} is not a picture of the guide (img/<file>.png)")
                continue
            used.add(found.group(1))
            if found.group(1) not in listed:
                problems.append(f"{rel}: the image {target} is not in {LIST}")
            if len(alt.strip()) < MIN_ALT:
                problems.append(f"{rel}: the image {target} needs alt text that says what the screen shows")
    for name in sorted(set(listed) - used):
        problems.append(f"{LIST}: {name} is not used by any page of the guide")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    try:
        problems = check(args.root)
    except (OSError, KeyError, ValueError) as error:
        print(f"check_guide_screenshots: cannot check: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        return 1
    print("Guide screenshots: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
