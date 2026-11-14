#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the Android launcher icon (docs/plan/stage-9.md, package 9.7) from the drawing in make_app_icon.py.

    python3 tools/store/make_android_icon.py [--res apps/app/packaging/android/res] [--preview icon-preview.png]

Writes, under the Android resources folder:

  drawable/ic_launcher_foreground.xml   the board as a vector, in the light icon's colours
  drawable/ic_launcher_monochrome.xml   the same shapes in one colour, which Android 13 and later tint (themed icons)
  mipmap-anydpi-v26/ic_launcher.xml     the adaptive icon: background, foreground and monochrome layers
  mipmap-anydpi-v26/ic_launcher_round.xml   the same, for launchers that ask for a round icon
  values/ic_launcher_background.xml     the background colour (white, as the light iOS icon)

Android draws an adaptive icon on a 108 x 108 dp canvas and cuts it to a shape (a circle, a rounded square, a squircle),
so only the middle 72 x 72 dp is always seen. The board is drawn 50 dp wide, so its corners are inside a circle of
72 dp across and no launcher shape cuts it. The shapes are the same as the iOS icon: the same tiles, gaps, corners and
lines, scaled. Android has no dark icon, so there is one set, the light one.

The output depends only on this file and make_app_icon.py. `--preview` also draws the icon in three launcher shapes
(needs Pillow, requirements/store.txt).
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import make_app_icon as design  # noqa: E402  (the drawing: cells, tiles, the f outline and the light colours)

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RES = ROOT / "apps" / "app" / "packaging" / "android" / "res"

CANVAS = 108.0  # dp, the adaptive icon's layer size
BOARD = 50.0  # dp, the width of the 4 x 4 board
VISIBLE_RADIUS = 36.0  # dp, half of the 72 dp that every launcher shape shows
SCALE = BOARD / (4 * design.CELL)  # dp per pixel of the 1024 px drawing

# The licence tag is written in two pieces so that the REUSE tool, which reads every line of this file, does not take
# the text of the header it writes for a header of this file.
LICENCE_TAG = "SPDX-License" + "-Identifier: GPL-3.0-or-later"
XML_HEAD = f'<?xml version="1.0" encoding="utf-8"?>\n<!-- {LICENCE_TAG} -->\n'
GENERATED = "<!-- Made by tools/store/make_android_icon.py from the drawing in tools/store/make_app_icon.py. Do not edit. -->\n"


@dataclass(frozen=True)
class Shape:
    """One path of the vector: a closed outline, filled or stroked, in dp on the 108 dp canvas."""

    data: str
    colour: str
    stroke: bool
    points: tuple[tuple[float, float], ...]  # the corners, for checking that the shape is inside the visible area


def dp(value: float) -> float:
    """A length of the 1024 px drawing in dp."""
    return value * SCALE


def point(x: float, y: float) -> tuple[float, float]:
    """A position in the 1024 px drawing in dp on the canvas: the drawing's centre is the canvas centre."""
    middle = design.SIZE / 2
    return CANVAS / 2 + (x - middle) * SCALE, CANVAS / 2 + (y - middle) * SCALE


def num(value: float) -> str:
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def rounded_box(left: float, top: float, right: float, bottom: float, radius: float) -> str:
    """The path of a rounded rectangle, in the dp of the canvas."""
    r = radius
    width, height = right - left, bottom - top
    return (
        f"M{num(left + r)},{num(top)}"
        f"H{num(right - r)}A{num(r)},{num(r)} 0 0 1 {num(right)},{num(top + r)}"
        f"V{num(bottom - r)}A{num(r)},{num(r)} 0 0 1 {num(right - r)},{num(bottom)}"
        f"H{num(left + r)}A{num(r)},{num(r)} 0 0 1 {num(left)},{num(bottom - r)}"
        f"V{num(top + r)}A{num(r)},{num(r)} 0 0 1 {num(left + r)},{num(top)}Z"
    ) if width > 2 * r and height > 2 * r else ""


def rounded_polygon(points: list[tuple[float, float]], radius: float) -> str:
    """A closed path along a polygon with each corner rounded by a quadratic curve, as make_app_icon draws the f."""
    count = len(points)
    parts = []
    for i in range(count):
        before, here, after = points[i - 1], points[i], points[(i + 1) % count]

        def toward(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, float]:
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = (dx * dx + dy * dy) ** 0.5
            return a[0] + dx / length * radius, a[1] + dy / length * radius

        start, end = toward(here, before), toward(here, after)
        parts.append(
            f"{'M' if i == 0 else 'L'}{num(start[0])},{num(start[1])}"
            f"Q{num(here[0])},{num(here[1])} {num(end[0])},{num(end[1])}"
        )
    return "".join(parts) + "Z"


def shapes(tile_colour: str, gold_colour: str) -> list[Shape]:
    """Every shape of the icon: the f outline, the filled tiles and the gold squares."""
    origin = (design.SIZE - 4 * design.CELL) // 2
    radius = dp(design.RADIUS)
    result: list[Shape] = []

    outline = design.inset_polygon([(x * design.CELL, y * design.CELL) for x, y in design.F_OUTLINE], design.GAP + design.LINE / 2)
    corners = [point(origin + x, origin + y) for x, y in outline]
    result.append(Shape(rounded_polygon(corners, radius), tile_colour, True, tuple(corners)))

    def box(row: int, column: int, inset: float) -> tuple[float, float, float, float]:
        left, top = origin + column * design.CELL + inset, origin + row * design.CELL + inset
        size = design.CELL - 2 * inset
        (x0, y0), (x1, y1) = point(left, top), point(left + size, top + size)
        return x0, y0, x1, y1

    for row, column in design.FILLED:
        x0, y0, x1, y1 = box(row, column, design.GAP)
        result.append(Shape(rounded_box(x0, y0, x1, y1, radius), tile_colour, False, ((x0, y0), (x1, y1))))
    for row, column in design.GOLD:
        # The line is drawn on the path, so the path is half a line inside the tile's edge.
        x0, y0, x1, y1 = box(row, column, design.GAP + design.LINE / 2)
        result.append(Shape(rounded_box(x0, y0, x1, y1, radius), gold_colour, True, ((x0, y0), (x1, y1))))
    return result


def farthest_corner(layer: list[Shape]) -> float:
    """The distance from the canvas centre to the farthest point of any shape, a half line included."""
    centre = CANVAS / 2
    reach = 0.0
    for shape in layer:
        for x, y in shape.points:
            reach = max(reach, ((x - centre) ** 2 + (y - centre) ** 2) ** 0.5 + (dp(design.LINE) / 2 if shape.stroke else 0))
    return reach


def vector(layer: list[Shape], what: str) -> str:
    lines = [
        XML_HEAD + GENERATED + f"<!-- {what} -->",
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"',
        f'    android:width="{num(CANVAS)}dp"',
        f'    android:height="{num(CANVAS)}dp"',
        f'    android:viewportWidth="{num(CANVAS)}"',
        f'    android:viewportHeight="{num(CANVAS)}">',
    ]
    for shape in layer:
        lines.append("    <path")
        if shape.stroke:
            lines += [
                f'        android:strokeColor="{shape.colour}"',
                f'        android:strokeWidth="{num(dp(design.LINE))}"',
                '        android:strokeLineJoin="round"',
            ]
        else:
            lines.append(f'        android:fillColor="{shape.colour}"')
        lines.append(f'        android:pathData="{shape.data}" />')
    lines.append("</vector>\n")
    return "\n".join(lines)


ADAPTIVE = (
    XML_HEAD
    + GENERATED
    + """<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@drawable/ic_launcher_foreground" />
    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />
</adaptive-icon>
"""
)

BACKGROUND = (
    XML_HEAD
    + GENERATED
    + f"""<resources>
    <color name="ic_launcher_background">{design.LIGHT.background}</color>
</resources>
"""
)

MONOCHROME_COLOUR = "#000000"  # only its shape counts: Android tints it


def files() -> dict[str, str]:
    """The resource files, by their path under res/."""
    colour = shapes(design.LIGHT.tile, design.LIGHT.gold)
    mono = shapes(MONOCHROME_COLOUR, MONOCHROME_COLOUR)
    return {
        "drawable/ic_launcher_foreground.xml": vector(colour, "The adaptive icon's foreground: the f and the t on a board of tiles."),
        "drawable/ic_launcher_monochrome.xml": vector(mono, "The themed icon (Android 13 and later): the same shapes, one colour."),
        "mipmap-anydpi-v26/ic_launcher.xml": ADAPTIVE,
        "mipmap-anydpi-v26/ic_launcher_round.xml": ADAPTIVE,
        "values/ic_launcher_background.xml": BACKGROUND,
    }


def make_all(res: Path = DEFAULT_RES) -> list[Path]:
    written = []
    for name, text in files().items():
        path = res / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
        written.append(path)
    return written


def preview(path: Path) -> None:
    """The icon in a circle, a rounded square and a squircle-ish shape, as launchers show it (needs Pillow)."""
    from PIL import Image, ImageDraw

    full = design.draw_icon(design.LIGHT)  # the 1024 px drawing, white background
    size = 540
    layer = Image.new("RGB", (size, size), design.LIGHT.background)
    board = round(size * BOARD / CANVAS)
    drawn = full.crop((192, 192, 192 + 640, 192 + 640)).resize((board, board), Image.Resampling.LANCZOS)
    layer.paste(drawn, ((size - board) // 2, (size - board) // 2))
    visible = round(size * 72 / CANVAS)
    shapes_ = {"circle": visible // 2, "rounded square": round(visible * 0.22), "squircle": round(visible * 0.34)}
    sheet = Image.new("RGB", (3 * (visible + 40) + 40, visible + 80), "#8A8F95")
    for i, (_, radius) in enumerate(shapes_.items()):
        crop = layer.crop(((size - visible) // 2, (size - visible) // 2, (size + visible) // 2, (size + visible) // 2))
        mask = Image.new("L", crop.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, crop.size[0] - 1, crop.size[1] - 1], radius=radius, fill=255)
        sheet.paste(crop, (40 + i * (visible + 40), 40), mask)
    sheet.save(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--res", type=Path, default=DEFAULT_RES, help="the Android res/ folder to write into")
    parser.add_argument("--preview", type=Path, help="also write a picture of the icon in three launcher shapes")
    args = parser.parse_args(argv)
    for path in make_all(args.res):
        print(path)
    if args.preview:
        preview(args.preview)
        print(args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
