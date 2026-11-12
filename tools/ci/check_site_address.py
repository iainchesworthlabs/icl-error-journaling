#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when the documentation site's address is written down in more than one way.

    python3 tools/ci/check_site_address.py [--root .]

`site_url` in mkdocs.yml is the one place that holds the address (docs/plan/stage-10.md, "Hosting"). The pages of the
guide write `{{ site_url }}` and a hook (tools/docs/site_address_hook.py) fills it in at build time. The files that cannot
read mkdocs.yml must hold the same address, and this check compares them:

- store/appstore/en-GB/support_url.txt, privacy_url.txt and marketing_url.txt: <site>support/, <site>privacy/, <site>;
- the demo setup file address in store/appstore/review_information/notes.txt;
- no page of docs/ other than the plan pages writes the host out.

The app's own constant joins this list with work package 10.6. A move to a custom domain is then: change `site_url`,
fix what this check names, and set the domain in the repository's Pages settings (an owner action).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import site_address  # noqa: E402

URL_FILES = {
    "store/appstore/en-GB/support_url.txt": "support/",
    "store/appstore/en-GB/privacy_url.txt": "privacy/",
    "store/appstore/en-GB/marketing_url.txt": "",
}
NOTES = "store/appstore/review_information/notes.txt"
# Pages that describe the decision itself and the history of the old address may name hosts.
PAGES_THAT_MAY_NAME_THE_HOST = ("docs/plan/", "docs/ci.md")


def check(root: Path) -> list[str]:
    problems: list[str] = []
    site = site_address.site_url(root)
    host = site_address.host(root)
    for rel, tail in URL_FILES.items():
        path = root / rel
        if not path.is_file():
            continue
        value = path.read_text(encoding="utf-8").strip()
        if value != site + tail:
            problems.append(f"{rel}: is {value!r} but site_url in mkdocs.yml gives {site + tail!r}")
    notes = root / NOTES
    if notes.is_file():
        for url in re.findall(r"https://[^\s\"')>]+", notes.read_text(encoding="utf-8")):
            if url.endswith(".ejconfig") and not url.startswith(site):
                problems.append(f"{NOTES}: {url} is not under {site}")
    for page in sorted((root / "docs").rglob("*.md")):
        rel = page.relative_to(root).as_posix()
        if rel.startswith(PAGES_THAT_MAY_NAME_THE_HOST[0]) or rel == PAGES_THAT_MAY_NAME_THE_HOST[1]:
            continue
        if host in page.read_text(encoding="utf-8"):
            problems.append(f"{rel}: writes the site's host out; write {{{{ site_url }}}} so one edit moves the site")
    return problems


def urlhost(url: str) -> str:
    return re.sub(r"^https://([^/]+).*$", r"\1", url)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=site_address.ROOT)
    args = parser.parse_args(argv)
    try:
        problems = check(args.root)
    except (OSError, ValueError) as error:
        print(f"check_site_address: cannot check: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        return 1
    print("Site address: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
