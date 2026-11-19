# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for docs/icl-app.json, the tile manifest of the company site (label: unit).

The company site (icl-site) reads https://iainchesworthlabs.com/field-test/icl-app.json when it is deployed and builds the Field Test
tile from it: name, summary, tags, icon and the colours of docs/theme.css. The rules below are the ones that site enforces (the
kit's README, "Show up in the product lists"); a manifest it rejects stops its deploy, so they are checked here first.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

DOCS = Path(__file__).resolve().parents[2] / "docs"
FIELDS = {"version", "name", "family", "summary", "tags", "status", "icon", "glyph", "order"}
FAMILIES = {"audio", "games", "test-equipment", "pool"}
ICON = re.compile(r"^(?!.*\.\.)(?![/\\])[A-Za-z0-9._/-]{1,100}\.(svg|png|webp)$")


class TileManifest(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = json.loads((DOCS / "icl-app.json").read_text(encoding="utf-8"))

    def test_fields_follow_the_company_rules(self) -> None:
        m = self.manifest
        self.assertLessEqual(set(m), FIELDS)
        self.assertEqual(m["version"], 1)
        self.assertEqual(m["family"] in FAMILIES, True)
        self.assertEqual(m["status"] in {"available", "coming-soon"}, True)
        self.assertTrue(1 <= len(m["name"]) <= 40)
        self.assertTrue(1 <= len(m["summary"]) <= 120)
        self.assertLessEqual(len(m["tags"]), 4)
        for text in [m["name"], m["summary"], *m["tags"]]:
            self.assertNotRegex(text, r"[<>]")

    def test_the_icon_is_a_file_that_the_site_publishes(self) -> None:
        icon = self.manifest["icon"]
        self.assertRegex(icon, ICON)
        self.assertTrue((DOCS / icon).is_file(), f"docs/{icon} is not there")

    def test_the_card_has_a_palette_to_read(self) -> None:
        self.assertIn('[data-app="field-test"]', (DOCS / "theme.css").read_text(encoding="utf-8"))

    def test_the_summary_does_not_describe_the_app_as_for_robots(self) -> None:
        # The app is for any software or hardware test session (docs/index.md); the early tile said "robot sessions".
        self.assertNotIn("robot", self.manifest["summary"].lower())


if __name__ == "__main__":
    unittest.main()
