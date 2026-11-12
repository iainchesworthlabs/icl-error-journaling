#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the store listing text in store/ against the stores' limits (docs/plan/stage-11.md, task 11.1).

    python3 tools/ci/check_store_text.py [--root .]

The upload (fastlane deliver and supply) sends each text file whole, so each file must hold the value only. It must be
UTF-8 with one final newline, no blank first or last line, and within the store's limit. The limits are the ones in the
plan's tables. Characters are counted as the stores count them (code points), except the App Store keywords, whose
limit is in bytes.

The keywords are a list separated by commas, with no space after a comma, no empty or repeated keyword, none of 2
characters or fewer, and no word of the app's name (the store already indexes the name). Every URL file holds one https URL on the documentation site's
host. The categories are store category identifiers.

The confidentiality guard (check_confidential_terms.py) reads every tracked file, store/ included, so the names are not
checked here.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import site_address  # noqa: E402

DOCS_HOST = site_address.host(ROOT)
DOCS_PATH = site_address.path(ROOT)  # both from mkdocs.yml, the one place that holds the address

# (path under store/, limit, unit, required)
LIMITS: list[tuple[str, int, str, bool]] = [
    ("appstore/en-GB/name.txt", 30, "chars", True),
    ("appstore/en-GB/subtitle.txt", 30, "chars", True),
    ("appstore/en-GB/promotional_text.txt", 170, "chars", True),
    ("appstore/en-GB/description.txt", 4000, "chars", True),
    ("appstore/en-GB/keywords.txt", 100, "bytes", True),
    ("appstore/en-GB/release_notes.txt", 4000, "chars", True),
    ("appstore/en-GB/support_url.txt", 2048, "chars", True),
    ("appstore/en-GB/marketing_url.txt", 2048, "chars", False),
    ("appstore/en-GB/privacy_url.txt", 2048, "chars", True),
    ("appstore/review_information/notes.txt", 4000, "chars", True),
    ("appstore/copyright.txt", 200, "chars", True),
    ("appstore/primary_category.txt", 64, "chars", True),
    ("appstore/secondary_category.txt", 64, "chars", False),
    ("play/en-GB/title.txt", 30, "chars", True),
    ("play/en-GB/short_description.txt", 80, "chars", True),
    ("play/en-GB/full_description.txt", 4000, "chars", True),
    ("play/en-GB/changelogs/default.txt", 500, "chars", True),
]
URL_FILES = ["appstore/en-GB/support_url.txt", "appstore/en-GB/marketing_url.txt", "appstore/en-GB/privacy_url.txt"]
SINGLE_LINE = {
    "appstore/en-GB/name.txt",
    "appstore/en-GB/subtitle.txt",
    "appstore/en-GB/keywords.txt",
    "appstore/copyright.txt",
    "appstore/primary_category.txt",
    "appstore/secondary_category.txt",
    "play/en-GB/title.txt",
    "play/en-GB/short_description.txt",
    *URL_FILES,
}
CAPTIONS = "appstore/en-GB/screenshot_captions.txt"
CAPTION_LIMIT = 60
CAPTION_COUNT = 10  # the most screenshots in an App Store set
APP_STORE_CATEGORIES = {
    "BOOKS", "BUSINESS", "DEVELOPER_TOOLS", "EDUCATION", "ENTERTAINMENT", "FINANCE", "FOOD_AND_DRINK", "GAMES",
    "GRAPHICS_AND_DESIGN", "HEALTH_AND_FITNESS", "LIFESTYLE", "MAGAZINES_AND_NEWSPAPERS", "MEDICAL", "MUSIC",
    "NAVIGATION", "NEWS", "PHOTO_AND_VIDEO", "PRODUCTIVITY", "REFERENCE", "SHOPPING", "SOCIAL_NETWORKING", "SPORTS",
    "STICKERS", "TRAVEL", "UTILITIES", "WEATHER",
}


def read_value(path: Path, rel: str, problems: list[str]) -> str | None:
    """The value of a text file without its final newline, or None (and a problem) when the file is not well formed."""
    try:
        raw = path.read_bytes()
    except OSError:
        return None
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        problems.append(f"{rel}: not UTF-8")
        return None
    if text.startswith("﻿"):
        problems.append(f"{rel}: starts with a byte order mark")
    if "\r" in text:
        problems.append(f"{rel}: has a carriage return; use LF line ends")
    if not text.endswith("\n") or text.endswith("\n\n"):
        problems.append(f"{rel}: must end with exactly one newline")
    value = text[:-1] if text.endswith("\n") else text
    if not value.strip():
        problems.append(f"{rel}: is empty")
        return value
    if value != value.strip("\n") or value.startswith((" ", "\t")):
        problems.append(f"{rel}: starts or ends with a blank line or space")
    if any(line != line.rstrip() for line in value.split("\n")):
        problems.append(f"{rel}: has trailing spaces")
    return value


def check_keywords(value: str, name: str, rel: str) -> list[str]:
    problems = []
    keywords = value.split(",")
    if any(not k for k in keywords):
        problems.append(f"{rel}: an empty keyword (two commas, or a comma at an end)")
    if any(k != k.strip() for k in keywords):
        problems.append(f"{rel}: a space next to a comma wastes a byte of the limit")
    lowered = [k.strip().lower() for k in keywords if k.strip()]
    short = sorted({k for k in lowered if len(k) <= 2})
    if short:
        problems.append(f"{rel}: keyword(s) of 2 characters or fewer, which Apple does not accept: {', '.join(short)}")
    repeats = sorted({k for k in lowered if lowered.count(k) > 1})
    if repeats:
        problems.append(f"{rel}: repeated keyword(s): {', '.join(repeats)}")
    name_words = {w.lower() for w in re.findall(r"[A-Za-z0-9]+", name)}
    in_name = sorted({w for k in lowered for w in re.findall(r"[a-z0-9]+", k) if w in name_words})
    if in_name:
        problems.append(f"{rel}: word(s) of the app's name, which the store indexes already: {', '.join(in_name)}")
    return problems


def check_url(value: str, rel: str) -> list[str]:
    url = urlparse(value)
    if url.scheme != "https" or url.netloc != DOCS_HOST or not url.path.startswith(DOCS_PATH):
        return [f"{rel}: must be an https URL under https://{DOCS_HOST}{DOCS_PATH}, not {value!r}"]
    return []


def check(store: Path) -> list[str]:
    problems: list[str] = []
    values: dict[str, str] = {}
    for rel, limit, unit, required in LIMITS:
        path = store / rel
        if not path.is_file():
            if required:
                problems.append(f"{rel}: missing")
            continue
        value = read_value(path, rel, problems)
        if value is None:
            continue
        values[rel] = value
        size = len(value.encode("utf-8")) if unit == "bytes" else len(value)
        if size > limit:
            problems.append(f"{rel}: {size} {unit}, over the limit of {limit}")
        if rel in SINGLE_LINE and "\n" in value:
            problems.append(f"{rel}: must be one line")

    if "appstore/en-GB/keywords.txt" in values:
        problems += check_keywords(values["appstore/en-GB/keywords.txt"], values.get("appstore/en-GB/name.txt", ""),
                                   "appstore/en-GB/keywords.txt")
    for rel in URL_FILES:
        if rel in values:
            problems += check_url(values[rel], rel)
    for rel in ("appstore/primary_category.txt", "appstore/secondary_category.txt"):
        if rel in values and values[rel] not in APP_STORE_CATEGORIES:
            problems.append(f"{rel}: {values[rel]!r} is not an App Store category identifier")
    if values.get("appstore/primary_category.txt") is not None and values.get(
            "appstore/primary_category.txt") == values.get("appstore/secondary_category.txt"):
        problems.append("appstore/secondary_category.txt: the same as the primary category")
    if values.get("appstore/en-GB/name.txt") is not None and values.get("play/en-GB/title.txt") is not None and values[
            "appstore/en-GB/name.txt"] != values["play/en-GB/title.txt"]:
        problems.append("play/en-GB/title.txt: differs from the App Store name")

    captions = store / CAPTIONS
    if captions.is_file():
        value = read_value(captions, CAPTIONS, problems)
        if value is not None:
            lines = value.split("\n")
            if len(lines) > CAPTION_COUNT:
                problems.append(f"{CAPTIONS}: {len(lines)} captions, more than the {CAPTION_COUNT} screenshots of a set")
            for number, line in enumerate(lines, 1):
                if not line.strip():
                    problems.append(f"{CAPTIONS}:{number}: empty caption")
                elif len(line) > CAPTION_LIMIT:
                    problems.append(f"{CAPTIONS}:{number}: {len(line)} characters, over {CAPTION_LIMIT}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    problems = check(args.root / "store")
    for problem in problems:
        print(f"::error::store/{problem}")
    if problems:
        return 1
    print("Store text: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
