# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/store/make_app_icon.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
Needs Pillow (requirements/store.txt); skipped without it.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "store"))

try:
    from PIL import Image, ImageChops

    import make_app_icon as icon
except ImportError:  # Pillow is not installed
    icon = None

APPICONSET = ROOT / "apps" / "app" / "packaging" / "ios" / "Assets.xcassets" / "AppIcon.appiconset"


def centre_of(row: int, column: int) -> tuple[int, int]:
    origin = (icon.SIZE - 4 * icon.CELL) // 2
    return origin + column * icon.CELL + icon.CELL // 2, origin + row * icon.CELL + icon.CELL // 2


def rgb(value: str) -> tuple[int, int, int]:
    return int(value[1:3], 16), int(value[3:5], 16), int(value[5:7], 16)


@unittest.skipIf(icon is None, "Pillow is not installed (pip install -r requirements/store.txt)")
class AppIconTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dir = tempfile.TemporaryDirectory()
        cls.paths = icon.make_all(Path(cls.dir.name))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.dir.cleanup()

    def test_both_icons_are_1024_square_and_opaque(self) -> None:
        # The App Store refuses an icon with an alpha channel.
        for path in self.paths.values():
            with Image.open(path) as image:
                self.assertEqual(image.format, "PNG")
                self.assertEqual(image.size, (1024, 1024))
                self.assertEqual(image.mode, "RGB")

    def test_the_t_tiles_are_filled_and_the_gold_squares_are_empty_inside(self) -> None:
        for name, palette in (("light", icon.LIGHT), ("dark", icon.DARK)):
            with Image.open(self.paths[name]) as image:
                self.assertEqual(image.getpixel((4, 4)), rgb(palette.background))
                for row, column in icon.FILLED:
                    self.assertEqual(image.getpixel(centre_of(row, column)), rgb(palette.tile), (name, row, column))
                for row, column in icon.GOLD:
                    self.assertEqual(image.getpixel(centre_of(row, column)), rgb(palette.background), (name, row, column))

    def test_the_gold_squares_are_outlined_in_gold(self) -> None:
        for name, palette in (("light", icon.LIGHT), ("dark", icon.DARK)):
            with Image.open(self.paths[name]) as image:
                for row, column in icon.GOLD:
                    x, y = centre_of(row, column)
                    edge = (x, y - icon.CELL // 2 + icon.GAP + icon.LINE // 2)  # the middle of the top line
                    self.assertEqual(image.getpixel(edge), rgb(palette.gold), (name, row, column))

    def test_the_f_is_an_outline_not_a_box(self) -> None:
        # The stem's left edge is the line; the middle of the stem and the notch beside the arms are not.
        origin = (icon.SIZE - 4 * icon.CELL) // 2
        with Image.open(self.paths["light"]) as image:
            line = (origin + icon.GAP + icon.LINE // 2, origin + 2 * icon.CELL)
            self.assertEqual(image.getpixel(line), rgb(icon.LIGHT.tile))
            self.assertEqual(image.getpixel(centre_of(1, 0)), rgb(icon.LIGHT.background))
            self.assertEqual(image.getpixel(centre_of(3, 0)), rgb(icon.LIGHT.background))

    def test_drawing_it_twice_gives_the_same_picture(self) -> None:
        with Image.open(self.paths["dark"]) as first:
            second = icon.draw_icon(icon.DARK)
            self.assertIsNone(ImageChops.difference(first, second).getbbox())

    def test_the_committed_icons_are_what_the_script_draws(self) -> None:
        # A different Pillow version may differ in a few edge pixels, so the tolerance is small but not zero.
        for name, filename in (("light", "AppIcon-1024.png"), ("dark", "AppIcon-1024-dark.png")):
            with Image.open(APPICONSET / filename) as committed:
                drawn = icon.draw_icon(icon.LIGHT if name == "light" else icon.DARK)
                histogram = ImageChops.difference(committed.convert("RGB"), drawn).convert("L").histogram()
                self.assertLessEqual(sum(histogram[17:]), 200, f"{filename} is not what make_app_icon.py draws")

    def test_the_asset_catalogue_names_the_light_and_the_dark_icon(self) -> None:
        contents = json.loads((APPICONSET / "Contents.json").read_text(encoding="utf-8"))
        by_name = {entry["filename"]: entry for entry in contents["images"]}
        self.assertEqual(set(by_name), {"AppIcon-1024.png", "AppIcon-1024-dark.png"})
        self.assertNotIn("appearances", by_name["AppIcon-1024.png"])
        self.assertEqual(
            by_name["AppIcon-1024-dark.png"]["appearances"], [{"appearance": "luminosity", "value": "dark"}]
        )
        for entry in by_name.values():
            self.assertEqual((entry["idiom"], entry["platform"], entry["size"]), ("universal", "ios", "1024x1024"))


if __name__ == "__main__":
    unittest.main()
