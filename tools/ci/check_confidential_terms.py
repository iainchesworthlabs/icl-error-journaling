#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when a confidential term appears in the tree, the commit messages or the pull request text (PRD NFR-10).

    CONFIDENTIAL_TERMS=<newline separated terms> python3 tools/ci/check_confidential_terms.py \
        [--range BASE..HEAD] [--event pull_request] [--secrets-unavailable]

Matching is case-insensitive and on whole words. The script never prints a term: it reports where
a match is (file and line, commit and line, or the pull request title or body line) and nothing else.
A path that itself contains a term is reported by its position in the file list, not by name.

The pull request title and body come from the environment (PR_TITLE, PR_BODY), never from the command line.

The term list is optional. Without terms the check warns that it did not run and passes, on every
event. (--secrets-unavailable only changes the wording of the warning: a fork or Dependabot pull
request cannot read the secret even when it is set.)
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from collections.abc import Mapping
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def parse_terms(raw: str | None) -> list[str]:
    if not raw:
        return []
    return [t for t in (line.strip() for line in raw.splitlines()) if t]


def compile_terms(terms: list[str]) -> re.Pattern[str]:
    alternation = "|".join(re.escape(t) for t in sorted(set(terms), key=len, reverse=True))
    return re.compile(rf"(?<!\w)(?:{alternation})(?!\w)", re.IGNORECASE)


def scan_text(pattern: re.Pattern[str], text: str) -> list[int]:
    """Return the 1-based numbers of the lines that match."""
    return [n for n, line in enumerate(text.splitlines(), 1) if pattern.search(line)]


def git(root: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(root), *args], check=True, capture_output=True).stdout


def scan_tree(pattern: re.Pattern[str], root: Path) -> list[str]:
    findings: list[str] = []
    paths = [p for p in git(root, "ls-files", "-z").decode("utf-8", "replace").split("\0") if p]
    for index, path in enumerate(paths, 1):
        label = path
        if pattern.search(path):
            label = f"<tracked file name #{index}>"
            findings.append(f"{label}: the file name matches")
        file = root / path
        if not file.is_file():
            continue
        text = file.read_bytes().decode("utf-8", "replace")
        findings += [f"{label}:{n}" for n in scan_text(pattern, text)]
    return findings


def scan_commits(pattern: re.Pattern[str], root: Path, rev_range: str) -> list[str]:
    findings: list[str] = []
    for sha in git(root, "rev-list", rev_range).decode().split():
        # Lines 1 and 2 are author and committer; the message starts at line 3.
        message = git(root, "log", "-1", "--format=%an <%ae>%n%cn <%ce>%n%B", sha).decode("utf-8", "replace")
        for n in scan_text(pattern, message):
            where = "author" if n == 1 else "committer" if n == 2 else f"message line {n - 2}"
            findings.append(f"commit {sha[:12]}: {where}")
    return findings


def scan_pull_request(pattern: re.Pattern[str], title: str, body: str) -> list[str]:
    findings = []
    if scan_text(pattern, title):
        findings.append("pull request title")
    findings += [f"pull request body line {n}" for n in scan_text(pattern, body)]
    return findings


def main(argv: list[str] | None = None, env: Mapping[str, str] | None = None) -> int:
    env = os.environ if env is None else env
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(ROOT))
    parser.add_argument("--range", dest="rev_range", default="", help="commits to check, BASE..HEAD")
    parser.add_argument("--event", default="")
    parser.add_argument("--secrets-unavailable", action="store_true",
                        help="the run cannot read the term list (fork or Dependabot pull request)")
    args = parser.parse_args(argv)

    terms = parse_terms(env.get("CONFIDENTIAL_TERMS"))
    if not terms:
        reason = ("is not available to this pull request" if args.secrets_unavailable
                  else "is empty or not set")
        print(f"::warning::CONFIDENTIAL_TERMS {reason}, so the confidential-term check did not run. "
              "Create the repository secret to enable it (docs/ci.md).")
        return 0

    pattern = compile_terms(terms)
    root = Path(args.root)
    findings = scan_tree(pattern, root)
    if args.rev_range:
        findings += scan_commits(pattern, root, args.rev_range)
    findings += scan_pull_request(pattern, env.get("PR_TITLE", ""), env.get("PR_BODY", ""))

    for finding in findings:
        print(f"error: {finding}: contains a confidential term", file=sys.stderr)
    if findings:
        print(f"{len(findings)} confidential-term match(es). The term is not printed; look at the location.",
              file=sys.stderr)
        return 1
    print(f"No confidential terms found ({len(terms)} terms checked).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
