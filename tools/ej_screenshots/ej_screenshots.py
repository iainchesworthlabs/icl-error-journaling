#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render every screen of the app in light, dark and large text, and write an index to look through them.

    python tools/ej_screenshots/ej_screenshots.py [--out screenshots/screens] [--exe PATH] [--qt DIR] [--only Main Review ...]
                                                  [--no-fonts] [--index-only]

Stage 7, package 7.1 (docs/plan/stage-7.md). The screens are the states of `tools/ej_screenshots/screens.json`, grouped by
flow; the modes are the ones that file names (light, dark, and large: text scale 3.1, the largest iOS size). The pictures
come from the `ui` test `screenshots.render` of `ej_ui_tests` (apps/app/tests/ui/visual_capture_test.cpp), which writes
`<State>.<mode>.png` and `screenshots.json` into `EJ_SCREENSHOT_DIR`. This script runs it with the folder set, then writes
`index.html`: one section per flow, one row per state, one column per mode.

The pictures are not golden files. The runner's fonts and renderer differ from the phone's; they are for reading the
screens side by side, not for comparing pixels. CI uploads the folder as the artifact `screenshots` (docs/ci.md).

Fonts: the offscreen platform has no usable font on Windows, so the script fetches Inter (tools/visual/fetch_inter.py) into
build/visual/fonts and passes it as EJ_CAPTURE_FONTS, as the visual capture does. --no-fonts uses the platform's font.
"""

from __future__ import annotations

import argparse
import html
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCREENS = Path(__file__).resolve().parent / "screens.json"
FETCH_INTER = ROOT / "tools" / "visual" / "fetch_inter.py"
PRESETS = ["windows-msvc-release", "linux-release", "linux-debug", "macos-release", "macos-debug"]
# Windows puts the program in apps/app, the other systems in apps/app/tests/ui, beside its test sources.
CANDIDATES = [
    ROOT / "build" / preset / "apps" / "app" / folder / name
    for preset in PRESETS
    for folder in ("", "tests/ui")
    for name in ("ej_ui_tests.exe", "ej_ui_tests")
]
DEFAULT_QT = os.environ.get("ICL_QT_ROOT", "")


def default_exe() -> Path | None:
    return next((path for path in CANDIDATES if path.is_file()), None)


def render(exe: Path, out: Path, qt: str, fonts: Path | None, only: list[str]) -> int:
    env = dict(os.environ)
    if qt:
        env["PATH"] = str(Path(qt) / "bin") + os.pathsep + env.get("PATH", "")
    env["EJ_SCREENSHOT_DIR"] = str(out)
    env.setdefault("QT_SCALE_FACTOR", "2")
    if fonts is not None:
        env["EJ_CAPTURE_FONTS"] = str(fonts)
    if only:
        env["EJ_SCREENSHOT_ONLY"] = ",".join(only)
    command = [str(exe), "screenshots.render*"]
    print("+", " ".join(command), flush=True)
    return subprocess.run(command, env=env, cwd=ROOT, check=False).returncode


def write_index(out: Path) -> Path:
    screens = json.loads(SCREENS.read_text(encoding="utf-8"))
    written = json.loads((out / "screenshots.json").read_text(encoding="utf-8"))
    files = {(row["state"], row["mode"]): row for row in written}
    modes = [mode["name"] for mode in screens["modes"]]
    parts = [
        "<!doctype html><meta charset='utf-8'><title>CS Bingo Board screenshots</title>",
        "<style>body{font:14px system-ui,sans-serif;margin:24px;background:#f3f4f6;color:#111}"
        "h2{margin-top:40px}table{border-collapse:collapse}td,th{padding:8px;vertical-align:top;text-align:left}"
        "img{width:260px;border:1px solid #ccc;background:#fff}.note{color:#555;max-width:260px}</style>",
        "<h1>CS Bingo Board screenshots</h1>",
        "<p>Every state of <code>tools/ej_screenshots/screens.json</code> in each mode. Rendered on the desktop: the fonts "
        "and the renderer are not the phone's.</p>",
    ]
    for flow in screens["flows"]:
        parts.append(f"<h2>{html.escape(flow['id'])}: {html.escape(flow['title'])}</h2><table><tr><th>State</th>")
        parts.extend(f"<th>{html.escape(mode)}</th>" for mode in modes)
        parts.append("</tr>")
        for state in flow["states"]:
            name = state["name"]
            note = next((files[(name, m)]["note"] for m in modes if (name, m) in files), "")
            parts.append(f"<tr><td><b>{html.escape(name)}</b><div class='note'>{html.escape(note)}</div></td>")
            for mode in modes:
                row = files.get((name, mode))
                cell = (f"<a href='{html.escape(row['file'])}'><img loading='lazy' src='{html.escape(row['file'])}'"
                        f" alt='{html.escape(name)}, {html.escape(mode)}'></a>") if row else "not rendered"
                parts.append(f"<td>{cell}</td>")
            parts.append("</tr>")
        parts.append("</table>")
    index = out / "index.html"
    index.write_text("\n".join(parts) + "\n", encoding="utf-8")
    return index


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "screenshots" / "screens")
    parser.add_argument("--exe", type=Path, default=None, help="ej_ui_tests; the first build found by default")
    parser.add_argument("--qt", default=DEFAULT_QT, help="the Qt kit whose bin directory goes on PATH (Windows)")
    parser.add_argument("--only", nargs="*", default=[], help="state names, such as Main Review")
    parser.add_argument("--no-fonts", action="store_true", help="use the platform's font instead of Inter")
    parser.add_argument("--index-only", action="store_true", help="write index.html for pictures already in --out")
    args = parser.parse_args(argv)

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not args.index_only:
        exe = args.exe or default_exe()
        if exe is None or not exe.is_file():
            print("ej_ui_tests not found: build the target ej_ui_tests, or pass --exe", file=sys.stderr)
            return 1
        fonts = None
        if not args.no_fonts:
            fonts = Path(os.environ.get("EJ_CAPTURE_FONTS", ROOT / "build" / "visual" / "fonts")).resolve()
            if subprocess.run([sys.executable, str(FETCH_INTER)], cwd=ROOT, check=False).returncode != 0:
                print("Inter could not be fetched; the platform's font is used", file=sys.stderr)
                fonts = None
        if render(exe, out, args.qt, fonts, args.only) != 0:
            return 1
    print(f"Wrote {write_index(out)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
