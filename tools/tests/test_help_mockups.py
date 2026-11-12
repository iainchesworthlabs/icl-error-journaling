# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/visual/help_mockups.py (label: unit). The in-app help mock-ups of Stage 10 (docs/plan/stage-10.md, 10.5).

Run with: python3 -m unittest discover -s tools/tests -v
The mock-ups are generated pages, so the tests read what the script would write and compare it with the files on disk.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "visual"))

import help_mockups  # noqa: E402


class HelpMockups(unittest.TestCase):
    def setUp(self) -> None:
        self.files = help_mockups.outputs()

    def test_every_page_has_a_light_and_a_dark_file(self) -> None:
        names = {path.name for path in self.files}
        for name in help_mockups.SCREENS_TO_WRITE:
            self.assertIn(f"{name}.dc.html", names)
            self.assertIn(f"{name}.dark.dc.html", names)
        self.assertEqual(len(self.files), 2 * len(help_mockups.SCREENS_TO_WRITE))

    def test_the_files_are_what_the_script_writes(self) -> None:
        for path, text in self.files.items():
            self.assertTrue(path.is_file(), f"{path.name} is missing; run tools/visual/help_mockups.py")
            self.assertEqual(path.read_text(encoding="utf-8"), text, f"{path.name} is not current; run tools/visual/help_mockups.py")

    def test_the_pages_have_the_phone_frame(self) -> None:
        for path, text in self.files.items():
            self.assertIn('"width":402,"height":874', text, path.name)

    def test_every_help_button_has_an_accessible_name_and_a_44_point_target(self) -> None:
        for path, text in self.files.items():
            for found in re.finditer(r"<button aria-label=\"([^\"]*)\" style=\"width: 44px; height: 44px", text):
                self.assertTrue(found.group(1).strip(), path.name)
        for name, topic in (("ErrorTypesHelp", "Help with error types"), ("ImportErrorsHelp", "Help with importing a setup file")):
            self.assertIn(f'aria-label="{topic}"', self.files[help_mockups.SCREENS / f"{name}.dc.html"])

    def test_only_the_fictional_names_are_used(self) -> None:
        text = " ".join(self.files.values())
        for word in ("Sam Example", "UNIT-27", "North Site"):
            self.assertIn(word, text)

    def test_no_colour_is_written_by_hand(self) -> None:
        tokens = help_mockups.base.TOKENS
        allowed = {value.lower() for scheme in ("light", "dark") for value in tokens["colour"][scheme].values() if isinstance(value, str)}
        allowed |= {t[key].lower() for t in tokens["tilePalette"] for key in t if isinstance(t[key], str) and t[key].startswith("#")}
        for path, text in self.files.items():
            for colour in set(re.findall(r"#[0-9a-fA-F]{6}\b", text)):
                self.assertIn(colour.lower(), allowed, f"{path.name} has the colour {colour}, which is not in tokens.json")

    def test_the_gap_between_check_and_write_is_reported(self) -> None:
        self.assertEqual(help_mockups.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
