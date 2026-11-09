#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail when the application's own code could use the network (docs/plan/stage-11.md, 11.2; PRD NFR-09).

    python3 tools/ci/check_no_network.py [--root .]

The store answers ("Data Not Collected", Play's "no data collected or shared") and the privacy policy say that the
application never connects to anything. Qt QML links Qt Network into the iOS executable all the same (the kit is built
with qml_network, so a QML item could load a remote URL), and packaging/ios/verify_app.sh reports that code. This check
covers the other half: the application's own sources in apps/ and libs/ (tests and test rigs aside) must not

  * use a network API: Qt Network classes or headers, NSURLSession or NSURLConnection, CFNetwork, Network.framework, BSD
    sockets or the resolver;
  * link Qt6::Network or ask find_package for Network;
  * name a remote URL (http, https, ftp, ws or wss), other than the project's own pages on GitHub and its documentation
    site. Those only ever open in the browser, outside the application, through Qt.openUrlExternally.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRS = ("apps", "libs")
SUFFIXES = {".cpp", ".hpp", ".h", ".mm", ".m", ".qml", ".js", ".txt", ".cmake", ".plist", ".in"}
SKIPPED_PARTS = {"tests", "test", "rig"}
ALLOWED_URL = re.compile(
    r"^https://(github\.com/iainchesworthlabs/|iainchesworthlabs\.github\.io/|www\.gnu\.org/licenses/)"
)
URL = re.compile(r"\b(?:https?|ftp|wss?)://[^\s\"'<>)]+", re.IGNORECASE)
# Property list and XML namespaces name a URL that nothing ever fetches.
SCHEMA_URL = re.compile(r"^http://www\.apple\.com/DTDs/|^http://www\.w3\.org/")
NETWORK_APIS = re.compile(
    r"\b(QNetwork\w+|QTcpSocket|QTcpServer|QUdpSocket|QSslSocket|QSslConfiguration|QWebSocket\w*|QHostInfo|QHostAddress|"
    r"QDnsLookup|QLocalSocket|QLocalServer|QtNetwork|NSURLSession\w*|NSURLConnection|CFNetwork|CFSocket\w*|"
    r"CFStreamCreatePairWithSocket\w*|nw_connection_\w+|nw_listener_\w+|getaddrinfo|gethostbyname)\b"
    r"|#\s*(?:include|import)\s*[<\"](?:sys/socket\.h|netinet/|arpa/inet\.h|netdb\.h|Network/|CFNetwork/)"
)
CMAKE_NETWORK = re.compile(r"Qt6?::Network\b|find_package\s*\([^)]*\bNetwork\b", re.IGNORECASE)


def sources(root: Path) -> list[Path]:
    found = []
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SUFFIXES:
                continue
            if SKIPPED_PARTS & set(path.relative_to(root).parts):
                continue
            found.append(path)
    return found


def check_text(rel: str, text: str) -> list[str]:
    problems = []
    is_cmake = rel.endswith((".txt", ".cmake"))
    for number, line in enumerate(text.splitlines(), 1):
        if is_cmake:
            if CMAKE_NETWORK.search(line.split("#", 1)[0]):
                problems.append(f"{rel}:{number}: links Qt Network; the application has no network access")
        else:
            match = NETWORK_APIS.search(line)
            if match:
                problems.append(f"{rel}:{number}: uses a network API ({match.group(0).strip()})")
        for url in URL.findall(line):
            if not (ALLOWED_URL.match(url) or SCHEMA_URL.match(url)):
                problems.append(f"{rel}:{number}: names a remote URL ({url}); only the project's own pages may appear")
    return problems


def check(root: Path) -> list[str]:
    problems = []
    for path in sources(root):
        rel = path.relative_to(root).as_posix()
        problems += check_text(rel, path.read_text(encoding="utf-8", errors="replace"))
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    problems = check(args.root)
    for problem in problems:
        print(f"::error::{problem}")
    if problems:
        return 1
    print("No network use: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
