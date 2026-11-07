#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a QML file whose visible text is not translatable.

    python3 tools/ci/check_qml_strings.py [--root .]

docs/plan/stage-4.md, "Strings" (NFR-11): every visible string is `qsTr("...")` with `%1` placeholders and no
concatenation. This finds, in the properties that show or speak text (`text`, `placeholderText`, `title`, `label`,
`displayText`, `Accessible.name`, `Accessible.description`, `ToolTip.text`) and in an assignment to one of them:

  * a string literal with a letter in it that is not inside `qsTr(`;
  * `qsTr(` joined to something with `+` (`qsTr("a") + b`, `b + qsTr("a")`, `qsTr("a" + b)`), or given something that is
    not a string literal (`qsTr(name)`), because the extractor cannot read it.

`.arg(...)` after `qsTr(...)` is how a value gets in. A literal with no letter (`"×"`, `"\\n"`, `":"`) is a symbol, not a
word. The Stage 0 window (`Main.qml`) is not checked.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import qml_source as source

TEXT_PROPERTIES = r"(?:text|placeholderText|title|label|displayText|Accessible\.name|Accessible\.description|ToolTip\.text)"
START = re.compile(rf"(?<!\w)(?P<name>{TEXT_PROPERTIES})\s*(?P<op>:|=(?!=))\s*")
TRANSLATORS = ("qsTr", "qsTrId", "qsTranslate", "QT_TR_NOOP", "QT_TRID_NOOP")
TRANSLATOR_CALL = re.compile(r"(?<![\w.])(?:qsTr|qsTrId|qsTranslate|QT_TR_NOOP|QT_TRID_NOOP)\s*\(")


def expression_end(code: str, start: int) -> int:
    """The end of the statement that starts at `start`: the first newline, `;` or closing brace at depth 0, unless the
    next non-space character continues the expression (`+`, `.`, `?`, `:`, `,`)."""
    depth = 0
    i = start
    n = len(code)
    while i < n:
        c = code[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "{":
            depth += 1
        elif c == "}":
            if depth == 0:
                return i
            depth -= 1
        elif c == ";" and depth <= 0:
            return i
        elif c == "\n" and depth <= 0:
            j = i + 1
            while j < n and code[j] in " \t\r\n":
                j += 1
            if j < n and code[j] in "+.?:,":
                i = j
                continue
            return i
        i += 1
    return n


def matching_paren(code: str, open_index: int) -> int:
    depth = 0
    for i in range(open_index, len(code)):
        if code[i] == "(":
            depth += 1
        elif code[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return len(code)


def has_letter(text: str) -> bool:
    return any(ch.isalpha() for ch in text)


def check_expression(name: str, line: int, prop: str, expression: str, blanked: str) -> list[str]:
    """`expression` has its comments removed; `blanked` is the same with the inside of strings blanked."""
    problems = []
    calls = [(m.start(), matching_paren(blanked, m.end() - 1)) for m in TRANSLATOR_CALL.finditer(blanked)]

    def inside_a_call(index: int) -> bool:
        return any(start < index < end for start, end in calls)

    for literal in source_strings(expression):
        start, end, text = literal
        if has_letter(text) and not inside_a_call(start):
            problems.append(f"{name}:{line}: {prop} is the literal \"{text}\" and not qsTr(); translate it")

    for start, end in calls:
        argument = expression[start:end]
        opening = argument.index("(")
        inner = blanked[start + opening + 1:end]
        # The argument of qsTr is one string literal (or, for the context forms, string literals separated by commas).
        if "+" in inner:
            problems.append(f"{name}:{line}: {prop} puts a + inside qsTr(); use one string with %1 placeholders")
        elif not re.fullmatch(r"\s*[\"'`][^\"'`]*[\"'`]\s*(?:,.*)?", inner.replace("\n", " ")):
            problems.append(f"{name}:{line}: {prop} gives qsTr() something that is not a string literal; "
                            "the extractor cannot read it")
        before = blanked[:start].rstrip()
        position = end + 1
        while True:
            rest = blanked[position:].lstrip()
            if not rest.startswith(".arg("):
                break
            position = matching_paren(blanked, position + (len(blanked[position:]) - len(rest)) + 4) + 1
        after = blanked[position:].lstrip()
        if before.endswith("+") or after.startswith("+"):
            problems.append(f"{name}:{line}: {prop} joins qsTr() to something with +; use %1 placeholders and .arg()")
    return problems


def source_strings(text: str) -> list[tuple[int, int, str]]:
    """(start, end, content) of each string literal of `text`."""
    found = []
    i = 0
    while i < len(text):
        if text[i] in "\"'`":
            end = source.string_end(text, i)
            found.append((i, end, text[i + 1:end - 1]))
            i = end
        else:
            i += 1
    return found


def check_text(name: str, text: str) -> list[str]:
    no_comments = source.blank_comments(text)
    blanked = source.blank_strings(no_comments)
    problems = []
    for match in START.finditer(blanked):
        # `text:` inside a string is not a property; blank_strings has emptied those, so every match is code.
        end = expression_end(blanked, match.end())
        expression = no_comments[match.end():end]
        problems += check_expression(name, source.line_of(text, match.start()), match.group("name"), expression,
                                     blanked[match.end():end])
    return problems


def check(root: Path) -> list[str]:
    problems = []
    for path in source.qml_files(root):
        problems += check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"))
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", type=Path)
    args = parser.parse_args(argv)
    return source.report(check(args.root.resolve()), "QML string check")


if __name__ == "__main__":
    sys.exit(main())
