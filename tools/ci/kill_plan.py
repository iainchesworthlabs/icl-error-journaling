#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plan a sharded run of the kill harness (docs/ci.md, "The kill harness tiers").

    python3 tools/ci/kill_plan.py --iterations 10000 --shards 10 [--seed 0x...] [--secondary 200]

Prints `key=value` lines for $GITHUB_OUTPUT:

    seed    the master seed as 0x followed by 16 hex digits: the one given, or a fresh one drawn here
    shards  a JSON list with one entry for each shard: {"index", "first", "last", "secondary"}

`first` and `last` are the random-kill iterations the shard runs (EJ_KILL_FIRST and EJ_KILL_LAST). Iteration `i` depends
only on the master seed and `i`, so the shards together run each iteration from 0 to `iterations - 1` once. `secondary`
is the shard's share of the random kills of recovery, of a migration and of the note draft (EJ_KILL_*_ITERATIONS); the
driver offsets their seeds by `first`, so shards do not repeat each other.
"""

from __future__ import annotations

import argparse
import json
import re
import secrets
import sys

SEED_PATTERN = re.compile(r"^(0[xX][0-9a-fA-F]{1,16}|[0-9]{1,19})$")


def normalise_seed(text: str) -> str:
    """The seed as 0x and 16 hex digits. An empty text draws a new one."""
    text = text.strip()
    if not text:
        return f"0x{secrets.randbits(64):016x}"
    if not SEED_PATTERN.match(text):
        raise ValueError(f"not a seed: {text!r} (decimal or 0x and up to 16 hex digits)")
    return f"0x{int(text, 0):016x}"


def split(iterations: int, shards: int, secondary: int) -> list[dict]:
    if iterations < 1 or shards < 1:
        raise ValueError("iterations and shards must be at least 1")
    shards = min(shards, iterations)
    per_shard = -(-iterations // shards)  # ceiling
    secondary_share = -(-secondary // shards) if secondary > 0 else 0
    result = []
    for index in range(shards):
        first = index * per_shard
        last = min(first + per_shard, iterations) - 1
        if first > last:
            break
        result.append({"index": index, "first": first, "last": last, "secondary": secondary_share})
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seed", default="", help="the master seed; empty draws a new one")
    parser.add_argument("--iterations", type=int, required=True, help="random-kill iterations in all")
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--secondary", type=int, default=25,
                        help="random kills of recovery, of a migration and of the draft, in all")
    args = parser.parse_args(argv)
    try:
        seed = normalise_seed(args.seed)
        shards = split(args.iterations, args.shards, args.secondary)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(f"seed={seed}")
    print(f"shards={json.dumps(shards, separators=(',', ':'))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
