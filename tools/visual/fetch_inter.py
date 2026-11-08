#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch the Inter font (SIL Open Font License 1.1) for the visual capture.

    python tools/visual/fetch_inter.py [--out build/visual/fonts]

The mock-ups in docs/design/screens ask for Inter from Google Fonts. The capture of the mock-ups (render_mockups.py) and the
capture of the app (the `[.capture]` case of apps/app/tests/ui, which reads the folder from EJ_CAPTURE_FONTS) use the same
files, so both draw the same glyphs. The files are written under `build/`, which git ignores; a font binary is never
committed. Running it again with the files in place does nothing.

Writes Inter-400.ttf, Inter-500.ttf, Inter-600.ttf and Inter-700.ttf (or Inter-Variable.ttf when the service only offers a
variable font).
"""

from __future__ import annotations

import argparse
import re
import sys
import urllib.request
from pathlib import Path

CSS_URL = "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap"
# An old Android browser makes Google Fonts answer with truetype files (other old browsers get woff).
USER_AGENT = (
    "Mozilla/5.0 (Linux; U; Android 2.2; en-us; Nexus One Build/FRF91) AppleWebKit/533.1 "
    "(KHTML, like Gecko) Version/4.0 Mobile Safari/533.1"
)
WEIGHTS = (400, 500, 600, 700)


def default_out() -> Path:
    return Path(__file__).resolve().parents[2] / "build" / "visual" / "fonts"


def fetch(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        return response.read()


def font_faces(css: str) -> list[tuple[str, str]]:
    """The (weight, url) of each @font-face; where the CSS has subset comments, those of the latin subset only."""
    faces = []
    for match in re.finditer(r"(?:/\*\s*([\w-]+)\s*\*/\s*)?@font-face\s*\{(.*?)\}", css, re.S):
        subset, body = match.groups()
        weight = re.search(r"font-weight:\s*(\d+)", body)
        url = re.search(r"url\(([^)]+)\)", body)
        if weight and url and subset in (None, "latin"):
            faces.append((weight.group(1), url.group(1).strip("'\"")))
    return faces


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=default_out())
    args = parser.parse_args(argv)
    out: Path = args.out
    wanted = [out / f"Inter-{w}.ttf" for w in WEIGHTS]
    if all(p.is_file() and p.stat().st_size > 0 for p in wanted) or (out / "Inter-Variable.ttf").is_file():
        print(f"Inter is already in {out}")
        for p in sorted(out.glob("Inter-*.ttf")):
            print(f"  {p.name}  {p.stat().st_size} bytes")
        return 0
    out.mkdir(parents=True, exist_ok=True)
    css = fetch(CSS_URL).decode("utf-8")
    faces = font_faces(css)
    if not faces:
        print("The font service answered with no @font-face rules.", file=sys.stderr)
        return 1
    written = {}
    for weight, url in faces:
        data = fetch(url)
        if data[:4] not in (b"\x00\x01\x00\x00", b"true", b"OTTO"):
            print(f"{url} is not a TrueType file", file=sys.stderr)
            return 1
        # One URL for every weight means a variable font.
        written.setdefault(url, []).append(weight)
        name = f"Inter-{weight}.ttf"
        (out / name).write_bytes(data)
    if len(written) == 1:
        # A variable font: keep one file and name it as such.
        first = out / f"Inter-{faces[0][0]}.ttf"
        variable = out / "Inter-Variable.ttf"
        variable.write_bytes(first.read_bytes())
        for weight, _ in faces:
            (out / f"Inter-{weight}.ttf").unlink(missing_ok=True)
    print(f"Inter (SIL OFL 1.1) from {CSS_URL}")
    for p in sorted(out.glob("Inter-*.ttf")):
        print(f"  {p.name}  {p.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
