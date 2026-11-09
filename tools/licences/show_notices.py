#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print what tools/licences/notices.py found, as Markdown, for a build log or a job summary.

    python3 tools/licences/show_notices.py build/ios-release/apps/app/about/third-party-notices.json [--inputs]

The components with their version, licence and how they are linked; with --inputs also every input of the link line and what
the generator made of it. Used by the iOS build workflow, so the facts for a pull request (which licences ship in the iPhone
binary, and whether each library is static) are read off the real link, not worked out by hand.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("notices", type=Path)
    parser.add_argument("--inputs", action="store_true", help="also list the inputs of the link line")
    args = parser.parse_args(argv)
    document = json.loads(args.notices.read_text(encoding="utf-8"))

    print(f"### Third-party notices for {document['platform']} (Qt {document.get('qtVersion', '')})")
    print()
    print(f"{len(document['components'])} components; {len(document['unresolved'])} unresolved.")
    print()
    print("| Component | Version | Licence | Linked |")
    print("|---|---|---|---|")
    for component in document["components"]:
        print(f"| {component['name']} | {component['version']} | {component['licenceDisplay']} | {component['linking']} |")
    print()
    kinds = Counter((item["kind"], item["linking"]) for item in document["linked"])
    print("Link inputs by kind and linking: " + ", ".join(f"{k} {l}: {n}" for (k, l), n in sorted(kinds.items())))
    ids = sorted({i for c in document["components"] for i in c["textIds"]} | set(document["exemptions"]))
    print()
    print("Licence ids: " + ", ".join(ids))
    for problem in document["unresolved"]:
        print(f"\nUNRESOLVED: {problem}")
    if args.inputs:
        print()
        print("<details><summary>Link inputs</summary>")
        print()
        print("| Kind | Linking | Input |")
        print("|---|---|---|")
        for item in document["linked"]:
            extra = f" (file starts {item['header']})" if item.get("header") else ""
            print(f"| {item['kind']} | {item['linking']} | `{item['input']}`{extra} |")
        print()
        print("</details>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
