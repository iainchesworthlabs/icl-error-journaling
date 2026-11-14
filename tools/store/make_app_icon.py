#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the app icon: an "f" and a "t" on a 4 x 4 board of tiles.

    python3 tools/store/make_app_icon.py [--out apps/app/packaging/ios/Assets.xcassets/AppIcon.appiconset]

Writes two opaque 1024 x 1024 PNGs, the light icon and the dark one that iOS 18 shows in dark appearance:

  AppIcon-1024.png        the drawing's colours on white
  AppIcon-1024-dark.png   the same board on the dark text colour of docs/design/tokens.json

The design is the owner's drawing, redrawn in the style of the board's tiles (rounded squares with a small gap):

  * the "t" is the seven filled tiles: a column down the middle, a bar across the second row and a foot on the fourth;
  * the "f" is the thin outline of the letter (a stem in the left column, arms on the first and third rows), with the
    "t"'s left tile and a gold square in the notches between and below its arms;
  * three gold outlined squares are the empty tiles of the board.

Everything is drawn at four times the size and scaled down, so the edges are smooth. The output depends only on this
file and on the Pillow version (requirements/store.txt). The Google Play icon is made from AppIcon-1024.png by
tools/store/make_play_graphics.py.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "apps" / "app" / "packaging" / "ios" / "Assets.xcassets" / "AppIcon.appiconset"

SIZE = 1024
SUPERSAMPLE = 4
CELL = 160  # the board is 4 x 4 cells, 640 px, centred
GAP = 4  # each tile is inset this much, so tiles are 2 * GAP apart
RADIUS = 22  # corner of a tile and of the "f" outline
LINE = 7  # the "f" outline and the gold squares

# (row, column) of the cells, from the drawing.
FILLED = [(0, 2), (1, 1), (1, 2), (1, 3), (2, 2), (3, 2), (3, 3)]
GOLD = [(0, 3), (2, 3), (3, 1)]
# The outline of the "f", in cell units (x is the column, y is the row), clockwise from the top left.
F_OUTLINE = [(0, 0), (2, 0), (2, 1), (1, 1), (1, 2), (2, 2), (2, 3), (1, 3), (1, 4), (0, 4)]


@dataclass(frozen=True)
class Palette:
    background: str
    tile: str
    gold: str


LIGHT = Palette(background="#FFFFFF", tile="#2F8FBF", gold="#F0B429")
DARK = Palette(background="#14212B", tile="#4DB2E0", gold="#FFC93C")  # background is textPrimary in tokens.json


def inset_polygon(points: list[tuple[float, float]], distance: float) -> list[tuple[float, float]]:
    """Move every edge of a clockwise right-angled polygon inward by `distance` (y grows downward)."""
    count = len(points)
    moved = []
    for i in range(count):
        before, here, after = points[i - 1], points[i], points[(i + 1) % count]
        step = [0.0, 0.0]
        for a, b in ((before, here), (here, after)):
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = (dx * dx + dy * dy) ** 0.5
            # Clockwise on screen: the inside is to the right of the direction of travel.
            step[0] += -dy / length * distance
            step[1] += dx / length * distance
        moved.append((here[0] + step[0], here[1] + step[1]))
    return moved


def rounded_outline(points: list[tuple[float, float]], radius: float, steps: int = 24) -> list[tuple[float, float]]:
    """A closed path along the polygon with every corner rounded by a quadratic curve."""
    count = len(points)
    path: list[tuple[float, float]] = []
    for i in range(count):
        before, here, after = points[i - 1], points[i], points[(i + 1) % count]

        def toward(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, float]:
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = (dx * dx + dy * dy) ** 0.5
            return a[0] + dx / length * radius, a[1] + dy / length * radius

        start, end = toward(here, before), toward(here, after)
        for k in range(steps + 1):
            t = k / steps
            path.append(
                (
                    (1 - t) ** 2 * start[0] + 2 * (1 - t) * t * here[0] + t * t * end[0],
                    (1 - t) ** 2 * start[1] + 2 * (1 - t) * t * here[1] + t * t * end[1],
                )
            )
    path.append(path[0])
    return path


def draw_icon(palette: Palette) -> Image.Image:
    scale = SUPERSAMPLE
    origin = (SIZE - 4 * CELL) // 2
    image = Image.new("RGB", (SIZE * scale, SIZE * scale), palette.background)
    draw = ImageDraw.Draw(image)

    def tile_box(row: int, column: int) -> list[int]:
        left, top = origin + column * CELL + GAP, origin + row * CELL + GAP
        return [left * scale, top * scale, (left + CELL - 2 * GAP) * scale - 1, (top + CELL - 2 * GAP) * scale - 1]

    # The "f": the outline sits where a tile's edge would, with the line inside it.
    outline = inset_polygon([(x * CELL, y * CELL) for x, y in F_OUTLINE], GAP + LINE / 2)
    outline = [((origin + x) * scale, (origin + y) * scale) for x, y in outline]
    draw.line(rounded_outline(outline, RADIUS * scale), fill=palette.tile, width=LINE * scale, joint="curve")

    for row, column in FILLED:
        draw.rounded_rectangle(tile_box(row, column), radius=RADIUS * scale, fill=palette.tile)
    for row, column in GOLD:
        draw.rounded_rectangle(tile_box(row, column), radius=RADIUS * scale, outline=palette.gold, width=LINE * scale)

    return image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def make_all(out: Path = DEFAULT_OUT) -> dict[str, Path]:
    out.mkdir(parents=True, exist_ok=True)
    paths = {"light": out / "AppIcon-1024.png", "dark": out / "AppIcon-1024-dark.png"}
    draw_icon(LIGHT).save(paths["light"], format="PNG")
    draw_icon(DARK).save(paths["dark"], format="PNG")
    return paths


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="the folder to write the two PNGs into")
    args = parser.parse_args(argv)
    for name, path in make_all(args.out).items():
        print(f"{name}: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
