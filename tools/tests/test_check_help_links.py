# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_help_links.py (label: unit). The in-app help and the user guide.

Run with: python3 -m unittest discover -s tools/tests -v
Each case copies the help topics, the guide, the pages the app opens, the QML (for the topic ids it asks for) and mkdocs.yml
into a temporary folder and changes one of them.
"""

from __future__ import annotations

import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ci"))

import check_help_links as links_check  # noqa: E402


class HelpLinksCheck(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        shutil.copytree(ROOT / "docs" / "guide", self.root / "docs" / "guide", ignore=shutil.ignore_patterns("img"))
        for name in ("index.md", "support.md", "privacy.md"):
            shutil.copyfile(ROOT / "docs" / name, self.root / "docs" / name)
        shutil.copyfile(ROOT / "mkdocs.yml", self.root / "mkdocs.yml")
        shutil.copytree(ROOT / "apps" / "app" / "qml", self.root / "apps" / "app" / "qml")

    def tearDown(self) -> None:
        self.dir.cleanup()

    def edit(self, relative: str, old: str, new: str) -> None:
        path = self.root / relative
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text, "the test's starting text moved; update the test")
        path.write_text(text.replace(old, new, 1), encoding="utf-8")

    def topics(self, old: str, new: str) -> None:
        self.edit(links_check.TOPICS, old, new)

    def problems(self) -> list[str]:
        return links_check.check(self.root)

    def test_the_real_help_and_guide_agree(self) -> None:
        self.assertEqual(self.problems(), [])

    def test_a_renamed_heading_is_found(self) -> None:
        self.edit("docs/guide/setup.md", "## Your board", "## The board")
        self.assertTrue(any("#your-board" in p and "board" in p for p in self.problems()))

    def test_a_moved_page_is_found(self) -> None:
        (self.root / "docs" / "guide" / "logging.md").rename(self.root / "docs" / "guide" / "a-day.md")
        self.assertTrue(any("guide/logging/" in p for p in self.problems()))

    def test_an_anchor_that_is_not_there_is_found(self) -> None:
        self.topics('"anchor": "note-templates"', '"anchor": "no-such-heading"')
        self.assertTrue(any("no-such-heading" in p for p in self.problems()))

    def test_a_topic_may_name_a_page_without_a_heading(self) -> None:
        # Today's details names its page only.
        self.assertEqual(self.problems(), [])
        self.assertIn('"anchor": ""', (self.root / links_check.TOPICS).read_text(encoding="utf-8"))

    def test_the_import_errors_row_is_held_to_its_heading(self) -> None:
        self.topics('"when-the-app-refuses-a-file"', '"when-it-refuses"')
        self.assertTrue(any("import errors row" in p for p in self.problems()))

    def test_the_pages_of_the_help_group_must_exist(self) -> None:
        (self.root / "docs" / "privacy.md").unlink()
        self.assertTrue(any("privacyPath" in p for p in self.problems()))

    def test_the_site_address_must_match_mkdocs(self) -> None:
        self.edit("mkdocs.yml", "site_url: https://iainchesworthlabs.github.io/", "site_url: https://docs.example.org/")
        self.assertTrue(any("siteUrl" in p and "docs.example.org" in p for p in self.problems()))

    def test_a_topic_that_a_screen_asks_for_must_exist(self) -> None:
        self.edit("apps/app/qml/shell/AppShell.qml", 'helpSheet.show(topic);', 'helpSheet.show(topic);\n        helpSheet.show("noSuchTopic");')
        self.assertTrue(any("noSuchTopic" in p for p in self.problems()))

    def test_a_topic_needs_two_to_five_paragraphs(self) -> None:
        self.topics(
            '"paragraphs": [qsTr("A template is a note you log often. Its chip is above the note field on the Log screen."), ',
            '"paragraphs": [',
        )
        self.topics(
            'qsTr("<b>Name</b> is what the chip says. <b>Question to ask</b> is what you are asked when you tap it, for example “Which song?”."), ',
            "",
        )
        self.topics(
            'qsTr("<b>Line in the log</b> is what gets written. Tap <b>Insert answer</b> to put the answer where you want it, for example “Played [answer]”."), ',
            "",
        )
        self.assertTrue(any("'templates' has 1 paragraphs" in p for p in self.problems()))

    def test_a_topic_without_a_page_is_found(self) -> None:
        self.topics('"page": "guide/todays-details/"', '"page": ""')
        self.assertTrue(any("names no page" in p for p in self.problems()))

    def test_slugs_are_the_ones_mkdocs_makes(self) -> None:
        self.assertEqual(links_check.slug("Today's details"), "todays-details")
        self.assertEqual(links_check.slug("Log an error: double-tap"), "log-an-error-double-tap")
        self.assertEqual(links_check.slug("Locations and unit environments"), "locations-and-unit-environments")
        self.assertEqual(links_check.slug("Q & A"), "q-a")

    def test_a_heading_inside_a_fence_is_not_a_heading(self) -> None:
        found = links_check.anchors("# Real\n\n```\n# Not a heading\n```\n\n## Another one {#custom}\n")
        self.assertEqual(found, {"real", "custom"})

    def test_the_command_line(self) -> None:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(links_check.main(["--root", str(self.root)]), 0)
            self.assertIn("ok", out.getvalue())
            self.edit("docs/guide/setup.md", "## Your board", "## The board")
            self.assertEqual(links_check.main(["--root", str(self.root)]), 1)
            self.assertIn("your-board", err.getvalue())
            (self.root / links_check.TOPICS).unlink()
            self.assertEqual(links_check.main(["--root", str(self.root)]), 2)


if __name__ == "__main__":
    unittest.main()
