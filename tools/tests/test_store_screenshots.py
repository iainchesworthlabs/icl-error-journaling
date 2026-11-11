# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for store_screenshots.py (label: unit). T11-8 of docs/plan/stage-11.md, the parts that need no build.

Run with: python3 -m unittest discover -s tools/tests -v
The pictures here are plain colour blocks of the capture size. The real run is the Screenshots job (docs/ci.md).
"""

from __future__ import annotations

import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ej_screenshots"))

import store_screenshots as store  # noqa: E402


class StoreScreenshotsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.plan = store.load_plan()
        self.screens = json.loads(store.SCREENS.read_text(encoding="utf-8"))

    def raw_folder(self, root: Path, colour: tuple[int, int, int] = (240, 240, 240), alpha: bool = False) -> Path:
        for item in self.plan["sets"]:
            for shot in item["shots"]:
                shot_class = shot["class"]
                size = tuple(p * self.plan["scale"][shot_class] for p in store.CLASSES[shot_class]["points"])
                folder = root / shot_class
                folder.mkdir(parents=True, exist_ok=True)
                image = Image.new("RGBA" if alpha else "RGB", size, colour + ((0,) if alpha else ()))
                image.save(folder / f"{shot['state']}.{shot['mode']}.png")
        return root

    def test_committed_shot_list_is_valid(self) -> None:
        self.assertEqual(store.check_plan(self.plan, self.screens), [])

    def test_unknown_state_is_reported(self) -> None:
        self.plan["sets"][0]["shots"][0]["state"] = "Nowhere"
        self.assertTrue(any("no state Nowhere" in p for p in store.check_plan(self.plan, self.screens)))

    def test_unknown_mode_is_reported(self) -> None:
        self.plan["sets"][0]["shots"][0]["mode"] = "sepia"
        self.assertTrue(any("no mode sepia" in p for p in store.check_plan(self.plan, self.screens)))

    def test_too_many_shots_are_reported(self) -> None:
        shots = self.plan["sets"][0]["shots"]
        self.plan["sets"][0]["shots"] = [dict(shots[0], file=f"{n:02d}") for n in range(11)]
        self.assertTrue(any("11 shots" in p for p in store.check_plan(self.plan, self.screens)))

    def test_play_aspect_over_two_to_one_is_reported(self) -> None:
        play = next(item for item in self.plan["sets"] if item["id"] == "play-phone")
        play["size"] = [1320, 2868]
        self.assertTrue(any("twice" in p for p in store.check_plan(self.plan, self.screens)))

    def test_phone_state_in_an_ipad_class_is_reported(self) -> None:
        self.plan["sets"][0]["shots"][0]["class"] = "ipad-portrait"
        self.assertTrue(any("is not a ipad-portrait scene" in p for p in store.check_plan(self.plan, self.screens)))

    def test_store_sizes_are_the_stores_sizes(self) -> None:
        sizes = {item["id"]: tuple(item["size"]) for item in self.plan["sets"]}
        self.assertEqual(sizes["appstore-iphone-6.9"], (1320, 2868))
        self.assertEqual(sizes["appstore-ipad-13-portrait"], (2064, 2752))
        self.assertEqual(sizes["appstore-ipad-13-landscape"], (2752, 2064))
        self.assertEqual(sizes["play-phone"], (1080, 1920))
        self.assertEqual(sizes["play-large-portrait"], (1440, 2560))
        self.assertEqual(sizes["play-large-landscape"], (2560, 1440))

    def test_every_shot_has_its_sets_exact_size_and_no_alpha(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = store.make(self.plan, self.raw_folder(root / "raw"), root / "out")
            self.assertEqual(len(manifest), sum(len(item["shots"]) for item in self.plan["sets"]))
            for item in self.plan["sets"]:
                for shot in item["shots"]:
                    path = root / "out" / "store" / item["store"] / item["id"] / f"{shot['file']}.png"
                    with Image.open(path) as image:
                        self.assertEqual(image.size, tuple(item["size"]), f"{item['id']}/{shot['file']}")
                        self.assertEqual(image.mode, "RGB")

    def test_two_runs_give_the_same_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            raw = self.raw_folder(root / "raw")
            self.assertEqual(store.make(self.plan, raw, root / "one"), store.make(self.plan, raw, root / "two"))

    def test_capture_of_the_wrong_size_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            store.compose(Image.new("RGB", (100, 100)), "phone", 3, (1320, 2868))

    def test_alpha_capture_gives_an_opaque_picture(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            store.make(self.plan, self.raw_folder(root / "raw", alpha=True), root / "out")
            first = self.plan["sets"][0]
            with Image.open(root / "out" / "store" / first["store"] / first["id"] / "01-log-board.png") as image:
                self.assertEqual(image.mode, "RGB")

    def test_status_bar_follows_the_screen_colour(self) -> None:
        light = Image.new("RGB", (1206, 2622), (250, 250, 250))
        dark = Image.new("RGB", (1206, 2622), (20, 20, 20))
        store.draw_status_bar(light, "phone", 3)
        store.draw_status_bar(dark, "phone", 3)
        band = (0, 0, 1206, 59 * 3)
        self.assertIn((0, 0, 0), {c for _, c in light.crop(band).getcolors(10**7)})
        self.assertIn((255, 255, 255), {c for _, c in dark.crop(band).getcolors(10**7)})
        self.assertEqual({c for _, c in light.crop((0, 59 * 3, 1206, 2622)).getcolors(10**7)}, {(250, 250, 250)})

    def test_margin_takes_the_screens_colour(self) -> None:
        picture = store.compose(Image.new("RGB", (1180 * 2, 820 * 2), (10, 20, 30)), "ipad-landscape", 2, (2752, 2064))
        self.assertEqual(picture.getpixel((5, 5)), (10, 20, 30))
        self.assertEqual(picture.getpixel((5, 2060)), (10, 20, 30))
        self.assertEqual(picture.size, (2752, 2064))

    def test_png_has_no_metadata(self) -> None:
        data = store.png_bytes(Image.new("RGB", (10, 10), (1, 2, 3)))
        with Image.open(io.BytesIO(data)) as image:
            self.assertEqual(image.info, {})


if __name__ == "__main__":
    unittest.main()
