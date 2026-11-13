# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard tests for the store metadata upload (label: unit). T11-10 of docs/plan/stage-11.md.

Run with: python3 -m unittest discover -s tools/tests -v

Submitting for review, releasing and rolling out are the owner's actions in the consoles, and builds come only from
release.yml. These tests read fastlane/Fastfile and .github/workflows/store-metadata.yml as text and fail when either
asks for a submission, a release, a rollout or a binary upload, or when the paths they read do not exist.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FASTFILE = ROOT / "fastlane" / "Fastfile"
WORKFLOW = ROOT / ".github" / "workflows" / "store-metadata.yml"


def code(path: Path) -> str:
    """The file without its comments, so that the header's prose does not count."""
    return "\n".join(line.split("#", 1)[0] for line in path.read_text(encoding="utf-8").splitlines())


class StoreUploadGuardTest(unittest.TestCase):
    def test_fastfile_never_submits_or_releases(self) -> None:
        fastfile = code(FASTFILE)
        self.assertRegex(fastfile, r"submit_for_review:\s*false")
        self.assertRegex(fastfile, r"automatic_release:\s*false")
        self.assertNotRegex(fastfile, r"submit_for_review:\s*true")
        self.assertNotRegex(fastfile, r"automatic_release:\s*true")
        for action in ("upload_to_app_store", "appstore(", "upload_to_play_store", "submit_review", "release_status",
                       "promote_track", "track_promote_to", "rollout", "pilot(", "upload_to_testflight", "gym(",
                       "build_app", "gradle("):
            self.assertNotIn(action, fastfile, action)

    def test_fastfile_never_uploads_a_build(self) -> None:
        fastfile = code(FASTFILE)
        self.assertRegex(fastfile, r"skip_binary_upload:\s*true")
        self.assertRegex(fastfile, r"skip_upload_apk:\s*true")
        self.assertRegex(fastfile, r"skip_upload_aab:\s*true")
        self.assertNotRegex(fastfile, r"\b(ipa|pkg|aab|apk):")

    def test_fastfile_reads_the_store_folder(self) -> None:
        fastfile = FASTFILE.read_text(encoding="utf-8")
        for folder in ('"store", "appstore"', '"store", "play"'):
            self.assertIn(folder, fastfile)
        self.assertTrue((ROOT / "store" / "appstore" / "en-AU" / "name.txt").is_file())
        self.assertTrue((ROOT / "store" / "play" / "en-AU" / "title.txt").is_file())

    def test_workflow_runs_by_hand_only(self) -> None:
        workflow = code(WORKFLOW)
        triggers = re.search(r"^on:\n((?:[ \t]+.*\n|\n)+?)^\S", workflow + "\nend", re.MULTILINE)
        self.assertIsNotNone(triggers)
        self.assertRegex(triggers.group(1), r"^\s{2}workflow_dispatch:", "the only trigger is workflow_dispatch")
        self.assertIsNone(re.search(r"^\s{2}(push|pull_request|pull_request_target|schedule|release|workflow_run):",
                                    triggers.group(1), re.MULTILINE))

    def test_workflow_calls_only_the_metadata_lanes(self) -> None:
        workflow = code(WORKFLOW)
        lanes = re.findall(r"fastlane\s+(\w+)\s+(\w+)", workflow)
        self.assertEqual(sorted(set(lanes)), [("android", "metadata"), ("ios", "inspect"), ("ios", "metadata")])

    def test_workflow_can_only_read_the_repository(self) -> None:
        workflow = code(WORKFLOW)
        self.assertIn("permissions: {}", workflow)
        self.assertEqual(re.findall(r"^\s+(\w[\w-]*):\s*write", workflow, re.MULTILINE), [])


if __name__ == "__main__":
    unittest.main()
