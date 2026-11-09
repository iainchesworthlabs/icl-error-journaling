# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_ios_plist.py and the *.ejconfig rule of check_config_files.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
Only fictional names are used.
"""

from __future__ import annotations

import copy
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_config_files as config_check  # noqa: E402
import check_ios_plist as plist_check  # noqa: E402

IDENTIFIER = "com.example.sample.config"
CONSTANTS = {
    "CONFIG_TYPE_IDENTIFIER": IDENTIFIER,
    "CONFIG_TYPE_DESCRIPTION": "Sample setup",
    "CONFIG_EXTENSION": "ejconfig",
}
GOOD = {
    "UTExportedTypeDeclarations": [
        {
            "UTTypeIdentifier": IDENTIFIER,
            "UTTypeDescription": "Sample setup",
            "UTTypeConformsTo": ["public.json"],
            "UTTypeTagSpecification": {"public.filename-extension": ["ejconfig"]},
        }
    ],
    "CFBundleDocumentTypes": [
        {"CFBundleTypeRole": "Viewer", "LSHandlerRank": "Owner", "LSItemContentTypes": [IDENTIFIER]},
        {"CFBundleTypeRole": "Viewer", "LSHandlerRank": "Alternate", "LSItemContentTypes": ["public.json"]},
    ],
    "LSSupportsOpeningDocumentsInPlace": False,
}

ROOT = Path(__file__).resolve().parent.parent.parent


def broken(change) -> list[str]:
    plist = copy.deepcopy(GOOD)
    change(plist)
    return plist_check.check(plist, CONSTANTS)


class PlistCheckTest(unittest.TestCase):
    def test_good_plist_passes(self) -> None:
        self.assertEqual(plist_check.check(GOOD, CONSTANTS), [])

    def test_identifier_differing_from_the_constant_fails(self) -> None:
        problems = broken(lambda p: p["UTExportedTypeDeclarations"][0].update(UTTypeIdentifier="com.example.other"))
        self.assertTrue(any("expected" in p for p in problems))

    def test_wrong_extension_fails(self) -> None:
        def change(p):
            p["UTExportedTypeDeclarations"][0]["UTTypeTagSpecification"]["public.filename-extension"] = ["cfg"]

        self.assertTrue(any("extension" in p for p in broken(change)))

    def test_exported_type_must_conform_to_json(self) -> None:
        self.assertTrue(any("public.json" in p for p in broken(lambda p: p["UTExportedTypeDeclarations"][0].update(UTTypeConformsTo=[]))))

    def test_missing_document_type_fails(self) -> None:
        self.assertTrue(any(IDENTIFIER in p for p in broken(lambda p: p["CFBundleDocumentTypes"].pop(0))))
        self.assertTrue(any("public.json" in p for p in broken(lambda p: p["CFBundleDocumentTypes"].pop(1))))

    def test_wrong_rank_or_role_fails(self) -> None:
        self.assertTrue(any("Owner" in p for p in broken(lambda p: p["CFBundleDocumentTypes"][0].update(LSHandlerRank="Default"))))
        self.assertTrue(any("Alternate" in p for p in broken(lambda p: p["CFBundleDocumentTypes"][1].update(LSHandlerRank="Owner"))))
        self.assertTrue(any("Viewer" in p for p in broken(lambda p: p["CFBundleDocumentTypes"][0].update(CFBundleTypeRole="Editor"))))

    def test_opening_in_place_must_be_false(self) -> None:
        self.assertTrue(any("InPlace" in p for p in broken(lambda p: p.update(LSSupportsOpeningDocumentsInPlace=True))))
        self.assertTrue(any("InPlace" in p for p in broken(lambda p: p.pop("LSSupportsOpeningDocumentsInPlace"))))

    def test_file_sharing_must_not_be_on(self) -> None:
        self.assertTrue(any("FileSharing" in p for p in broken(lambda p: p.update(UIFileSharingEnabled=True))))
        self.assertEqual(broken(lambda p: p.update(UIFileSharingEnabled=False)), [])

    def test_usage_description_fails(self) -> None:
        self.assertTrue(any("NSCameraUsageDescription" in p for p in broken(lambda p: p.update(NSCameraUsageDescription="x"))))

    def test_missing_constant_is_reported(self) -> None:
        self.assertTrue(plist_check.check(GOOD, {}))

    def test_constants_are_read_from_the_header(self) -> None:
        header = 'inline constexpr std::string_view CONFIG_EXTENSION = "ejconfig";\ninline constexpr int OTHER = 1;\n'
        self.assertEqual(plist_check.read_constants(header), {"CONFIG_EXTENSION": "ejconfig"})

    def test_placeholders_are_stubbed(self) -> None:
        text = (
            '<?xml version="1.0"?><plist version="1.0"><dict><key>CFBundleName</key>'
            "<string>${MACOSX_BUNDLE_BUNDLE_NAME}</string></dict></plist>"
        )
        self.assertEqual(plist_check.parse_plist(text), {"CFBundleName": "x"})

    def test_this_repository_passes(self) -> None:
        self.assertEqual(plist_check.run(ROOT), [])


TOKENS = {"colour": {"light": {"background": "#F2F4F7"}, "dark": {"background": "#0E141A"}}}
LAUNCH = {"UILaunchScreen": {"UIColorName": "Back", "UIImageName": "Board", "UIImageRespectsSafeAreaInsets": False}}


def png(width: int = 1206, height: int = 2622, colour_type: int = 2, transparency: bool = False) -> bytes:
    """The chunks of a PNG that the check reads (no image data: it never decodes)."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + b"\0\0\0\0"

    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    body = chunk(b"IHDR", header) + (chunk(b"tRNS", b"\0") if transparency else b"") + chunk(b"IEND", b"")
    return plist_check.PNG_SIGNATURE + body


def colour_entry(red: str, green: str, blue: str, dark: bool = False) -> dict:
    entry = {"color": {"color-space": "srgb", "components": {"alpha": "1.000", "red": red, "green": green, "blue": blue}}}
    if dark:
        entry["appearances"] = [{"appearance": "luminosity", "value": "dark"}]
    return entry


def image_entry(filename: str, dark: bool = False, scale: str = "3x") -> dict:
    entry = {"filename": filename, "idiom": "universal", "scale": scale}
    if dark:
        entry["appearances"] = [{"appearance": "luminosity", "value": "dark"}]
    return entry


class LaunchScreenCheckTest(unittest.TestCase):
    """The launch screen against a catalogue built in a temporary directory."""

    def setUp(self) -> None:
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.catalogue = Path(self._directory.name)
        self.colours = [colour_entry("0xF2", "0xF4", "0xF7"), colour_entry("0x0E", "0x14", "0x1A", dark=True)]
        self.images = [image_entry("light.png"), image_entry("dark.png", dark=True)]
        self.files = {"light.png": png(), "dark.png": png()}

    def problems(self, plist: dict | None = None) -> list[str]:
        colour_set = self.catalogue / "Back.colorset"
        image_set = self.catalogue / "Board.imageset"
        colour_set.mkdir(exist_ok=True)
        image_set.mkdir(exist_ok=True)
        (colour_set / "Contents.json").write_text(json.dumps({"colors": self.colours}), encoding="utf-8")
        (image_set / "Contents.json").write_text(json.dumps({"images": self.images}), encoding="utf-8")
        for name, data in self.files.items():
            (image_set / name).write_bytes(data)
        return plist_check.check_launch_screen(copy.deepcopy(LAUNCH) if plist is None else plist, self.catalogue, TOKENS)

    def test_good_launch_screen_passes(self) -> None:
        self.assertEqual(self.problems(), [])

    def test_fractions_are_read_as_well_as_hex(self) -> None:
        self.colours = [colour_entry("0.949", "0.957", "0.969"), colour_entry("0.055", "0.078", "0.102", dark=True)]
        self.assertEqual(self.problems(), [])

    def test_missing_launch_screen_fails(self) -> None:
        self.assertTrue(any("UILaunchScreen" in p for p in self.problems({})))

    def test_missing_names_fail(self) -> None:
        plist = {"UILaunchScreen": {"UIImageRespectsSafeAreaInsets": False}}
        problems = self.problems(plist)
        self.assertTrue(any("UIColorName" in p for p in problems))
        self.assertTrue(any("UIImageName" in p for p in problems))

    def test_image_must_ignore_the_safe_area(self) -> None:
        plist = copy.deepcopy(LAUNCH)
        plist["UILaunchScreen"]["UIImageRespectsSafeAreaInsets"] = True
        self.assertTrue(any("SafeArea" in p for p in self.problems(plist)))
        del plist["UILaunchScreen"]["UIImageRespectsSafeAreaInsets"]
        self.assertTrue(any("SafeArea" in p for p in self.problems(plist)))

    def test_names_that_are_not_in_the_catalogue_fail(self) -> None:
        plist = copy.deepcopy(LAUNCH)
        plist["UILaunchScreen"]["UIColorName"] = "Nothing"
        plist["UILaunchScreen"]["UIImageName"] = "Nothing"
        problems = self.problems(plist)
        self.assertTrue(any("colour Nothing" in p for p in problems))
        self.assertTrue(any("image Nothing" in p for p in problems))

    def test_colour_without_a_dark_variant_fails(self) -> None:
        del self.colours[1]
        self.assertTrue(any("dark entry" in p for p in self.problems()))

    def test_image_without_a_dark_variant_fails(self) -> None:
        del self.images[1]
        self.assertTrue(any("dark entry" in p for p in self.problems()))

    def test_colour_that_is_not_the_background_token_fails(self) -> None:
        self.colours[0] = colour_entry("0xFF", "0xC9", "0x3C")
        self.assertTrue(any("light is #FFC93C" in p for p in self.problems()))
        self.colours = [colour_entry("0xF2", "0xF4", "0xF7"), colour_entry("0x00", "0x00", "0x00", dark=True)]
        self.assertTrue(any("dark is #000000" in p for p in self.problems()))

    def test_image_file_that_is_missing_fails(self) -> None:
        del self.files["dark.png"]
        self.assertTrue(any("dark.png" in p and "not there" in p for p in self.problems()))

    def test_image_of_the_wrong_size_fails(self) -> None:
        self.files["light.png"] = png(width=1179, height=2556)
        self.assertTrue(any("1179 x 2556" in p for p in self.problems()))

    def test_image_with_an_alpha_channel_fails(self) -> None:
        self.files["light.png"] = png(colour_type=6)
        self.assertTrue(any("opaque" in p for p in self.problems()))
        self.files["light.png"] = png(colour_type=3, transparency=True)
        self.assertTrue(any("opaque" in p for p in self.problems()))

    def test_file_that_is_not_a_png_fails(self) -> None:
        self.files["dark.png"] = b"not a picture at all, but long enough to be read"
        self.assertTrue(any("not a PNG" in p for p in self.problems()))

    def test_image_must_be_the_3x_one(self) -> None:
        self.images[0] = image_entry("light.png", scale="2x")
        self.assertTrue(any("3x" in p for p in self.problems()))


class EjconfigFilesTest(unittest.TestCase):
    def test_ejconfig_files_are_rejected(self) -> None:
        paths = ["setup.ejconfig", "x/Y/My Setup.EJCONFIG", "config/sample-config.json"]
        self.assertEqual(config_check.offending(paths), paths[:2])

    def test_similar_names_pass(self) -> None:
        self.assertEqual(config_check.offending(["ejconfig.md", "docs/ejconfig-format.md", "ejconfig"]), [])


if __name__ == "__main__":
    unittest.main()
