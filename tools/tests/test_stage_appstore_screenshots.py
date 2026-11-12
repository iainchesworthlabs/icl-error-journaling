# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for stage_appstore_screenshots.py (label: unit). Task 11.7 of docs/plan/stage-11.md.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "store"))

import stage_appstore_screenshots as stager  # noqa: E402


class StageAppStoreScreenshotsTest(unittest.TestCase):
    def make_source(self, root: Path, skip: str = "") -> Path:
        for folder, _ in stager.SETS:
            if folder == skip:
                continue
            (root / folder).mkdir(parents=True)
            for name in ("01-a.png", "02-b.png"):
                (root / folder / name).write_bytes(folder.encode())
        return root

    def test_files_are_flat_and_ordered_by_set(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            written = stager.stage(self.make_source(root / "src"), root / "out" / "en-GB")
            names = [path.name for path in written]
            ipad = [name for name in names if name.startswith("ipad")]
            self.assertEqual(ipad, sorted(ipad))
            self.assertTrue(ipad[0].startswith("ipad-1-portrait"))
            self.assertEqual(names[0], "iphone-01-a.png")
            self.assertEqual(len(names), 6)
            self.assertEqual((root / "out" / "en-GB" / "ipad-2-landscape-02-b.png").read_bytes(), b"appstore-ipad-13-landscape")

    def test_target_is_emptied_first(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "out").mkdir()
            (root / "out" / "old.png").write_bytes(b"x")
            stager.stage(self.make_source(root / "src"), root / "out")
            self.assertFalse((root / "out" / "old.png").exists())

    def test_missing_set_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(FileNotFoundError):
                stager.stage(self.make_source(root / "src", skip="appstore-ipad-13-portrait"), root / "out")


if __name__ == "__main__":
    unittest.main()
