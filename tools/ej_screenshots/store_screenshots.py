#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the store screenshots from the app's own screens, at the exact pixel sizes the stores ask for.

    python tools/ej_screenshots/store_screenshots.py [--out screenshots] [--exe PATH] [--qt DIR] [--raw DIR] [--check]

Stage 11, task 11.4 (docs/plan/stage-11.md). The shots are listed in `tools/ej_screenshots/store_shots.json`: each names a
state of `screens.json` and a mode. The script renders those states with ej_screenshots (the phone at a scale of 3, the iPad
at 2), then puts each picture on a canvas of the set's size:

- a neutral status bar is drawn in the safe-area band: 9:41, four bars of signal and a full battery (decision D6; the
  desktop render has no status bar of its own);
- the picture is scaled to fit the canvas, and any margin is the screen's own background colour;
- the file is an RGB PNG with no alpha and no metadata, so two runs give the same bytes.

The sets go to `<out>/store/<store>/<set>/<file>.png`, with `<out>/store/manifest.json` (size and SHA-256 of each file).
There is no caption (D6). --check reads the shot list only: it needs no build and is what the unit tests run.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SHOTS = HERE / "store_shots.json"
SCREENS = HERE / "screens.json"

# The window sizes in points and the safe-area bands the capture rig leaves empty at the top (visual_capture_test.cpp).
CLASSES = {
    "phone": {"points": (402, 874), "inset": 59},
    "ipad-portrait": {"points": (820, 1180), "inset": 24},
    "ipad-landscape": {"points": (1180, 820), "inset": 24},
}
CLOCK = "9:41"


def load_plan(path: Path = SHOTS) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def check_plan(plan: dict, screens: dict) -> list[str]:
    """The problems with the shot list: unknown states, modes or classes, counts, names, and shapes the stores refuse."""
    problems: list[str] = []
    states = {state["name"] for flow in screens["flows"] for state in flow["states"]}
    modes = {mode["name"] for mode in screens["modes"]}
    ids: set[str] = set()
    for item in plan["sets"]:
        name = item["id"]
        if name in ids:
            problems.append(f"{name}: listed twice")
        ids.add(name)
        limit = plan["limits"][item["limit"]]
        count = len(item["shots"])
        if not limit["min"] <= count <= limit["max"]:
            problems.append(f"{name}: {count} shots, the store takes {limit['min']} to {limit['max']}")
        width, height = item["size"]
        if item["store"] == "play" and not (320 <= min(width, height) and max(width, height) <= 3840):
            problems.append(f"{name}: a Play side must be 320 to 3840 px")
        if item["store"] == "play" and max(width, height) > 2 * min(width, height):
            problems.append(f"{name}: the long side of a Play screenshot is at most twice the short side")
        files: set[str] = set()
        for shot in item["shots"]:
            label = f"{name}/{shot['file']}"
            if shot["file"] in files:
                problems.append(f"{label}: file name used twice")
            files.add(shot["file"])
            if shot["state"] not in states:
                problems.append(f"{label}: no state {shot['state']} in screens.json")
            if shot["mode"] not in modes:
                problems.append(f"{label}: no mode {shot['mode']} in screens.json")
            if shot["class"] not in CLASSES or shot["class"] not in plan["scale"]:
                problems.append(f"{label}: unknown class {shot['class']}")
            elif not (shot["state"].endswith("." + shot["class"]) if shot["class"] != "phone" else ".ipad-" not in shot["state"]):
                problems.append(f"{label}: state {shot['state']} is not a {shot['class']} scene")
    return problems


def _luma(colour: tuple[int, int, int]) -> float:
    return 0.2126 * colour[0] + 0.7152 * colour[1] + 0.0722 * colour[2]


def draw_status_bar(image: Image.Image, shot_class: str, scale: int) -> None:
    """Draws 9:41, signal and battery in the safe-area band, in black on a light screen and white on a dark one."""
    inset = CLASSES[shot_class]["inset"] * scale
    background = image.getpixel((2, 2))
    colour = (0, 0, 0) if _luma(background) > 128 else (255, 255, 255)
    draw = ImageDraw.Draw(image)
    phone = shot_class == "phone"
    unit = scale * (1.0 if phone else 0.8)
    # The phone's clock sits beside the island, in the upper part of the band.
    middle = inset * 0.62 / 2 + inset * 0.05 if phone else inset / 2
    margin = int((30 if phone else 24) * scale)
    draw.text((margin, middle), CLOCK, fill=colour, font=ImageFont.load_default(int(17 * unit)), anchor="lm")
    right = image.width - margin
    # Battery: an outline, a fill and a nub.
    bw, bh = int(25 * unit), int(12 * unit)
    top = int(middle - bh / 2)
    draw.rounded_rectangle((right - bw, top, right, top + bh), radius=int(3.5 * unit), outline=colour,
                           width=max(1, int(unit)))
    pad = max(2, int(2 * unit))
    draw.rounded_rectangle((right - bw + pad, top + pad, right - pad, top + bh - pad), radius=int(1.5 * unit),
                           fill=colour)
    draw.rectangle((right + int(unit), top + int(4 * unit), right + int(2.5 * unit), top + bh - int(4 * unit)),
                   fill=colour)
    # Signal: four bars of rising height.
    left = right - bw - int(14 * unit) - 4 * int(5 * unit)
    base = int(middle + 6 * unit)
    for n in range(4):
        x = left + n * int(5 * unit)
        draw.rectangle((x, base - int((4 + 2.5 * n) * unit), x + int(3 * unit), base), fill=colour)


def compose(raw: Image.Image, shot_class: str, scale: int, size: tuple[int, int]) -> Image.Image:
    """The picture on a canvas of the set's size: status bar drawn, fitted, margins in the screen's own colour."""
    picture = raw.convert("RGB")
    expected = tuple(point * scale for point in CLASSES[shot_class]["points"])
    if picture.size != expected:
        raise ValueError(f"capture is {picture.size}, expected {expected} for {shot_class}")
    draw_status_bar(picture, shot_class, scale)
    width, height = size
    factor = min(width / picture.width, height / picture.height)
    fitted = (min(width, round(picture.width * factor)), min(height, round(picture.height * factor)))
    if fitted != picture.size:
        picture = picture.resize(fitted, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", size, picture.getpixel((2, picture.height - 3)))
    canvas.paste(picture, ((width - fitted[0]) // 2, (height - fitted[1]) // 2))
    return canvas


def png_bytes(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, format="PNG", optimize=False, compress_level=9)
    return out.getvalue()


def make(plan: dict, raw: Path, out: Path) -> list[dict]:
    """Writes every set. `raw` holds <class>/<State>.<mode>.png."""
    manifest: list[dict] = []
    for item in plan["sets"]:
        folder = out / "store" / item["store"] / item["id"]
        folder.mkdir(parents=True, exist_ok=True)
        for shot in item["shots"]:
            source = raw / shot["class"] / f"{shot['state']}.{shot['mode']}.png"
            with Image.open(source) as opened:
                image = compose(opened, shot["class"], plan["scale"][shot["class"]], tuple(item["size"]))
            data = png_bytes(image)
            target = folder / f"{shot['file']}.png"
            target.write_bytes(data)
            manifest.append({"set": item["id"], "file": target.relative_to(out / "store").as_posix(),
                             "size": list(image.size), "sha256": hashlib.sha256(data).hexdigest()})
    (out / "store").mkdir(parents=True, exist_ok=True)
    (out / "store" / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def render_raw(plan: dict, raw: Path, exe: Path, qt: str) -> int:
    sys.path.insert(0, str(HERE))
    import ej_screenshots  # noqa: PLC0415

    fonts = None
    if ej_screenshots.FETCH_INTER.is_file() and \
            subprocess.run([sys.executable, str(ej_screenshots.FETCH_INTER)], cwd=ROOT, check=False).returncode == 0:
        fonts = Path(os.environ.get("EJ_CAPTURE_FONTS", ROOT / "build" / "visual" / "fonts")).resolve()
    for shot_class, scale in plan["scale"].items():
        states = sorted({shot["state"] for item in plan["sets"] for shot in item["shots"] if shot["class"] == shot_class})
        folder = raw / shot_class
        folder.mkdir(parents=True, exist_ok=True)
        os.environ["QT_SCALE_FACTOR"] = str(scale)
        if ej_screenshots.render(exe, folder, qt, fonts, states) != 0:
            return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "screenshots")
    parser.add_argument("--exe", type=Path, default=None, help="ej_ui_tests; the first build found by default")
    parser.add_argument("--qt", default=os.environ.get("ICL_QT_ROOT", ""), help="the Qt kit whose bin directory goes on PATH")
    parser.add_argument("--raw", type=Path, default=None, help="captures already made: <class>/<State>.<mode>.png")
    parser.add_argument("--check", action="store_true", help="check the shot list and stop")
    args = parser.parse_args(argv)

    plan = load_plan()
    problems = check_plan(plan, json.loads(SCREENS.read_text(encoding="utf-8")))
    for problem in problems:
        print(f"::error::store_shots.json: {problem}")
    if problems or args.check:
        return 1 if problems else 0

    out = args.out.resolve()
    raw = args.raw.resolve() if args.raw else out / "store-raw"
    if args.raw is None:
        sys.path.insert(0, str(HERE))
        import ej_screenshots  # noqa: PLC0415

        exe = args.exe or ej_screenshots.default_exe()
        if exe is None or not exe.is_file():
            print("ej_ui_tests not found: build the target ej_ui_tests, or pass --exe", file=sys.stderr)
            return 1
        if render_raw(plan, raw, exe, args.qt) != 0:
            return 1
    manifest = make(plan, raw, out)
    print(f"Wrote {len(manifest)} store screenshots in {out / 'store'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
