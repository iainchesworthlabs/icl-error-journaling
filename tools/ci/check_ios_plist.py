#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the iOS property list template against what the code and the plan declare.

    python3 tools/ci/check_ios_plist.py [--root .]

Parses apps/app/packaging/ios/Info.plist.in with the CMake placeholders stubbed and checks:

  * the exported type's identifier, description and extension equal the constants in
    libs/ios_bridge/include/error_journaling/document_types.hpp;
  * a document type names the exported type (role Viewer, handler rank Owner) and one names public.json (rank Alternate);
  * LSSupportsOpeningDocumentsInPlace is false, UIFileSharingEnabled is absent or false;
  * no usage-description key (the application asks for no permission).

Later packages add the launch screen and the entitlements. docs/plan/stage-5.md, "CI changes outside the test suites".
"""

from __future__ import annotations

import argparse
import plistlib
import re
import sys
from pathlib import Path

PLIST = Path("apps/app/packaging/ios/Info.plist.in")
CONSTANTS = Path("libs/ios_bridge/include/error_journaling/document_types.hpp")
CONSTANT_NAMES = ("CONFIG_TYPE_IDENTIFIER", "CONFIG_TYPE_DESCRIPTION", "CONFIG_EXTENSION")


def read_constants(header: str) -> dict[str, str]:
    """The string_view constants of document_types.hpp by name."""
    found = {}
    for name in CONSTANT_NAMES:
        match = re.search(rf'\b{name}\s*=\s*"([^"]*)"', header)
        if match:
            found[name] = match.group(1)
    return found


def parse_plist(text: str) -> dict:
    """Info.plist.in with each CMake placeholder replaced by a stand-in string."""
    return plistlib.loads(re.sub(r"\$\{[^}]*\}", "x", text).encode("utf-8"))


def check(plist: dict, constants: dict[str, str]) -> list[str]:
    problems = []
    for name in CONSTANT_NAMES:
        if name not in constants:
            problems.append(f"document_types.hpp does not define {name}")
    if problems:
        return problems

    identifier = constants["CONFIG_TYPE_IDENTIFIER"]
    exported = plist.get("UTExportedTypeDeclarations")
    if not isinstance(exported, list) or len(exported) != 1:
        problems.append("UTExportedTypeDeclarations must hold exactly the one exported type")
    else:
        declaration = exported[0]
        if declaration.get("UTTypeIdentifier") != identifier:
            problems.append(f"the exported type is '{declaration.get('UTTypeIdentifier')}', expected '{identifier}'")
        if declaration.get("UTTypeDescription") != constants["CONFIG_TYPE_DESCRIPTION"]:
            problems.append("the exported type's description differs from CONFIG_TYPE_DESCRIPTION")
        if "public.json" not in declaration.get("UTTypeConformsTo", []):
            problems.append("the exported type must conform to public.json")
        extensions = declaration.get("UTTypeTagSpecification", {}).get("public.filename-extension", [])
        if extensions != [constants["CONFIG_EXTENSION"]]:
            problems.append(f"the exported type's extension is {extensions}, expected ['{constants['CONFIG_EXTENSION']}']")

    document_types = plist.get("CFBundleDocumentTypes")
    if not isinstance(document_types, list):
        problems.append("CFBundleDocumentTypes is missing")
    else:
        by_type = {}
        for entry in document_types:
            for content_type in entry.get("LSItemContentTypes", []):
                by_type[content_type] = entry
        for content_type, rank in ((identifier, "Owner"), ("public.json", "Alternate")):
            entry = by_type.get(content_type)
            if entry is None:
                problems.append(f"no document type names {content_type}")
                continue
            if entry.get("CFBundleTypeRole") != "Viewer":
                problems.append(f"the document type for {content_type} must have the role Viewer")
            if entry.get("LSHandlerRank") != rank:
                problems.append(f"the document type for {content_type} must have the handler rank {rank}")

    if plist.get("LSSupportsOpeningDocumentsInPlace") is not False:
        problems.append("LSSupportsOpeningDocumentsInPlace must be false (the system copies the file into the sandbox)")
    if plist.get("UIFileSharingEnabled") not in (None, False):
        problems.append("UIFileSharingEnabled must be absent or false (it would list the Documents folder in Files)")
    for key in sorted(plist):
        if key.endswith("UsageDescription"):
            problems.append(f"{key} is set: the application asks for no permission")
    return problems


def run(root: Path) -> list[str]:
    plist_path = root / PLIST
    header_path = root / CONSTANTS
    for path in (plist_path, header_path):
        if not path.is_file():
            return [f"{path} not found"]
    try:
        plist = parse_plist(plist_path.read_text(encoding="utf-8"))
    except Exception as error:  # a malformed template is a finding, not a crash
        return [f"{PLIST} is not a valid property list: {error}"]
    return check(plist, read_constants(header_path.read_text(encoding="utf-8")))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent.parent))
    args = parser.parse_args(argv)
    problems = run(Path(args.root))
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
