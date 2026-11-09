# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/docs/demo_setup_hook.py (label: unit). T11-4 of docs/plan/stage-11.md.

Run with: python3 -m unittest discover -s tools/tests -v
That the sample itself imports cleanly is tested by the core and bridge tests, which read the same file (EJ_SAMPLE_CONFIG).
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "docs"))

import demo_setup_hook as hook  # noqa: E402


class DemoSetupHookTest(unittest.TestCase):
    def test_publishes_the_sample_byte_for_byte(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = hook.publish(Path(directory))
            self.assertEqual(target, Path(directory) / "demo" / "demo-setup.ejconfig")
            self.assertEqual(target.read_bytes(), (ROOT / "config" / "sample-config.json").read_bytes())

    def test_on_post_build_uses_the_site_dir(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            hook.on_post_build({"site_dir": directory})
            self.assertTrue((Path(directory) / "demo" / "demo-setup.ejconfig").is_file())

    def test_missing_source_fails_the_build(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                hook.publish(Path(directory), Path(directory) / "missing.json")

    def test_sample_is_a_setup_file_with_stand_ins(self) -> None:
        sample = json.loads((ROOT / "config" / "sample-config.json").read_text(encoding="utf-8"))
        self.assertEqual(sample["schema_version"], 1)
        self.assertEqual(sample["preferences"]["facilitator"], "Sam Example")
        self.assertEqual(sample["preferences"]["unit_prefix"], "UNIT")
        self.assertIn("North Site", [location["name"] for location in sample["locations"]])

    def test_mkdocs_runs_the_hook(self) -> None:
        mkdocs = (ROOT / "mkdocs.yml").read_text(encoding="utf-8")
        self.assertIn("tools/docs/demo_setup_hook.py", mkdocs)

    def test_review_notes_name_the_published_address(self) -> None:
        notes = ROOT / "store" / "appstore" / "review_information" / "notes.txt"
        if not notes.is_file():
            self.skipTest("store/ is not on this branch yet")
        self.assertIn("/icl-error-journaling/demo/demo-setup.ejconfig", notes.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
