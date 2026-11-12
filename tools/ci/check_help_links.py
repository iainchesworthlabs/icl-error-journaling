#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when the in-app help and the user guide do not agree.

    python3 tools/ci/check_help_links.py [--root .]

Stage 10, package 10.6 (docs/plan/stage-10.md, "Keeping the docs in step"). It reads the help topics of the app
(apps/app/qml/components/HelpTopics.qml) and the pages of the guide (docs/), and needs no build:

- the site address of the app is `site_url` in mkdocs.yml, to the character, so a move to another host changes both;
- every topic has an id, a title, two to five paragraphs and a page, and its page is a file of the guide;
- when a topic names a heading of its page (`anchor`), the page has that heading, as MkDocs makes the anchor of it;
- the pages and the heading that the Help group and the import errors screen open exist the same way;
- every topic id that a screen asks for (`helpRequested("...")`, `helpTopicRequested("...")`, `show("...")`) is a topic.

So a heading of the guide cannot be renamed, and a page cannot be moved, without the app's links being changed in the
same pull request. Exit codes: 0 all agree, 1 they do not, 2 a file could not be read.
"""

from __future__ import annotations

import argparse
import re
import sys
import unicodedata
from pathlib import Path

TOPICS = "apps/app/qml/components/HelpTopics.qml"
MKDOCS = "mkdocs.yml"
DOCS = "docs"
QML = "apps/app/qml"
MIN_PARAGRAPHS = 2
MAX_PARAGRAPHS = 5

TOPIC = re.compile(r'"id":\s*"(?P<id>[^"]+)"(?P<body>.*?)"anchor":\s*"(?P<anchor>[^"]*)"', re.DOTALL)
PROPERTY = re.compile(r'readonly property string (?P<name>\w+):\s*"(?P<value>[^"]*)"')
HEADING = re.compile(r"^(?P<marks>#{1,6})\s+(?P<text>.+?)\s*$")
ATTRIBUTE_ID = re.compile(r"\{[^}]*#(?P<id>[A-Za-z0-9_-]+)[^}]*\}\s*$")
ASKS = re.compile(r'\b(?:helpRequested|helpTopicRequested|showHelp|show)\(\s*"(?P<id>[A-Za-z]+)"\s*\)')


def slug(text: str) -> str:
    """The anchor that MkDocs (Python-Markdown's `toc`) makes of a heading."""
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii")
    text = re.sub(r"[^\w\s-]", "", text).strip().lower()
    return re.sub(r"[-\s]+", "-", text)


def strip_markup(text: str) -> str:
    """The text of a heading as the reader sees it: links by their words, no code ticks, no emphasis stars."""
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", lambda match: match.group(1), text)
    return text.replace("`", "").replace("*", "")


def anchors(markdown: str) -> set[str]:
    """The anchors of the headings of a page, outside fenced blocks."""
    found: set[str] = set()
    fenced = False
    for line in markdown.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            continue
        heading = HEADING.match(line)
        if not heading:
            continue
        text = heading.group("text")
        explicit = ATTRIBUTE_ID.search(text)
        if explicit:
            found.add(explicit.group("id"))
            continue
        found.add(slug(strip_markup(text)))
    return found


def page_file(root: Path, page: str) -> Path:
    """The Markdown file that the site address `page` ('guide/logging/', 'support/', '') is built from."""
    trimmed = page.strip("/")
    if not trimmed:
        return root / DOCS / "index.md"
    return root / DOCS / f"{trimmed}.md"


def site_url(root: Path) -> str | None:
    match = re.search(r"^site_url:\s*(\S+)\s*$", (root / MKDOCS).read_text(encoding="utf-8"), re.MULTILINE)
    if not match:
        return None
    value = match.group(1).strip("'\"")
    return value if value.endswith("/") else value + "/"


def check_target(root: Path, what: str, page: str, anchor: str, cache: dict[Path, set[str]]) -> list[str]:
    path = page_file(root, page)
    if not path.is_file():
        return [f"{what}: the page '{page}' is not a file of the guide ({path.relative_to(root).as_posix()})"]
    if anchor:
        if path not in cache:
            cache[path] = anchors(path.read_text(encoding="utf-8"))
        if anchor not in cache[path]:
            return [f"{what}: '{path.relative_to(root).as_posix()}' has no heading with the anchor #{anchor}"]
    return []


def check(root: Path) -> list[str]:
    problems: list[str] = []
    text = (root / TOPICS).read_text(encoding="utf-8")
    properties = {m.group("name"): m.group("value") for m in PROPERTY.finditer(text)}
    cache: dict[Path, set[str]] = {}

    expected = site_url(root)
    if expected is None:
        problems.append(f"{MKDOCS}: there is no site_url")
    elif properties.get("siteUrl") != expected:
        problems.append(
            f"{TOPICS}: siteUrl is '{properties.get('siteUrl')}' and {MKDOCS} has site_url '{expected}'; change them together"
        )

    topics = list(TOPIC.finditer(text))
    if not topics:
        problems.append(f"{TOPICS}: no topic was found")
    seen: set[str] = set()
    for match in topics:
        topic = match.group("id")
        body = match.group("body")
        if topic in seen:
            problems.append(f"{TOPICS}: the topic '{topic}' is listed twice")
        seen.add(topic)
        if not re.search(r'"title":\s*qsTr\("[^"]+"\)', body):
            problems.append(f"{TOPICS}: the topic '{topic}' has no title")
        paragraphs = re.search(r'"paragraphs":\s*\[(?P<list>.*?)\]\s*,\s*"page"', body, re.DOTALL)
        count = len(re.findall(r"qsTr\(", paragraphs.group("list"))) if paragraphs else 0
        if not MIN_PARAGRAPHS <= count <= MAX_PARAGRAPHS:
            problems.append(
                f"{TOPICS}: the topic '{topic}' has {count} paragraphs; a help sheet has {MIN_PARAGRAPHS} to {MAX_PARAGRAPHS}"
            )
        page = re.search(r'"page":\s*"(?P<page>[^"]*)"', body)
        if not page or not page.group("page"):
            problems.append(f"{TOPICS}: the topic '{topic}' names no page of the guide")
            continue
        problems += check_target(root, f"topic '{topic}'", page.group("page"), match.group("anchor"), cache)

    for name in ("guidePath", "supportPath", "privacyPath"):
        if name not in properties:
            problems.append(f"{TOPICS}: {name} is missing")
        else:
            problems += check_target(root, name, properties[name], "", cache)
    if "setupFileFixPage" not in properties or "setupFileFixAnchor" not in properties:
        problems.append(f"{TOPICS}: setupFileFixPage and setupFileFixAnchor are needed by the import errors screen")
    else:
        problems += check_target(
            root, "the import errors row", properties["setupFileFixPage"], properties["setupFileFixAnchor"], cache
        )

    for path in sorted((root / QML).rglob("*.qml")):
        if path.name == "HelpTopics.qml":
            continue
        for ask in ASKS.finditer(path.read_text(encoding="utf-8")):
            if ask.group("id") not in seen:
                problems.append(f"{path.relative_to(root).as_posix()}: asks for the help topic '{ask.group('id')}', which does not exist")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent.parent))
    args = parser.parse_args(argv)
    root = Path(args.root)
    try:
        problems = check(root)
    except OSError as error:
        print(f"Help links: cannot read {error.filename}: {error.strerror}", file=sys.stderr)
        return 2
    if problems:
        print("Help links: the app and the guide do not agree:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    print("Help links: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
