#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the source file that tells the About page which commit, build and date the program came from.

    python -I tools/licences/build_info.py --source-dir . --out build/about/build_info_values.cpp [--build-number 57] [--print]

Run by the build (the target `ej_build_info`, cmake/EjAbout.cmake) on every build, so the commit is the one that was
compiled and not the one that was configured. The file is written only when its text changes, so an unchanged commit
recompiles nothing.

Where each value comes from, and what a build without it says:

  commit        `git rev-parse --short=7 HEAD` in the source directory, then the first 7 characters of $GITHUB_SHA, then
                "unknown". `dirty` is true when git reports a change to a tracked file.
  ref           the tag that HEAD is exactly at, then the branch, then $GITHUB_REF_NAME, then empty (nothing is shown)
  build number  the value CMake gives (--build-number: the iOS CFBundleVersion), then $GITHUB_RUN_NUMBER, then "unknown"
  date          $SOURCE_DATE_EPOCH when it is set (the reproducible-builds convention: CI supplies it), then the date of the
                commit, then "unknown". It is never the time of the build, so building the same commit twice gives the
                same program. In UTC, as "2026-11-09 03:21 UTC".

A folder that is not a repository, or a machine without git, is not an error: the values are "unknown".
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

UNKNOWN = "unknown"


def git(source_dir: Path, *arguments: str) -> str | None:
    """The output of a git command in `source_dir`, or None when git is missing, the folder is no repository or it failed."""
    try:
        done = subprocess.run(["git", "-C", str(source_dir), *arguments], capture_output=True, text=True, timeout=30,
                              check=False)
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    return done.stdout.strip()


def format_date(epoch: int) -> str:
    moment = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc)
    return moment.strftime("%Y-%m-%d %H:%M UTC")


def collect(source_dir: Path, build_number: str, environment: dict[str, str]) -> dict:
    commit = git(source_dir, "rev-parse", "--short=7", "HEAD")
    in_repository = bool(commit)
    if not commit:
        sha = environment.get("GITHUB_SHA", "").strip().lower()
        commit = sha[:7] if len(sha) >= 7 and all(c in "0123456789abcdef" for c in sha[:7]) else UNKNOWN
    dirty = False
    ref = ""
    commit_epoch = None
    if in_repository:
        dirty = bool(git(source_dir, "status", "--porcelain", "--untracked-files=no"))
        ref = git(source_dir, "describe", "--tags", "--exact-match", "HEAD") or git(
            source_dir, "symbolic-ref", "--short", "-q", "HEAD") or ""
        stamp = git(source_dir, "log", "-1", "--format=%ct")
        if stamp and stamp.isdigit():
            commit_epoch = int(stamp)
    if not ref:
        ref = environment.get("GITHUB_REF_NAME", "").strip()

    date = UNKNOWN
    epoch = environment.get("SOURCE_DATE_EPOCH", "").strip()
    if epoch.isdigit():
        date = format_date(int(epoch))
    elif commit_epoch is not None:
        date = format_date(commit_epoch)

    number = build_number.strip() or environment.get("GITHUB_RUN_NUMBER", "").strip() or UNKNOWN
    return {"commit": commit, "dirty": dirty, "ref": ref, "buildNumber": number, "date": date}


def cpp_string(text: str) -> str:
    safe = text.encode("ascii", "replace").decode("ascii")
    safe = safe.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ").replace("\r", " ")
    return f'"{safe}"'


def render_source(values: dict) -> str:
    return (
        "// SPDX-" "License-Identifier: GPL-3.0-or-later\n"
        "// Written by tools/licences/build_info.py on every build. Do not edit.\n"
        '#include "build_info.hpp"\n'
        "\n"
        "namespace ej::app\n"
        "{\n"
        "\n"
        "BuildInfo generated_build_info()\n"
        "{\n"
        f"    return BuildInfo{{{cpp_string(values['commit'])}, {'true' if values['dirty'] else 'false'}, "
        f"{cpp_string(values['ref'])},\n"
        f"                     {cpp_string(values['buildNumber'])}, {cpp_string(values['date'])},\n"
        f"                     {cpp_string(values.get('sourceUrl', ''))}}};\n"
        "}\n"
        "\n"
        "} // namespace ej::app\n"
    )


def write_source(path: Path, values: dict) -> None:
    text = render_source(values)
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--build-number", default="")
    parser.add_argument("--source-url", default="", help="where the source is (the project's homepage)")
    parser.add_argument("--print", action="store_true", help="print the values as JSON")
    args = parser.parse_args(argv)
    values = collect(args.source_dir, args.build_number, dict(os.environ))
    values["sourceUrl"] = args.source_url
    if args.print:
        print(json.dumps(values, indent=1))
    if args.out:
        write_source(args.out, values)
    return 0


if __name__ == "__main__":
    sys.exit(main())
