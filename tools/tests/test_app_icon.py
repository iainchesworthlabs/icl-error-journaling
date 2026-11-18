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


def contrast(a: str, b: str) -> float:
    def luminance(value: str) -> float:
        parts = [channel / 255 for channel in rgb(value)]
        parts = [p / 12.92 if p <= 0.03928 else ((p + 0.055) / 1.055) ** 2.4 for p in parts]
        return 0.2126 * parts[0] + 0.7152 * parts[1] + 0.0722 * parts[2]

    high, low = sorted((luminance(a), luminance(b)), reverse=True)
    return (high + 0.05) / (low + 0.05)


@unittest.skipIf(icon is None, "Pillow is not installed (pip install -r requirements/store.txt)")
class AppIconTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dir = tempfile.TemporaryDirectory()
        cls.paths = icon.make_all(Path(cls.dir.name))
        cls.image = Image.open(cls.paths["icon"])

    @classmethod
    def tearDownClass(cls) -> None:
        cls.image.close()
        cls.dir.cleanup()

    def test_the_icon_is_1024_square_and_opaque(self) -> None:
        # The App Store refuses an icon with an alpha channel.
        self.assertEqual(self.image.format, "PNG")
        self.assertEqual(self.image.size, (1024, 1024))
        self.assertEqual(self.image.mode, "RGB")

    def test_only_the_one_icon_is_written(self) -> None:
        self.assertEqual({path.name for path in Path(self.dir.name).iterdir()}, {"AppIcon-1024.png"})

    def test_the_t_tiles_are_filled_and_the_empty_squares_are_background_inside(self) -> None:
        palette = icon.LEAF
        self.assertEqual(self.image.getpixel((4, 4)), rgb(palette.background))
        for row, column in icon.FILLED:
            self.assertEqual(self.image.getpixel(centre_of(row, column)), rgb(palette.tile), (row, column))
        for row, column in icon.EMPTY:
            self.assertEqual(self.image.getpixel(centre_of(row, column)), rgb(palette.background), (row, column))

    def test_the_empty_squares_are_outlined(self) -> None:
        for row, column in icon.EMPTY:
            x, y = centre_of(row, column)
            top_line = (x, y - icon.CELL // 2 + icon.GAP + icon.LINE // 2)  # the middle of the top line
            self.assertEqual(self.image.getpixel(top_line), rgb(icon.LEAF.tile), (row, column))

    def test_the_f_is_six_tiles_with_the_t_in_its_notch(self) -> None:
        # The stem and the two arms are the letter's colour; the notch between the arms is the t's left tile and the
        # one below them is an empty square.
        self.assertEqual(sorted(icon.F_TILES), [(0, 0), (0, 1), (1, 0), (2, 0), (2, 1), (3, 0)])
        for row, column in icon.F_TILES:
            self.assertEqual(self.image.getpixel(centre_of(row, column)), rgb(icon.LEAF.letter), (row, column))
        self.assertEqual(self.image.getpixel(centre_of(1, 1)), rgb(icon.LEAF.tile))
        self.assertEqual(self.image.getpixel(centre_of(3, 1)), rgb(icon.LEAF.background))

    def test_the_tiles_of_the_f_are_apart_like_the_other_tiles(self) -> None:
        # The gap between two tiles of the f is background, as between the tiles of the t.
        x, y = centre_of(0, 0)
        self.assertEqual(self.image.getpixel((x + icon.CELL // 2, y)), rgb(icon.LEAF.background))
        self.assertEqual(self.image.getpixel((x, y + icon.CELL // 2)), rgb(icon.LEAF.background))

    def test_no_cell_is_in_two_parts_of_the_drawing(self) -> None:
        cells = icon.F_TILES + icon.FILLED + icon.EMPTY
        self.assertEqual(len(cells), len(set(cells)))

    def test_the_f_and_the_t_stand_out_from_the_background(self) -> None:
        # The first icon had a thin outline for the f and it blurred into the background when small.
        self.assertGreaterEqual(contrast(icon.LEAF.letter, icon.LEAF.background), 2.5)
        self.assertGreaterEqual(contrast(icon.LEAF.tile, icon.LEAF.background), 4.5)

    def test_drawing_it_twice_gives_the_same_picture(self) -> None:
        self.assertIsNone(ImageChops.difference(self.image, icon.draw_icon(icon.LEAF)).getbbox())

    def test_the_committed_icon_is_what_the_script_draws(self) -> None:
        # A different Pillow version may differ in a few edge pixels, so the tolerance is small but not zero.
        with Image.open(APPICONSET / "AppIcon-1024.png") as committed:
            histogram = ImageChops.difference(committed.convert("RGB"), icon.draw_icon(icon.LEAF)).convert("L").histogram()
            self.assertLessEqual(sum(histogram[17:]), 200, "AppIcon-1024.png is not what make_app_icon.py draws")

    def test_the_asset_catalogue_names_the_one_icon_with_no_dark_variant(self) -> None:
        contents = json.loads((APPICONSET / "Contents.json").read_text(encoding="utf-8"))
        self.assertEqual(len(contents["images"]), 1)
        entry = contents["images"][0]
        self.assertEqual(entry["filename"], "AppIcon-1024.png")
        self.assertNotIn("appearances", entry)
        self.assertEqual((entry["idiom"], entry["platform"], entry["size"]), ("universal", "ios", "1024x1024"))
        self.assertEqual({path.name for path in APPICONSET.iterdir()}, {"AppIcon-1024.png", "Contents.json"})


if __name__ == "__main__":
    unittest.main()
