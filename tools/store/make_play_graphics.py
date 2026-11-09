#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the Google Play listing graphics (docs/plan/stage-11.md, task 11.5).

    python3 tools/store/make_play_graphics.py [--out store/play/en-GB/images]

Writes, in the layout `fastlane supply` reads:

  icon.png              512 x 512, 32-bit PNG: the iOS app icon (AppIcon-1024.png) scaled down. Play draws its own mask.
  featureGraphic.png    1024 x 500, opaque PNG: the board's twelve tiles in the palette of docs/design/tokens.json on the
                        light background, with the app icon beside them. No text, so nothing to translate and no font.

Everything comes from files in the repository, and the output depends only on them and on the Pillow version
(requirements/store.txt), so CI can make the graphics again for each release. The PNGs are not committed (.gitignore).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
TOKENS = ROOT / "docs" / "design" / "tokens.json"
APP_ICON = ROOT / "apps" / "app" / "packaging" / "ios" / "Assets.xcassets" / "AppIcon.appiconset" / "AppIcon-1024.png"
DEFAULT_OUT = ROOT / "store" / "play" / "en-GB" / "images"

ICON_SIZE = 512
FEATURE_SIZE = (1024, 500)
SUPERSAMPLE = 4  # drawn at 4x and scaled down, for smooth corners

# The feature graphic's layout, in output pixels.
COLUMNS, ROWS = 4, 3
TILE = (120, 100)
GAP = 14
TILE_RADIUS = 16
BOARD_LEFT = 72
FEATURE_ICON = 300
ICON_RADIUS_RATIO = 0.2237  # the corner of the iOS icon shape, as a share of its side
DASH = (10, 7)


def hex_colour(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)


def load_tokens(path: Path = TOKENS) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def make_icon(source: Path = APP_ICON) -> Image.Image:
    with Image.open(source) as icon:
        if icon.size != (1024, 1024):
            raise ValueError(f"{source} is {icon.size[0]} x {icon.size[1]}, expected 1024 x 1024")
        return icon.convert("RGBA").resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)


def dashed_rounded_rectangle(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], radius: int,
                             colour: tuple[int, int, int], width: int, dash: tuple[int, int]) -> None:
    """A rounded rectangle's outline with dashed straight sides and solid corners (the Other tile's border)."""
    left, top, right, bottom = box
    on, off = dash
    for start, end, fixed, horizontal in ((left + radius, right - radius, top, True),
                                          (left + radius, right - radius, bottom - width, True),
                                          (top + radius, bottom - radius, left, False),
                                          (top + radius, bottom - radius, right - width, False)):
        position = start
        while position < end:
            stop = min(position + on, end)
            if horizontal:
                draw.rectangle((position, fixed, stop, fixed + width), fill=colour)
            else:
                draw.rectangle((fixed, position, fixed + width, stop), fill=colour)
            position = stop + off
    for corner_box, start_angle in (((left, top, left + 2 * radius, top + 2 * radius), 180),
                                    ((right - 2 * radius, top, right, top + 2 * radius), 270),
                                    ((right - 2 * radius, bottom - 2 * radius, right, bottom), 0),
                                    ((left, bottom - 2 * radius, left + 2 * radius, bottom), 90)):
        draw.arc(corner_box, start_angle, start_angle + 90, fill=colour, width=width)


def make_feature_graphic(tokens: dict, source: Path = APP_ICON) -> Image.Image:
    s = SUPERSAMPLE
    width, height = FEATURE_SIZE
    background = hex_colour(tokens["colour"]["light"]["background"])
    canvas = Image.new("RGB", (width * s, height * s), background)
    draw = ImageDraw.Draw(canvas)

    palette = sorted(tokens["tilePalette"], key=lambda tile: tile["slot"])
    if len(palette) != COLUMNS * ROWS:
        raise ValueError(f"the tile palette has {len(palette)} slots, expected {COLUMNS * ROWS}")
    board_height = ROWS * TILE[1] + (ROWS - 1) * GAP
    top = (height - board_height) // 2
    for index, tile in enumerate(palette):
        column, row = index % COLUMNS, index // COLUMNS
        x = BOARD_LEFT + column * (TILE[0] + GAP)
        y = top + row * (TILE[1] + GAP)
        box = (x * s, y * s, (x + TILE[0]) * s, (y + TILE[1]) * s)
        draw.rounded_rectangle(box, radius=TILE_RADIUS * s, fill=hex_colour(tile["hex"]))
        if "border" in tile:
            dashed_rounded_rectangle(draw, box, TILE_RADIUS * s, hex_colour(tile["border"]),
                                     round(tile.get("borderWidthPt", 1.5) * s), (DASH[0] * s, DASH[1] * s))

    board_right = BOARD_LEFT + COLUMNS * TILE[0] + (COLUMNS - 1) * GAP
    icon_left = board_right + (width - board_right - FEATURE_ICON) // 2
    icon_top = (height - FEATURE_ICON) // 2
    with Image.open(source) as icon_source:
        icon = icon_source.convert("RGB").resize((FEATURE_ICON * s, FEATURE_ICON * s), Image.Resampling.LANCZOS)
    mask = Image.new("L", icon.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, icon.size[0] - 1, icon.size[1] - 1),
                                           radius=round(FEATURE_ICON * ICON_RADIUS_RATIO * s), fill=255)
    canvas.paste(icon, (icon_left * s, icon_top * s), mask)
    return canvas.resize(FEATURE_SIZE, Image.Resampling.LANCZOS)


def save_png(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG", optimize=True)


def make_all(out: Path, tokens_path: Path = TOKENS, icon_path: Path = APP_ICON) -> list[Path]:
    tokens = load_tokens(tokens_path)
    written = [out / "icon.png", out / "featureGraphic.png"]
    save_png(make_icon(icon_path), written[0])
    save_png(make_feature_graphic(tokens, icon_path), written[1])
    return written


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args(argv)
    for path in make_all(args.out):
        with Image.open(path) as image:
            print(f"{path}: {image.size[0]} x {image.size[1]} {image.mode}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
