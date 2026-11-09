# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_platform_layout.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_platform_layout as layout  # noqa: E402

ARC_CMAKE = "set_source_files_properties(${bridge_sources} PROPERTIES COMPILE_OPTIONS -fobjc-arc)\n"


def make(root: Path, files: dict[str, str]) -> None:
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


class PlatformLayoutTest(unittest.TestCase):
    def check(self, files: dict[str, str]) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            make(Path(tmp), files)
            return layout.run(Path(tmp))

    def test_pairs_in_step_pass(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/desktop/share.cpp": "",
        }
        self.assertEqual(self.check(files), [])

    def test_missing_stub_is_reported(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/ios/haptics.mm": "",
            "libs/x/src/platform/desktop/share.cpp": "",
        }
        problems = self.check(files)
        self.assertEqual(len(problems), 1)
        self.assertIn("haptics.cpp", problems[0])

    def test_stub_without_ios_file_is_reported(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/desktop/share.cpp": "",
            "libs/x/src/platform/desktop/picker.cpp": "",
        }
        problems = self.check(files)
        self.assertEqual(len(problems), 1)
        self.assertIn("picker.mm", problems[0])

    def test_internal_helper_needs_no_sibling(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/ios/top_view_controller.mm": "",
            "libs/x/src/platform/desktop/share.cpp": "",
        }
        self.assertEqual(self.check(files), [])

    def test_mm_outside_ios_directory_is_reported(self) -> None:
        files = {"libs/x/src/oops.mm": "", "apps/a/src/platform/desktop/y.mm": ""}
        problems = self.check(files)
        self.assertEqual(len(problems), 2)

    def test_missing_arc_flag_is_reported(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": "add_library(x)\n",
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/desktop/share.cpp": "",
        }
        problems = self.check(files)
        self.assertEqual(len(problems), 1)
        self.assertIn("-fobjc-arc", problems[0])

    def test_per_os_layout_of_another_library_is_not_paired(self) -> None:
        files = {
            "libs/s/src/platform/ios/sync.cpp": "",
            "libs/s/src/platform/linux/sync.cpp": "",
            "libs/s/src/platform/linux/lock.cpp": "",
        }
        self.assertEqual(self.check(files), [])

    def test_android_directory_in_step_passes(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/android/share.cpp": "",
            "libs/x/src/platform/desktop/share.cpp": "",
        }
        self.assertEqual(self.check(files), [])

    def test_android_directory_missing_a_capability_is_reported(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/ios/haptics.mm": "",
            "libs/x/src/platform/android/share.cpp": "",
            "libs/x/src/platform/desktop/share.cpp": "",
            "libs/x/src/platform/desktop/haptics.cpp": "",
        }
        problems = self.check(files)
        self.assertEqual(len(problems), 1)
        self.assertIn("android", problems[0])
        self.assertIn("haptics.cpp", problems[0])

    def test_android_only_capability_is_reported(self) -> None:
        files = {
            "libs/x/CMakeLists.txt": ARC_CMAKE,
            "libs/x/src/platform/ios/share.mm": "",
            "libs/x/src/platform/android/share.cpp": "",
            "libs/x/src/platform/android/vibrate.cpp": "",
            "libs/x/src/platform/desktop/share.cpp": "",
        }
        problems = self.check(files)
        self.assertEqual(len(problems), 1)
        self.assertIn("vibrate.mm", problems[0])

    def test_this_repository_is_in_step(self) -> None:
        self.assertEqual(layout.run(Path(__file__).resolve().parent.parent.parent), [])


if __name__ == "__main__":
    unittest.main()
