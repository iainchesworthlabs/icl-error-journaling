#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn a copy of this template into a new project.

    python3 tools/instantiate.py --name my_project [--description "One line"] [--repo my-project] [--dry-run]

--name is lower snake case. It replaces icl_template in namespaces, targets, directories and
include paths; MY_PROJECT replaces ICL_TEMPLATE (CMake options and the variable prefix); my-project
replaces icl-template (the vcpkg name and archive names). --repo is the name of the GitHub repository
when it differs from my-project; it replaces icl-cpp-template in URLs. The iOS bundle identifier
com.iainchesworthlabs.icltemplate becomes com.iainchesworthlabs.myproject.

The script also rewrites what describes the template: the README and the documentation index become
short stubs for the new project, docs/new-project.md is removed, the description and the Pages address go
into .github/repo-settings.json and CMakeLists.txt, and mkdocs.yml is renamed. The changelog is reset
and the script removes itself unless --keep is given.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_DIRS = {".git", "build", ".icl-infra", "stage", "dist", "__pycache__", ".venv"}
NAME = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


def is_text(path: Path) -> bool:
    try:
        path.read_text(encoding="utf-8")
        return True
    except (UnicodeDecodeError, OSError):
        return False


def files() -> list[Path]:
    out = []
    for path in sorted(ROOT.rglob("*")):
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIRS for part in rel.parts):
            continue
        if path.is_file() and path != Path(__file__).resolve():
            out.append(path)
    return out


def rewrite_description(kebab: str, repo: str, description: str) -> None:
    """Replace the text that describes the template with text for the new project."""
    def sub(rel: str, pairs: list[tuple[str, str]]) -> None:
        path = ROOT / rel
        if not path.exists():
            return
        text = path.read_text(encoding="utf-8")
        for old, new in pairs:
            text = text.replace(old, new)
        path.write_text(text, encoding="utf-8", newline="")

    sub("CMakeLists.txt", [('DESCRIPTION "Template for new C++23 projects"', f'DESCRIPTION "{description}"')])
    sub("mkdocs.yml", [("site_name: ICL C++ template", f"site_name: {kebab}"),
                       ("site_description: Template for C++23 projects in the iainchesworthlabs organisation",
                        f"site_description: {description}"),
                       ("  - Starting a project: new-project.md\n", "")])
    (ROOT / "docs" / "new-project.md").unlink(missing_ok=True)
    (ROOT / "docs" / "index.md").write_text(
        f"# {kebab}\n\n{description}. It builds, tests and releases with the shared infrastructure in\n"
        "[icl-common-infra](https://github.com/iainchesworthlabs/icl-common-infra).\n\n"
        "| Page | Covers |\n|---|---|\n"
        "| [Building](building.md) | Prerequisites, shared roots, presets |\n"
        "| [Architecture](architecture.md) | Layout, platform sources, naming |\n"
        "| [Testing](testing.md) | Test kinds, labels, coverage floors |\n"
        "| [Continuous integration](ci.md) | What runs when, required checks |\n"
        "| [Releasing](releasing.md) | Changelog, tags, signing, publishing |\n", encoding="utf-8")
    (ROOT / "README.md").write_text(
        f"# {kebab}\n\n"
        f"[![CI](https://github.com/iainchesworthlabs/{repo}/actions/workflows/ci.yml/badge.svg?branch=develop)]"
        f"(https://github.com/iainchesworthlabs/{repo}/actions/workflows/ci.yml)\n\n"
        f"{description}. It builds with CMake presets and vcpkg, tests with Catch2 and CTest, and uses the shared\n"
        "CI, release and repository settings in\n"
        "[icl-common-infra](https://github.com/iainchesworthlabs/icl-common-infra).\n\n"
        "```bash\ncmake --preset linux-debug\ncmake --build --preset linux-debug\nctest --preset linux-debug\n```\n\n"
        "Documentation is in `docs/`.\n\n"
        "Licence: GPL-3.0-or-later. Contributions need a DCO sign-off (`git commit -s`).\n", encoding="utf-8")

    settings = ROOT / ".github" / "repo-settings.json"
    if settings.exists():
        import json
        data = json.loads(settings.read_text(encoding="utf-8"))
        data = {"description": description, "homepage": f"https://iainchesworthlabs.github.io/{repo}/", **data}
        settings.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--description", default="")
    ap.add_argument("--repo", default="", help="repository name when it is not the kebab-case project name")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep this script")
    args = ap.parse_args()
    if not NAME.match(args.name):
        print("--name must be lower snake case, for example my_project", file=sys.stderr)
        return 2
    snake, upper, kebab = args.name, args.name.upper(), args.name.replace("_", "-")
    repo = args.repo or kebab
    description = args.description or f"{kebab} project"
    # The bundle identifier is com.iainchesworthlabs.<name without separators>, as the other applications' are. The display
    # name is the one under the icon; the owner may want a nicer one.
    pairs = [("com.iainchesworthlabs.icltemplate", f"com.iainchesworthlabs.{snake.replace('_', '')}"),
             ("ICL Template", snake.replace("_", " ").title()),
             ("icl-cpp-template", repo), ("icl_template", snake), ("ICL_TEMPLATE", upper), ("icl-template", kebab)]

    changed = 0
    for path in files():
        if not is_text(path):
            continue
        text = path.read_text(encoding="utf-8")
        new = text
        for old, repl in pairs:
            new = new.replace(old, repl)
        if new != text:
            changed += 1
            print(f"edit   {path.relative_to(ROOT)}")
            if not args.dry_run:
                path.write_text(new, encoding="utf-8", newline="")

    # Rename deepest paths first so a parent rename does not invalidate a child path.
    renames = sorted((p for p in ROOT.rglob("*") if "icl_template" in p.name
                      and not any(part in SKIP_DIRS for part in p.relative_to(ROOT).parts)),
                     key=lambda p: len(p.parts), reverse=True)
    for path in renames:
        target = path.with_name(path.name.replace("icl_template", snake))
        print(f"rename {path.relative_to(ROOT)} -> {target.relative_to(ROOT)}")
        if not args.dry_run:
            path.rename(target)

    if not args.dry_run:
        rewrite_description(kebab, repo, description)
        for fragment in (ROOT / "changelog.d").glob("*.md"):
            if fragment.name != "README.md":
                fragment.unlink()
        changelog = ROOT / "CHANGELOG.md"
        if changelog.exists():
            changelog.write_text("# Changelog\n\nAll notable changes are listed here. Each release section is assembled from the\n"
                                 "fragments in `changelog.d/`.\n", encoding="utf-8")
        if not args.keep:
            Path(__file__).resolve().unlink()
    print(f"{changed} files edited, {len(renames)} paths renamed")
    print("Next: set the topics in .github/repo-settings.json, update .github/CODEOWNERS, then apply the "
          "repository settings with icl-common-infra/admin/apply.sh (dry run first) and open the first pull request.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
