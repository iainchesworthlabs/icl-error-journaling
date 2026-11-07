#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail a QML file that `qmlformat` would change.

    python3 tools/ci/check_qml_format.py --qmlformat <path to qmlformat> [--root .]

qmlformat has no verify mode, so each file is formatted to text and compared with itself (line endings aside). The settings
are `apps/app/qml/.qmlformat.ini`. The checked files are the module's (the theme, components and screens) and the test
stubs. To fix a file: `qmlformat -i <file>`.
"""

from __future__ import annotations

import argparse
import difflib
import subprocess
import sys
from pathlib import Path

import qml_source as source


def files(root: Path) -> list[Path]:
    return source.qml_files(root, include_theme=True) + sorted((root / source.TEST_DIR / "stubs").glob("*.qml"))


def formatted(qmlformat: str, path: Path) -> str:
    result = subprocess.run([qmlformat, str(path)], capture_output=True, text=True, encoding="utf-8", check=False)
    if result.returncode != 0:
        raise RuntimeError(f"qmlformat failed on {path}: {result.stderr.strip()}")
    return result.stdout


def check(root: Path, qmlformat: str) -> list[str]:
    problems = []
    for path in files(root):
        current = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        wanted = formatted(qmlformat, path).replace("\r\n", "\n")
        if current != wanted:
            diff = list(difflib.unified_diff(current.split("\n"), wanted.split("\n"), "current", "qmlformat", lineterm="", n=0))
            first = next((line for line in diff if line.startswith(("+", "-")) and not line.startswith(("+++", "---"))), "")
            problems.append(f"{path.relative_to(root).as_posix()}: not formatted (first difference: {first.strip()[:80]}); "
                            "run qmlformat -i on it")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", type=Path)
    parser.add_argument("--qmlformat", required=True)
    args = parser.parse_args(argv)
    return source.report(check(args.root.resolve(), args.qmlformat), "QML format check")


if __name__ == "__main__":
    sys.exit(main())
