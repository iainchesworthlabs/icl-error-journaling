#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add up the result lines the kill harness's shards wrote (docs/ci.md, "The kill harness tiers").

    python3 tools/ci/kill_summary.py --tier exit-gate --seed 0x... --expected-runs 20000 DIR...

Each directory holds `summary.txt` files, one for each shard of each leg, with the lines `ej-kill result ...`,
`ej-kill recovery result ...`, `ej-kill migration result ...` and `ej-kill draft result ...` that `ej_kill_driver`
appends to $EJ_KILL_SUMMARY. Prints a Markdown summary (for $GITHUB_STEP_SUMMARY) and exits 1 when any run failed, when no
result line was found, or when fewer random-kill runs were counted than expected, which is how a shard that died without
reporting is caught.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PAIR = re.compile(r"(\w+)=(\S+)")
KINDS = {
    "ej-kill result": "random kills",
    "ej-kill recovery result": "recovery killed at random moments",
    "ej-kill migration result": "migration killed at random moments",
    "ej-kill draft result": "typed draft killed",
}


def parse_line(line: str) -> tuple[str, dict[str, str]] | None:
    for prefix, kind in KINDS.items():
        if line.startswith(prefix + " "):
            return kind, dict(PAIR.findall(line[len(prefix):]))
    return None


def read(paths: list[Path]) -> list[tuple[str, str, dict[str, str]]]:
    """(file, kind, values) for every result line under the given directories (or files)."""
    found = []
    for path in paths:
        files = [path] if path.is_file() else sorted(path.rglob("summary.txt"))
        for file in files:
            for line in file.read_text(encoding="utf-8", errors="replace").splitlines():
                parsed = parse_line(line.strip())
                if parsed:
                    found.append((str(file.parent.name), parsed[0], parsed[1]))
    return found


def number(values: dict[str, str], key: str) -> int:
    try:
        return int(values.get(key, "0"))
    except ValueError:
        return 0


def summarise(lines: list[tuple[str, str, dict[str, str]]], tier: str, seed: str, expected_runs: int) -> tuple[str, bool]:
    totals: dict[str, dict[str, int]] = {}
    for _, kind, values in lines:
        entry = totals.setdefault(kind, {"lines": 0, "runs": 0, "failures": 0, "acknowledged": 0})
        entry["lines"] += 1
        # The random-kill line says `runs`; the others say `iterations`.
        entry["runs"] += number(values, "runs") or number(values, "iterations")
        entry["failures"] += number(values, "failures")
        entry["acknowledged"] += number(values, "acknowledged_operations")
    random_kills = totals.get("random kills", {"runs": 0, "failures": 0, "acknowledged": 0, "lines": 0})
    ok = bool(lines) and all(entry["failures"] == 0 for entry in totals.values())
    ok = ok and random_kills["runs"] >= expected_runs
    out = [f"### Kill harness, {tier}", "", f"Master seed `{seed}`", "",
           "| What | Shard results | Runs | Failures | Acknowledged operations |", "|---|---|---|---|---|"]
    for kind in KINDS.values():
        if kind in totals:
            entry = totals[kind]
            out.append(f"| {kind} | {entry['lines']} | {entry['runs']} | {entry['failures']} | "
                       f"{entry['acknowledged'] or ''} |")
    out.append("")
    if not lines:
        out.append("**No result line was found: no shard reported.**")
    elif random_kills["runs"] < expected_runs:
        out.append(f"**Only {random_kills['runs']} of {expected_runs} random-kill runs were counted.**")
    elif ok:
        out.append(f"{random_kills['runs']} random-kill runs, no failures.")
    else:
        out.append("**Failures: see the failing shards and their `kill-failure-*` artifacts.**")
    return "\n".join(out) + "\n", ok


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tier", required=True)
    parser.add_argument("--seed", required=True)
    parser.add_argument("--expected-runs", type=int, default=0, help="random-kill runs that must have been counted")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args(argv)
    text, ok = summarise(read(args.paths), args.tier, args.seed, args.expected_runs)
    sys.stdout.write(text)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
