#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Work out which third-party components the application really links, and write their notices.

    python -I tools/licences/notices.py --binary-dir build/ios-release --source-dir . --target ej_app \\
        --platform ios --qt-prefix ~/Qt/6.12.0/ios --qt-version 6.12.0 --out-dir build/ios-release/about --strict

Run by the build (the target `ej_notices`, cmake/EjAbout.cmake) before the application is linked, and by the test
`about.notices`. It writes to --out-dir

  third-party-notices.json   the components, their licence texts and the link inputs it classified. The application embeds
                             it as the Qt resource :/about/third-party-notices.json and the About pages show it.
  THIRD-PARTY-NOTICES.txt    the same, as plain text.

Where each fact comes from, so nothing is typed twice:

  what is linked   the CMake file API (the codemodel's resolved link line of the target, written at configure time), so
                   every static or shared library on the real command line for this platform and configuration
  vcpkg ports      the installed tree: vcpkg/status (version, dependencies), vcpkg/info/*.list (which port owns a file) and
                   share/<port>/copyright (the licence text). A port that no link input reaches is not listed; one that is
                   installed, declared in vcpkg.json and not in `testOnlyPorts` of data/manifest.json is an error.
  Qt               data/qt-modules.json, a snapshot of the SBOMs that Qt ships in its kits (qt_sbom_manifest.py), including
                   the bundled third-party libraries each module contains (FreeType, HarfBuzz, libpng, PCRE2, SQLite ...)
  the rest         data/manifest.json (hand-kept: the project, test-only ports) and data/texts/<SPDX id>.txt

Anything it cannot account for is listed under "unresolved" in the JSON and printed. With --strict the exit status is 1 then,
so the build stops, and with --check nothing is written. Nothing is guessed: a library under the Qt prefix that the snapshot
does not know is a failure to look at.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
SCHEMA = 1
COMMERCIAL = "LicenseRef-Qt-Commercial"
SYSTEM_PATH = re.compile(
    r"windows kits|visual studio|/usr/lib|/usr/local/lib|^/lib|/system/library|/library/developer|xcode|/ndk/|sysroot|"
    r"c:/windows/|/platforms/ios|iphoneos|iphonesimulator|\.sdk/"
)
STATIC_SUFFIXES = (".a", ".o", ".obj")
DYNAMIC_SUFFIXES = (".dylib", ".so", ".dll", ".tbd")


# ---------------------------------------------------------------------------------------------------------------------
# small helpers


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def read_text(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n").replace("\r", "\n")
    return text.strip("\n") + "\n"


def slash(path: str | Path) -> str:
    return str(path).replace("\\", "/")


def lower_path(path: str | Path) -> str:
    return re.sub(r"([^/])/+", r"\1/", slash(path)).lower()


def spdx_ids(expression: str) -> list[str]:
    """The licence ids in an SPDX expression, in order: "A OR B WITH X" -> A, B, X."""
    cleaned = re.sub(r"[()]", " ", expression)
    parts = re.split(r"\s+(?:AND|OR|WITH)\s+", cleaned.strip())
    seen: list[str] = []
    for part in parts:
        part = part.strip()
        if part and part not in seen:
            seen.append(part)
    return seen


def file_key(path: str) -> str:
    """The key of qt-modules.json "files" for a library (the same normalisation as qt_sbom_manifest.file_key)."""
    name = slash(path).rsplit("/", 1)[-1].lower()
    name = re.sub(r"\.(lib|a|dylib|dll|framework|so(\.[0-9]+)*)$", "", name)
    name = re.sub(r"^lib", "", name)
    name = re.sub(r"_(arm64-v8a|armeabi-v7a|x86_64|x86)$", "", name)
    return name


def display_licence(expression: str) -> str:
    return expression.replace(COMMERCIAL, "Qt commercial licence")


def looks_like_library(lowered: str) -> bool:
    """Whether a path on the link line names a library or an object file, as opposed to a directory or a flag's argument."""
    name = lowered.rsplit("/", 1)[-1]
    return (lowered.endswith(STATIC_SUFFIXES + DYNAMIC_SUFFIXES + (".lib",)) or ".so." in name or ".framework/" in lowered
            or ".dylib." in name)


def linking_of(token: str) -> str:
    """static, dynamic or unknown, from the name of a library and, when the name does not say, from the file itself.

    `.a` and object files are static, `.dylib`, `.so` and `.dll` dynamic. A Qt framework on an iPhone is a file inside
    `QtCore.framework` that is a static archive, so those are read: an archive (`!<arch>`) is static, a Mach-O dynamic library
    (file type 6) is dynamic. A Windows `.lib` may be a static library or the import library of a DLL and is not guessed at.
    """
    lowered = token.lower()
    if lowered.endswith(STATIC_SUFFIXES):
        return "static"
    if lowered.endswith(DYNAMIC_SUFFIXES) or ".so." in lowered.rsplit("/", 1)[-1] or ".dylib." in lowered.rsplit("/", 1)[-1]:
        return "dynamic"
    if lowered.endswith(".lib"):
        return "unknown"
    try:
        with open(token, "rb") as handle:
            header = handle.read(16)
            if header[:4] in (bytes([0xCA, 0xFE, 0xBA, 0xBE]), bytes([0xCA, 0xFE, 0xBA, 0xBF])):
                # A universal binary: read the first of its slices.
                handle.seek(0)
                fat = handle.read(32)
                offset = int.from_bytes(fat[16:20], "big") if header[3] == 0xBE else int.from_bytes(fat[16:24], "big")
                handle.seek(offset)
                header = handle.read(16)
    except OSError:
        return "unknown"
    if header.startswith(b"!<arch>\n"):
        return "static"
    if header[:4] in (bytes([0xCF, 0xFA, 0xED, 0xFE]), bytes([0xCE, 0xFA, 0xED, 0xFE])):
        file_type = int.from_bytes(header[12:16], "little")
        return "dynamic" if file_type == 6 else "static" if file_type == 1 else "unknown"
    if header[:4] == bytes([0x7F, 0x45, 0x4C, 0x46]):
        file_type = int.from_bytes(header[16:18], "little")
        return "dynamic" if file_type == 3 else "static" if file_type == 1 else "unknown"
    return "unknown"


# ---------------------------------------------------------------------------------------------------------------------
# 1. the link line, from the CMake file API


def tokenise(fragment: str) -> list[str]:
    tokens = []
    for raw in re.findall(r'"[^"]*"|\S+', fragment):
        tokens.append(raw[1:-1] if len(raw) >= 2 and raw[0] == '"' and raw[-1] == '"' else raw)
    return tokens


def read_link_line(binary_dir: Path, target: str, config: str, problems: list[str]) -> tuple[list[str], Path | None]:
    """The tokens of the target's link line and the target's own build directory (Xcode gives paths relative to it)."""
    reply = binary_dir / ".cmake" / "api" / "v1" / "reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        problems.append(
            f"No CMake file API reply under {reply}: the link line of {target} cannot be read "
            "(cmake_file_api(QUERY ...) is issued by cmake/EjAbout.cmake at configure time)"
        )
        return [], None
    index = read_json(indexes[-1])
    codemodel_name = index.get("reply", {}).get("codemodel-v2", {}).get("jsonFile")
    if not codemodel_name or not (reply / codemodel_name).is_file():
        problems.append("The file API reply has no codemodel-v2 object")
        return [], None
    codemodel = read_json(reply / codemodel_name)
    configurations = codemodel.get("configurations", [])
    chosen = [c for c in configurations if not config or c.get("name") == config] or configurations
    target_file = None
    for configuration in chosen:
        for entry in configuration.get("targets", []):
            if entry.get("name") == target:
                target_file = entry.get("jsonFile")
                break
        if target_file:
            break
    if not target_file or not (reply / target_file).is_file():
        problems.append(f"Target {target} was not found in the file API codemodel")
        return [], None
    description = read_json(reply / target_file)
    build_rel = description.get("paths", {}).get("build", ".")
    target_build_dir = binary_dir / build_rel if build_rel not in ("", ".") else binary_dir
    tokens: list[str] = []
    for fragment in description.get("link", {}).get("commandFragments", []):
        if fragment.get("role") in ("libraries", "flags"):
            tokens.extend(tokenise(fragment.get("fragment", "")))
    if not tokens:
        problems.append(f"The link line of {target} in the file API reply is empty, so nothing could be checked")
    return tokens, target_build_dir


# ---------------------------------------------------------------------------------------------------------------------
# 2. vcpkg


class Vcpkg:
    """What the installed tree of a triplet says: packages, versions, dependencies and which file belongs to which."""

    def __init__(self, triplet_dir: Path | None):
        self.m_Dir = triplet_dir if triplet_dir and triplet_dir.is_dir() else None
        self.m_Versions: dict[str, str] = {}
        self.m_Depends: dict[str, list[str]] = {}
        self.m_Owner: dict[str, str] = {}
        if self.m_Dir is None:
            return
        root = self.m_Dir.parent
        triplet = self.m_Dir.name
        status = root / "vcpkg" / "status"
        if status.is_file():
            for block in re.split(r"\n\s*\n", status.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")):
                if not re.search(r"Status: \w+ ok installed", block):
                    continue
                name = re.search(r"^Package: (\S+)", block, re.M)
                if not name:
                    continue
                port = name.group(1)
                arch = re.search(r"^Architecture: (\S+)", block, re.M)
                if arch and arch.group(1) != triplet:
                    continue
                depends = re.search(r"^Depends: (.+)$", block, re.M)
                dependencies = []
                if depends:
                    dependencies = [re.sub(r"[:(\[].*$", "", d).strip() for d in depends.group(1).split(",")]
                if re.search(r"^Feature: ", block, re.M):
                    self.m_Depends.setdefault(port, []).extend(d for d in dependencies if d)
                    continue
                version = re.search(r"^Version: (\S+)", block, re.M)
                port_version = re.search(r"^Port-Version: (\d+)", block, re.M)
                text = version.group(1) if version else ""
                if port_version and port_version.group(1) != "0":
                    text += "#" + port_version.group(1)
                self.m_Versions[port] = text
                self.m_Depends.setdefault(port, []).extend(d for d in dependencies if d)
        info = root / "vcpkg" / "info"
        if info.is_dir():
            for listing in info.glob(f"*_{triplet}.list"):
                port = listing.name.split("_", 1)[0]
                for line in listing.read_text(encoding="utf-8", errors="replace").splitlines():
                    line = line.strip()
                    if line.startswith(triplet + "/") and not line.endswith("/"):
                        self.m_Owner[line[len(triplet) + 1:].lower()] = port

    def owner_of(self, relative: str) -> str:
        return self.m_Owner.get(relative.lower(), "")

    def ships_files(self, port: str) -> bool:
        """Whether the port installs anything outside share/ (a library, a header, a program)."""
        return any(owner == port and not relative.startswith("share/") for relative, owner in self.m_Owner.items())

    def has_copyright(self, port: str) -> bool:
        return self.m_Dir is not None and (self.m_Dir / "share" / port / "copyright").is_file()

    def copyright_text(self, port: str) -> str:
        return read_text(self.m_Dir / "share" / port / "copyright") if self.has_copyright(port) else ""

    def installed_ports(self) -> list[str]:
        return sorted(self.m_Versions)

    def closure(self, roots: list[str]) -> list[str]:
        seen: list[str] = []
        work = list(roots)
        while work:
            port = work.pop(0)
            if port in seen or port not in self.m_Versions:
                continue
            seen.append(port)
            work.extend(self.m_Depends.get(port, []))
        return seen


def manifest_dependencies(vcpkg_json: Path) -> list[str]:
    """The names a vcpkg.json asks for, in its dependencies and in its features (host tools left out)."""
    if not vcpkg_json.is_file():
        return []
    document = read_json(vcpkg_json)
    names: list[str] = []

    def collect(entries) -> None:
        for entry in entries or []:
            if isinstance(entry, str):
                names.append(entry)
            elif not entry.get("host"):
                names.append(entry["name"])

    collect(document.get("dependencies"))
    for feature in (document.get("features") or {}).values():
        collect(feature.get("dependencies"))
    return names


# ---------------------------------------------------------------------------------------------------------------------
# the generator


class Notices:
    def __init__(self, args: argparse.Namespace):
        self.m_Args = args
        self.m_Unresolved: list[str] = []
        self.m_Warnings: list[str] = []
        self.m_Manifest = read_json(Path(args.manifest))
        self.m_Qt = read_json(Path(args.qt_modules))
        self.m_Texts_Dir = Path(args.texts)
        self.m_Licenses_Dir = Path(args.licenses_dir) if args.licenses_dir else Path(args.source_dir) / "LICENSES"
        self.m_Source = Path(args.source_dir)
        self.m_Binary = Path(args.binary_dir)
        self.m_Qt_Prefix = lower_path(args.qt_prefix) if args.qt_prefix else ""
        self.m_Platform = args.platform
        self.m_Vcpkg = Vcpkg(Path(args.vcpkg_triplet_dir) if args.vcpkg_triplet_dir else None)
        self.m_Linked: list[dict] = []
        self.m_Qt_Roots: list[str] = []
        self.m_Qt_Linking: dict[str, set[str]] = {}
        self.m_Ports: list[str] = []
        self.m_Port_Linking: dict[str, set[str]] = {}
        self.m_Components: list[dict] = []
        self.m_Texts: dict[str, str] = {}

    # -- problems ---------------------------------------------------------------------------------------------------

    def unresolved(self, message: str) -> None:
        if message not in self.m_Unresolved:
            self.m_Unresolved.append(message)

    def warning(self, message: str) -> None:
        if message not in self.m_Warnings:
            self.m_Warnings.append(message)

    # -- Qt lookups ------------------------------------------------------------------------------------------------

    def qt_lookup(self, key: str) -> str:
        """The component id of qt-modules.json for the stem of a library, or ''."""
        files = self.m_Qt["files"]
        aliases = self.m_Manifest.get("qt", {}).get("fileAliases", {})
        candidates = [key]
        for stem in (key, re.sub(r"(_debug|_init)$", "", key), re.sub(r"d$", "", key)):
            if stem not in candidates:
                candidates.append(stem)
        for candidate in list(candidates):
            match = re.match(r"^qt([a-z].*)$", candidate)
            if match:
                candidates.append("qt6" + match.group(1))
            stripped = re.sub(r"(_debug|_init)$", "", candidate)
            if stripped not in candidates:
                candidates.append(stripped)
        for candidate in candidates:
            if candidate in files:
                return files[candidate]
            if candidate in aliases:
                return aliases[candidate]
        for prefix, component in self.m_Manifest.get("qt", {}).get("filePrefixes", {}).items():
            if not prefix.startswith("_") and key.startswith(prefix):
                return component
        return ""

    def qt_object_key(self, token: str) -> str:
        """A static Qt links objects, not libraries; they say what they belong to by where they sit under the Qt prefix."""
        lowered = lower_path(token)
        relative = lowered[len(self.m_Qt_Prefix) + 1:]
        match = re.match(r"^(.*)/objects-[^/]+/([^/]+)/", relative)
        if not match:
            return ""
        directory, target = match.group(1), match.group(2)
        name = lowered.rsplit("/", 1)[-1]
        candidates: list[str] = []
        init = re.match(r"^(.+)_init\.cpp\.(?:o|obj)$", name)
        if init and not name.startswith("qrc_"):
            candidates += [directory.replace("/", "_") + "_" + init.group(1), init.group(1)]
        resources = re.match(r"^(.+)_resources_[0-9]+$", target)
        if resources:
            base = resources.group(1)
            candidates += [f"qt6{base}", base, f"qt{base}", f"{directory.replace('/', '_')}_{base}plugin",
                           f"{base}plugin", f"{directory.replace('/', '_')}_{base}"]
        for candidate in candidates:
            found = self.qt_lookup(candidate)
            if found:
                return found
        return ""

    # -- 1. classify the link line ---------------------------------------------------------------------------------

    def classify_link_line(self) -> None:
        args = self.m_Args
        problems: list[str] = []
        tokens, target_build_dir = read_link_line(self.m_Binary, args.target, args.config, problems)
        for problem in problems:
            self.unresolved(problem)
        if not tokens and not problems:
            self.unresolved(f"The link line of {args.target} is empty")
        triplet = lower_path(self.m_Vcpkg.m_Dir) if self.m_Vcpkg.m_Dir else ""
        binary = lower_path(self.m_Binary)
        source = lower_path(self.m_Source)
        skip_next = False
        for raw in tokens:
            # -Wl,-force_load,/path/libqios.a is one token on the link line of an Xcode build
            pieces = raw.split(",") if raw.startswith("-Wl,") else [raw]
            for token in pieces:
                if skip_next:
                    skip_next = False
                    key = file_key(token)
                    component = self.qt_lookup(key)
                    if component:
                        self.record(f"-framework {token}", "qt", component, "dynamic")
                        self.m_Qt_Roots.append(component)
                    else:
                        self.record(f"-framework {token}", "system", "", "dynamic")
                    continue
                if token == "-framework":
                    skip_next = True
                    continue
                self.classify_token(token, triplet, binary, source, target_build_dir)
        if tokens and not self.m_Linked:
            self.unresolved(f"The link line of {args.target} names no library, so nothing could be checked")

    def classify_token(self, raw: str, triplet: str, binary: str, source: str, target_build_dir: Path | None) -> None:
        token = slash(raw)
        token = re.sub(r"([^/])/+", r"\1/", token)
        token = re.sub(r"^/(WHOLEARCHIVE|DEFAULTLIB):", "", token, flags=re.I)
        if token.startswith("-l"):
            self.record(token, "system", "", "dynamic")
            return
        if token.startswith("-") or not token or token.startswith("@"):
            return
        if re.match(r"^/[A-Za-z][A-Za-z0-9_]*([:=][^/]*)?$", token):
            return  # an MSVC link flag such as /DEBUG or /machine:x64
        if "/" not in token:
            if token.lower().endswith(STATIC_SUFFIXES + DYNAMIC_SUFFIXES + (".lib",)):
                self.record(token, "system", "", linking_of(token))
            return  # anything else is the argument of a flag ("-arch arm64")
        if not re.match(r"^([A-Za-z]:)?/", token):
            if token.startswith("../") and target_build_dir is not None:
                token = slash(target_build_dir / token)
            else:
                token = slash(self.m_Binary / token)
        if "/../" in token:
            token = slash(Path(token).resolve())
        lowered = lower_path(token)
        if not looks_like_library(lowered):
            return  # a directory (-Wl,-rpath,<dir>) or another argument of a flag
        linking = linking_of(token)

        in_vcpkg = bool(triplet) and lowered.startswith(triplet + "/")
        in_qt = bool(self.m_Qt_Prefix) and lowered.startswith(self.m_Qt_Prefix + "/")
        first_party = (lowered.startswith(binary + "/") or lowered.startswith(source + "/")) and not in_vcpkg and not in_qt
        if first_party:
            self.record(token, "first-party", "", linking)
            return
        if in_vcpkg:
            owner = self.m_Vcpkg.owner_of(lowered[len(triplet) + 1:])
            if not owner:
                self.unresolved(f"Linked library {token} is under the vcpkg tree but no port owns it")
                self.record(token, "unresolved", "", linking)
                return
            self.m_Ports.append(owner)
            self.m_Port_Linking.setdefault(owner, set()).add(linking)
            self.record(token, "vcpkg", owner, linking)
            return
        if in_qt:
            key = file_key(token)
            component = self.qt_lookup(key)
            if not component and lowered.endswith((".o", ".obj")):
                component = self.qt_object_key(token)
            if not component:
                self.unresolved(f"Linked Qt library {token} (key '{key}') is not in tools/licences/data/qt-modules.json")
                self.record(token, "unresolved", "", linking)
                return
            self.m_Qt_Roots.append(component)
            self.m_Qt_Linking.setdefault(component, set()).add(linking)
            self.record(token, "qt", component, linking)
            return
        if SYSTEM_PATH.search(lowered):
            self.record(token, "system", "", linking)
            return
        self.unresolved(f"Linked library {token} is not first-party, not from vcpkg, not Qt and not a known system location")
        self.record(token, "unresolved", "", linking)

    def record(self, token: str, kind: str, component: str, linking: str) -> None:
        entry = {"input": token, "kind": kind, "component": component, "linking": linking}
        if linking == "unknown" and kind == "qt":
            try:
                with open(token, "rb") as handle:
                    entry["header"] = handle.read(16).hex()
            except OSError:
                entry["header"] = "unreadable"
        self.m_Linked.append(entry)

    # -- 2. Qt closure ---------------------------------------------------------------------------------------------

    def qt_closure(self) -> list[str]:
        verified = self.m_Platform in self.m_Qt.get("verifiedPlatforms", [])
        if not verified:
            self.warning(
                f"qt-modules.json was generated from the SBOMs of {self.m_Qt.get('verifiedPlatforms')}; for "
                f"'{self.m_Platform}' every component seen on any of them is listed (conservative)"
            )
        components = self.m_Qt["components"]
        closure: list[str] = []
        work = list(self.m_Qt_Roots)
        while work:
            cid = work.pop(0)
            if cid in closure:
                continue
            record = components.get(cid)
            if record is None:
                self.unresolved(f"Qt component '{cid}' is not in tools/licences/data/qt-modules.json")
                continue
            platforms = record.get("platforms", [])
            if verified and "*" not in platforms and self.m_Platform not in platforms:
                continue
            closure.append(cid)
            work.extend(record.get("depends", []))
            extra = record.get("platformDepends", {})
            if verified:
                work.extend(extra.get(self.m_Platform, []))
            else:
                for names in extra.values():
                    work.extend(names)
        return closure

    # -- 3. components ---------------------------------------------------------------------------------------------

    def add(self, component: dict) -> None:
        component["licenceDisplay"] = display_licence(component["licence"])
        component["exemptIds"] = [i for i in spdx_ids(component["licence"]) if i in self.exemptions()]
        self.m_Components.append(component)

    def build_components(self) -> None:
        qt_version = self.m_Args.qt_version or (self.m_Qt.get("qtVersions") or [""])[0]
        expected = self.m_Manifest.get("qt", {}).get("expectedQtVersion", "")
        if self.m_Args.qt_version and expected and not self.m_Args.qt_version.startswith(expected):
            self.unresolved(
                f"Qt {self.m_Args.qt_version} is being built against but tools/licences/data was written for Qt "
                f"{expected}.x: regenerate qt-modules.json (tools/licences/qt_sbom_manifest.py) and review manifest.json"
            )
        closure = self.qt_closure()
        components = self.m_Qt["components"]
        names = self.m_Manifest.get("qt", {}).get("repoNames", {})

        # Qt itself: one entry for each repository and licence expression, listing the modules and plugins used.
        groups: dict[tuple[str, str], list[str]] = {}
        for cid in closure:
            record = components[cid]
            if record["kind"] in ("module", "plugin") and COMMERCIAL in record["license"]:
                groups.setdefault((record["repo"], record["license"]), []).append(cid)
        by_repo: dict[str, list[tuple[str, str]]] = {}
        for repo, licence in groups:
            by_repo.setdefault(repo, []).append((repo, licence))
        for repo, keys in sorted(by_repo.items()):
            keys.sort(key=lambda k: -len(groups[k]))
            for index, key in enumerate(keys):
                members = sorted({components[c]["name"] for c in groups[key]})
                label = names.get(repo, repo)
                name = label if index == 0 else f"Qt: {', '.join(members)}"
                download = next((components[c]["download"] for c in groups[key] if components[c].get("download")), "")
                identifier = f"qt/{repo}" if index == 0 else f"qt/{repo}/{members[0]}"
                self.add({
                    "id": identifier,
                    "name": name,
                    "version": qt_version,
                    "kind": "qt",
                    "licence": key[1],
                    "copyright": "Copyright (C) The Qt Company Ltd. and other contributors.",
                    "source": self.qt_source(download, qt_version, repo),
                    "linking": self.qt_linking(),
                    "note": "Modules and plugins used: " + ", ".join(members) + ".",
                    "textIds": self.text_ids(key[1]),
                })

        # What Qt bundles and attributes: FreeType, HarfBuzz, libpng, PCRE2, SQLite, zlib and so on.
        seen: set[tuple[str, str, str]] = set()
        bundled: list[tuple[str, str]] = []
        for cid in closure:
            record = components[cid]
            licence = record["license"]
            third_party = record["kind"] in ("bundled", "attribution") or (
                record["kind"] in ("module", "plugin") and COMMERCIAL not in licence and licence)
            if not third_party:
                continue
            display = re.sub(r"^Bundled", "", re.sub(r"^.*_Attribution_", "", record["name"]))
            display = re.sub(r"Private$", "", display)
            key = (display.lower(), record["version"], licence)
            if key in seen:
                continue
            seen.add(key)
            bundled.append((display.lower(), cid))
        for _, cid in sorted(bundled):
            record = components[cid]
            display = re.sub(r"^Bundled", "", re.sub(r"^.*_Attribution_", "", record["name"]))
            display = re.sub(r"Private$", "", display)
            display = self.m_Manifest.get("displayNames", {}).get(display.lower(), display)
            self.add({
                "id": f"qt-3rdparty/{cid}",
                "name": f"{display} (in Qt)",
                "version": record["version"] if record["version"] not in ("", "unknown") else "",
                "kind": "qt-bundled",
                "licence": record["license"],
                "copyright": record.get("copyright", ""),
                "source": record.get("download", ""),
                "linking": self.qt_linking(),
                "note": "Part of " + names.get(record["repo"], record["repo"]) + ".",
                "textIds": self.text_ids(record["license"]),
            })

        # vcpkg ports the link line reached, and their dependencies.
        directly_linked = set(self.m_Ports)
        for port in self.m_Vcpkg.closure(sorted(set(self.m_Ports))):
            if port not in directly_linked and not self.m_Vcpkg.ships_files(port):
                continue  # a build helper (vcpkg-cmake) or a meta port (pthreads): it installs nothing that is linked
            if not self.m_Vcpkg.has_copyright(port):
                self.unresolved(f"vcpkg port {port} is linked but has no share/{port}/copyright")
                continue
            licence = self.m_Manifest.get("portLicences", {}).get(port, "")
            if not licence:
                self.warning(f"vcpkg port {port} has no entry in portLicences of manifest.json; its own text is shown")
            self.add({
                "id": f"vcpkg/{port}",
                "name": port,
                "version": self.m_Vcpkg.m_Versions.get(port, ""),
                "kind": "vcpkg",
                "licence": licence or "see the licence text",
                "copyright": "",
                "source": "",
                "linking": self.linking(self.m_Port_Linking, [port]),
                "note": "",
                "textIds": [],
                "ownText": self.m_Vcpkg.copyright_text(port),
            })

        # Declared runtime dependencies that are installed and not linked.
        linked_ports = set(self.m_Vcpkg.closure(sorted(set(self.m_Ports))))
        test_only = set(self.m_Manifest.get("testOnlyPorts", []))
        for port in manifest_dependencies(self.m_Source / "vcpkg.json"):
            if port in test_only or port in linked_ports or port not in self.m_Vcpkg.m_Versions:
                continue
            self.unresolved(
                f"vcpkg dependency {port} is installed but the application does not link it, and it is not in "
                "testOnlyPorts of tools/licences/data/manifest.json: either it ships (find why the link line misses it) "
                "or it is only a test or build tool (list it)"
            )

    @staticmethod
    def linking(table: dict[str, set[str]], ids: list[str]) -> str:
        """How the library files of these components are linked: static, dynamic, or unknown."""
        found: set[str] = set()
        for identifier in ids:
            found |= table.get(identifier, set())
        found.discard("unknown")
        return "/".join(sorted(found)) if found else "unknown"

    def qt_linking(self) -> str:
        """Every Qt library of one build is linked the same way (a static kit, or a shared one)."""
        return self.linking(self.m_Qt_Linking, list(self.m_Qt_Linking))

    def qt_source(self, download: str, version: str, repo: str) -> str:
        series = ".".join(version.split(".")[:2])
        tarballs = f"https://download.qt.io/official_releases/qt/{series}/{version}/submodules/" if series else ""
        if download:
            return f"{download} ({tarballs})" if tarballs else download
        return tarballs

    def exemptions(self) -> dict[str, str]:
        return {k: v for k, v in self.m_Manifest.get("textExemptions", {}).items() if not k.startswith("_")}

    def text_ids(self, expression: str) -> list[str]:
        exempt = self.exemptions()
        return [i for i in spdx_ids(expression) if i not in exempt]

    # -- 4. licence texts ------------------------------------------------------------------------------------------

    def text_for(self, identifier: str) -> str:
        aliases = self.m_Manifest.get("textAliases", {})
        for name in (identifier, aliases.get(identifier, "")):
            if not name:
                continue
            for directory in (self.m_Texts_Dir, self.m_Licenses_Dir):
                path = directory / f"{name}.txt"
                if path.is_file():
                    return read_text(path)
        return ""

    def collect_texts(self) -> None:
        project = self.m_Manifest["project"]
        wanted: list[tuple[str, str]] = [(i, project["name"]) for i in spdx_ids(project["licence"])]
        for component in self.m_Components:
            wanted.extend((i, component["name"]) for i in component["textIds"])
            if component.get("ownText"):
                self.m_Texts[component["id"]] = component.pop("ownText")
        choices = {c["name"]: c["licence"] for c in self.m_Components}
        choices[project["name"]] = project["licence"]
        for identifier, needed_by in wanted:
            if identifier in self.m_Texts:
                continue
            text = self.text_for(identifier)
            if not text and " OR " in choices.get(needed_by, "") and any(
                    other in self.m_Texts or self.text_for(other) or other in self.exemptions()
                    for other in spdx_ids(choices[needed_by]) if other != identifier):
                # A licence offered as a choice ("AFL-2.1 OR GPL-2.0-or-later") needs the text of one option, which is there.
                self.warning(f"No licence text for {identifier}, one of the options of {needed_by}; another option has its text")
                continue
            if not text:
                message = (f"No licence text for {identifier} (needed by {needed_by}): add "
                           f"tools/licences/data/texts/{identifier}.txt and say where it came from in the README there")
                if self.m_Platform in self.m_Qt.get("verifiedPlatforms", []):
                    self.unresolved(message)
                else:
                    # A platform whose Qt SBOMs were not read lists every component seen on any other (conservative), some
                    # of which are for another system (D-Bus on Linux). Nothing ships from such a build, so it only warns.
                    self.warning(message)
                continue
            self.m_Texts[identifier] = text

    # -- output ----------------------------------------------------------------------------------------------------

    def document(self) -> dict:
        project = dict(self.m_Manifest["project"])
        project["textIds"] = spdx_ids(project["licence"])
        components = sorted(self.m_Components, key=lambda c: (c["kind"] != "qt", c["name"].lower()))
        return {
            "schema": SCHEMA,
            "generator": "tools/licences/notices.py",
            "platform": self.m_Platform,
            "qtVersion": self.m_Args.qt_version,
            "project": project,
            "components": components,
            "texts": dict(sorted(self.m_Texts.items())),
            "exemptions": self.exemptions(),
            "linked": self.m_Linked,
            "unresolved": self.m_Unresolved,
            "warnings": self.m_Warnings,
        }

    def plain_text(self, document: dict) -> str:
        project = document["project"]
        rule = "=" * 80
        lines = [f"Third-party notices for {project['name']}", "",
                 f"{project['name']} itself is licensed under {project['licence']}. Its source: {project['source']}", ""]
        for component in document["components"]:
            lines += [rule, component["name"] + (f" {component['version']}" if component["version"] else ""), rule,
                      f"Licence: {component['licenceDisplay']}", f"Linked: {component['linking']}"]
            if component["copyright"]:
                lines.append(component["copyright"])
            if component["source"]:
                lines.append(f"Source: {component['source']}")
            if component["note"]:
                lines.append(component["note"])
            lines.append("")
        for identifier, text in document["texts"].items():
            lines += [rule, f"Licence text: {identifier}", rule, text.rstrip("\n"), ""]
        return "\n".join(lines) + "\n"


def write_if_changed(path: Path, content: str) -> None:
    if path.is_file() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary-dir", required=True)
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--target", required=True, help="the target whose link line is read")
    parser.add_argument("--platform", required=True, help="ios, macos, windows or linux")
    parser.add_argument("--config", default="", help="the configuration of a multi-config generator")
    parser.add_argument("--qt-prefix", default="")
    parser.add_argument("--qt-version", default="")
    parser.add_argument("--vcpkg-triplet-dir", default="")
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--manifest", default=str(DATA / "manifest.json"))
    parser.add_argument("--qt-modules", default=str(DATA / "qt-modules.json"))
    parser.add_argument("--texts", default=str(DATA / "texts"))
    parser.add_argument("--licenses-dir", default="", help="the repository's LICENSES folder (default: <source-dir>/LICENSES)")
    parser.add_argument("--strict", action="store_true", help="exit 1 when anything is unresolved")
    parser.add_argument("--check", action="store_true", help="write nothing")
    args = parser.parse_args(argv)

    notices = Notices(args)
    notices.classify_link_line()
    notices.build_components()
    notices.collect_texts()
    document = notices.document()

    for problem in document["unresolved"]:
        print(f"notices: UNRESOLVED: {problem}", file=sys.stderr)
    for warning in document["warnings"]:
        print(f"notices: warning: {warning}", file=sys.stderr)
    counts: dict[str, int] = {}
    for item in document["linked"]:
        counts[item["kind"]] = counts.get(item["kind"], 0) + 1
    print(f"notices: {len(document['components'])} components for {args.platform}; link inputs by kind: "
          + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    if not args.check:
        out = Path(args.out_dir)
        write_if_changed(out / "third-party-notices.json", json.dumps(document, indent=1, ensure_ascii=False) + "\n")
        write_if_changed(out / "THIRD-PARTY-NOTICES.txt", notices.plain_text(document))
    if document["unresolved"] and args.strict:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
