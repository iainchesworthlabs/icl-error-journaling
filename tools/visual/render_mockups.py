#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render the design mock-ups (docs/design/screens/*.dc.html) to PNG with headless Edge.

    python tools/visual/render_mockups.py [--out out] [--only Main Review ...] [--scale 2] [--jobs 4]

Writes `<Name>.mock.png` (light) and `<Name>.dark.mock.png` (dark), each 402 x 874 logical pixels at --scale (2 by default,
so 804 x 1748). The app's capture (apps/app/tests/ui/visual_capture_test.cpp) writes at the same scale, so the two compare.

The mock-ups were authored for a design-canvas runtime (`support.js`, not in the repository). Rendering them as they are
shows the unexpanded templates of `Main`, so the pages are preprocessed into a temporary folder (build/visual/tmp), and the
files in docs/design are not touched:

  * the `support.js` tag and the Google Fonts links are dropped, and `@font-face` rules for the local Inter files
    (build/visual/fonts, from fetch_inter.py) are put in, so that no network is needed and the glyphs are the ones the
    capture of the app uses;
  * `Main` (light and dark) is the only page with `<sc-for>`, `<sc-if>` and `{{...}}` bindings. A small shim, a stand-in for
    the runtime, runs the page's own `class Component extends DCLogic` and expands the bindings with its `renderVals()`
    from the initial state (7 entries, a toast for Mute Delay). Nothing else is simulated: no hover, no click, no timer;
  * `<x-dc>` and `<helmet>` are unknown elements and render without help.

Edge is the one at "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe" unless --edge or EDGE says otherwise.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCREENS = ROOT / "docs" / "design" / "screens"
DEFAULT_EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
WIDTH, HEIGHT = 402, 874
WEIGHTS = (400, 500, 600, 700)

# The stand-in for the runtime of the design canvas, for the pages with bindings. It runs after the page is parsed, takes
# the script of `type="text/x-dc"`, evaluates it with a minimal DCLogic, and expands `<sc-for list as>`, `<sc-if value>` and
# `{{path}}` in text and attributes. Event attributes are dropped.
SHIM = r"""
<script>
(function () {
  function lookup(path, scope) {
    var parts = path.trim().split('.');
    var value = scope[parts[0]];
    for (var i = 1; i < parts.length && value != null; i++) value = value[parts[i]];
    return value;
  }
  function interpolate(text, scope) {
    return text.replace(/\{\{([^}]*)\}\}/g, function (_, path) {
      var v = lookup(path, scope);
      return v == null ? '' : String(v);
    });
  }
  function expand(node, scope) {
    if (node.nodeType === 3) { node.nodeValue = interpolate(node.nodeValue, scope); return; }
    if (node.nodeType !== 1) return;
    var tag = node.tagName.toLowerCase();
    if (tag === 'sc-if') {
      var on = lookup(node.getAttribute('value').replace(/[{}]/g, ''), scope);
      var kids = Array.prototype.slice.call(node.childNodes);
      if (on) { kids.forEach(function (k) { node.parentNode.insertBefore(k, node); expand(k, scope); }); }
      node.remove();
      return;
    }
    if (tag === 'sc-for') {
      var list = lookup(node.getAttribute('list').replace(/[{}]/g, ''), scope) || [];
      var as = node.getAttribute('as');
      var template = Array.prototype.slice.call(node.childNodes);
      list.forEach(function (item) {
        var inner = Object.create(scope); inner[as] = item;
        template.forEach(function (t) {
          var copy = t.cloneNode(true);
          node.parentNode.insertBefore(copy, node);
          expand(copy, inner);
        });
      });
      node.remove();
      return;
    }
    Array.prototype.slice.call(node.attributes).forEach(function (a) {
      if (/^on/i.test(a.name) || a.name.indexOf('hint-') === 0) { node.removeAttribute(a.name); return; }
      if (a.value.indexOf('{{') >= 0) {
        var text = interpolate(a.value, scope);
        node.setAttribute(a.name, text);
        if (a.name === 'value' && node.tagName === 'INPUT') node.value = text;
      }
    });
    Array.prototype.slice.call(node.childNodes).forEach(function (k) { expand(k, scope); });
  }
  var code = document.querySelector('script[type="text/x-dc"]').textContent;
  var Component = new Function('DCLogic', code + '; return Component;')(
    class DCLogic { constructor(props) { this.props = props; } setState(s) { Object.assign(this.state, s); } });
  var vals = new Component({}).renderVals();
  var scope = {};
  Object.keys(vals).forEach(function (k) { scope[k] = vals[k]; });
  expand(document.querySelector('x-dc'), scope);
})();
</script>
"""


def font_faces(fonts: Path) -> str:
    rules = []
    variable = fonts / "Inter-Variable.ttf"
    if variable.is_file():
        rules.append(
            f"@font-face{{font-family:'Inter';font-weight:100 900;src:url('{variable.as_uri()}') format('truetype')}}"
        )
    else:
        for weight in WEIGHTS:
            path = fonts / f"Inter-{weight}.ttf"
            if not path.is_file():
                raise SystemExit(f"{path} is missing: run tools/visual/fetch_inter.py")
            rules.append(
                f"@font-face{{font-family:'Inter';font-weight:{weight};src:url('{path.as_uri()}') format('truetype')}}"
            )
    return "\n".join(rules)


def preprocess(source: str, fonts: Path) -> str:
    html = re.sub(r"<script\s+src=\"\./support\.js\"></script>", "", source)
    html = re.sub(r"<link[^>]*fonts\.(googleapis|gstatic)\.com[^>]*>", "", html)
    css = (
        f"<style>{font_faces(fonts)}\n"
        "html,body{margin:0;padding:0;background:#fff;overflow:hidden}\n"
        "x-dc{display:block}helmet{display:none}</style>"
    )
    html = html.replace("</head>", css + "</head>", 1)
    if "<sc-for" in html or "<sc-if" in html:
        html = html.replace("</body>", SHIM + "</body>", 1)
    return html


def wait_for_picture(png: Path, seconds: float = 90.0) -> bool:
    """Edge's launcher can return before the picture is written: wait until the file is there and has stopped growing."""
    deadline = time.time() + seconds
    last = -1
    while time.time() < deadline:
        size = png.stat().st_size if png.is_file() else 0
        if size > 0 and size == last:
            return True
        last = size
        time.sleep(0.4)
    return False


def render_one(edge: str, page: Path, png: Path, scale: float, budget_ms: int) -> None:
    profile = Path(tempfile.mkdtemp(prefix="ej-edge-"))
    png.unlink(missing_ok=True)
    try:
        command = [
            edge,
            "--headless",
            "--disable-gpu",
            "--no-first-run",
            "--no-default-browser-check",
            "--allow-file-access-from-files",
            "--hide-scrollbars",
            f"--user-data-dir={profile}",
            f"--window-size={WIDTH},{HEIGHT}",
            f"--force-device-scale-factor={scale:g}",
            f"--virtual-time-budget={budget_ms}",
            f"--screenshot={png}",
            page.as_uri(),
        ]
        subprocess.run(command, check=True, capture_output=True, timeout=120)
        written = wait_for_picture(png)
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    if not written:
        raise RuntimeError(f"Edge wrote no picture for {page.name}")


def mockup_names() -> list[str]:
    return sorted(p.name[: -len(".dc.html")] for p in SCREENS.glob("*.dc.html") if ".dark." not in p.name)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "out")
    parser.add_argument("--only", nargs="*", default=[], help="mock-up names, such as Main Review")
    parser.add_argument("--scale", type=float, default=2.0)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--budget-ms", type=int, default=4000, help="virtual time for fonts and scripts")
    parser.add_argument(
        "--fonts", type=Path, default=Path(os.environ.get("EJ_CAPTURE_FONTS", ROOT / "build" / "visual" / "fonts"))
    )
    parser.add_argument("--edge", default=os.environ.get("EDGE", DEFAULT_EDGE))
    args = parser.parse_args(argv)

    if not Path(args.edge).is_file():
        print(f"Edge not found at {args.edge} (use --edge or EDGE)", file=sys.stderr)
        return 1
    names = args.only or mockup_names()
    unknown = [n for n in names if not (SCREENS / f"{n}.dc.html").is_file()]
    if unknown:
        print(f"No such mock-up: {', '.join(unknown)}", file=sys.stderr)
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    tmp = ROOT / "build" / "visual" / "tmp"
    tmp.mkdir(parents=True, exist_ok=True)

    jobs = []
    for name in names:
        for suffix in ("", ".dark"):
            source = SCREENS / f"{name}{suffix}.dc.html"
            page = tmp / f"{name}{suffix}.html"
            page.write_text(preprocess(source.read_text(encoding="utf-8"), args.fonts), encoding="utf-8")
            jobs.append((page, args.out / f"{name}{suffix}.mock.png"))

    started = time.time()
    failures = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [(png, pool.submit(render_one, args.edge, page, png, args.scale, args.budget_ms)) for page, png in jobs]
        for png, future in futures:
            try:
                future.result()
            except Exception as error:  # noqa: BLE001 - report every page, not only the first
                failures.append(f"{png.name}: {error}")
    print(f"Rendered {len(jobs) - len(failures)} of {len(jobs)} pictures into {args.out} in {time.time() - started:.0f} s")
    for line in failures:
        print(f"  FAILED {line}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
