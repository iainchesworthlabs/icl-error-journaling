#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when a tracked file other than config/sample-config.json is named *-config.json or *.ejconfig.

    python3 tools/ci/check_config_files.py [--root .]

The real configuration holds confidential values (PRD NFR-10) and is never committed.
"""

from __future__ import annotations

import argparse
import fnmatch
import subprocess
import sys
from pathlib import Path

ALLOWED = "config/sample-config.json"
PATTERNS = ("*-config.json", "*.ejconfig")


def offending(paths: list[str]) -> list[str]:
    return [
        p
        for p in paths
        if p != ALLOWED and any(fnmatch.fnmatch(p.rsplit("/", 1)[-1].lower(), pattern) for pattern in PATTERNS)
    ]


def tracked(root: Path) -> list[str]:
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], check=True, capture_output=True).stdout
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent.parent))
    args = parser.parse_args(argv)
    bad = offending(tracked(Path(args.root)))
    for path in bad:
        print(f"error: {path}: only {ALLOWED} may be committed as a configuration file (a *.ejconfig is one too)", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
