#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cancel merge-queue workflow runs whose queue entry is gone.

    python3 tools/ci/cancel_orphan_queue_runs.py --repo OWNER/NAME [--dry-run] [--min-age-minutes 10]

When a merge-queue entry is ejected (checks_timed_out, a conflict, a dequeue), GitHub deletes its
`gh-readonly-queue/...` branch, but the workflow runs started for it carry on and hold runners. A run is an
orphan only when all of these hold:

  * its event is merge_group, and it is queued or in progress;
  * its head branch starts with `gh-readonly-queue/`;
  * it is older than --min-age-minutes (the queue creates the branch before the run, so this is slack for races);
  * GitHub answers 404 for that branch on two lookups --recheck-seconds apart.

Any other answer to the lookup (a rate limit, a 5xx, a network failure) is "unknown" and the run is left alone.
A live entry's branch exists for as long as its runs do, so a live run is never cancelled.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.parse
from datetime import datetime, timezone

QUEUE_PREFIX = "gh-readonly-queue/"


def is_candidate(run: dict, now: datetime, min_age_minutes: float) -> bool:
    """True when the run is a queue run old enough to be judged."""
    if run.get("event") != "merge_group" or run.get("status") not in ("queued", "in_progress"):
        return False
    if not str(run.get("head_branch", "")).startswith(QUEUE_PREFIX):
        return False
    created = datetime.fromisoformat(run["created_at"].replace("Z", "+00:00"))
    return (now - created).total_seconds() >= min_age_minutes * 60


def gh(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["gh", *args], capture_output=True, text=True, check=False)


def branch_state(repo: str, branch: str) -> str:
    """'exists', 'gone' (a definite 404) or 'unknown'."""
    ref = urllib.parse.quote(f"heads/{branch}", safe="/")
    result = gh("api", f"repos/{repo}/git/ref/{ref}", "--silent")
    if result.returncode == 0:
        return "exists"
    if "HTTP 404" in result.stderr:
        return "gone"
    return "unknown"


def list_runs(repo: str, status: str) -> list[dict]:
    result = gh("api", "--paginate", f"repos/{repo}/actions/runs?event=merge_group&status={status}&per_page=100",
                "--jq", ".workflow_runs[]")
    if result.returncode != 0:
        print(f"::warning::could not list {status} runs: {result.stderr.strip()}")
        return []
    return [json.loads(line) for line in result.stdout.splitlines() if line.strip()]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--min-age-minutes", type=float, default=10)
    parser.add_argument("--recheck-seconds", type=float, default=20)
    args = parser.parse_args()

    now = datetime.now(timezone.utc)
    runs = {r["id"]: r for status in ("queued", "in_progress") for r in list_runs(args.repo, status)}
    candidates = [r for r in runs.values() if is_candidate(r, now, args.min_age_minutes)]
    print(f"{len(runs)} queue runs active, {len(candidates)} old enough to judge")

    gone: list[dict] = []
    for run in candidates:
        if branch_state(args.repo, run["head_branch"]) == "gone":
            gone.append(run)
        else:
            print(f"keep  run {run['id']}: entry branch present or unknown ({run['head_branch']})")
    if gone:
        time.sleep(args.recheck_seconds)
    for run in gone:
        if branch_state(args.repo, run["head_branch"]) != "gone":
            print(f"keep  run {run['id']}: entry branch is back or unknown on the second look")
            continue
        label = f"run {run['id']} ({run.get('name')}, {run['head_branch']}, started {run['created_at']})"
        if args.dry_run:
            print(f"would cancel {label}")
            continue
        result = gh("api", "-X", "POST", f"repos/{args.repo}/actions/runs/{run['id']}/cancel", "--silent")
        status = "cancelled" if result.returncode == 0 else f"not cancelled: {result.stderr.strip()}"
        print(f"{status} {label}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
