#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a QML file that has no test file named after it.

    python3 tools/ci/check_qml_tests.py [--root .]

docs/plan/stage-4.md, "Coverage": Catch2 does not measure QML, so the rule is that each component and each screen has a
test file. `qml/theme/Theme.qml` is tested by `apps/app/tests/ui/theme_test.cpp`, `qml/components/TabBar.qml` by
`tab_bar_test.cpp`, `qml/screens/log/LogScreen.qml` by `log_screen_test.cpp`: the file name in snake case plus `_test.cpp`,
in `apps/app/tests/ui/`, with at least one `TEST_CASE`. A file under a folder named `private` is a part of the file next to
it and is tested through that one. The window (`Main.qml`) and the shell
(`shell/AppShell.qml`) are tested by `app_flow_test.cpp`, which drives the whole app.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import qml_source as source

PRIVATE = "private"
# The window and the shell are one test, of the whole app (Stage 4, 4.15): the shell is the window's content, kept apart only
# so that the test rig, which loads an item, can load it.
TESTED_BY = {"Main": "app_flow", "AppShell": "app_flow"}


def check(root: Path) -> list[str]:
    problems = []
    tests = root / source.TEST_DIR
    for path in source.qml_files(root, include_theme=True):
        relative = path.relative_to(root / source.QML_DIR)
        if PRIVATE in relative.parts[:-1]:
            continue
        test = tests / f"{TESTED_BY.get(path.stem, source.snake_case(path.stem))}_test.cpp"
        shown = path.relative_to(root).as_posix()
        if not test.is_file():
            problems.append(f"{shown}: no test file {test.relative_to(root).as_posix()}")
        elif "TEST_CASE" not in test.read_text(encoding="utf-8"):
            problems.append(f"{shown}: {test.relative_to(root).as_posix()} has no TEST_CASE")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", type=Path)
    args = parser.parse_args(argv)
    return source.report(check(args.root.resolve()), "QML test-file check")


if __name__ == "__main__":
    sys.exit(main())
