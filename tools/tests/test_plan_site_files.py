# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that a change to the documentation site's own files plans no build (label: unit).

Publishing the site runs no CI, and the pull request that gets a change to `develop` runs the static checks and the site build
only when every file it changes is in the `docs` list of .github/ci/plan.json (docs/ci.md, "A site update runs no build, and
publishing it runs no CI"). A file that no list names builds everything, silently, for an hour. So the files that only the
site reads are checked here: a new kind of file under docs/ (a font, a video) fails this test until it is added to the list.

The match is the one of plan_gate.py in icl-common-infra (fnmatch, where `*` also matches `/`, and `{a,b}` alternatives),
copied so that the test needs no checkout of that repository.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import fnmatch
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAN = json.loads((ROOT / ".github" / "ci" / "plan.json").read_text(encoding="utf-8"))

# The application reads these as well as the site, so a change to one of them has to build.
APPLICATION_READS = {"docs/design/tokens.json", "config/sample-config.json"}
PUBLISHING_WORKFLOWS = [".github/workflows/docs.yml", ".github/workflows/docs-stage.yml", ".github/workflows/publish-site.yml"]


def expand(pattern: str) -> list[str]:
    match = re.search(r"\{([^{}]*)\}", pattern)
    if not match:
        return [pattern]
    return [x for alt in match.group(1).split(",") for x in expand(pattern[:match.start()] + alt + pattern[match.end():])]


def matches(path: str, patterns: list[str]) -> bool:
    for pattern in (e for p in patterns for e in expand(p)):
        if fnmatch.fnmatch(path, pattern) or fnmatch.fnmatch(path, pattern.replace("**/", "*").replace("/**", "/*")):
            return True
    return False


def site_files() -> list[str]:
    """Every file that only the documentation site reads, as repository-relative paths."""
    found = [p.relative_to(ROOT).as_posix() for p in (ROOT / "docs").rglob("*") if p.is_file()]
    found += [p.relative_to(ROOT).as_posix() for p in (ROOT / "tools" / "docs").rglob("*")
              if p.is_file() and "__pycache__" not in p.parts]
    found += ["mkdocs.yml", "requirements/docs.txt", *PUBLISHING_WORKFLOWS]
    return sorted(f for f in found if f not in APPLICATION_READS)


class SiteFilesPlanNoBuild(unittest.TestCase):
    def test_there_are_site_files_to_check(self) -> None:
        files = site_files()
        self.assertGreater(len(files), 50)
        self.assertIn("docs/theme.css", files)
        self.assertIn("docs/guide/screenshots.json", files)

    def test_every_file_that_only_the_site_reads_is_documentation(self) -> None:
        unlisted = [f for f in site_files() if not matches(f, PLAN["docs"])]
        self.assertEqual(unlisted, [], "these files plan a full build; add them to `docs` in .github/ci/plan.json")

    def test_the_files_the_application_reads_still_build(self) -> None:
        # Neither is documentation, so the plan builds on a change to them: the colours are a `build` path, and the sample setup
        # is named by no list, which the planner treats as a build too.
        for path in APPLICATION_READS:
            self.assertFalse(matches(path, PLAN["docs"]), f"{path} is read by the application, so it must not be documentation")
            self.assertFalse(matches(path, PLAN["no_build"]), f"{path} is read by the application, so it must build")
        self.assertTrue(matches("docs/design/tokens.json", PLAN["build"]))

    def test_the_gates_own_files_still_build_everything(self) -> None:
        for path in [".github/workflows/ci.yml", ".github/workflows/release.yml", ".github/ci/plan.json",
                     "tools/ci/check_site_pages.py"]:
            self.assertFalse(matches(path, PLAN["docs"]), f"{path} must not be documentation")
            self.assertTrue(matches(path, PLAN["machinery"]), f"{path} must stay machinery")

    def test_the_two_publishing_workflows_are_documentation(self) -> None:
        for path in PUBLISHING_WORKFLOWS:
            self.assertTrue(matches(path, PLAN["docs"]), path)


if __name__ == "__main__":
    unittest.main()
