# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/ci/cancel_orphan_queue_runs.py: which runs may be judged, and that unknown answers keep a run."""

import subprocess
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))

import cancel_orphan_queue_runs as cancel  # noqa: E402

NOW = datetime(2026, 11, 10, 12, 0, tzinfo=timezone.utc)


def run(**over):
    base = {"id": 1, "event": "merge_group", "status": "in_progress",
            "head_branch": "gh-readonly-queue/develop/pr-1-abc", "created_at": "2026-11-10T11:00:00Z"}
    base.update(over)
    return base


class CandidateTests(unittest.TestCase):
    def test_old_queue_run_is_judged(self):
        self.assertTrue(cancel.is_candidate(run(), NOW, 10))

    def test_young_run_is_not(self):
        self.assertFalse(cancel.is_candidate(run(created_at="2026-11-10T11:55:00Z"), NOW, 10))

    def test_other_events_and_branches_are_not(self):
        self.assertFalse(cancel.is_candidate(run(event="pull_request"), NOW, 10))
        self.assertFalse(cancel.is_candidate(run(head_branch="develop"), NOW, 10))

    def test_finished_runs_are_not(self):
        self.assertFalse(cancel.is_candidate(run(status="completed"), NOW, 10))


class BranchStateTests(unittest.TestCase):
    def state(self, returncode, stderr=""):
        done = subprocess.CompletedProcess([], returncode, "", stderr)
        with mock.patch.object(cancel, "gh", return_value=done):
            return cancel.branch_state("o/r", "gh-readonly-queue/develop/pr-1-abc")

    def test_found(self):
        self.assertEqual(self.state(0), "exists")

    def test_not_found_is_gone(self):
        self.assertEqual(self.state(1, "gh: Not Found (HTTP 404)"), "gone")

    def test_errors_are_unknown_and_never_gone(self):
        self.assertEqual(self.state(1, "gh: Bad Gateway (HTTP 502)"), "unknown")
        self.assertEqual(self.state(1, "gh: API rate limit exceeded (HTTP 403)"), "unknown")
        self.assertEqual(self.state(1, "dial tcp: lookup api.github.com"), "unknown")


if __name__ == "__main__":
    unittest.main()
