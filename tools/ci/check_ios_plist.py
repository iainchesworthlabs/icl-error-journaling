#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the iOS property list template against what the code and the plan declare.

    python3 tools/ci/check_ios_plist.py [--root .]

Parses apps/app/packaging/ios/Info.plist.in with the CMake placeholders stubbed and checks:

  * the exported type's identifier, description and extension equal the constants in
    libs/ios_bridge/include/error_journaling/document_types.hpp;
  * the exported type conforms to public.data and not to public.json (the system would preview a JSON file instead of
    opening the application from AirDrop);
  * a document type names the exported type (role Viewer, handler rank Owner) and one names public.json (rank Alternate);
  * LSSupportsOpeningDocumentsInPlace is false, UIFileSharingEnabled is absent or false;
  * no usage-description key (the application asks for no permission);
  * CADisableMinimumFrameDurationOnPhone is true, so that a ProMotion phone can run the application at 120 Hz
    (docs/plan/stage-4.md, U7 and package 4.14);
  * the launch screen: UILaunchScreen names a colour and an image that exist in the asset catalogue, each with a light and
    a dark variant, the image ignores the safe area, the PNGs are 1206 x 2622 px (402 x 874 pt at 3x) and opaque, and the
    colour is the `background` token in both modes.

It also checks the privacy manifest, apps/app/packaging/ios/PrivacyInfo.xcprivacy (docs/plan/stage-11.md, task 11.2):
no tracking, no tracking domains, no collected data types, and each accessed API category one of Apple's with reason
codes of that category. packaging/ios/verify_app.sh checks the built executable's symbols against it.

It also checks the orientations (docs/plan/stage-8.md, "Info.plist and packaging"): the iPhone is portrait only, the iPad
has all four, there is no UIRequiresFullScreen, and multiple scenes are not turned on; and that the launch image is an
iPhone image (an iPad shows the background colour alone).

A later package adds the entitlements. docs/plan/stage-5.md, "CI changes outside the test suites".
"""

from __future__ import annotations

import argparse
import json
import plistlib
import re
import struct
import sys
from pathlib import Path

PLIST = Path("apps/app/packaging/ios/Info.plist.in")
PRIVACY_MANIFEST = Path("apps/app/packaging/ios/PrivacyInfo.xcprivacy")
# Apple's required-reason API categories and the reason codes an application (not a third-party SDK) may give for each.
# 0A2A.1 and the other wrapper reasons are for an SDK's own manifest; Qt is static here, so the application is the caller.
PRIVACY_REASONS = {
    "NSPrivacyAccessedAPICategoryFileTimestamp": {"DDA9.1", "C617.1", "3B52.1"},
    "NSPrivacyAccessedAPICategorySystemBootTime": {"35F9.1", "8FFB.1", "3D61.1"},
    "NSPrivacyAccessedAPICategoryDiskSpace": {"85F4.1", "E174.1", "7D9E.1", "B728.1"},
    "NSPrivacyAccessedAPICategoryActiveKeyboards": {"3EC4.1", "54BD.1"},
    "NSPrivacyAccessedAPICategoryUserDefaults": {"CA92.1", "1C8F.1", "AC6B.1"},
}
CATALOGUE = Path("apps/app/packaging/ios/Assets.xcassets")
TOKENS = Path("docs/design/tokens.json")
LAUNCH_IMAGE_SIZE = (1206, 2622)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
# The PNG colour types without an alpha channel: grey, RGB and palette (a palette can still carry a tRNS chunk).
OPAQUE_COLOUR_TYPES = (0, 2, 3)
CONSTANTS = Path("libs/ios_bridge/include/error_journaling/document_types.hpp")
FRAME_RATE_KEY = "CADisableMinimumFrameDurationOnPhone"
PORTRAIT = "UIInterfaceOrientationPortrait"
IPAD_ORIENTATIONS = {
    PORTRAIT,
    "UIInterfaceOrientationPortraitUpsideDown",
    "UIInterfaceOrientationLandscapeLeft",
    "UIInterfaceOrientationLandscapeRight",
}
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
        conforms = declaration.get("UTTypeConformsTo", [])
        if "public.data" not in conforms:
            problems.append("the exported type must conform to public.data")
        if "public.json" in conforms:
            problems.append(
                "the exported type must not conform to public.json (the system previews JSON, and an AirDropped setup file "
                "would open as text instead of in the application)"
            )
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
    if plist.get(FRAME_RATE_KEY) is not True:
        problems.append(f"{FRAME_RATE_KEY} must be true (without it a ProMotion phone runs the application at 60 Hz)")
    for key in sorted(plist):
        if key.endswith("UsageDescription") and key not in ALLOWED_USAGE_KEYS:
            problems.append(f"{key} is set: the application asks for no permission except {sorted(ALLOWED_USAGE_KEYS)}")
    # Dictation (NOTE-05): the two permissions it asks for at the first tap, each with the text the system shows.
    for key in sorted(ALLOWED_USAGE_KEYS):
        value = plist.get(key)
        if not isinstance(value, str) or not value.strip():
            problems.append(f"{key} must be a non-empty text (dictation asks for it at the first tap)")
    return problems


# The only permissions the application asks for: the microphone and speech recognition, for dictation (PRD NOTE-05, NOTE-07).
ALLOWED_USAGE_KEYS = frozenset({"NSMicrophoneUsageDescription", "NSSpeechRecognitionUsageDescription"})


def check_orientations(plist: dict) -> list[str]:
    """The iPhone is portrait only; the iPad turns to all four orientations, is not full screen only and has one scene."""
    problems = []
    phone = plist.get("UISupportedInterfaceOrientations")
    if phone != [PORTRAIT]:
        problems.append(f"UISupportedInterfaceOrientations is {phone}, expected [{PORTRAIT!r}] (the phone is portrait only)")
    tablet = plist.get("UISupportedInterfaceOrientations~ipad")
    if not isinstance(tablet, list) or len(tablet) != 4 or set(tablet) != IPAD_ORIENTATIONS:
        problems.append("UISupportedInterfaceOrientations~ipad must list all four orientations once each (App Store Connect asks for them)")
    if "UIRequiresFullScreen" in plist or "UIRequiresFullScreen~ipad" in plist:
        problems.append("UIRequiresFullScreen must be absent: the iPad application runs in Split View, Slide Over and Stage Manager")
    if plist.get("UIApplicationSupportsMultipleScenes") not in (None, False):
        problems.append("UIApplicationSupportsMultipleScenes must be absent or false: the application has one window")
    return problems


def is_dark(entry: dict) -> bool:
    """Whether an entry of a catalogue's Contents.json is the dark appearance."""
    return any(a.get("appearance") == "luminosity" and a.get("value") == "dark" for a in entry.get("appearances", []))


def split_appearances(entries: list, kind: str, problems: list[str]) -> tuple[dict | None, dict | None]:
    """The light and the dark entry of a colour set or an image set; a problem is added if either is not there once."""
    light = [e for e in entries if not e.get("appearances")]
    dark = [e for e in entries if is_dark(e)]
    if len(light) != 1:
        problems.append(f"{kind} must have exactly one light entry, it has {len(light)}")
    if len(dark) != 1:
        problems.append(f"{kind} must have exactly one dark entry (luminosity dark), it has {len(dark)}")
    if len(light) + len(dark) != len(entries):
        problems.append(f"{kind} has an entry that is neither light nor dark")
    return (light[0] if len(light) == 1 else None, dark[0] if len(dark) == 1 else None)


def colour_value(entry: dict) -> str | None:
    """The sRGB colour of a colour set entry as #RRGGBB. Components are hex ("0xF2") or fractions ("0.949")."""
    components = entry.get("color", {}).get("components", {})
    channels = []
    for name in ("red", "green", "blue"):
        text = str(components.get(name, ""))
        try:
            value = int(text, 16) if text.lower().startswith("0x") else round(float(text) * 255)
        except ValueError:
            return None
        if not 0 <= value <= 255:
            return None
        channels.append(value)
    try:
        if float(components.get("alpha", "1")) != 1.0:
            return None
    except ValueError:
        return None
    return "#{:02X}{:02X}{:02X}".format(*channels)


def png_facts(data: bytes) -> tuple[tuple[int, int], int, bool] | None:
    """(width, height), the colour type and whether a tRNS chunk is there, read from a PNG's chunks; None if not a PNG."""
    if not data.startswith(PNG_SIGNATURE) or data[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", data[16:24])
    colour_type = data[25]
    transparent = False
    offset = len(PNG_SIGNATURE)
    while offset + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[offset : offset + 8])
        if kind == b"tRNS":
            transparent = True
        if kind == b"IEND":
            break
        offset += 12 + length
    return (width, height), colour_type, transparent


def check_colour_set(catalogue: Path, name: str, tokens: dict) -> list[str]:
    path = catalogue / f"{name}.colorset" / "Contents.json"
    if not path.is_file():
        return [f"the colour {name} is not in the asset catalogue ({path})"]
    problems: list[str] = []
    entries = json.loads(path.read_text(encoding="utf-8")).get("colors", [])
    light, dark = split_appearances(entries, f"{name}.colorset", problems)
    for entry, mode in ((light, "light"), (dark, "dark")):
        wanted = str(tokens.get("colour", {}).get(mode, {}).get("background", "")).upper()
        if entry is not None and colour_value(entry) != wanted:
            problems.append(f"{name}.colorset {mode} is {colour_value(entry)}, the background token is {wanted}")
    return problems


def check_image_set(catalogue: Path, name: str) -> list[str]:
    folder = catalogue / f"{name}.imageset"
    path = folder / "Contents.json"
    if not path.is_file():
        return [f"the image {name} is not in the asset catalogue ({path})"]
    problems: list[str] = []
    entries = json.loads(path.read_text(encoding="utf-8")).get("images", [])
    light, dark = split_appearances(entries, f"{name}.imageset", problems)
    for entry in entries:
        # The picture is the phone's Log screen; one picture cannot fit the iPad's two orientations and its window sizes, so
        # an iPad launches on the background colour alone (docs/plan/stage-8.md, "Launch screen on iPad").
        if entry.get("idiom") != "iphone":
            problems.append(f"{name}.imageset has an image with the idiom '{entry.get('idiom')}', every one must be 'iphone'")
    for entry, mode in ((light, "light"), (dark, "dark")):
        if entry is None:
            continue
        if entry.get("scale") != "3x":
            problems.append(f"{name}.imageset {mode} must be the 3x image")
        filename = entry.get("filename")
        if not filename or not (folder / filename).is_file():
            problems.append(f"{name}.imageset {mode} names a file that is not there: {filename}")
            continue
        facts = png_facts((folder / filename).read_bytes())
        if facts is None:
            problems.append(f"{filename} is not a PNG")
            continue
        size, colour_type, transparent = facts
        if size != LAUNCH_IMAGE_SIZE:
            problems.append(f"{filename} is {size[0]} x {size[1]} px, expected {LAUNCH_IMAGE_SIZE[0]} x {LAUNCH_IMAGE_SIZE[1]}")
        if colour_type not in OPAQUE_COLOUR_TYPES or transparent:
            problems.append(f"{filename} is not opaque (it has an alpha channel or transparency)")
    return problems


def check_launch_screen(plist: dict, catalogue: Path, tokens: dict) -> list[str]:
    """UILaunchScreen against the asset catalogue and the background token."""
    launch = plist.get("UILaunchScreen")
    if not isinstance(launch, dict):
        return ["UILaunchScreen is missing (iOS would run the program in a legacy window)"]
    problems = []
    colour_name = launch.get("UIColorName")
    image_name = launch.get("UIImageName")
    if not colour_name:
        problems.append("UILaunchScreen names no UIColorName")
    if not image_name:
        problems.append("UILaunchScreen names no UIImageName")
    if launch.get("UIImageRespectsSafeAreaInsets") is not False:
        problems.append("UIImageRespectsSafeAreaInsets must be false: the image fills the screen under the status bar")
    if colour_name:
        problems += check_colour_set(catalogue, colour_name, tokens)
    if image_name:
        problems += check_image_set(catalogue, image_name)
    return problems


def check_privacy_manifest(manifest: dict) -> list[str]:
    """The manifest declares no tracking and no collection, and gives valid reasons for each accessed API category."""
    problems = []
    if manifest.get("NSPrivacyTracking") is not False:
        problems.append("PrivacyInfo.xcprivacy: NSPrivacyTracking must be false (the application tracks no one)")
    if manifest.get("NSPrivacyTrackingDomains", []) != []:
        problems.append("PrivacyInfo.xcprivacy: NSPrivacyTrackingDomains must be empty (the application has no network access)")
    if manifest.get("NSPrivacyCollectedDataTypes", []) != []:
        problems.append("PrivacyInfo.xcprivacy: NSPrivacyCollectedDataTypes must be empty (the application collects no data)")
    for key in ("NSPrivacyTracking", "NSPrivacyTrackingDomains", "NSPrivacyCollectedDataTypes", "NSPrivacyAccessedAPITypes"):
        if key not in manifest:
            problems.append(f"PrivacyInfo.xcprivacy: {key} is missing")
    seen = set()
    for entry in manifest.get("NSPrivacyAccessedAPITypes", []):
        category = entry.get("NSPrivacyAccessedAPIType")
        reasons = entry.get("NSPrivacyAccessedAPITypeReasons", [])
        if category not in PRIVACY_REASONS:
            problems.append(f"PrivacyInfo.xcprivacy: {category!r} is not a required-reason API category")
            continue
        if category in seen:
            problems.append(f"PrivacyInfo.xcprivacy: {category} is declared twice")
        seen.add(category)
        if not reasons:
            problems.append(f"PrivacyInfo.xcprivacy: {category} gives no reason")
        for reason in reasons:
            if reason not in PRIVACY_REASONS[category]:
                problems.append(f"PrivacyInfo.xcprivacy: {reason!r} is not an application's reason for {category}")
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
    problems = check(plist, read_constants(header_path.read_text(encoding="utf-8"))) + check_orientations(plist)
    manifest_path = root / PRIVACY_MANIFEST
    if not manifest_path.is_file():
        problems.append(f"{manifest_path} not found")
    else:
        try:
            problems += check_privacy_manifest(plistlib.loads(manifest_path.read_bytes()))
        except Exception as error:  # a malformed manifest is a finding, not a crash
            problems.append(f"{PRIVACY_MANIFEST} is not a valid property list: {error}")
    tokens_path = root / TOKENS
    if not tokens_path.is_file():
        return problems + [f"{tokens_path} not found"]
    tokens = json.loads(tokens_path.read_text(encoding="utf-8"))
    return problems + check_launch_screen(plist, root / CATALOGUE, tokens)


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
