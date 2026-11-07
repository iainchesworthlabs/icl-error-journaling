# SPDX-License-Identifier: GPL-3.0-or-later
"""What the QML checks (check_qml_*.py) share: finding the files and reading them without their comments.

The checks read text and do not parse QML. They are as strict as a line-oriented reader can be, and the tests in
tools/tests/test_qml_checks.py show what each one finds and what it lets through.
"""

from __future__ import annotations

import re
from pathlib import Path

QML_DIR = Path("apps/app/qml")
TEST_DIR = Path("apps/app/tests/ui")

# The token singleton and the safe frame are where literals live.
THEME_DIR = "theme"


def qml_files(root: Path, *, include_theme: bool = False) -> list[Path]:
    """The QML files of the module, sorted, without the theme unless asked for."""
    base = root / QML_DIR
    found = []
    for path in sorted(base.rglob("*.qml")):
        relative = path.relative_to(base)
        if not include_theme and relative.parts[0] == THEME_DIR:
            continue
        found.append(path)
    return found


def blank_comments(text: str) -> str:
    """The text with every comment replaced by spaces, so that line and column numbers stay the same. Comment markers
    inside a string are not comments."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c in "\"'`":
            end = string_end(text, i)
            out.append(text[i:end])
            i = end
        elif c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def string_end(text: str, start: int) -> int:
    """The index just after the string literal that starts at `start`."""
    quote = text[start]
    i = start + 1
    n = len(text)
    while i < n:
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == quote:
            return i + 1
        if text[i] == "\n" and quote != "`":
            return i  # an unterminated string ends at the line
        i += 1
    return n


def blank_strings(text: str) -> str:
    """The text with the inside of every string literal replaced by spaces (the quotes stay), for matching code."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text[i] in "\"'`":
            end = string_end(text, i)
            out.append(text[i])
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i + 1:end - 1]))
            if end - 1 > i:
                out.append(text[end - 1])
            i = end
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def snake_case(name: str) -> str:
    """`TabBar` is `tab_bar`, `UiStubGood` is `ui_stub_good`, `SafeFrame` is `safe_frame`."""
    step = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", step).lower()


def report(problems: list[str], what: str) -> int:
    """Prints the problems and returns the exit status."""
    if problems:
        print(f"{what}: {len(problems)} problem(s)")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(f"{what}: ok")
    return 0
