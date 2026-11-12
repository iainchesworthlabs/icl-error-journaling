# SPDX-License-Identifier: GPL-3.0-or-later
"""The address of the documentation site, read from the one place that holds it: `site_url` in mkdocs.yml.

Moving the site to a custom domain is a change to that line and to the files that cannot read it (the store URL files and
the app's constant), and tools/ci/check_site_address.py fails until they all agree. See docs/ci.md, "The site address".
"""

from __future__ import annotations

import re
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[2]


def site_url(root: Path = ROOT) -> str:
    """`site_url` of mkdocs.yml, with exactly one trailing slash."""
    text = (root / "mkdocs.yml").read_text(encoding="utf-8")
    found = re.search(r"^site_url:\s*(\S+)\s*$", text, re.MULTILINE)
    if not found:
        raise ValueError("mkdocs.yml has no site_url")
    value = found.group(1).strip("'\"").rstrip("/") + "/"
    parts = urlparse(value)
    if parts.scheme != "https" or not parts.netloc:
        raise ValueError(f"site_url must be an https address, not {value!r}")
    return value


def host(root: Path = ROOT) -> str:
    return urlparse(site_url(root)).netloc


def path(root: Path = ROOT) -> str:
    return urlparse(site_url(root)).path
