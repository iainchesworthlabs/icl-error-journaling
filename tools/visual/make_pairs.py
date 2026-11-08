#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Put each mock-up beside the app's capture of it, and list every pair in index.html.

    python tools/visual/make_pairs.py [--dir out]

In DIR, for each mock-up Name (from docs/design/screens) and for light and dark:

    <Name>.mock.png   <Name>.app.png   ->  <Name>.pair.png        (mock-up left, app right)
    <Name>.dark.mock.png  <Name>.dark.app.png  ->  <Name>.dark.pair.png

The two pictures are placed as they are, at the same scale (render_mockups.py and the capture test both write 804 x 1748),
with a small label above each. A missing app picture leaves a grey panel that says "No app state". An optional
`capture-manifest.json` (written by the capture test)

    {"Main": {"captured": true, "note": "..."}, "Other": {"captured": false, "note": "why not"}}

gives the note shown in index.html for each Name. index.html uses no external resources and opens from file://.

Needs Pillow (`python -m pip install pillow`); the rest of the repository does not.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCREENS = ROOT / "docs" / "design" / "screens"

GAP = 24
MARGIN = 24
LABEL_HEIGHT = 44
BACKGROUND = (214, 218, 222)
LABEL_COLOUR = (40, 48, 56)
PANEL = (160, 166, 172)
PANEL_TEXT = (255, 255, 255)
FONT_CANDIDATES = (r"C:\Windows\Fonts\segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVuSans.ttf")


def mockup_names() -> list[str]:
    return sorted(p.name[: -len(".dc.html")] for p in SCREENS.glob("*.dc.html") if ".dark." not in p.name)


def title_of(name: str) -> str:
    source = (SCREENS / f"{name}.dc.html").read_text(encoding="utf-8")
    match = re.search(r"<title>(.*?)</title>", source, re.S)
    return html.unescape(match.group(1).strip()) if match else name


def load_font(size: int):
    from PIL import ImageFont

    for candidate in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default()


def centred(draw, box, text: str, font, fill) -> None:
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    x = box[0] + (box[2] - box[0] - (right - left)) / 2 - left
    y = box[1] + (box[3] - box[1] - (bottom - top)) / 2 - top
    draw.text((x, y), text, font=font, fill=fill)


def wrap(draw, text: str, font, width: int) -> list[str]:
    lines, line = [], ""
    for word in text.split():
        trial = f"{line} {word}".strip()
        if draw.textlength(trial, font=font) <= width or not line:
            line = trial
        else:
            lines.append(line)
            line = word
    return lines + ([line] if line else [])


def make_pair(mock: Path, app: Path, out: Path, label_font, note: str = "") -> bool:
    """Writes the pair; returns whether the app picture was there."""
    from PIL import Image, ImageDraw

    left = Image.open(mock).convert("RGB")
    have_app = app.is_file()
    right = Image.open(app).convert("RGB") if have_app else Image.new("RGB", left.size, PANEL)
    if have_app and right.size != left.size:
        print(f"  note: {app.name} is {right.size[0]}x{right.size[1]}, {mock.name} is {left.size[0]}x{left.size[1]}")
    height = max(left.height, right.height)
    width = MARGIN * 2 + left.width + GAP + right.width
    sheet = Image.new("RGB", (width, LABEL_HEIGHT + height + MARGIN), BACKGROUND)
    draw = ImageDraw.Draw(sheet)
    x_left = MARGIN
    x_right = MARGIN + left.width + GAP
    centred(draw, (x_left, 0, x_left + left.width, LABEL_HEIGHT), "Mock-up", label_font, LABEL_COLOUR)
    centred(draw, (x_right, 0, x_right + right.width, LABEL_HEIGHT), "App", label_font, LABEL_COLOUR)
    sheet.paste(left, (x_left, LABEL_HEIGHT))
    sheet.paste(right, (x_right, LABEL_HEIGHT))
    if not have_app:
        centred(
            draw,
            (x_right, LABEL_HEIGHT, x_right + right.width, LABEL_HEIGHT + right.height),
            "No app state",
            load_font(44),
            PANEL_TEXT,
        )
        if note:
            note_font = load_font(26)
            lines = wrap(draw, note, note_font, right.width - 80)
            top = LABEL_HEIGHT + right.height // 2 + 50
            for line in lines:
                centred(draw, (x_right, top, x_right + right.width, top + 34), line, note_font, PANEL_TEXT)
                top += 36
    sheet.save(out)
    return have_app


def write_index(directory: Path, rows: list[dict]) -> None:
    captured = sum(1 for r in rows if r["captured"])
    parts = [
        "<!doctype html>",
        '<html lang="en"><head><meta charset="utf-8"><title>Mock-ups beside the app</title>',
        "<style>",
        "body{font:14px/1.4 Segoe UI,Arial,sans-serif;margin:24px;background:#eef0f2;color:#1b232a}",
        "h1{font-size:20px;margin:0 0 4px}p.summary{margin:0 0 20px;color:#44525e}",
        "table{border-collapse:collapse;width:100%}",
        "td,th{vertical-align:top;padding:10px;border-bottom:1px solid #cfd5da;text-align:left}",
        "td.shots{white-space:nowrap}",
        "td.shots a{display:inline-block;margin-right:12px}",
        "td.shots img{height:380px;width:auto;border:1px solid #b9c0c6;background:#fff}",
        ".name{font-weight:600;font-size:15px}.title{color:#44525e}",
        ".yes{color:#17653a;font-weight:600}",
        ".no{color:#8a1f12;font-weight:700}",
        "tr.missing td.info{background:#fbeceb}",
        ".note{margin-top:6px;max-width:34em}",
        "</style></head><body>",
        "<h1>Mock-ups beside the app</h1>",
        f'<p class="summary">{len(rows)} mock-ups; {captured} with an app state, {len(rows) - captured} without. '
        "Each sheet: mock-up left, app right, light and dark. Click a sheet for the full picture.</p>",
        "<table><thead><tr><th>Screen</th><th>Light</th><th>Dark</th></tr></thead><tbody>",
    ]
    for r in rows:
        name = html.escape(r["name"])
        state = (
            '<span class="yes">app state: yes</span>'
            if r["captured"]
            else '<span class="no">app state: NO (no app capture)</span>'
        )
        note = f'<div class="note">{html.escape(r["note"])}</div>' if r["note"] else ""
        parts.append(f'<tr class="{"" if r["captured"] else "missing"}">')
        parts.append(
            f'<td class="info"><div class="name">{name}</div><div class="title">{html.escape(r["title"])}</div>'
            f"<div>{state}</div>{note}</td>"
        )
        for suffix in ("", ".dark"):
            pair = f"{r['name']}{suffix}.pair.png"
            link = html.escape(pair)
            parts.append(
                f'<td class="shots"><a href="{link}"><img src="{link}" alt="{name}{suffix}" loading="lazy"></a></td>'
            )
        parts.append("</tr>")
    parts.append("</tbody></table></body></html>")
    (directory / "index.html").write_text("\n".join(parts), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", type=Path, default=ROOT / "out")
    args = parser.parse_args(argv)
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("Pillow is needed: python -m pip install pillow", file=sys.stderr)
        return 1

    directory: Path = args.dir
    manifest_path = directory / "capture-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else {}
    label_font = load_font(24)

    rows = []
    missing_mock = []
    for name in mockup_names():
        entry = manifest.get(name, {})
        light_app = (directory / f"{name}.app.png").is_file()
        dark_app = (directory / f"{name}.dark.app.png").is_file()
        note = entry.get("note", "")
        if entry.get("captured") is False and not note:
            note = "No app state for this mock-up."
        if not manifest_path.is_file() or name not in manifest:
            # No manifest entry: the pictures decide.
            captured = light_app and dark_app
        else:
            captured = bool(entry.get("captured", False)) and light_app
        if light_app != dark_app:
            note = (note + " " if note else "") + f"(only the {'light' if light_app else 'dark'} app capture exists)"
        for suffix in ("", ".dark"):
            mock = directory / f"{name}{suffix}.mock.png"
            if not mock.is_file():
                missing_mock.append(mock.name)
                continue
            make_pair(mock, directory / f"{name}{suffix}.app.png", directory / f"{name}{suffix}.pair.png", label_font, note)
        rows.append({"name": name, "title": title_of(name), "captured": captured, "note": note})

    write_index(directory, rows)
    have = sum(1 for r in rows if r["captured"])
    print(f"{len(rows)} mock-ups: {have} with an app state, {len(rows) - have} without. Wrote {directory / 'index.html'}")
    for name in missing_mock:
        print(f"  missing {name}: run tools/visual/render_mockups.py", file=sys.stderr)
    return 1 if missing_mock else 0


if __name__ == "__main__":
    sys.exit(main())
