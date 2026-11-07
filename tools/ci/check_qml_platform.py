#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a QML file that asks which platform it runs on.

    python3 tools/ci/check_qml_platform.py [--root .]

The QML counterpart of the rule against `#if` on a platform (AGENTS.md, docs/plan/stage-4.md "No platform tests in QML"):
what differs between the phone and a desktop (a share sheet, a document picker, a haptic) arrives as a property or a signal
of the bridge, so a screen never branches on `Qt.platform`. The theme is checked too.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import qml_source as source

FORBIDDEN = [
    (re.compile(r"\bQt\s*\.\s*platform\b"), "Qt.platform"),
    (re.compile(r"\bQt\s*\.\s*application\s*\.\s*platform\b"), "Qt.application.platform"),
    (re.compile(r"\bPlatform\s*\.\s*(?:os|isIOS|isMacOS|isAndroid|isLinux|isWindows)\b"), "Platform.<os>"),
    (re.compile(r"\bSystemInformation\b"), "SystemInformation"),
    (re.compile(r"\bQt\s*\.\s*(?:platformOs|os)\b"), "a platform name on Qt"),
]


def check_text(name: str, text: str) -> list[str]:
    code = source.blank_strings(source.blank_comments(text))
    problems = []
    for pattern, label in FORBIDDEN:
        for match in pattern.finditer(code):
            problems.append(f"{name}:{source.line_of(code, match.start())}: asks the platform ({label}); "
                            "take it from a property or a signal of the bridge")
    return problems


def check(root: Path) -> list[str]:
    problems = []
    for path in source.qml_files(root, include_theme=True):
        problems += check_text(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"))
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", type=Path)
    args = parser.parse_args(argv)
    return source.report(check(args.root.resolve()), "QML platform check")


if __name__ == "__main__":
    sys.exit(main())
