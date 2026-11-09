# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/store/make_play_graphics.py (label: unit). Task 11.5 of docs/plan/stage-11.md.

Run with: python3 -m unittest discover -s tools/tests -v
Needs Pillow (requirements/store.txt); skipped without it.
"""

from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "store"))

try:
    from PIL import Image

    import make_play_graphics as graphics
except ImportError:  # Pillow is not installed
    graphics = None


@unittest.skipIf(graphics is None, "Pillow is not installed (pip install -r requirements/store.txt)")
class PlayGraphicsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dir = tempfile.TemporaryDirectory()
        cls.out = Path(cls.dir.name)
        cls.paths = graphics.make_all(cls.out)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.dir.cleanup()

    def test_icon_is_512_square_32_bit(self) -> None:
        with Image.open(self.out / "icon.png") as icon:
            self.assertEqual(icon.format, "PNG")
            self.assertEqual(icon.size, (512, 512))
            self.assertEqual(icon.mode, "RGBA")

    def test_feature_graphic_is_1024_by_500_and_opaque(self) -> None:
        with Image.open(self.out / "featureGraphic.png") as feature:
            self.assertEqual(feature.format, "PNG")
            self.assertEqual(feature.size, (1024, 500))
            self.assertEqual(feature.mode, "RGB")

    def test_feature_graphic_uses_the_tile_palette(self) -> None:
        tokens = graphics.load_tokens()
        palette = sorted(tokens["tilePalette"], key=lambda tile: tile["slot"])
        top = (500 - (graphics.ROWS * graphics.TILE[1] + (graphics.ROWS - 1) * graphics.GAP)) // 2
        with Image.open(self.out / "featureGraphic.png") as feature:
            for index, tile in enumerate(palette):
                column, row = index % graphics.COLUMNS, index // graphics.COLUMNS
                x = graphics.BOARD_LEFT + column * (graphics.TILE[0] + graphics.GAP) + graphics.TILE[0] // 2
                y = top + row * (graphics.TILE[1] + graphics.GAP) + graphics.TILE[1] // 2
                self.assertEqual(feature.getpixel((x, y)), graphics.hex_colour(tile["hex"]), tile["name"])
            self.assertEqual(feature.getpixel((4, 4)), graphics.hex_colour(tokens["colour"]["light"]["background"]))

    def test_output_is_the_same_each_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            again = graphics.make_all(Path(directory))
            for first, second in zip(self.paths, again):
                self.assertEqual(hashlib.sha256(first.read_bytes()).hexdigest(),
                                 hashlib.sha256(second.read_bytes()).hexdigest(), first.name)

    def test_icon_of_the_wrong_size_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            small = Path(directory) / "small.png"
            Image.new("RGB", (100, 100)).save(small)
            with self.assertRaises(ValueError):
                graphics.make_icon(small)

    def test_hex_colour(self) -> None:
        self.assertEqual(graphics.hex_colour("#F2F4F7"), (0xF2, 0xF4, 0xF7))


if __name__ == "__main__":
    unittest.main()
