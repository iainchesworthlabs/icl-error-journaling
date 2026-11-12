#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the pictures of the user guide from the app's own screens.

    python tools/docs/guide_screenshots.py [--src screenshots/screens] [--render] [--exe PATH] [--qt DIR]

Stage 10, packages 10.3 and 10.7 (docs/plan/stage-10.md). The pictures are listed in `docs/guide/screenshots.json`: each
names a state of `tools/ej_screenshots/screens.json` and a mode. This script takes `<State>.<mode>.png` from the folder
`ej_screenshots` writes (--src), scales it to the list's width, reduces it to 128 colours, strips every chunk but the
pixels and writes `docs/guide/img/<file>.png`. With --render it runs `tools/ej_screenshots/ej_screenshots.py` first, for
the listed states only; the CI job `Screenshots` makes the same folder and uploads it as the artifact `screenshots`.

The pictures are committed, so building the site needs no Qt. They are made from the sample data of the app's UI test and
never from a phone with a real setup (the confidential-terms rule, PRD NFR-10). Run this again, and commit the result,
whenever a screen in the list changes (docs/releasing.md).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LIST = ROOT / "docs" / "guide" / "screenshots.json"
OUT = ROOT / "docs" / "guide" / "img"
EJ = ROOT / "tools" / "ej_screenshots" / "ej_screenshots.py"


def load(path: Path = LIST) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def shrink(source: Path, target: Path, width: int) -> None:
    """Scale to `width`, keep the proportions, 128 colours, no metadata: the same input gives the same bytes."""
    with Image.open(source) as image:
        image = image.convert("RGB")
        height = round(image.height * width / image.width)
        image = image.resize((width, height), Image.Resampling.LANCZOS)
        image = image.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target, format="PNG", optimize=True)


def render(states: list[str], src: Path, exe: str | None, qt: str | None) -> None:
    command = [sys.executable, str(EJ), "--out", str(src), "--only", *states]
    if exe:
        command += ["--exe", exe]
    if qt:
        command += ["--qt", qt]
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--src", type=Path, default=ROOT / "screenshots" / "screens")
    parser.add_argument("--render", action="store_true", help="run ej_screenshots first, for the listed states")
    parser.add_argument("--exe")
    parser.add_argument("--qt")
    args = parser.parse_args(argv)

    plan = load()
    if args.render:
        render(sorted({item["state"] for item in plan["images"]}), args.src, args.exe, args.qt)
    missing = []
    for item in plan["images"]:
        source = args.src / f"{item['state']}.{item['mode']}.png"
        if not source.is_file():
            missing.append(str(source))
            continue
        shrink(source, OUT / f"{item['file']}.png", plan["width"])
        print(f"{item['file']}.png  <-  {source.name}")
    if missing:
        print("missing: " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
