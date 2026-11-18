#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the built documentation site: the Field Test pages are plain HTML, load nothing from other sites, and link to files that exist.

    python3 tools/ci/check_site_pages.py [--site site] [--root .]

Without --site it builds the site with `mkdocs build --strict` into a temporary folder (it needs the packages of
requirements/docs.txt) and checks that. With --site it checks a folder that is already built.

Every page:
- has a <title>, exactly one <h1>, lang="en-AU" on the Field Test pages, and a canonical address;
- links to a file that exists, with an anchor that exists in it. Links that start with "/" are the company shell's
  (/, /support/, /assets/v1/...) and are not followed, but none of them may start with /field-test/ (the pages must work
  under a preview address, so the pages of this site use relative links). An absolute link is allowed to GitHub and to gnu.org only.

The Field Test pages (tools/docs/theme/marketing.html), which run under a preview that blocks every script, also have

- no <script> except a JSON-LD data block, no <style> and no style= attribute;
- nothing loaded from another host: no src, srcset, stylesheet link or CSS url() or @import that starts with http(s):// or //;
- no mailto: link (support is through GitHub issues only), no picture that is known to show an old name of the app.

And the addresses that are printed in the store listing or in the app exist: /, privacy/, support/, guide/,
demo/demo-setup.ejconfig and the pages of the guide.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[2]
FIXED = ["index.html", "privacy/index.html", "support/index.html", "guide/index.html", "demo/demo-setup.ejconfig",
         "how-to-use/index.html", "features/index.html", "faq/index.html", "whats-new/index.html",
         "accessibility/index.html", "press/index.html", "theme.css"]
ALLOWED_HOSTS = {"github.com", "www.gnu.org"}
# Pictures that still show an old name of the app ("CS Bingo Board"): not for the Field Test pages until they are re-rendered
# (the owner does that on a Mac with Qt; docs/guide/screenshots.json lists them). Remove a name here when it is done.
STALE_PICTURES = {"first-launch.png"}


class Page(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title = ""
        self._in_title = False
        self.h1 = 0
        self.lang = ""
        self.canonical = ""
        self.ids: set[str] = set()
        self.refs: list[tuple[str, str, str]] = []  # (tag, attribute, value)
        self.scripts: list[str] = []  # the type of each <script>
        self.styles = 0
        self.style_attrs = 0
        self.material = False

    def handle_starttag(self, tag, attrs):  # noqa: ANN001, ANN201
        a = {k: (v or "") for k, v in attrs}
        if tag == "html":
            self.lang = a.get("lang", "")
        if tag == "title":
            self._in_title = True
        if tag == "h1":
            self.h1 += 1
        if tag == "script":
            self.scripts.append(a.get("type", ""))
        if tag == "style":
            self.styles += 1
        if "style" in a:
            self.style_attrs += 1
        if "id" in a:
            self.ids.add(a["id"])
        if tag == "a" and a.get("name"):
            self.ids.add(a["name"])
        if "class" in a and "md-container" in a["class"].split():
            self.material = True
        if tag == "link" and a.get("rel") == "canonical":
            self.canonical = a.get("href", "")
            return
        for attribute in ("href", "src"):
            if attribute in a:
                self.refs.append((tag, attribute, a[attribute]))
        if "srcset" in a:
            for part in a["srcset"].split(","):
                if part.strip():
                    self.refs.append((tag, "srcset", part.split()[0]))

    def handle_endtag(self, tag):  # noqa: ANN001, ANN201
        if tag == "title":
            self._in_title = False

    def handle_data(self, data):  # noqa: ANN001, ANN201
        if self._in_title:
            self.title += data


def parse(path: Path) -> Page:
    page = Page()
    page.feed(path.read_text(encoding="utf-8"))
    return page


def target_file(site: Path, page_file: Path, link: str) -> Path | None:
    """The file a relative link names, or None when it leaves the site."""
    path = unquote(urlparse(link).path)
    page_file = page_file.resolve()
    target = (page_file.parent / path).resolve() if path else page_file
    try:
        target.relative_to(site.resolve())
    except ValueError:
        return None
    if target.is_dir():
        target = target / "index.html"
    return target


def check_site(site: Path, root: Path) -> list[str]:
    problems: list[str] = []
    for name in FIXED:
        if not (site / name).is_file():
            problems.append(f"{name}: is not in the built site (it is printed in the store listing, or is a Field Test page)")
    pages = {p: parse(p) for p in sorted(site.rglob("*.html"))}
    for path, page in pages.items():
        rel = path.relative_to(site).as_posix()
        if rel == "404.html":
            continue
        field_test = not page.material
        where = rel
        if not page.title.strip():
            problems.append(f"{where}: no <title>")
        if page.h1 != 1:
            problems.append(f"{where}: has {page.h1} <h1>, not one")
        if not page.canonical:
            problems.append(f"{where}: no canonical address")
        if field_test and page.lang != "en-AU":
            problems.append(f'{where}: <html> needs lang="en-AU"')
        if field_test:
            for kind in page.scripts:
                if kind != "application/ld+json":
                    problems.append(f"{where}: has a <script>; the Field Test pages are script-free (a preview blocks scripts)")
            if page.styles or page.style_attrs:
                problems.append(f"{where}: has <style> or a style= attribute; put it in docs/assets/field-test.css")
        for tag, attribute, value in page.refs:
            value = value.strip()
            if not value or value.startswith(("data:", "javascript:")):
                continue
            if value.startswith("mailto:"):
                problems.append(f"{where}: {value}: support is through GitHub issues only, no email address")
                continue
            parts = urlparse(value)
            if parts.scheme in ("http", "https") or value.startswith("//"):
                if not field_test:
                    continue  # the guide's own links (Material's credit in the footer)
                if parts.scheme not in ("http", "https") or parts.netloc not in ALLOWED_HOSTS:
                    problems.append(f"{where}: {value}: not a link to a host this site may name ({', '.join(sorted(ALLOWED_HOSTS))})")
                elif field_test and (attribute == "srcset" or (tag != "a" and tag != "link")):
                    problems.append(f"{where}: {value}: loads something from another host")
                elif field_test and tag == "link" and "icon" not in value:
                    problems.append(f"{where}: {value}: a <link> to another host")
                continue
            if value.startswith("/"):
                if value.startswith("/field-test/"):
                    problems.append(f"{where}: {value}: write a relative link, so the page works under a preview address")
                continue
            if parts.scheme:
                problems.append(f"{where}: {value}: unknown link type")
                continue
            target = target_file(site, path, value)
            if target is None or not target.exists():
                problems.append(f"{where}: {value}: no such file in the built site")
                continue
            if field_test and tag == "img" and target.name in STALE_PICTURES:
                problems.append(f"{where}: {target.name} still shows the old name of the app; leave it off the Field Test pages")
            if parts.fragment and target.suffix == ".html":
                other = pages.get(target) or parse(target)
                if unquote(parts.fragment) not in other.ids:
                    problems.append(f"{where}: {value}: no anchor #{parts.fragment} in {target.relative_to(site).as_posix()}")
    for name in ("theme.css", "assets/field-test.css", "assets/guide.css", "assets/icl.css"):
        css = site / name
        if css.is_file():
            text = re.sub(r"/\*.*?\*/", "", css.read_text(encoding="utf-8"), flags=re.S)
            if re.search(r"@import|url\(\s*['\"]?(https?:)?//", text):
                problems.append(f"{name}: loads something with @import or an address; the site loads nothing from other sites")
    return problems


def build(root: Path, out: Path) -> int:
    result = subprocess.run([sys.executable, "-m", "mkdocs", "build", "--strict", "--quiet", "--site-dir", str(out)],
                            cwd=root, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
    return result.returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--site", type=Path, help="a site that is already built")
    args = parser.parse_args(argv)
    tmp = None
    site = args.site
    if site is None:
        if subprocess.run([sys.executable, "-c", "import mkdocs, material"], capture_output=True, check=False).returncode != 0:
            print("check_site_pages: mkdocs-material is not installed (pip install -r requirements/docs.txt): cannot build the site", file=sys.stderr)
            return 2
        tmp = tempfile.mkdtemp(prefix="ej-site-")
        site = Path(tmp)
        if build(args.root, site) != 0:
            shutil.rmtree(tmp, ignore_errors=True)
            return 1
    try:
        problems = check_site(site, args.root)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        return 1
    print("Site pages: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
