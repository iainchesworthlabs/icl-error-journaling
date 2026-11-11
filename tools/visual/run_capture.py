#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the pairs of mock-up and app for the visual pass: every screen of docs/design/screens, light and dark.

    python tools/visual/run_capture.py [--out screenshots/pairs] [--only Main Review ...] [--exe PATH] [--qt DIR] [--scale 2]
                                       [--skip-app] [--skip-mockups]

It runs, in order, and each step can be run on its own:

  1. tools/visual/fetch_inter.py          Inter (OFL) into build/visual/fonts, once (the folder is not committed)
  2. ej_ui_tests "capture.mockups*"       the hidden case [.capture] of apps/app/tests/ui/visual_capture_test.cpp: the app
                                          in each state of a mock-up, `<Name>.app.png` and `<Name>.dark.app.png`, and
                                          `capture-manifest.json` (what was captured, and where the state differs)
  3. tools/visual/render_mockups.py       the mock-ups in headless Edge, `<Name>.mock.png` and `<Name>.dark.mock.png`
  4. tools/visual/make_pairs.py           `<Name>.pair.png` and `<Name>.dark.pair.png`, and `index.html`

The test program is the one the `windows-msvc-release` preset builds (`cmake --build build/windows-msvc-release --target
ej_ui_tests`, from a Visual Studio developer prompt, with the Qt bin directory on PATH). The case is not a test of
behaviour: Catch2 does not select a hidden case with the spec that ctest uses, so no ctest run starts it, and the case skips
itself when EJ_CAPTURE_DIR is not set.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
DEFAULT_EXE = ROOT / "build" / "windows-msvc-release" / "apps" / "app" / "ej_ui_tests.exe"
DEFAULT_QT = Path(os.environ.get("ICL_QT_ROOT", r"C:\Repos\common\qt\6.12.0\msvc2022_64"))


def run(command: list[str], env: dict[str, str] | None = None) -> int:
    print("+", " ".join(command), flush=True)
    return subprocess.run(command, env=env, cwd=ROOT, check=False).returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "screenshots" / "pairs")
    parser.add_argument("--only", nargs="*", default=[], help="mock-up names, such as Main Review")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE, help="ej_ui_tests")
    parser.add_argument("--qt", type=Path, default=DEFAULT_QT, help="the Qt kit, whose bin directory goes on PATH")
    parser.add_argument("--scale", type=float, default=2.0)
    parser.add_argument("--skip-app", action="store_true")
    parser.add_argument("--skip-mockups", action="store_true")
    args = parser.parse_args(argv)

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    fonts = Path(os.environ.get("EJ_CAPTURE_FONTS", ROOT / "build" / "visual" / "fonts")).resolve()

    if run([sys.executable, str(TOOLS / "fetch_inter.py")]) != 0:
        return 1

    if not args.skip_app:
        if not args.exe.is_file():
            print(f"{args.exe} not found: build the target ej_ui_tests first", file=sys.stderr)
            return 1
        env = dict(os.environ)
        env["PATH"] = str(args.qt / "bin") + os.pathsep + env.get("PATH", "")
        env["EJ_CAPTURE_DIR"] = str(out)
        env["EJ_CAPTURE_FONTS"] = str(fonts)
        env["QT_SCALE_FACTOR"] = str(args.scale)
        if args.only:
            env["EJ_CAPTURE_ONLY"] = ",".join(args.only)
        if run([str(args.exe), "capture.mockups*"], env) != 0:
            return 1

    if not args.skip_mockups:
        only = ["--only", *args.only] if args.only else []
        if run([sys.executable, str(TOOLS / "render_mockups.py"), "--out", str(out), "--scale", str(args.scale), *only]) != 0:
            return 1

    return run([sys.executable, str(TOOLS / "make_pairs.py"), "--dir", str(out)])


if __name__ == "__main__":
    sys.exit(main())
