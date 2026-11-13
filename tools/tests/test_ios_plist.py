# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_ios_plist.py and the *.ejconfig rule of check_config_files.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
Only fictional names are used.
"""

from __future__ import annotations

import copy
import json
import plistlib
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
    "CADisableMinimumFrameDurationOnPhone": True,
    "NSMicrophoneUsageDescription": "Dictation listens to you while you tap the microphone.",
    "NSSpeechRecognitionUsageDescription": "Dictation turns what you say into text on this phone.",
}

ROOT = Path(__file__).resolve().parent.parent.parent


def broken(change) -> list[str]:
    plist = copy.deepcopy(GOOD)
    change(plist)
    return plist_check.check(plist, CONSTANTS)


PHONE_ONLY = ["UIInterfaceOrientationPortrait"]
ALL_FOUR = [
    "UIInterfaceOrientationPortrait",
    "UIInterfaceOrientationPortraitUpsideDown",
    "UIInterfaceOrientationLandscapeLeft",
    "UIInterfaceOrientationLandscapeRight",
]


class OrientationCheckTest(unittest.TestCase):
    """plist.orientations: the phone is portrait only, the iPad has four, and it is not full screen only."""

    def plist(self, **changes) -> dict:
        plist = {"UISupportedInterfaceOrientations": list(PHONE_ONLY), "UISupportedInterfaceOrientations~ipad": list(ALL_FOUR)}
        plist.update(changes)
        return plist

    def test_good_orientations_pass(self) -> None:
        self.assertEqual(plist_check.check_orientations(self.plist()), [])

    def test_a_phone_that_turns_fails(self) -> None:
        self.assertTrue(plist_check.check_orientations(self.plist(UISupportedInterfaceOrientations=ALL_FOUR)))

    def test_an_ipad_in_portrait_only_fails(self) -> None:
        self.assertTrue(plist_check.check_orientations(self.plist(**{"UISupportedInterfaceOrientations~ipad": PHONE_ONLY})))

    def test_an_ipad_with_three_orientations_fails(self) -> None:
        self.assertTrue(plist_check.check_orientations(self.plist(**{"UISupportedInterfaceOrientations~ipad": ALL_FOUR[:3]})))

    def test_an_ipad_with_an_orientation_twice_fails(self) -> None:
        listed = ALL_FOUR[:3] + [ALL_FOUR[0]]
        self.assertTrue(plist_check.check_orientations(self.plist(**{"UISupportedInterfaceOrientations~ipad": listed})))

    def test_requiring_full_screen_fails(self) -> None:
        self.assertTrue(plist_check.check_orientations(self.plist(UIRequiresFullScreen=True)))
        self.assertTrue(plist_check.check_orientations(self.plist(UIRequiresFullScreen=False)))

    def test_multiple_scenes_fail(self) -> None:
        self.assertTrue(plist_check.check_orientations(self.plist(UIApplicationSupportsMultipleScenes=True)))
        self.assertEqual(plist_check.check_orientations(self.plist(UIApplicationSupportsMultipleScenes=False)), [])

    def test_this_repository_passes(self) -> None:
        template = (ROOT / plist_check.PLIST).read_text(encoding="utf-8")
        self.assertEqual(plist_check.check_orientations(plist_check.parse_plist(template)), [])


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

    def test_frame_rate_key_must_be_true(self) -> None:
        for change in (
            lambda p: p.pop("CADisableMinimumFrameDurationOnPhone"),
            lambda p: p.update(CADisableMinimumFrameDurationOnPhone=False),
            lambda p: p.update(CADisableMinimumFrameDurationOnPhone="YES"),
        ):
            self.assertTrue(any("CADisableMinimumFrameDurationOnPhone" in p for p in broken(change)))

    def test_usage_description_fails(self) -> None:
        self.assertTrue(any("NSCameraUsageDescription" in p for p in broken(lambda p: p.update(NSCameraUsageDescription="x"))))

    def test_dictation_usage_texts_are_required(self) -> None:
        for key in ("NSMicrophoneUsageDescription", "NSSpeechRecognitionUsageDescription"):
            self.assertTrue(any(key in p for p in broken(lambda p: p.pop(key))))
            self.assertTrue(any(key in p for p in broken(lambda p: p.update({key: " "}))))

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
    entry = {"filename": filename, "idiom": "iphone", "scale": scale}
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

    def test_launch_image_is_for_the_iphone_only(self) -> None:
        """launch.ipad_plain: an iPad has no launch picture, only the background colour."""
        for idiom in ("universal", "ipad"):
            self.images = [dict(entry, idiom=idiom) for entry in self.images]
            self.assertTrue(any("idiom" in problem for problem in self.problems()), idiom)

    def test_the_repository_launch_image_is_for_the_iphone_only(self) -> None:
        contents = json.loads((ROOT / plist_check.CATALOGUE / "LaunchBoard.imageset" / "Contents.json").read_text(encoding="utf-8"))
        self.assertEqual({entry["idiom"] for entry in contents["images"]}, {"iphone"})

    def test_image_must_be_the_3x_one(self) -> None:
        self.images[0] = image_entry("light.png", scale="2x")
        self.assertTrue(any("3x" in p for p in self.problems()))


class PrivacyManifestCheckTest(unittest.TestCase):
    """The privacy manifest (docs/plan/stage-11.md, 11.2, T11-6)."""

    GOOD = {
        "NSPrivacyTracking": False,
        "NSPrivacyTrackingDomains": [],
        "NSPrivacyCollectedDataTypes": [],
        "NSPrivacyAccessedAPITypes": [
            {"NSPrivacyAccessedAPIType": "NSPrivacyAccessedAPICategoryFileTimestamp",
             "NSPrivacyAccessedAPITypeReasons": ["C617.1"]},
        ],
    }

    def problems(self, **changes) -> list[str]:
        manifest = copy.deepcopy(self.GOOD)
        for key, value in changes.items():
            if value is None:
                manifest.pop(key)
            else:
                manifest[key] = value
        return plist_check.check_privacy_manifest(manifest)

    def test_repository_manifest_passes(self) -> None:
        manifest = plistlib.loads((ROOT / plist_check.PRIVACY_MANIFEST).read_bytes())
        self.assertEqual(plist_check.check_privacy_manifest(manifest), [])

    def test_good_manifest_passes(self) -> None:
        self.assertEqual(self.problems(), [])

    def test_tracking_true_fails(self) -> None:
        self.assertIn("NSPrivacyTracking must be false", " ".join(self.problems(NSPrivacyTracking=True)))

    def test_missing_tracking_fails(self) -> None:
        self.assertIn("NSPrivacyTracking is missing", " ".join(self.problems(NSPrivacyTracking=None)))

    def test_tracking_domain_fails(self) -> None:
        self.assertIn("TrackingDomains must be empty", " ".join(self.problems(NSPrivacyTrackingDomains=["example.com"])))

    def test_collected_type_fails(self) -> None:
        collected = [{"NSPrivacyCollectedDataType": "NSPrivacyCollectedDataTypeCrashData"}]
        self.assertIn("CollectedDataTypes must be empty", " ".join(self.problems(NSPrivacyCollectedDataTypes=collected)))

    def test_unknown_category_fails(self) -> None:
        types = [{"NSPrivacyAccessedAPIType": "NSPrivacyAccessedAPICategoryMadeUp",
                  "NSPrivacyAccessedAPITypeReasons": ["C617.1"]}]
        self.assertIn("not a required-reason API category", " ".join(self.problems(NSPrivacyAccessedAPITypes=types)))

    def test_sdk_reason_fails(self) -> None:
        types = [{"NSPrivacyAccessedAPIType": "NSPrivacyAccessedAPICategoryFileTimestamp",
                  "NSPrivacyAccessedAPITypeReasons": ["0A2A.1"]}]
        self.assertIn("not an application's reason", " ".join(self.problems(NSPrivacyAccessedAPITypes=types)))

    def test_reason_of_other_category_fails(self) -> None:
        types = [{"NSPrivacyAccessedAPIType": "NSPrivacyAccessedAPICategorySystemBootTime",
                  "NSPrivacyAccessedAPITypeReasons": ["C617.1"]}]
        self.assertIn("not an application's reason", " ".join(self.problems(NSPrivacyAccessedAPITypes=types)))

    def test_no_reason_fails(self) -> None:
        types = [{"NSPrivacyAccessedAPIType": "NSPrivacyAccessedAPICategoryFileTimestamp",
                  "NSPrivacyAccessedAPITypeReasons": []}]
        self.assertIn("gives no reason", " ".join(self.problems(NSPrivacyAccessedAPITypes=types)))

    def test_category_twice_fails(self) -> None:
        entry = self.GOOD["NSPrivacyAccessedAPITypes"][0]
        self.assertIn("declared twice", " ".join(self.problems(NSPrivacyAccessedAPITypes=[entry, entry])))

    def test_missing_manifest_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for rel in (plist_check.PLIST, plist_check.CONSTANTS):
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_bytes((ROOT / rel).read_bytes())
            self.assertTrue(any("PrivacyInfo.xcprivacy not found" in p for p in plist_check.run(root)))


class EjconfigFilesTest(unittest.TestCase):
    def test_ejconfig_files_are_rejected(self) -> None:
        paths = ["setup.ejconfig", "x/Y/My Setup.EJCONFIG", "config/sample-config.json"]
        self.assertEqual(config_check.offending(paths), paths[:2])

    def test_similar_names_pass(self) -> None:
        self.assertEqual(config_check.offending(["ejconfig.md", "docs/ejconfig-format.md", "ejconfig"]), [])


if __name__ == "__main__":
    unittest.main()
