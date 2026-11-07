# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_ios_plist.py and the *.ejconfig rule of check_config_files.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
Only fictional names are used.
"""

from __future__ import annotations

import copy
import sys
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


class EjconfigFilesTest(unittest.TestCase):
    def test_ejconfig_files_are_rejected(self) -> None:
        paths = ["setup.ejconfig", "x/Y/My Setup.EJCONFIG", "config/sample-config.json"]
        self.assertEqual(config_check.offending(paths), paths[:2])

    def test_similar_names_pass(self) -> None:
        self.assertEqual(config_check.offending(["ejconfig.md", "docs/ejconfig-format.md", "ejconfig"]), [])


if __name__ == "__main__":
    unittest.main()
