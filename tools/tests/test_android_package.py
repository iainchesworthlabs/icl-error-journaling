# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_android_package.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_android_package as package  # noqa: E402

NS = "http://schemas.android.com/apk/res/android"


def manifest(
    *,
    application_id: str = package.APPLICATION_ID,
    minimum: int = 28,
    target: int = 36,
    permissions: tuple[str, ...] = (),
    backup: str = "(type 0x12)0xffffffff",
    orientation: int = 1,
    extra: str = "",
    filters: str = "",
    rules: tuple[str, ...] = ("dataExtractionRules", "fullBackupContent"),
    direct_boot: bool = False,
) -> str:
    """aapt2 dump xmltree output in the shape aapt2 prints it."""
    lines = [
        f"N: android={NS} (line=2)",
        "  E: manifest (line=2)",
        f"    A: {NS}:versionCode(0x0101021b)=100",
        f'    A: package="{application_id}" (Raw: "{application_id}")',
        "    E: uses-sdk (line=7)",
        f"      A: {NS}:minSdkVersion(0x0101020c)={minimum}",
        f"      A: {NS}:targetSdkVersion(0x01010270)={target}",
    ]
    for name in permissions:
        lines += ["    E: uses-permission (line=9)", f'      A: {NS}:name(0x01010003)="{name}" (Raw: "{name}")']
    activity = package.ACTIVITY_CLASS
    lines += [
        "    E: application (line=15)",
        f"      A: {NS}:allowBackup(0x01010280)={backup}",
        *(f"      A: {NS}:{name}(0x0101fe00)=@0x7f0e0001" for name in rules),
        "      E: activity (line=22)",
        f'        A: {NS}:name(0x01010003)="{activity}" (Raw: "{activity}")',
        f"        A: {NS}:exported(0x01010010)=(type 0x12)0xffffffff",
        *([f"        A: {NS}:directBootAware(0x01010505)=(type 0x12)0xffffffff"] if direct_boot else []),
        f"        A: {NS}:screenOrientation(0x0101001e)={orientation}",
        "        E: intent-filter (line=29)",
        "          E: action (line=30)",
        f'            A: {NS}:name(0x01010003)="android.intent.action.MAIN" (Raw: "android.intent.action.MAIN")',
        "          E: category (line=31)",
        f'            A: {NS}:name(0x01010003)="android.intent.category.LAUNCHER" (Raw: "android.intent.category.LAUNCHER")',
        *(filters.splitlines() if filters else []),
        "      E: provider (line=44)",
        f'        A: {NS}:name(0x01010003)="androidx.core.content.FileProvider" (Raw: "androidx.core.content.FileProvider")',
        f"        A: {NS}:exported(0x01010010)=(type 0x12)0x0",
    ]
    return "\n".join(lines) + "\n" + extra


HEADER = "  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align\n"


def headers(*rows: str) -> str:
    return "Program Headers:\n" + HEADER + "".join(f"  {row}\n" for row in rows)


class ManifestTest(unittest.TestCase):
    def test_expected_manifest_passes(self) -> None:
        self.assertEqual(package.manifest_problems(manifest()), [])

    def test_internet_permission_is_refused(self) -> None:
        problems = package.manifest_problems(manifest(permissions=("android.permission.INTERNET",)))
        self.assertEqual(len(problems), 1)
        self.assertIn("android.permission.INTERNET", problems[0])

    def test_advertising_id_permission_is_refused(self) -> None:
        problems = package.manifest_problems(manifest(permissions=("com.google.android.gms.permission.AD_ID",)))
        self.assertEqual(len(problems), 1)

    def test_own_signature_permission_is_allowed(self) -> None:
        own = package.APPLICATION_ID + ".DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION"
        self.assertEqual(package.manifest_problems(manifest(permissions=(own,))), [])

    def test_another_application_prefix_is_not_own(self) -> None:
        other = package.APPLICATION_ID + "other.PERMISSION"
        self.assertEqual(len(package.manifest_problems(manifest(permissions=(other,)))), 1)

    def test_sdk_range_is_checked(self) -> None:
        self.assertIn("minSdkVersion is 26, expected 28", package.manifest_problems(manifest(minimum=26)))
        self.assertIn("targetSdkVersion is 35, expected 36", package.manifest_problems(manifest(target=35)))

    def test_hex_sdk_values_are_read(self) -> None:
        text = manifest().replace("=28", "=(type 0x10)0x1c").replace("=36", "=(type 0x10)0x24")
        self.assertEqual(package.manifest_problems(text), [])

    def test_a_new_application_id_keeps_the_activity_class(self) -> None:
        # The ID will change when the name is settled; the Java package of EjActivity does not.
        text = manifest(application_id="com.example.newname")
        self.assertEqual(package.manifest_problems(text, "com.example.newname"), [])
        self.assertTrue(package.manifest_problems(text))  # against the default ID it is wrong

    def test_application_id_is_checked(self) -> None:
        problems = package.manifest_problems(manifest(application_id="org.qtproject.example"))
        self.assertTrue(any("application ID" in p for p in problems))

    def test_backup_off_is_refused(self) -> None:
        problems = package.manifest_problems(manifest(backup="(type 0x12)0x0"))
        self.assertEqual(len(problems), 1)
        self.assertIn("allowBackup", problems[0])

    def test_both_backup_rule_attributes_are_required(self) -> None:
        for missing in ("dataExtractionRules", "fullBackupContent"):
            kept = tuple(r for r in ("dataExtractionRules", "fullBackupContent") if r != missing)
            problems = package.manifest_problems(manifest(rules=kept))
            self.assertEqual(len(problems), 1, missing)
            self.assertIn(missing, problems[0])

    def test_a_direct_boot_aware_activity_is_refused(self) -> None:
        problems = package.manifest_problems(manifest(direct_boot=True))
        self.assertEqual(len(problems), 1)
        self.assertIn("direct-boot aware", problems[0])

    def test_landscape_activity_is_refused(self) -> None:
        problems = package.manifest_problems(manifest(orientation=0))
        self.assertEqual(problems, [f"{package.ACTIVITY_CLASS} is not portrait"])

    def test_exported_receiver_is_refused(self) -> None:
        receiver = (
            "      E: receiver (line=50)\n"
            f'        A: {NS}:name(0x01010003)="x.Receiver" (Raw: "x.Receiver")\n'
            f"        A: {NS}:exported(0x01010010)=(type 0x12)0xffffffff\n"
        )
        problems = package.manifest_problems(manifest(extra=receiver))
        self.assertEqual(problems, ["the receiver x.Receiver is exported without a permission"])

    def test_exported_receiver_behind_a_permission_passes(self) -> None:
        name = "androidx.profileinstaller.ProfileInstallReceiver"
        receiver = (
            "      E: receiver (line=50)\n"
            f'        A: {NS}:name(0x01010003)="{name}" (Raw: "{name}")\n'
            f"        A: {NS}:exported(0x01010010)=(type 0x12)0xffffffff\n"
            f'        A: {NS}:permission(0x01010006)="android.permission.DUMP" (Raw: "android.permission.DUMP")\n'
        )
        self.assertEqual(package.manifest_problems(manifest(extra=receiver)), [])

    @staticmethod
    def view_filter(mime: str) -> str:
        return "\n".join(
            [
                "        E: intent-filter (line=33)",
                "          E: action (line=34)",
                f'            A: {NS}:name(0x01010003)="android.intent.action.VIEW" (Raw: "android.intent.action.VIEW")',
                "          E: data (line=35)",
                f'            A: {NS}:scheme(0x01010027)="content" (Raw: "content")',
                "          E: data (line=36)",
                f'            A: {NS}:mimeType(0x01010026)="{mime}" (Raw: "{mime}")',
            ]
        )

    def test_the_setup_file_types_are_allowed(self) -> None:
        for mime in ("application/json", "application/octet-stream"):
            self.assertEqual(package.manifest_problems(manifest(filters=self.view_filter(mime))), [])

    def test_an_intent_filter_for_every_type_is_refused(self) -> None:
        for mime in ("*/*", "*", "application/*"):
            problems = package.manifest_problems(manifest(filters=self.view_filter(mime)))
            self.assertEqual(len(problems), 1, mime)
            self.assertIn(mime, problems[0])

    def test_missing_manifest_is_reported(self) -> None:
        self.assertEqual(package.manifest_problems(""), ["the package has no manifest element"])


class ElfTest(unittest.TestCase):
    def test_aligned_library_passes(self) -> None:
        text = headers(
            "LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x0cfaf0 0x0cfaf0 R E 0x4000",
            "LOAD           0x0cfaf0 0x00000000000dfaf0 0x00000000000dfaf0 0x01510 0x03510 RW  0x4000",
            "GNU_RELRO      0x0cfaf0 0x00000000000dfaf0 0x00000000000dfaf0 0x00510 0x00510 R   0x1",
        )
        self.assertEqual(package.elf_problems("lib/x.so", text), [])

    def test_four_kilobyte_load_is_refused(self) -> None:
        text = headers("LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x0680 0x0680 R E 0x1000")
        problems = package.elf_problems("lib/x.so", text)
        self.assertEqual(len(problems), 1)
        self.assertIn("0x1000", problems[0])

    def test_relro_ending_mid_page_with_data_after_is_refused(self) -> None:
        # Android's example 1: RELRO ends at 0xe1000 and the segment goes on to 0xe4000.
        text = headers(
            "LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x0cfaf0 0x0cfaf0 R E 0x4000",
            "LOAD           0x0cfaf0 0x00000000000dfaf0 0x00000000000dfaf0 0x02510 0x04510 RW  0x4000",
            "GNU_RELRO      0x0cfaf0 0x00000000000dfaf0 0x00000000000dfaf0 0x01510 0x01510 R   0x1",
        )
        problems = package.elf_problems("lib/x.so", text)
        self.assertEqual(len(problems), 1)
        self.assertIn("GNU_RELRO", problems[0])

    def test_relro_filling_its_segment_passes(self) -> None:
        # Android's example 4: RELRO ends mid-page but nothing follows it in its segment.
        text = headers(
            "LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x000680 0x000680 R E 0x4000",
            "LOAD           0x000680 0x0000000000004680 0x0000000000004680 0x0001d8 0x000980 RW  0x4000",
            "GNU_RELRO      0x000680 0x0000000000004680 0x0000000000004680 0x0001d8 0x000980 R   0x1",
        )
        self.assertEqual(package.elf_problems("lib/x.so", text), [])

    def test_four_kilobyte_alignment_is_allowed_on_32_bit(self) -> None:
        text = headers("LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x0680 0x0680 R E 0x1000")
        self.assertEqual(package.elf_problems("lib/armeabi-v7a/x.so", text, require_16kb=False), [])

    def test_a_32_bit_file_that_is_not_elf_is_still_reported(self) -> None:
        self.assertEqual(len(package.elf_problems("lib/armeabi-v7a/x.so", "x", require_16kb=False)), 1)

    def test_no_load_segment_is_reported(self) -> None:
        self.assertEqual(len(package.elf_problems("lib/x.so", "not an ELF file")), 1)


class DependencyTest(unittest.TestCase):
    def test_needed_entries_are_read(self) -> None:
        text = (
            "Dynamic section at offset 0x1 contains 3 entries:\n"
            "  Tag                Type     Name/Value\n"
            "  0x0000000000000001 (NEEDED) Shared library: [libQt6Core_arm64-v8a.so]\n"
            "  0x0000000000000001 (NEEDED) Shared library: [libc++_shared.so]\n"
            "  0x000000000000000e (SONAME) Library soname: [libx.so]\n"
        )
        self.assertEqual(package.parse_needed(text), ["libQt6Core_arm64-v8a.so", "libc++_shared.so"])

    def test_complete_package_passes(self) -> None:
        needed = {
            "lib/arm64-v8a/libapp.so": ["libQt6Core_arm64-v8a.so", "libc++_shared.so", "liblog.so"],
            "lib/arm64-v8a/libQt6Core_arm64-v8a.so": ["libc++_shared.so", "libc.so"],
            "lib/arm64-v8a/libc++_shared.so": ["libc.so"],
        }
        self.assertEqual(package.missing_dependencies(needed, {"libc.so", "liblog.so"}), [])

    def test_missing_cpp_runtime_is_reported(self) -> None:
        # The fault a half-cleaned Gradle directory produced: the app links libc++_shared.so, which was left out.
        needed = {"lib/arm64-v8a/libapp.so": ["libc++_shared.so", "libc.so"]}
        problems = package.missing_dependencies(needed, {"libc.so"})
        self.assertEqual(len(problems), 1)
        self.assertIn("libc++_shared.so", problems[0])

    def test_a_library_of_another_abi_does_not_count(self) -> None:
        needed = {
            "lib/arm64-v8a/libapp.so": ["libc++_shared.so"],
            "lib/x86_64/libc++_shared.so": [],
        }
        self.assertEqual(len(package.missing_dependencies(needed, set())), 1)

    def test_required_libraries_present_pass(self) -> None:
        libraries = [
            "lib/arm64-v8a/liberror-journaling_arm64-v8a.so",
            "lib/arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so",
            "lib/arm64-v8a/libplugins_sqldrivers_qsqlite_arm64-v8a.so",
        ]
        self.assertEqual(package.missing_required(libraries), [])

    def test_missing_sql_driver_is_reported(self) -> None:
        # The fault the first emulator run showed: "can not load requested driver 'QSQLITE'".
        libraries = [
            "base/lib/arm64-v8a/liberror-journaling_arm64-v8a.so",
            "base/lib/arm64-v8a/libplugins_platforms_qtforandroid_arm64-v8a.so",
        ]
        problems = package.missing_required(libraries)
        self.assertEqual(len(problems), 1)
        self.assertIn("libplugins_sqldrivers_qsqlite_arm64-v8a.so", problems[0])

    def test_bundle_paths_have_their_abi(self) -> None:
        self.assertEqual(package.abi_of("base/lib/arm64-v8a/libx.so"), "arm64-v8a")
        self.assertEqual(package.abi_of("lib/x86_64/libx.so"), "x86_64")


if __name__ == "__main__":
    unittest.main()
