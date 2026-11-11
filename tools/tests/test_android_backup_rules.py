# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Android backup rules in apps/app/packaging/android (label: unit).

The built package may rename its resources, so what the rules say is checked here, in the sources; the package check
(check_android_package.py) checks that the built manifest points at rules at all.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PACKAGING = ROOT / "apps" / "app" / "packaging" / "android"
ANDROID = "{http://schemas.android.com/apk/res/android}"


def load(path: Path) -> ET.Element:
    return ET.parse(path).getroot()


class BackupRulesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.extraction = load(PACKAGING / "res" / "xml" / "ej_data_extraction_rules.xml")
        self.full_backup = load(PACKAGING / "res" / "xml" / "ej_backup_rules.xml")
        self.manifest = load(PACKAGING / "AndroidManifest.xml")

    def includes(self, element: ET.Element) -> list[tuple[str, str]]:
        return [(e.get("domain", ""), e.get("path", "")) for e in element.findall("include")]

    def test_android_12_rules_copy_the_files_directory_to_the_cloud_and_to_a_new_phone(self) -> None:
        self.assertEqual(self.extraction.tag, "data-extraction-rules")
        self.assertEqual([child.tag for child in self.extraction], ["cloud-backup", "device-transfer"])
        for section in self.extraction:
            self.assertEqual(self.includes(section), [("file", ".")], section.tag)

    def test_nothing_else_is_included(self) -> None:
        # With an <include>, only what is included is copied: not the cache (the share and import copies), the databases
        # directory, the shared preferences or the external storage.
        for section in [*self.extraction, self.full_backup]:
            for include in section.findall("include"):
                self.assertEqual(include.get("domain"), "file", section.tag)
            self.assertEqual(section.findall("exclude"), [], "no excludes are needed, and none may cancel the include")

    def test_android_11_rules_say_the_same(self) -> None:
        self.assertEqual(self.full_backup.tag, "full-backup-content")
        self.assertEqual(self.includes(self.full_backup), [("file", ".")])

    def test_the_manifest_points_at_both_and_keeps_backup_on(self) -> None:
        application = self.manifest.find("application")
        assert application is not None
        self.assertEqual(application.get(f"{ANDROID}allowBackup"), "true")
        self.assertEqual(application.get(f"{ANDROID}dataExtractionRules"), "@xml/ej_data_extraction_rules")
        self.assertEqual(application.get(f"{ANDROID}fullBackupContent"), "@xml/ej_backup_rules")

    def test_nothing_in_the_manifest_is_direct_boot_aware(self) -> None:
        for element in self.manifest.iter():
            self.assertIsNone(element.get(f"{ANDROID}directBootAware"), element.tag)


if __name__ == "__main__":
    unittest.main()
