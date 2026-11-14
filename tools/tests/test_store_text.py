# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_store_text.py (label: unit). T11-1 and T11-3 of docs/plan/stage-11.md.

Run with: python3 -m unittest discover -s tools/tests -v
Only fictional names are used.
"""

from __future__ import annotations

import contextlib
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_store_text as store_check  # noqa: E402
import site_address  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
URL = site_address.site_url(ROOT) + "support/"
GOOD = {
    "appstore/en-AU/name.txt": "Sample Board",
    "appstore/en-AU/subtitle.txt": "A short subtitle",
    "appstore/en-AU/promotional_text.txt": "Promotional text.",
    "appstore/en-AU/description.txt": "First paragraph.\n\nSecond paragraph.",
    "appstore/en-AU/keywords.txt": "tester,error log,robot",
    "appstore/en-AU/release_notes.txt": "First release.",
    "appstore/en-AU/support_url.txt": URL,
    "appstore/en-AU/privacy_url.txt": URL.replace("support", "privacy"),
    "appstore/review_information/notes.txt": "Notes for the reviewer.",
    "appstore/copyright.txt": "2026 Example Labs",
    "appstore/primary_category.txt": "PRODUCTIVITY",
    "play/en-AU/title.txt": "Sample Board",
    "play/en-AU/short_description.txt": "Short.",
    "play/en-AU/full_description.txt": "Full description.",
    "play/en-AU/changelogs/default.txt": "First release.",
}


class StoreTextTest(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir)
        for rel, value in GOOD.items():
            self.write(rel, value + "\n")

    def write(self, rel: str, text: str) -> None:
        path = self.dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))

    def problems(self) -> list[str]:
        return store_check.check(self.dir)

    def assert_problem(self, fragment: str) -> None:
        problems = self.problems()
        self.assertTrue(any(fragment in p for p in problems), f"no problem with {fragment!r} in {problems}")

    def test_repository_store_text_passes(self) -> None:
        self.assertEqual(store_check.check(ROOT / "store"), [])

    def test_good_set_passes(self) -> None:
        self.assertEqual(self.problems(), [])

    def test_name_over_30_characters(self) -> None:
        self.write("appstore/en-AU/name.txt", "x" * 31 + "\n")
        self.assert_problem("31 chars, over the limit of 30")

    def test_limit_counts_characters_not_bytes(self) -> None:
        self.write("appstore/en-AU/subtitle.txt", "é" * 30 + "\n")
        self.assertEqual(self.problems(), [])

    def test_keywords_limit_is_bytes(self) -> None:
        self.write("appstore/en-AU/keywords.txt", ",".join(["é" * 9] * 6) + "\n")  # 59 characters, 113 bytes
        self.assert_problem("over the limit of 100")

    def test_play_short_description_over_80(self) -> None:
        self.write("play/en-AU/short_description.txt", "x" * 81 + "\n")
        self.assert_problem("81 chars, over the limit of 80")

    def test_play_changelog_over_500(self) -> None:
        self.write("play/en-AU/changelogs/default.txt", "x" * 501 + "\n")
        self.assert_problem("over the limit of 500")

    def test_missing_required_file(self) -> None:
        (self.dir / "appstore/en-AU/privacy_url.txt").unlink()
        self.assert_problem("privacy_url.txt: missing")

    def test_optional_file_may_be_missing(self) -> None:
        self.assertFalse((self.dir / "appstore/en-AU/marketing_url.txt").exists())
        self.assertEqual(self.problems(), [])

    def test_no_final_newline(self) -> None:
        self.write("appstore/en-AU/subtitle.txt", "A subtitle")
        self.assert_problem("exactly one newline")

    def test_two_final_newlines(self) -> None:
        self.write("appstore/en-AU/subtitle.txt", "A subtitle\n\n")
        self.assert_problem("exactly one newline")

    def test_carriage_return(self) -> None:
        self.write("appstore/en-AU/description.txt", "One.\r\nTwo.\n")
        self.assert_problem("carriage return")

    def test_trailing_space(self) -> None:
        self.write("appstore/en-AU/description.txt", "One. \nTwo.\n")
        self.assert_problem("trailing spaces")

    def test_empty_file(self) -> None:
        self.write("appstore/en-AU/release_notes.txt", "\n")
        self.assert_problem("is empty")

    def test_single_line_field_with_two_lines(self) -> None:
        self.write("appstore/en-AU/subtitle.txt", "One\nTwo\n")
        self.assert_problem("must be one line")

    def test_keyword_space_after_comma(self) -> None:
        self.write("appstore/en-AU/keywords.txt", "tester, robot\n")
        self.assert_problem("space next to a comma")

    def test_keyword_empty(self) -> None:
        self.write("appstore/en-AU/keywords.txt", "tester,,robot\n")
        self.assert_problem("empty keyword")

    def test_keyword_repeated(self) -> None:
        self.write("appstore/en-AU/keywords.txt", "robot,Robot\n")
        self.assert_problem("repeated keyword(s): robot")

    def test_keyword_too_short(self) -> None:
        self.write("appstore/en-AU/keywords.txt", "tester,qa\n")
        self.assert_problem("2 characters or fewer, which Apple does not accept: qa")

    def test_keyword_from_name(self) -> None:
        self.write("appstore/en-AU/keywords.txt", "tester,board log\n")
        self.assert_problem("app's name, which the store indexes already: board")

    def test_url_must_be_https(self) -> None:
        self.write("appstore/en-AU/support_url.txt", URL.replace("https", "http") + "\n")
        self.assert_problem("must be an https URL")

    def test_url_must_be_on_docs_site(self) -> None:
        self.write("appstore/en-AU/privacy_url.txt", "https://example.com/privacy/\n")
        self.assert_problem("must be an https URL")

    def test_unknown_category(self) -> None:
        self.write("appstore/primary_category.txt", "Productivity\n")
        self.assert_problem("not an App Store category identifier")

    def test_secondary_equals_primary(self) -> None:
        self.write("appstore/secondary_category.txt", "PRODUCTIVITY\n")
        self.assert_problem("the same as the primary category")

    def test_play_title_must_match_name(self) -> None:
        self.write("play/en-AU/title.txt", "Other Board\n")
        self.assert_problem("differs from the App Store name")

    def test_caption_too_long(self) -> None:
        self.write(store_check.CAPTIONS, "x" * 61 + "\n")
        self.assert_problem("over 60")

    def test_too_many_captions(self) -> None:
        self.write(store_check.CAPTIONS, "\n".join(f"Caption {n}" for n in range(11)) + "\n")
        self.assert_problem("11 captions")

    def test_main_reports_and_fails(self) -> None:
        self.write("appstore/en-AU/name.txt", "x" * 31 + "\n")
        (self.dir / "store").mkdir()
        for child in list(self.dir.iterdir()):
            if child.name != "store":
                shutil.move(str(child), self.dir / "store" / child.name)
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(store_check.main(["--root", str(self.dir)]), 1)
        self.assertIn("::error::store/appstore/en-AU/name.txt", out.getvalue())


if __name__ == "__main__":
    unittest.main()
