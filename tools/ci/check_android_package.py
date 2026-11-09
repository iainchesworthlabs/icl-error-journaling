#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Look inside a built Android package, the counterpart of apps/app/packaging/ios/verify_app.sh.

    python3 tools/ci/check_android_package.py --sdk "$ANDROID_SDK_ROOT" --ndk "$ANDROID_NDK_ROOT" \\
        [--application-id com.iainchesworthlabs.errorjournaling] PACKAGE.apk [PACKAGE.aab ...]

For an APK it reads the merged manifest (`aapt2 dump xmltree`) and refuses:
- any permission except the application's own (NFR-09: no network, so no INTERNET, and no AD_ID; androidx declares one
  signature permission named after the application for its unexported receivers);
- a minimum SDK other than 28 or a target SDK other than 36 (Google Play's requirement from 31 August 2026);
- another application ID, a launcher activity other than EjActivity, an activity that is not portrait, another
  component exported without a permission guarding it, and backup turned off (Stage 2 decision 8: the store stays in
  the device backup).

For an APK and a bundle it checks that every 64-bit native library (arm64-v8a, x86_64) is aligned for 16 KB pages
(`llvm-readelf`: each LOAD
segment aligned to at least 16 KB, and the end of GNU_RELRO on a 16 KB boundary unless nothing writable follows it in its
segment), and for an APK that the libraries are 16 KB aligned in the zip (`zipalign -c -P 16 4`). Google Play requires
both for targets of API 35 and up (docs/plan/stage-9.md, "Builds and toolchain"). It also checks that every library a
packaged library links (its NEEDED entries) is in the package or provided by Android at API 28 (the NDK's sysroot),
so that a package that would stop at launch with "dlopen failed" fails here instead.

The parsing is pure and tested in tools/tests/test_android_package.py; only main() runs the tools.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

APPLICATION_ID = "com.iainchesworthlabs.errorjournaling"
MIN_SDK = 28
TARGET_SDK = 36
# The Java class of the launcher activity. Its package is the one the Java sources are in, which stays as it is when the
# application ID changes (the ID is set in CMake and need not equal the package of any class).
ACTIVITY_CLASS = "com.iainchesworthlabs.errorjournaling.EjActivity"
# The ABIs of 64-bit devices. Google Play asks for 16 KB page alignment on those only.
SIXTY_FOUR_BIT = ("arm64-v8a", "x86_64")
PAGE = 0x4000  # 16 KB

# android:screenOrientation values as aapt2 prints them (the enum's integer); 1 is "portrait".
PORTRAIT = 1
# How aapt2 prints a boolean attribute.
TRUE_VALUES = {"(type 0x12)0xffffffff", "true"}
FALSE_VALUES = {"(type 0x12)0x0", "false"}
COMPONENTS = ("activity", "activity-alias", "service", "receiver", "provider")


@dataclass
class Element:
    name: str
    attributes: dict[str, str] = field(default_factory=dict)
    children: list["Element"] = field(default_factory=list)

    def all(self, name: str) -> list["Element"]:
        """Every element called `name` at any depth below this one."""
        found = []
        for child in self.children:
            if child.name == name:
                found.append(child)
            found += child.all(name)
        return found


ELEMENT = re.compile(r"^(?P<indent>\s*)E: (?P<name>[\w.-]+)")
# `A: http://schemas.android.com/apk/res/android:minSdkVersion(0x0101020c)=28` or `A: package="x" (Raw: "x")`.
ATTRIBUTE = re.compile(r"^(?P<indent>\s*)A: (?:[^\s=]*:)?(?P<name>[\w.-]+)(?:\(0x[0-9a-f]+\))?=(?P<value>.*)$")


def parse_xmltree(text: str) -> Element:
    """The tree of elements of `aapt2 dump xmltree` output, under a synthetic root."""
    root = Element("")
    stack: list[tuple[int, Element]] = [(-1, root)]
    for line in text.splitlines():
        element = ELEMENT.match(line)
        if element:
            depth = len(element.group("indent"))
            while stack[-1][0] >= depth:
                stack.pop()
            node = Element(element.group("name"))
            stack[-1][1].children.append(node)
            stack.append((depth, node))
            continue
        attribute = ATTRIBUTE.match(line)
        if attribute and len(stack) > 1:
            value = attribute.group("value").strip()
            quoted = re.match(r'^"(.*?)"(?: \(Raw: .*\))?$', value)
            stack[-1][1].attributes[attribute.group("name")] = quoted.group(1) if quoted else value
    return root


def is_true(value: str | None) -> bool:
    return value is not None and value in TRUE_VALUES


def as_int(value: str | None) -> int | None:
    if value is None:
        return None
    match = re.search(r"(0x[0-9a-fA-F]+|-?\d+)$", value)
    return int(match.group(1), 0) if match else None


def manifest_problems(text: str, application_id: str = APPLICATION_ID) -> list[str]:
    """What is wrong with the merged manifest that `aapt2 dump xmltree` printed."""
    problems = []
    root = parse_xmltree(text)
    manifests = root.all("manifest")
    if len(manifests) != 1:
        return ["the package has no manifest element"]
    manifest = manifests[0]
    activity_id = ACTIVITY_CLASS

    package = manifest.attributes.get("package")
    if package != application_id:
        problems.append(f"the application ID is '{package}', expected '{application_id}'")

    own = application_id + "."
    for tag in ("uses-permission", "uses-permission-sdk-23"):
        for element in manifest.all(tag):
            name = element.attributes.get("name", "")
            if not name.startswith(own):
                problems.append(f"the package asks for the permission '{name}' (NFR-09: none is allowed)")

    sdk = manifest.all("uses-sdk")
    minimum = as_int(sdk[0].attributes.get("minSdkVersion")) if sdk else None
    target = as_int(sdk[0].attributes.get("targetSdkVersion")) if sdk else None
    if minimum != MIN_SDK:
        problems.append(f"minSdkVersion is {minimum}, expected {MIN_SDK}")
    if target != TARGET_SDK:
        problems.append(f"targetSdkVersion is {target}, expected {TARGET_SDK}")

    applications = manifest.all("application")
    if len(applications) != 1:
        problems.append("the manifest has no single application element")
        return problems
    application = applications[0]
    backup = application.attributes.get("allowBackup")
    if backup is not None and not is_true(backup):
        problems.append(f"allowBackup is {backup}, expected true (Stage 2 decision 8)")

    launchers = [
        activity
        for activity in application.all("activity")
        if any(
            action.attributes.get("name") == "android.intent.action.MAIN"
            for intent in activity.all("intent-filter")
            for action in intent.all("action")
        )
    ]
    names = [activity.attributes.get("name", "") for activity in launchers]
    if names != [activity_id]:
        problems.append(f"the launcher activities are {names}, expected ['{activity_id}']")

    for activity in application.all("activity"):
        name = activity.attributes.get("name", "")
        if name == activity_id and as_int(activity.attributes.get("screenOrientation")) != PORTRAIT:
            problems.append(f"{name} is not portrait")

    for tag in COMPONENTS:
        for component in application.all(tag):
            name = component.attributes.get("name", "")
            # An exported component that a permission guards (androidx's ProfileInstallReceiver needs DUMP, which only
            # the shell holds) cannot be reached by another app.
            guarded = bool(component.attributes.get("permission"))
            if is_true(component.attributes.get("exported")) and name != activity_id and not guarded:
                problems.append(f"the {tag} {name} is exported without a permission")
    return problems


SEGMENT = re.compile(
    r"^\s*(?P<type>[A-Z_]+)\s+0x(?P<offset>[0-9a-f]+)\s+0x(?P<vaddr>[0-9a-f]+)\s+0x(?P<paddr>[0-9a-f]+)\s+"
    r"0x(?P<filesz>[0-9a-f]+)\s+0x(?P<memsz>[0-9a-f]+)\s+(?P<flags>[RWE ]+?)\s+0x(?P<align>[0-9a-f]+)\s*$"
)


@dataclass(frozen=True)
class Segment:
    type: str
    vaddr: int
    memsz: int
    align: int

    @property
    def end(self) -> int:
        return self.vaddr + self.memsz


def parse_segments(text: str) -> list[Segment]:
    """The program headers of `llvm-readelf -Wl` output."""
    segments = []
    for line in text.splitlines():
        match = SEGMENT.match(line)
        if match:
            segments.append(
                Segment(
                    match.group("type"),
                    int(match.group("vaddr"), 16),
                    int(match.group("memsz"), 16),
                    int(match.group("align"), 16),
                )
            )
    return segments


def elf_problems(library: str, text: str, require_16kb: bool = True) -> list[str]:
    """What stops one shared library from loading on a 16 KB device, from its program headers. A 32-bit library
    (require_16kb false) only has to be an ELF file: Google Play asks for 16 KB alignment on 64-bit devices."""
    segments = parse_segments(text)
    loads = [s for s in segments if s.type == "LOAD"]
    if not loads:
        return [f"{library}: no LOAD segment was read (is it an ELF file?)"]
    if not require_16kb:
        return []
    problems = [
        f"{library}: a LOAD segment is aligned to {hex(s.align)}, below 16 KB (0x4000)" for s in loads if s.align < PAGE
    ]
    for relro in (s for s in segments if s.type == "GNU_RELRO"):
        if relro.end % PAGE == 0:
            continue
        containing = [s for s in loads if s.vaddr <= relro.vaddr < s.end]
        if containing and containing[0].end == relro.end:
            continue  # nothing writable follows the RELRO data in its segment
        problems.append(f"{library}: GNU_RELRO ends at {hex(relro.end)}, not on a 16 KB boundary, with data after it")
    return problems


NEEDED = re.compile(r"\(NEEDED\)\s+Shared library: \[(?P<name>[^\]]+)\]")

# The NDK's sysroot directory of each ABI, whose API-level folder lists the libraries Android itself provides.
ABI_TRIPLES = {
    "arm64-v8a": "aarch64-linux-android",
    "x86_64": "x86_64-linux-android",
    "armeabi-v7a": "arm-linux-androideabi",
    "x86": "i686-linux-android",
}


def parse_needed(text: str) -> list[str]:
    """The NEEDED entries of `llvm-readelf -d` output: the shared libraries a library links."""
    return [match.group("name") for match in NEEDED.finditer(text)]


def missing_dependencies(needed: dict[str, list[str]], system: set[str]) -> list[str]:
    """Each library a packaged library links that is neither in the package (the same ABI directory) nor provided by
    Android. Such a package installs and then stops at launch ("dlopen failed: library ... not found")."""
    problems = []
    packaged: dict[str, set[str]] = {}
    for path in needed:
        directory, _, name = path.rpartition("/")
        packaged.setdefault(directory, set()).add(name)
    for path, names in sorted(needed.items()):
        directory = path.rpartition("/")[0]
        for name in names:
            if name not in packaged[directory] and name not in system:
                problems.append(f"{path} needs {name}, which is neither in the package nor part of Android")
    return problems


def abi_of(path: str) -> str:
    """The ABI directory of a library path such as lib/arm64-v8a/libx.so or base/lib/arm64-v8a/libx.so."""
    return path.rpartition("/")[0].rpartition("/")[2]


def system_libraries(ndk: Path, abi: str, api: int = MIN_SDK) -> set[str]:
    """The libraries Android provides at the minimum API level, from the NDK's sysroot."""
    triple = ABI_TRIPLES.get(abi)
    if triple is None:
        return set()
    folders = sorted(ndk.glob(f"toolchains/llvm/prebuilt/*/sysroot/usr/lib/{triple}/{api}"))
    return {p.name for p in folders[-1].glob("*.so")} if folders else set()


def native_libraries(package: Path) -> list[str]:
    with zipfile.ZipFile(package) as archive:
        return sorted(n for n in archive.namelist() if n.endswith(".so") and "/lib/" in "/" + n)


# What the application loads by name, which no NEEDED entry names: the program itself, the platform plugin Qt starts
# with, and the SQL driver the store opens (Qt picks plugins from the modules the application links, so a module linked
# only through a static library is missed; apps/app/packaging/android.cmake links Qt Sql for this reason).
REQUIRED_STEMS = ("liberror-journaling_", "libplugins_platforms_qtforandroid_", "libplugins_sqldrivers_qsqlite_")


def missing_required(libraries: list[str]) -> list[str]:
    """Each library the application loads by name and the package lacks, for every ABI it has."""
    problems = []
    by_abi: dict[str, set[str]] = {}
    for path in libraries:
        by_abi.setdefault(abi_of(path), set()).add(path.rpartition("/")[2])
    for abi, names in sorted(by_abi.items()):
        for stem in REQUIRED_STEMS:
            if f"{stem}{abi}.so" not in names:
                problems.append(f"{stem}{abi}.so is not in the package for {abi}")
    return problems


def find_tool(base: Path, pattern: str) -> Path:
    found = sorted(base.glob(pattern))
    if not found:
        raise SystemExit(f"error: no {pattern} under {base}")
    return found[-1]


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, check=False)


def check_package(package: Path, sdk: Path, ndk: Path, application_id: str) -> list[str]:
    problems = []
    readelf = find_tool(ndk, "toolchains/llvm/prebuilt/*/bin/llvm-readelf*")
    libraries = native_libraries(package)
    if not libraries:
        problems.append(f"{package.name}: no native library in the package")
    problems += [f"{package.name}: {p}" for p in missing_required(libraries)]
    needed: dict[str, list[str]] = {}
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(package) as archive:
            for name in libraries:
                path = Path(archive.extract(name, tmp))
                result = run([str(readelf), "-Wl", "-d", str(path)])
                if result.returncode != 0:
                    problems.append(f"{name}: llvm-readelf failed: {result.stderr.strip()}")
                    continue
                problems += elf_problems(name, result.stdout, abi_of(name) in SIXTY_FOUR_BIT)
                needed[name] = parse_needed(result.stdout)
    system = set().union(*(system_libraries(ndk, abi) for abi in {abi_of(n) for n in libraries})) if libraries else set()
    if libraries and not system:
        problems.append(f"{package.name}: no system library list in the NDK's sysroot for API {MIN_SDK}")
    else:
        problems += [f"{package.name}: {p}" for p in missing_dependencies(needed, system)]
    if package.suffix == ".apk":
        aapt2 = find_tool(sdk, "build-tools/*/aapt2*")
        result = run([str(aapt2), "dump", "xmltree", "--file", "AndroidManifest.xml", str(package)])
        if result.returncode != 0:
            problems.append(f"{package.name}: aapt2 could not read the manifest: {result.stderr.strip()}")
        else:
            problems += [f"{package.name}: {p}" for p in manifest_problems(result.stdout, application_id)]
        zipalign = find_tool(sdk, "build-tools/*/zipalign*")
        result = run([str(zipalign), "-c", "-P", "16", "4", str(package)])
        if result.returncode != 0:
            problems.append(f"{package.name}: zipalign -c -P 16 4 failed: the libraries are not 16 KB aligned in the zip")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sdk", required=True, type=Path, help="the Android SDK (ANDROID_SDK_ROOT)")
    parser.add_argument("--ndk", required=True, type=Path, help="the NDK (ANDROID_NDK_ROOT)")
    parser.add_argument("--application-id", default=APPLICATION_ID)
    parser.add_argument("packages", nargs="+", type=Path, help=".apk or .aab files")
    args = parser.parse_args(argv)
    problems = []
    for package in args.packages:
        if not package.is_file():
            problems.append(f"{package}: no such file")
            continue
        found = check_package(package, args.sdk, args.ndk, args.application_id)
        problems += found
        if not found:
            print(f"{package.name}: ok ({len(native_libraries(package))} native libraries)")
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
