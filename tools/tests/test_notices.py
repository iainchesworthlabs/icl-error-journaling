# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for tools/licences/notices.py and tools/licences/build_info.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
Only fictional names are used. The link lines are made up, shaped like the ones the CMake file API gives for a static Qt on
an iPhone, a shared Qt on a desktop and a vcpkg library; the texts and the Qt snapshot are the real files of the repository,
so a licence id the snapshot can reach that has no text fails here and not in the build of an iPhone.
"""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
LICENCES = ROOT / "tools" / "licences"
sys.path.insert(0, str(LICENCES))

import notices  # noqa: E402

QT = "/Users/sam/Qt/6.12.0/ios"
IOS_LINK_LINE = [
    "-Wl,-force_load,{qt}/plugins/platforms/libqios.a",
    "{qt}/lib/libQt6Quick.a",
    "{qt}/lib/libQt6Qml.a",
    "{qt}/lib/libQt6QmlModels.a",
    "{qt}/lib/libQt6Sql.a",
    "{qt}/plugins/sqldrivers/libqsqlite.a",
    "{qt}/lib/libQt6Gui.a",
    "{qt}/lib/libQt6Network.a",
    "{qt}/lib/libQt6Core.a",
    "{qt}/lib/libQt6BundledLibpng.a",
    "{qt}/lib/libQt6BundledHarfbuzz.a",
    "{qt}/lib/libQt6BundledPcre2.a",
    "{qt}/qml/QtQuick/libqtquick2plugin.a",
    "{qt}/qml/QtQuick/libqtquick2plugin_init.a",
    "-framework", "Foundation",
    "-lz",
    "libs/core/libej_core.a",
]


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def make_reply(binary: Path, tokens: list[str], target_build: str = ".", config: str = "Release") -> None:
    """The three files of a CMake file API reply that notices.py reads."""
    reply = binary / ".cmake" / "api" / "v1" / "reply"
    write(reply / "index-1.json", json.dumps({"reply": {"codemodel-v2": {"jsonFile": "codemodel-v2-1.json"}}}))
    write(reply / "codemodel-v2-1.json", json.dumps({"configurations": [
        {"name": "Debug", "targets": [{"name": "ej_app", "jsonFile": "target-debug.json"}]},
        {"name": config, "targets": [{"name": "ej_app", "jsonFile": "target-ej_app.json"}]},
    ]}))
    write(reply / "target-debug.json", json.dumps({"paths": {"build": "."}, "link": {"commandFragments": [
        {"role": "libraries", "fragment": "/Users/sam/Qt/6.12.0/ios/lib/libQt6Core_debug.a"}]}}))
    write(reply / "target-ej_app.json", json.dumps({"paths": {"build": target_build}, "link": {"commandFragments": [
        {"role": "flags", "fragment": "-arch arm64"},
        {"role": "libraries", "fragment": " ".join(f'"{t}"' if "/" in t else t for t in tokens)},
    ]}}))


def run(binary: Path, out: Path, *extra: str, platform: str = "ios", qt: str = QT, triplet: str = "") -> tuple[int, dict]:
    arguments = ["--binary-dir", str(binary), "--source-dir", str(binary / "src"), "--target", "ej_app",
                 "--platform", platform, "--config", "Release", "--qt-prefix", qt, "--qt-version", "6.12.0",
                 "--out-dir", str(out), "--texts", str(LICENCES / "data" / "texts"), "--licenses-dir", str(ROOT / "LICENSES"), *extra]
    if triplet:
        arguments += ["--vcpkg-triplet-dir", triplet]
    status = notices.main(arguments)
    document_path = out / "third-party-notices.json"
    return status, json.loads(document_path.read_text(encoding="utf-8")) if document_path.is_file() else {}


class SpdxTests(unittest.TestCase):
    def test_ids_of_an_expression(self) -> None:
        self.assertEqual(notices.spdx_ids("FTL OR GPL-2.0-only"), ["FTL", "GPL-2.0-only"])
        self.assertEqual(notices.spdx_ids("(MIT AND MIT-open-group) OR Zlib WITH X"), ["MIT", "MIT-open-group", "Zlib", "X"])
        self.assertEqual(notices.spdx_ids("MIT OR MIT"), ["MIT"])

    def test_the_commercial_option_is_named_in_words(self) -> None:
        self.assertEqual(notices.display_licence("LicenseRef-Qt-Commercial OR LGPL-3.0-only"),
                         "Qt commercial licence OR LGPL-3.0-only")

    def test_library_keys_agree_with_the_snapshot_tool(self) -> None:
        spec = importlib.util.spec_from_file_location("qt_sbom_manifest", LICENCES / "qt_sbom_manifest.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for name in ("libQt6Core.a", "Qt6Quick.lib", "/x/lib/libqios.a", "QtCore.framework", "libQt6Core_arm64-v8a.so"):
            self.assertEqual(notices.file_key(name), module.file_key(name))

    def test_linking_from_the_file_suffix(self) -> None:
        self.assertEqual(notices.linking_of("/a/libQt6Core.a"), "static")
        self.assertEqual(notices.linking_of("/a/libQt6Core.dylib"), "dynamic")
        self.assertEqual(notices.linking_of("/a/Qt6Core.dll"), "dynamic")


class LinkingFromTheFileTests(unittest.TestCase):
    def setUp(self) -> None:
        self.m_Temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.m_Temp.cleanup)

    def file_with(self, name: str, content: bytes) -> str:
        path = Path(self.m_Temp.name) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return str(path).replace("\\", "/")

    def test_a_framework_that_holds_an_archive_is_static(self) -> None:
        path = self.file_with("QtCore.framework/QtCore", b"!<arch>\n" + bytes(8))
        self.assertEqual(notices.linking_of(path), "static")

    def test_a_framework_that_holds_a_dynamic_library_is_dynamic(self) -> None:
        header = bytes([0xCF, 0xFA, 0xED, 0xFE]) + (12).to_bytes(4, "little") + (0).to_bytes(4, "little") + (6).to_bytes(4, "little")
        self.assertEqual(notices.linking_of(self.file_with("QtCore.framework/QtCore", header)), "dynamic")

    def test_a_file_that_is_not_there_or_not_known_is_unknown(self) -> None:
        self.assertEqual(notices.linking_of("/nowhere/QtCore.framework/QtCore"), "unknown")
        self.assertEqual(notices.linking_of(self.file_with("odd.bin", b"hello")), "unknown")
        self.assertEqual(notices.linking_of("C:/Qt/lib/Qt6Core.lib"), "unknown")


class LinkLineTests(unittest.TestCase):
    def setUp(self) -> None:
        self.m_Temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.m_Temp.cleanup)
        self.m_Binary = Path(self.m_Temp.name) / "build"
        self.m_Out = Path(self.m_Temp.name) / "out"

    def test_an_iphone_link_line_is_accounted_for_and_static(self) -> None:
        make_reply(self.m_Binary, [t.format(qt=QT) for t in IOS_LINK_LINE])
        status, document = run(self.m_Binary, self.m_Out, "--strict")
        self.assertEqual(document["unresolved"], [])
        self.assertEqual(status, 0)
        names = {c["name"] for c in document["components"]}
        self.assertIn("Qt Declarative", names)
        self.assertTrue(any(n.startswith("FreeType") for n in names), names)
        self.assertTrue(any(n.startswith("SQLite") for n in names), names)
        self.assertTrue(any(n.startswith("libpng") for n in names), names)
        for component in document["components"]:
            self.assertEqual(component["linking"], "static", component["name"])
            self.assertTrue(component["textIds"] or component["exemptIds"] or component["kind"] == "vcpkg", component["name"])
        for component in document["components"]:
            for identifier in component["textIds"]:
                self.assertIn(identifier, document["texts"])
        kinds = {item["input"]: item["kind"] for item in document["linked"]}
        self.assertEqual(kinds["-lz"], "system")
        self.assertEqual(kinds["-framework Foundation"], "system")
        self.assertEqual(kinds[str(self.m_Binary / "libs" / "core" / "libej_core.a").replace("\\", "/")], "first-party")
        self.assertTrue((self.m_Out / "THIRD-PARTY-NOTICES.txt").is_file())

    def test_the_other_configuration_is_not_read(self) -> None:
        make_reply(self.m_Binary, [t.format(qt=QT) for t in IOS_LINK_LINE])
        _, document = run(self.m_Binary, self.m_Out)
        self.assertFalse(any("_debug" in item["input"] for item in document["linked"]))

    def test_a_qt_library_the_snapshot_does_not_know_fails_the_strict_build(self) -> None:
        make_reply(self.m_Binary, [f"{QT}/lib/libQt6Core.a", f"{QT}/lib/libQt6NoSuchModule.a"])
        status, document = run(self.m_Binary, self.m_Out, "--strict")
        self.assertEqual(status, 1)
        self.assertTrue(any("NoSuchModule" in message for message in document["unresolved"]))

    def test_without_strict_the_exit_status_is_zero(self) -> None:
        make_reply(self.m_Binary, [f"{QT}/lib/libQt6NoSuchModule.a"])
        status, document = run(self.m_Binary, self.m_Out)
        self.assertEqual(status, 0)
        self.assertTrue(document["unresolved"])

    def test_check_writes_nothing(self) -> None:
        make_reply(self.m_Binary, [f"{QT}/lib/libQt6Core.a"])
        run(self.m_Binary, self.m_Out, "--check")
        self.assertFalse(self.m_Out.exists())

    def test_no_file_api_reply_is_a_failure_and_not_an_empty_list(self) -> None:
        status, document = run(self.m_Binary, self.m_Out, "--strict")
        self.assertEqual(status, 1)
        self.assertTrue(any("file API" in message for message in document["unresolved"]))

    def test_a_linux_link_line_with_rpaths_and_versioned_libraries(self) -> None:
        qt = "/opt/qt/6.12.0/gcc_64"
        make_reply(self.m_Binary, ["-Wl,-rpath,/opt/qt/6.12.0/gcc_64/lib", "-Wl,--as-needed", f"{qt}/lib/libQt6Core.so.6.12.0",
                                   f"{qt}/lib/libQt6Quick.so.6.12.0", f"{qt}/lib/libQt6Qml.so.6.12.0",
                                   f"{qt}/lib/libQt6Gui.so.6.12.0", f"{qt}/lib/libQt6Sql.so.6.12.0", "/usr/lib/x86_64-linux-gnu/libm.so",
                                   "-lpthread"])
        _, document = run(self.m_Binary, self.m_Out, platform="linux", qt=qt)
        self.assertEqual(document["unresolved"], [])
        self.assertTrue(all(c["linking"] == "dynamic" for c in document["components"]), document["components"][0])

    def test_an_empty_link_line_is_a_failure(self) -> None:
        make_reply(self.m_Binary, [])
        status, _ = run(self.m_Binary, self.m_Out, "--strict")
        self.assertEqual(status, 1)

    def test_a_library_that_belongs_to_nobody_is_unresolved(self) -> None:
        make_reply(self.m_Binary, ["/opt/somewhere/libmystery.a"])
        status, document = run(self.m_Binary, self.m_Out, "--strict")
        self.assertEqual(status, 1)
        self.assertTrue(any("libmystery" in message for message in document["unresolved"]))

    def test_xcode_gives_a_library_relative_to_the_target_directory(self) -> None:
        make_reply(self.m_Binary, ["../../libs/core/libej_core.a", f"{QT}/lib/libQt6Core.a"], target_build="apps/app")
        _, document = run(self.m_Binary, self.m_Out)
        first_party = [i for i in document["linked"] if i["kind"] == "first-party"]
        self.assertEqual(len(first_party), 1)

    def test_msvc_flags_and_system_libraries_are_not_components(self) -> None:
        qt = "C:/Qt/6.12.0/msvc2022_64"
        make_reply(self.m_Binary, ["/machine:x64", "kernel32.lib", f"{qt}/lib/Qt6Core.lib", f"{qt}/lib/Qt6Quick.lib",
                                   f"{qt}/lib/Qt6Qml.lib", f"{qt}/lib/Qt6Gui.lib", f"{qt}/lib/Qt6Sql.lib",
                                   f"{qt}/lib/Qt6EntryPoint.lib"])
        _, document = run(self.m_Binary, self.m_Out, platform="windows", qt=qt)
        self.assertEqual(document["unresolved"], [])
        self.assertEqual({i["kind"] for i in document["linked"]} - {"system", "qt"}, set())
        self.assertTrue(all(c["linking"] == "unknown" for c in document["components"]))


class VcpkgTests(unittest.TestCase):
    def setUp(self) -> None:
        self.m_Temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.m_Temp.cleanup)
        base = Path(self.m_Temp.name)
        self.m_Binary = base / "build"
        self.m_Out = base / "out"
        self.m_Installed = self.m_Binary / "vcpkg_installed"
        self.m_Triplet = self.m_Installed / "arm64-sample"
        write(self.m_Installed / "vcpkg" / "status",
              "Package: libsample\nVersion: 1.2.3\nPort-Version: 2\nDepends: libhelper\nArchitecture: arm64-sample\n"
              "Multi-Arch: same\nStatus: install ok installed\n\n"
              "Package: libhelper\nVersion: 4.0\nArchitecture: arm64-sample\nStatus: install ok installed\n\n"
              "Package: testframe\nVersion: 3.0\nArchitecture: arm64-sample\nStatus: install ok installed\n")
        write(self.m_Installed / "vcpkg" / "info" / "libsample_1.2.3_arm64-sample.list",
              "arm64-sample/\narm64-sample/lib/\narm64-sample/lib/libsample.a\n")
        write(self.m_Installed / "vcpkg" / "info" / "libhelper_4.0_arm64-sample.list", "arm64-sample/lib/libhelper.a\n")
        write(self.m_Triplet / "share" / "libsample" / "copyright", "Sample Library, MIT-style terms.\n")
        write(self.m_Triplet / "share" / "libhelper" / "copyright", "Helper Library terms.\n")
        write(self.m_Triplet / "share" / "testframe" / "copyright", "Test frame terms.\n")
        write(self.m_Binary / "src" / "vcpkg.json", json.dumps({"dependencies": [
            "libsample", {"name": "testframe", "platform": "!ios"}, {"name": "tool", "host": True}]}))
        self.m_Manifest = base / "manifest.json"
        manifest = json.loads((LICENCES / "data" / "manifest.json").read_text(encoding="utf-8"))
        manifest["portLicences"] = {"libsample": "MIT"}
        manifest["testOnlyPorts"] = ["testframe"]
        write(self.m_Manifest, json.dumps(manifest))

    def run_it(self, tokens: list[str], *extra: str) -> tuple[int, dict]:
        make_reply(self.m_Binary, tokens)
        return run(self.m_Binary, self.m_Out, "--manifest", str(self.m_Manifest), *extra, triplet=str(self.m_Triplet))

    def test_a_linked_port_and_its_dependency_get_their_own_text(self) -> None:
        library = str(self.m_Triplet / "lib" / "libsample.a").replace("\\", "/")
        status, document = self.run_it([library, f"{QT}/lib/libQt6Core.a"], "--strict")
        self.assertEqual(document["unresolved"], [])
        self.assertEqual(status, 0)
        ports = {c["name"]: c for c in document["components"] if c["kind"] == "vcpkg"}
        self.assertEqual(set(ports), {"libsample", "libhelper"})
        self.assertEqual(ports["libsample"]["version"], "1.2.3#2")
        self.assertEqual(ports["libsample"]["linking"], "static")
        self.assertEqual(document["texts"]["vcpkg/libsample"], "Sample Library, MIT-style terms.\n")

    def test_a_declared_dependency_that_is_installed_and_not_linked_must_be_named_test_only(self) -> None:
        status, document = self.run_it([f"{QT}/lib/libQt6Core.a"], "--strict")
        self.assertEqual(status, 1)
        self.assertEqual([m for m in document["unresolved"] if "libsample" in m][0][:22], "vcpkg dependency libsa")
        self.assertFalse(any("testframe" in m for m in document["unresolved"]))

    def test_an_unowned_file_under_the_vcpkg_tree_is_unresolved(self) -> None:
        status, document = self.run_it([str(self.m_Triplet / "lib" / "libghost.a").replace("\\", "/")], "--strict")
        self.assertEqual(status, 1)
        self.assertTrue(any("no port owns" in m for m in document["unresolved"]))


class TextTests(unittest.TestCase):
    def test_every_licence_a_qt_module_of_the_app_can_bring_has_its_text(self) -> None:
        """Whatever the app comes to link from Qt, the licences have a text: a new import cannot break the iPhone build late."""
        qt = QT
        libraries = [t.format(qt=qt) for t in IOS_LINK_LINE] + [
            f"{qt}/plugins/imageformats/lib{n}.a" for n in ("qgif", "qico", "qjpeg", "qsvg")] + [
            f"{qt}/plugins/iconengines/libqsvgicon.a", f"{qt}/plugins/tls/libqsecuretransportbackend.a",
            f"{qt}/plugins/networkinformation/libqapplenetworkinformation.a", f"{qt}/lib/libQt6Svg.a",
            f"{qt}/lib/libQt6BundledLibjpeg.a", f"{qt}/lib/libQt6BundledFreetype.a", f"{qt}/lib/libQt6BundledZLIB.a"]
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "build"
            make_reply(binary, libraries)
            status, document = run(binary, Path(temp) / "out", "--strict")
        self.assertEqual(document["unresolved"], [])
        self.assertEqual(status, 0)

    def test_the_licence_of_the_application_has_its_text(self) -> None:
        manifest = json.loads((LICENCES / "data" / "manifest.json").read_text(encoding="utf-8"))
        self.assertTrue((ROOT / "LICENSES" / f"{manifest['project']['licence']}.txt").is_file())


class BuildInfoTests(unittest.TestCase):
    def setUp(self) -> None:
        self.m_Temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.m_Temp.cleanup)
        self.m_Dir = Path(self.m_Temp.name)
        spec = importlib.util.spec_from_file_location("build_info", LICENCES / "build_info.py")
        self.m_Module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.m_Module)

    def git(self, *arguments: str) -> None:
        subprocess.run(["git", "-C", str(self.m_Dir), "-c", "user.name=Sam Example", "-c", "user.email=sam@example.invalid",
                        *arguments], check=True, capture_output=True,
                       env={**os.environ, "GIT_COMMITTER_DATE": "2026-01-02T03:04:05+00:00",
                            "GIT_AUTHOR_DATE": "2026-01-02T03:04:05+00:00"})

    def values(self, **environment: str) -> dict:
        clean = {k: v for k, v in os.environ.items() if k not in ("SOURCE_DATE_EPOCH", "GITHUB_SHA", "GITHUB_REF_NAME",
                                                                  "GITHUB_RUN_NUMBER", "GITHUB_REF_TYPE")}
        return self.m_Module.collect(self.m_Dir, build_number="", environment={**clean, **environment})

    def test_a_folder_that_is_not_a_repository_says_unknown_and_does_not_fail(self) -> None:
        values = self.values()
        self.assertEqual(values["commit"], "unknown")
        self.assertEqual(values["date"], "unknown")
        self.assertEqual(values["ref"], "")
        self.assertFalse(values["dirty"])

    def test_a_repository_gives_the_short_hash_the_commit_date_and_the_branch(self) -> None:
        self.git("init", "-b", "feature/sample")
        write(self.m_Dir / "a.txt", "a\n")
        self.git("add", "a.txt")
        self.git("commit", "-m", "First")
        values = self.values()
        self.assertRegex(values["commit"], r"^[0-9a-f]{7}$")
        self.assertEqual(values["date"], "2026-01-02 03:04 UTC")
        self.assertEqual(values["ref"], "feature/sample")
        self.assertFalse(values["dirty"])
        self.git("tag", "v1.2.3")
        self.assertEqual(self.values()["ref"], "v1.2.3")
        write(self.m_Dir / "a.txt", "changed\n")
        self.assertTrue(self.values()["dirty"])

    def test_source_date_epoch_decides_the_date_when_it_is_set(self) -> None:
        self.assertEqual(self.values(SOURCE_DATE_EPOCH="86400")["date"], "1970-01-02 00:00 UTC")

    def test_github_values_are_used_without_git(self) -> None:
        values = self.values(GITHUB_SHA="0123456789abcdef0123456789abcdef01234567", GITHUB_REF_NAME="develop",
                             GITHUB_RUN_NUMBER="57")
        self.assertEqual(values["commit"], "0123456")
        self.assertEqual(values["ref"], "develop")
        self.assertEqual(values["buildNumber"], "57")

    def test_a_build_number_given_to_cmake_wins(self) -> None:
        values = self.m_Module.collect(self.m_Dir, build_number="12", environment={"GITHUB_RUN_NUMBER": "57"})
        self.assertEqual(values["buildNumber"], "12")

    def test_the_generated_source_holds_the_values_and_is_written_only_when_it_changes(self) -> None:
        out = self.m_Dir / "values.cpp"
        values = {"commit": "abc1234", "dirty": True, "ref": 'odd"name', "buildNumber": "3", "date": "2026-01-02 03:04 UTC"}
        self.m_Module.write_source(out, values)
        text = out.read_text(encoding="utf-8")
        self.assertIn('"abc1234"', text)
        self.assertIn('"odd\\"name"', text)
        stamp = out.stat().st_mtime_ns
        self.m_Module.write_source(out, values)
        self.assertEqual(out.stat().st_mtime_ns, stamp)


if __name__ == "__main__":
    unittest.main()
