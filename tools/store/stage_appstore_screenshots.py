#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Put the App Store screenshots where `fastlane deliver` reads them (docs/plan/stage-11.md, tasks 11.4 and 11.7).

    python3 tools/store/stage_appstore_screenshots.py SOURCE TARGET

SOURCE is the `store/appstore` folder of the screenshots artifact (one folder per set, made by
tools/ej_screenshots/store_screenshots.py). TARGET is the language folder deliver reads, store/appstore/screenshots/en-GB.
deliver picks the display size from the pixel size of each file and orders the files of one size by name, so the files are
copied flat with a prefix that keeps the order: the iPhone set, then the iPad portrait set, then the iPad landscape set.
The target is emptied first. Nothing is uploaded here.
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

# Folder of the set in the artifact, and the prefix that orders its files in the target.
SETS = (
    ("appstore-iphone-6.9", "iphone"),
    ("appstore-ipad-13-portrait", "ipad-1-portrait"),
    ("appstore-ipad-13-landscape", "ipad-2-landscape"),
)


def stage(source: Path, target: Path) -> list[Path]:
    """Copies the sets in SETS; returns the files written. Refuses a missing set or an empty one."""
    for folder, _ in SETS:
        if not any((source / folder).glob("*.png")):
            raise FileNotFoundError(f"no screenshots in {source / folder}")
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    written: list[Path] = []
    for folder, prefix in SETS:
        for picture in sorted((source / folder).glob("*.png")):
            copy = target / f"{prefix}-{picture.name}"
            shutil.copyfile(picture, copy)
            written.append(copy)
    return written


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        written = stage(Path(argv[0]), Path(argv[1]))
    except FileNotFoundError as error:
        print(f"::error::{error}")
        return 1
    print(f"Staged {len(written)} App Store screenshots in {argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
