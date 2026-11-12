# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_guide_screenshots.py and guide_screenshots.py (label: unit). The pictures of the user guide.

Run with: python3 -m unittest discover -s tools/tests -v
Each case copies the list, the screens file, the pictures and the pages into a temporary folder and changes one of them.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ci"))

import check_guide_screenshots as shots_check  # noqa: E402

from PIL import Image  # noqa: E402


def load_tool():
    spec = importlib.util.spec_from_file_location("guide_screenshots", ROOT / "tools" / "docs" / "guide_screenshots.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GuideScreenshotsCheck(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        (self.root / "docs").mkdir()
        shutil.copytree(ROOT / "docs" / "guide", self.root / "docs" / "guide")
        (self.root / "tools" / "ej_screenshots").mkdir(parents=True)
        shutil.copyfile(ROOT / shots_check.SCREENS, self.root / shots_check.SCREENS)

    def tearDown(self) -> None:
        self.dir.cleanup()

    def plan(self) -> dict:
        return json.loads((self.root / shots_check.LIST).read_text(encoding="utf-8"))

    def save_plan(self, plan: dict) -> None:
        (self.root / shots_check.LIST).write_text(json.dumps(plan), encoding="utf-8")

    def change_page(self, page: str, old: str, new: str) -> None:
        path = self.root / "docs" / "guide" / page
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text, "the test's starting text moved; update the test")
        path.write_text(text.replace(old, new, 1), encoding="utf-8")

    def problems(self) -> list[str]:
        return shots_check.check(self.root)

    def test_the_real_guide_is_consistent(self) -> None:
        self.assertEqual(self.problems(), [])

    def test_an_unknown_state_is_found(self) -> None:
        plan = self.plan()
        plan["images"][0]["state"] = "NoSuchScreen"
        self.save_plan(plan)
        self.assertTrue(any("NoSuchScreen" in p for p in self.problems()))

    def test_an_unknown_mode_is_found(self) -> None:
        plan = self.plan()
        plan["images"][0]["mode"] = "sepia"
        self.save_plan(plan)
        self.assertTrue(any("sepia" in p for p in self.problems()))

    def test_a_picture_listed_twice_is_found(self) -> None:
        plan = self.plan()
        plan["images"].append(dict(plan["images"][0]))
        self.save_plan(plan)
        self.assertTrue(any("listed twice" in p for p in self.problems()))

    def test_a_bad_file_name_is_found(self) -> None:
        plan = self.plan()
        plan["images"][0]["file"] = "Log Board"
        self.save_plan(plan)
        self.assertTrue(any("lower-case" in p for p in self.problems()))

    def test_a_missing_file_is_found(self) -> None:
        (self.root / "docs" / "guide" / "img" / "log-board.png").unlink()
        self.assertTrue(any("log-board.png: missing" in p for p in self.problems()))

    def test_a_file_that_is_not_listed_is_found(self) -> None:
        shutil.copyfile(self.root / "docs/guide/img/log-board.png", self.root / "docs/guide/img/extra.png")
        self.assertTrue(any("extra.png" in p and "not in" in p for p in self.problems()))

    def test_a_wrong_width_is_found(self) -> None:
        plan = self.plan()
        plan["width"] = 300
        self.save_plan(plan)
        self.assertTrue(any("402 pixels wide" in p for p in self.problems()))

    def test_a_file_that_is_not_a_png_is_found(self) -> None:
        (self.root / "docs/guide/img/log-board.png").write_bytes(b"not a picture at all, just text")
        self.assertTrue(any("not a PNG" in p for p in self.problems()))

    def test_a_page_image_that_is_not_listed_is_found(self) -> None:
        self.change_page("logging.md", "img/log-tap-hint.png", "img/not-listed.png")
        found = self.problems()
        self.assertTrue(any("not-listed.png is not in" in p for p in found))
        self.assertTrue(any("log-tap-hint is not used" in p for p in found))

    def test_missing_alt_text_is_found(self) -> None:
        self.change_page("history.md", "![History: logs grouped by date. Each row shows the unit and place, the number of entries and Shared with a time, or Not shared.](img/history.png)", "![History](img/history.png)")
        self.assertTrue(any("needs alt text" in p for p in self.problems()))

    def test_an_image_outside_img_is_found(self) -> None:
        self.change_page("history.md", "(img/history.png)", "(https://example.test/history.png)")
        self.assertTrue(any("is not a picture of the guide" in p for p in self.problems()))

    def test_main_reports_ok_failure_and_a_missing_input(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(shots_check.main(["--root", str(self.root)]), 0)
        self.assertIn("ok", out.getvalue())
        (self.root / "docs/guide/img/log-board.png").unlink()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(shots_check.main(["--root", str(self.root)]), 1)
        (self.root / shots_check.LIST).unlink()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(shots_check.main(["--root", str(self.root)]), 2)


class GuideScreenshotsTool(unittest.TestCase):
    def test_shrink_scales_to_the_width_and_is_repeatable(self) -> None:
        tool = load_tool()
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "Made.light.png"
            Image.new("RGB", (804, 1748), (20, 90, 120)).save(source)
            first, second = Path(folder) / "a.png", Path(folder) / "b.png"
            tool.shrink(source, first, 402)
            tool.shrink(source, second, 402)
            self.assertEqual(shots_check.png_width(first), 402)
            with Image.open(first) as image:
                self.assertEqual(image.size, (402, 874))
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_a_missing_source_picture_is_reported(self) -> None:
        tool = load_tool()
        with tempfile.TemporaryDirectory() as folder, contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(tool.main(["--src", folder]), 1)
        self.assertIn("missing", err.getvalue())


if __name__ == "__main__":
    unittest.main()
