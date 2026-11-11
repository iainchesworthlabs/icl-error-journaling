# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_setup_doc.py (label: unit). The setup-file page of the guide against libs/core.

Run with: python3 -m unittest discover -s tools/tests -v
Each case copies the files the check reads into a temporary folder and changes one of them. Only fictional names are
used.
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

import check_setup_doc as doc_check  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
FILES = (doc_check.CPP, doc_check.HEADER, doc_check.UNIT_HEADER, doc_check.PAGE, doc_check.SAMPLE)


class SetupDocCheck(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        for name in FILES:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)

    def tearDown(self) -> None:
        self.dir.cleanup()

    def change(self, name: str, old: str, new: str) -> None:
        path = self.root / name
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text, "the test's starting text moved; update the test")
        path.write_text(text.replace(old, new, 1), encoding="utf-8")

    def problems(self) -> list[str]:
        return doc_check.check(self.root)

    def test_the_real_page_matches_the_reader(self) -> None:
        self.assertEqual(self.problems(), [])

    def test_main_reports_ok_and_failure(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(doc_check.main(["--root", str(self.root)]), 0)
        self.assertIn("ok", out.getvalue())
        self.change(doc_check.PAGE, "| `board_position` |", "| `board_pos` |")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(doc_check.main(["--root", str(self.root)]), 1)

    def test_a_missing_input_is_not_a_pass(self) -> None:
        (self.root / doc_check.PAGE).unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(doc_check.main(["--root", str(self.root)]), 2)

    def test_a_key_the_reader_accepts_must_be_listed(self) -> None:
        self.change(doc_check.CPP, '{"id", "name", "retired", "board_position"}',
                    '{"id", "name", "retired", "board_position", "colour"}')
        self.assertTrue(any("`colour`" in p and "does not list" in p for p in self.problems()))

    def test_a_key_the_reader_does_not_accept_must_not_be_listed(self) -> None:
        self.change(doc_check.PAGE, "| `board_position` |", "| `board_pos` |")
        found = self.problems()
        self.assertTrue(any("`board_pos`" in p and "does not accept" in p for p in found))
        self.assertTrue(any("`board_position`" in p for p in found))

    def test_required_must_match(self) -> None:
        self.change(doc_check.PAGE, "| `facilitator` | Required |", "| `facilitator` | Optional |")
        self.assertTrue(any("preferences.facilitator should be marked Required" in p for p in self.problems()))

    def test_a_list_is_required_at_the_top_level(self) -> None:
        self.change(doc_check.PAGE, "| `locations` | Required |", "| `locations` | Optional |")
        self.assertTrue(any("top.locations should be marked Required" in p for p in self.problems()))

    def test_a_changed_text_limit_is_found(self) -> None:
        self.change(doc_check.CPP, "constexpr int MAX_NAME = 200;", "constexpr int MAX_NAME = 250;")
        self.assertTrue(any("250 characters" in p for p in self.problems()))

    def test_a_changed_range_is_found(self) -> None:
        self.change(doc_check.CPP, "constexpr int MAX_SORT_ORDER = 1'000'000;", "constexpr int MAX_SORT_ORDER = 5'000;")
        self.assertTrue(any("locations.sort_order should show the bound 5000" in p for p in self.problems()))

    def test_a_changed_environment_length_is_found(self) -> None:
        self.change(doc_check.UNIT_HEADER, "MAX_ENVIRONMENT_LENGTH = 20", "MAX_ENVIRONMENT_LENGTH = 24")
        self.assertTrue(any("unit_environments" in p and "24" in p for p in self.problems()))

    def test_a_new_clock_choice_is_found(self) -> None:
        self.change(doc_check.CPP, 'QLatin1StringView("12h")', 'QLatin1StringView("12hr")')
        self.assertTrue(any('"12hr"' in p for p in self.problems()))

    def test_a_changed_file_limit_is_found(self) -> None:
        self.change(doc_check.HEADER, "CONFIG_MAX_ENTRIES_PER_LIST = 500", "CONFIG_MAX_ENTRIES_PER_LIST = 600")
        self.assertTrue(any("CONFIG_MAX_ENTRIES_PER_LIST" in p and "600" in p for p in self.problems()))

    def test_a_new_message_kind_must_be_documented(self) -> None:
        self.change(doc_check.HEADER, "UNKNOWN_PLACEHOLDER   //", "UNKNOWN_PLACEHOLDER,\n        BRAND_NEW   //")
        self.assertTrue(any("Problem::BRAND_NEW" in p for p in self.problems()))

    def test_a_new_code_must_be_documented(self) -> None:
        self.change(doc_check.HEADER, "        BAD_ID\n", "        BAD_ID,\n        BRAND_NEW_CODE\n")
        self.assertTrue(any("Code::BRAND_NEW_CODE" in p for p in self.problems()))

    def test_a_row_for_a_missing_kind_is_found(self) -> None:
        self.change(doc_check.PAGE, "<!-- code:BAD_ID -->", "<!-- code:NOT_A_CODE -->")
        found = self.problems()
        self.assertTrue(any("Code::BAD_ID has no row" in p for p in found))
        self.assertTrue(any("code:NOT_A_CODE" in p for p in found))

    def test_the_example_must_be_the_sample(self) -> None:
        self.change(doc_check.PAGE, '"facilitator": "Sam Example"', '"facilitator": "Someone Else"')
        self.assertTrue(any("differs from" in p for p in self.problems()))

    def test_a_broken_example_is_found(self) -> None:
        self.change(doc_check.PAGE, '"schema_version": 1,', '"schema_version": 1')
        self.assertTrue(any("not valid JSON" in p for p in self.problems()))

    def test_a_table_without_its_marker_is_found(self) -> None:
        self.change(doc_check.PAGE, "<!-- fields: locations -->", "")
        self.assertTrue(any("`<!-- fields: locations -->`" in p for p in self.problems()))


if __name__ == "__main__":
    unittest.main()
