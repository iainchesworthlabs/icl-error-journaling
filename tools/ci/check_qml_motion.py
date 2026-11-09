#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a QML file that moves something every frame with a property animation instead of an animator.

    python3 tools/ci/check_qml_motion.py [--root .]

docs/plan/stage-4.md, "Motion" and package 4.14 (the CI test `motion.animators`): a `NumberAnimation`, `PropertyAnimation`,
`SmoothedAnimation` or `SpringAnimation` on `x`, `y`, `scale` or `opacity`, and a `Behavior` on one of them, runs on the
GUI thread and drops frames while a commit holds it. `XAnimator`, `YAnimator`, `ScaleAnimator` and `OpacityAnimator` run on
the render thread and hold the display rate (U7). This finds

  * `NumberAnimation on x { ... }` and the other property animation types the same way;
  * a property animation whose `property:` is one of the four, or whose `properties:` lists one of them;
  * `Behavior on opacity { ... }` and the like, whatever animation they hold.

An animation of any other property (a height, a colour) is not looked at: no animator exists for it.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import qml_source as source

PER_FRAME = ("x", "y", "scale", "opacity")
PROPERTY_ANIMATIONS = ("NumberAnimation", "PropertyAnimation", "SmoothedAnimation", "SpringAnimation")
ANIMATOR_FOR = {"x": "XAnimator", "y": "YAnimator", "scale": "ScaleAnimator", "opacity": "OpacityAnimator"}

# `NumberAnimation {` or `NumberAnimation on x {`, and `Behavior on x {`.
ANIMATION_START = re.compile(r"\b(" + "|".join(PROPERTY_ANIMATIONS) + r")\b\s*(?:on\s+([A-Za-z_][\w.]*)\s*)?\{")
BEHAVIOR_START = re.compile(r"\bBehavior\s+on\s+([A-Za-z_][\w.]*)\s*\{")
PROPERTY_LINE = re.compile(r"(?:^|[;{\s])(property|properties)\s*:\s*(\"[^\"\n]*\"|'[^'\n]*')")


def block_end(code: str, open_index: int) -> int:
    """The index of the brace that closes the one at `open_index`; the end of the text when it never closes."""
    depth = 0
    for i in range(open_index, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(code)


def per_frame_properties(names: str) -> list[str]:
    """The properties of the four in a `property:` or `properties:` value (a comma or space separated list)."""
    return [name for name in re.split(r"[\s,]+", names.strip("\"' ")) if name in PER_FRAME]


def check_text(name: str, text: str) -> list[str]:
    no_comments = source.blank_comments(text)
    code = source.blank_strings(no_comments)  # the same length, so an index in one is an index in the other
    problems: list[str] = []
    for match in ANIMATION_START.finditer(code):
        kind, target = match.group(1), match.group(2)
        found: list[str] = []
        if target is not None:
            found += per_frame_properties(target.split(".")[-1])
        body = no_comments[match.end():block_end(code, match.end() - 1)]
        for line in PROPERTY_LINE.finditer(body):
            found += per_frame_properties(line.group(2))
        for prop in dict.fromkeys(found):
            problems.append(f"{name}:{source.line_of(code, match.start())}: a {kind} on {prop} runs on the GUI thread; "
                            f"use {ANIMATOR_FOR[prop]}")
    for match in BEHAVIOR_START.finditer(code):
        prop = match.group(1).split(".")[-1]
        if prop in PER_FRAME:
            problems.append(f"{name}:{source.line_of(code, match.start())}: a Behavior on {prop} animates on the GUI thread; "
                            f"use {ANIMATOR_FOR[prop]} in a state change or a Transition")
    return problems


def check(root: Path) -> list[str]:
    problems: list[str] = []
    for path in source.qml_files(root, include_theme=True):
        problems += check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"))
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", type=Path)
    args = parser.parse_args(argv)
    return source.report(check(args.root.resolve()), "QML motion check")


if __name__ == "__main__":
    sys.exit(main())
