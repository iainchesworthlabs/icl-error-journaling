#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a QML file that writes a colour, a size, a radius, a spacing or a duration instead of using the Theme.

    python3 tools/ci/check_qml_literals.py [--root .]

docs/plan/stage-4.md, "Tokens, not numbers": outside `qml/theme/` no colour, size, radius, spacing or duration appears
except as a `Theme.*` value. This finds

  * a string that is a hex colour (`"#1E5B7A"`), `Qt.rgba(`, `Qt.hsla(`, `Qt.tint(` and the like, and a named colour
    (`"red"`) given to a colour property (`"transparent"` is allowed);
  * a number other than 0 in a size-like property (`width`, `height`, `radius`, `spacing`, `margins`, `padding`,
    `font.pixelSize`, `duration`, ...) that is the whole value, or is added or subtracted, or is a function argument.
    A number that scales (`* 2`, `/ 2`, `* 0.5`) is a ratio and is allowed.

A line may end in `// literal-ok: <reason>` when a literal is right (a layout that is fixed by the platform, not the
design); the reason is required and the list of them is printed so that a review sees each one. The theme and the Stage 0
window (`Main.qml`) are not checked.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import qml_source as source

HEX_COLOUR = re.compile(r"#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6}|[0-9a-fA-F]{3,4})")
COLOUR_FUNCTIONS = re.compile(r"\bQt\s*\.\s*(?:rgba|hsla|hsva|color|tint|lighter|darker)\s*\(|\bQt\s*\.\s*(?:lighter|darker)\b")
NAMED_COLOURS = {
    "black", "white", "red", "green", "blue", "yellow", "orange", "purple", "pink", "brown", "gray", "grey", "cyan",
    "magenta", "lightgray", "lightgrey", "darkgray", "darkgrey", "silver", "gold", "navy", "teal", "lime", "maroon",
    "olive", "aqua", "fuchsia", "lightblue", "lightgreen", "darkblue", "darkred", "darkgreen", "steelblue", "skyblue",
    "salmon", "tomato", "crimson", "indigo", "violet", "beige", "ivory", "khaki", "coral", "plum", "orchid",
}
COLOUR_PROPERTY = re.compile(r"(?:^|\.)(?:\w*[cC]olou?r)$")

SIZE_PROPERTIES = {
    "x", "y", "width", "height", "implicitWidth", "implicitHeight", "minimumWidth", "minimumHeight", "maximumWidth",
    "maximumHeight", "preferredWidth", "preferredHeight", "radius", "topLeftRadius", "topRightRadius",
    "bottomLeftRadius", "bottomRightRadius", "spacing", "columnSpacing", "rowSpacing", "margins", "topMargin",
    "bottomMargin", "leftMargin", "rightMargin", "horizontalCenterOffset", "verticalCenterOffset", "padding",
    "topPadding", "bottomPadding", "leftPadding", "rightPadding", "horizontalPadding", "verticalPadding", "pixelSize",
    "pointSize", "size", "duration", "interval", "cellWidth", "cellHeight", "contentWidth", "contentHeight",
    "lineWidth", "dashOffset",
}

# `name: value` or `border.width: value` or `property real radius: value`, one statement on one line.
BINDING = re.compile(
    r"^\s*(?:(?:readonly|required|default)\s+)*(?:property\s+\w+\s+)?(?P<name>[A-Za-z_][\w.]*)\s*:\s*(?P<value>[^{}]*?)\s*;?\s*$"
)
SIZE_SUFFIX = re.compile(r"(?:Radius|Spacing|Margin|Padding|Duration|Width|Height|Size|Interval|Offset)$")
NUMBER = re.compile(r"(?<![\w.])(\d+(?:\.\d+)?|\.\d+)(?![\w.])")
LITERAL_OK = re.compile(r"//\s*literal-ok:\s*(\S.*)$")
STRING = re.compile(r"\"((?:[^\"\\]|\\.)*)\"|'((?:[^'\\]|\\.)*)'")


def last_segment(name: str) -> str:
    return name.split(".")[-1]


def is_scaling(code: str, start: int, end: int) -> bool:
    """True when the number at `start:end` is an operand of `*`, `/` or `%`, which makes it a ratio."""
    return code[:start].rstrip().endswith(("*", "/", "%")) or code[end:].lstrip().startswith(("*", "/", "%"))


def check_line(name: str, number: int, raw: str, code: str, with_strings: str) -> list[str]:
    problems = []
    for match in COLOUR_FUNCTIONS.finditer(code):
        problems.append(f"{name}:{number}: a colour made in the file ({match.group(0).strip()}); use a Theme.colour value")
    for literal in STRING.finditer(with_strings):
        text = literal.group(1) if literal.group(1) is not None else literal.group(2)
        if HEX_COLOUR.fullmatch(text):
            problems.append(f"{name}:{number}: the colour \"{text}\"; use a Theme.colour value")

    binding = BINDING.match(code)
    if binding is None:
        return problems
    prop = binding.group("name")
    value = binding.group("value")
    value_start = binding.start("value")
    if COLOUR_PROPERTY.search(prop):
        for literal in STRING.finditer(with_strings[value_start:]):
            text = literal.group(1) if literal.group(1) is not None else literal.group(2)
            if text.lower() in NAMED_COLOURS:
                problems.append(f"{name}:{number}: the colour \"{text}\"; use a Theme.colour value")
    segment = last_segment(prop)
    # `lineHeight` is a ratio of the font size, not a length.
    if segment in SIZE_PROPERTIES or (segment != "lineHeight" and SIZE_SUFFIX.search(segment)):
        for found in NUMBER.finditer(value):
            if float(found.group(1)) == 0:
                continue
            if is_scaling(value, found.start(), found.end()):
                continue
            problems.append(f"{name}:{number}: the number {found.group(1)} in {prop}; use a Theme.size, Theme.radius, "
                            "Theme.space, Theme.font or Theme.motion value")
    return problems


def check_text(name: str, text: str) -> tuple[list[str], list[str]]:
    """The problems of one file and its `literal-ok` lines."""
    no_comments = source.blank_comments(text)
    no_strings = source.blank_strings(no_comments)
    problems: list[str] = []
    allowed: list[str] = []
    for number, (raw, with_strings, code) in enumerate(
            zip(text.split("\n"), no_comments.split("\n"), no_strings.split("\n")), start=1):
        ok = LITERAL_OK.search(raw)
        found = check_line(name, number, raw, code, with_strings)
        if found and ok:
            allowed.append(f"{name}:{number}: {ok.group(1).strip()}")
            continue
        problems += found
    for number, raw in enumerate(text.split("\n"), start=1):
        if re.search(r"//\s*literal-ok\b", raw) and not LITERAL_OK.search(raw):
            problems.append(f"{name}:{number}: `literal-ok` needs a reason after the colon")
    return problems, allowed


def check(root: Path) -> tuple[list[str], list[str]]:
    problems: list[str] = []
    allowed: list[str] = []
    for path in source.qml_files(root):
        found, ok = check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"))
        problems += found
        allowed += ok
    return problems, allowed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", type=Path)
    args = parser.parse_args(argv)
    problems, allowed = check(args.root.resolve())
    if allowed:
        print("literal-ok lines:")
        for line in allowed:
            print(f"  {line}")
    return source.report(problems, "QML literal check")


if __name__ == "__main__":
    sys.exit(main())
