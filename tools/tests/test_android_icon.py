# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/store/make_android_icon.py and the launcher icon in apps/app/packaging/android (label: unit).

The built package may rename its resources, so what the icon says is checked here, in the sources; the package build
(CI) links the resources and the manifest to each other.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import re
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "store"))

try:
    import make_android_icon as android
    import make_app_icon as design
except ImportError:  # Pillow is not installed: make_app_icon, which holds the drawing, needs it
    android = design = None

PACKAGING = ROOT / "apps" / "app" / "packaging"
RES = PACKAGING / "android" / "res"
NS = "{http://schemas.android.com/apk/res/android}"


def attributes(path: Path) -> list[dict[str, str]]:
    """The attributes of every <path> of a vector drawable, without the namespace."""
    return [{key.replace(NS, ""): value for key, value in p.attrib.items()} for p in ET.parse(path).getroot().iter("path")]


@unittest.skipIf(android is None, "Pillow is not installed (pip install -r requirements/store.txt)")
class AndroidIconTest(unittest.TestCase):
    def test_the_committed_files_are_what_the_script_writes(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            android.make_all(Path(folder))
            for name in android.files():
                self.assertEqual(
                    (RES / name).read_text(encoding="utf-8"),
                    (Path(folder) / name).read_text(encoding="utf-8"),
                    f"{name} is not what make_android_icon.py writes; run it again",
                )

    def test_every_resource_is_well_formed_xml_with_a_licence_header(self) -> None:
        for name in android.files():
            text = (RES / name).read_text(encoding="utf-8")
            ET.fromstring(text)
            # In two pieces, so that the REUSE tool does not take this line for a header of this file.
            self.assertIn("SPDX-License" + "-Identifier: GPL-3.0-or-later", text, name)

    def test_the_adaptive_icon_has_a_background_a_foreground_and_a_monochrome_layer(self) -> None:
        for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
            root = ET.parse(RES / "mipmap-anydpi-v26" / name).getroot()
            self.assertEqual(root.tag, "adaptive-icon")
            layers = {child.tag: child.get(NS + "drawable") for child in root}
            self.assertEqual(
                layers,
                {
                    "background": "@color/ic_launcher_background",
                    "foreground": "@drawable/ic_launcher_foreground",
                    "monochrome": "@drawable/ic_launcher_monochrome",
                },
            )
        for layer in ("ic_launcher_foreground", "ic_launcher_monochrome"):
            self.assertTrue((RES / "drawable" / f"{layer}.xml").is_file(), layer)

    def test_the_round_icon_is_the_same_as_the_square_one(self) -> None:
        folder = RES / "mipmap-anydpi-v26"
        self.assertEqual((folder / "ic_launcher.xml").read_text(), (folder / "ic_launcher_round.xml").read_text())

    def test_the_background_is_the_icons_leaf_green(self) -> None:
        colours = {c.get("name"): c.text for c in ET.parse(RES / "values" / "ic_launcher_background.xml").getroot()}
        self.assertEqual(colours, {"ic_launcher_background": design.LEAF.background})

    def test_the_foreground_uses_the_icons_colours_and_nothing_else(self) -> None:
        colours = set()
        for p in attributes(RES / "drawable" / "ic_launcher_foreground.xml"):
            colours.add(p.get("fillColor") or p.get("strokeColor"))
        self.assertEqual(colours, {design.LEAF.tile, design.LEAF.letter})

    def test_the_themed_layer_is_the_same_shapes_in_one_colour(self) -> None:
        colour = attributes(RES / "drawable" / "ic_launcher_foreground.xml")
        mono = attributes(RES / "drawable" / "ic_launcher_monochrome.xml")
        self.assertEqual([p["pathData"] for p in mono], [p["pathData"] for p in colour])
        self.assertEqual({p.get("fillColor") or p.get("strokeColor") for p in mono}, {android.MONOCHROME_COLOUR})

    def test_the_icon_has_the_solid_f_the_seven_tiles_and_the_three_outlined_squares(self) -> None:
        paths = attributes(RES / "drawable" / "ic_launcher_foreground.xml")
        filled = [p for p in paths if "fillColor" in p]
        stroked = [p for p in paths if "strokeColor" in p]
        self.assertEqual([p["fillColor"] for p in filled], [design.LEAF.letter] + [design.LEAF.tile] * len(design.FILLED))
        self.assertEqual([p["strokeColor"] for p in stroked], [design.LEAF.tile] * len(design.EMPTY))

    def test_everything_is_inside_the_area_every_launcher_shape_shows(self) -> None:
        # Android cuts the 108 dp canvas to a shape and always shows the middle 72 dp: a circle of 36 dp from the centre.
        layer = android.shapes(design.LEAF.tile, design.LEAF.letter)
        self.assertLessEqual(android.farthest_corner(layer), android.VISIBLE_RADIUS)

    def test_every_number_in_the_paths_is_on_the_canvas(self) -> None:
        for p in attributes(RES / "drawable" / "ic_launcher_foreground.xml"):
            for number in re.findall(r"-?\d+\.?\d*", p["pathData"]):
                self.assertTrue(0 <= float(number) <= android.CANVAS or float(number) in (0, 1), number)

    def test_the_manifest_names_the_icon_and_the_round_icon(self) -> None:
        application = ET.parse(PACKAGING / "android" / "AndroidManifest.xml").getroot().find("application")
        self.assertEqual(application.get(NS + "icon"), "@mipmap/ic_launcher")
        self.assertEqual(application.get(NS + "roundIcon"), "@mipmap/ic_launcher_round")

    def test_the_minimum_sdk_takes_adaptive_icons(self) -> None:
        # The icon exists only in mipmap-anydpi-v26, so the minimum SDK must be 26 or more.
        text = (PACKAGING / "android.cmake").read_text(encoding="utf-8")
        match = re.search(r"set\(ERROR_JOURNALING_ANDROID_MIN_SDK (\d+)\)", text)
        self.assertIsNotNone(match)
        self.assertGreaterEqual(int(match.group(1)), 26)


if __name__ == "__main__":
    unittest.main()
