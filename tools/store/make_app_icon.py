#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw the app icon: an "f" and a "t" on a 4 x 4 board of tiles.

    python3 tools/store/make_app_icon.py [--out apps/app/packaging/ios/Assets.xcassets/AppIcon.appiconset]

Writes one opaque 1024 x 1024 PNG, AppIcon-1024.png, which iOS uses in light and in dark appearance, and which
tools/store/make_play_graphics.py and tools/store/make_android_icon.py take the Google Play and Android icons from.

The design is the owner's drawing, redrawn in the style of the board's tiles (rounded squares with a small gap):

  * the "f" is a solid white letter: a stem in the left column, arms on the first and third rows;
  * the "t" is the seven filled tiles: a column down the middle, a bar across the second row and a foot on the fourth,
    one of them in the notch between the arms of the "f";
  * three outlined squares are the empty tiles of the board, one in the notch below the arms of the "f".

On leaf green, with the "t" and the outlines in a deep blue-grey. The owner chose the green and a "t" a little softer
than the app's near-black after looking at the options, and a solid "f" because a thin outline blurred into the
background at small sizes.

Everything is drawn at four times the size and scaled down, so the edges are smooth. The output depends only on this
file and on the Pillow version (requirements/store.txt).
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
RADIUS = 22  # corner of a tile and of the "f"
LINE = 9  # the outlines of the empty squares

# (row, column) of the cells, from the drawing.
FILLED = [(0, 2), (1, 1), (1, 2), (1, 3), (2, 2), (3, 2), (3, 3)]
EMPTY = [(0, 3), (2, 3), (3, 1)]
# The "f", in cell units (x is the column, y is the row), clockwise from the top left: the stem is the left column and
# the arms are the first and third rows.
F_SHAPE = [(0, 0), (2, 0), (2, 1), (1, 1), (1, 2), (2, 2), (2, 3), (1, 3), (1, 4), (0, 4)]


@dataclass(frozen=True)
class Palette:
    background: str
    tile: str  # the "t", and the outline of the empty squares
    letter: str  # the "f"


LEAF = Palette(background="#4CAF6A", tile="#1F303C", letter="#FFFFFF")


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


def draw_icon(palette: Palette = LEAF) -> Image.Image:
    scale = SUPERSAMPLE
    origin = (SIZE - 4 * CELL) // 2
    image = Image.new("RGB", (SIZE * scale, SIZE * scale), palette.background)
    draw = ImageDraw.Draw(image)

    def tile_box(row: int, column: int) -> list[int]:
        left, top = origin + column * CELL + GAP, origin + row * CELL + GAP
        return [left * scale, top * scale, (left + CELL - 2 * GAP) * scale - 1, (top + CELL - 2 * GAP) * scale - 1]

    # The "f": solid, with its edge where a tile's edge would be.
    shape = inset_polygon([(x * CELL, y * CELL) for x, y in F_SHAPE], GAP)
    shape = [((origin + x) * scale, (origin + y) * scale) for x, y in shape]
    draw.polygon(rounded_outline(shape, RADIUS * scale), fill=palette.letter)

    for row, column in FILLED:
        draw.rounded_rectangle(tile_box(row, column), radius=RADIUS * scale, fill=palette.tile)
    for row, column in EMPTY:
        draw.rounded_rectangle(tile_box(row, column), radius=RADIUS * scale, outline=palette.tile, width=LINE * scale)

    return image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def make_all(out: Path = DEFAULT_OUT) -> dict[str, Path]:
    out.mkdir(parents=True, exist_ok=True)
    paths = {"icon": out / "AppIcon-1024.png"}
    draw_icon(LEAF).save(paths["icon"], format="PNG")
    return paths


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="the folder to write the PNG into")
    args = parser.parse_args(argv)
    for name, path in make_all(args.out).items():
        print(f"{name}: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
