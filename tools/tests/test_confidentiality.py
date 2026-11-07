# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_config_files.py and check_confidential_terms.py (label: unit).

Run with: python3 -m unittest discover -s tools/tests -v
Only fictional terms are used.
"""

from __future__ import annotations

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_confidential_terms as terms_check  # noqa: E402
import check_config_files as config_check  # noqa: E402

TERM = "Quokka Kettle"
OTHER = "zanzibar"
ENV = {"CONFIDENTIAL_TERMS": f"{TERM}\n\n  {OTHER}  \n"}


def git(root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(root), "-c", "user.name=Sam Example", "-c", "user.email=sam@example.invalid",
         "-c", "commit.gpgsign=false", *args],
        check=True, capture_output=True, text=True).stdout.strip()


class Repo:
    def __init__(self) -> None:
        self._dir = tempfile.TemporaryDirectory()
        self.root = Path(self._dir.name)
        git(self.root, "init", "-q")

    def commit(self, files: dict[str, str], message: str = "Add files") -> str:
        for name, content in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        git(self.root, "add", "-A", "-f")
        git(self.root, "commit", "-q", "-m", message)
        return git(self.root, "rev-parse", "HEAD")

    def close(self) -> None:
        self._dir.cleanup()


def run(argv: list[str], env: dict[str, str]) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = terms_check.main(argv, env)
    return code, out.getvalue(), err.getvalue()


class ConfigFilesTest(unittest.TestCase):
    def test_sample_is_allowed(self) -> None:
        self.assertEqual(config_check.offending(["config/sample-config.json", "src/a.cpp"]), [])

    def test_other_config_files_are_rejected(self) -> None:
        paths = ["real-config.json", "x/y/site-config.json", "config/SAMPLE-config.json", "config/sample-config.json"]
        self.assertEqual(config_check.offending(paths), paths[:3])

    def test_similar_names_pass(self) -> None:
        self.assertEqual(config_check.offending(["config.json", "my-config.json.md", "configs/a.json"]), [])

    def test_tracked_files_of_a_repository(self) -> None:
        repo = Repo()
        try:
            repo.commit({"config/sample-config.json": "{}"})
            self.assertEqual(config_check.main(["--root", str(repo.root)]), 0)
            repo.commit({"live-config.json": "{}"})
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(config_check.main(["--root", str(repo.root)]), 1)
        finally:
            repo.close()

    def test_repository_sample_exists_and_is_versioned(self) -> None:
        import json
        root = Path(__file__).resolve().parent.parent.parent
        sample = json.loads((root / "config" / "sample-config.json").read_text(encoding="utf-8"))
        self.assertEqual(sample["schemaVersion"], 1)
        self.assertEqual(len(sample["errorTypes"]), 11)


class TermMatchingTest(unittest.TestCase):
    def pattern(self):
        return terms_check.compile_terms(terms_check.parse_terms(ENV["CONFIDENTIAL_TERMS"]))

    def test_parse_ignores_blank_lines_and_whitespace(self) -> None:
        self.assertEqual(terms_check.parse_terms(ENV["CONFIDENTIAL_TERMS"]), [TERM, OTHER])
        self.assertEqual(terms_check.parse_terms(None), [])
        self.assertEqual(terms_check.parse_terms("\n \n"), [])

    def test_case_insensitive_whole_word(self) -> None:
        text = "fine\nthe QUOKKA kettle here\nZanzibar.\nzanzibars\nunzanzibar\n"
        self.assertEqual(terms_check.scan_text(self.pattern(), text), [2, 3])

    def test_regex_characters_in_terms_are_literal(self) -> None:
        pattern = terms_check.compile_terms(["a.b", "c+"])
        self.assertEqual(terms_check.scan_text(pattern, "axb\na.b\nc+\ncc"), [2, 3])


class CheckRunTest(unittest.TestCase):
    def setUp(self) -> None:
        self.repo = Repo()
        self.addCleanup(self.repo.close)

    def test_clean_tree_passes(self) -> None:
        self.repo.commit({"a.txt": "nothing to see\n"})
        code, out, _ = run(["--root", str(self.repo.root)], ENV)
        self.assertEqual(code, 0)
        self.assertNotIn(TERM, out)

    def test_tree_match_reports_file_and_line_but_not_the_term(self) -> None:
        self.repo.commit({"docs/a.md": f"ok\nsee {TERM.upper()} now\n"})
        code, out, err = run(["--root", str(self.repo.root)], ENV)
        self.assertEqual(code, 1)
        self.assertIn("docs/a.md:2", err)
        for term in (TERM, OTHER):
            self.assertNotIn(term.lower(), (out + err).lower())

    def test_file_name_match_does_not_print_the_name(self) -> None:
        self.repo.commit({f"notes-{OTHER}.txt": "clean\n"})
        code, out, err = run(["--root", str(self.repo.root)], ENV)
        self.assertEqual(code, 1)
        self.assertNotIn(OTHER, (out + err).lower())
        self.assertIn("<tracked file name #1>", err)

    def test_commit_messages_in_range(self) -> None:
        base = self.repo.commit({"a.txt": "x\n"}, "Start")
        self.repo.commit({"b.txt": "y\n"}, f"Subject\n\nBody mentions {OTHER}")
        head = self.repo.commit({"c.txt": "z\n"}, "Clean")
        code, out, err = run(["--root", str(self.repo.root), "--range", f"{base}..{head}"], ENV)
        self.assertEqual(code, 1)
        self.assertIn("message line 3", err)
        self.assertNotIn(OTHER, (out + err).lower())

    def test_commit_before_range_is_not_checked(self) -> None:
        self.repo.commit({"a.txt": "x\n"}, f"Old {OTHER}")
        base = git(self.repo.root, "rev-parse", "HEAD")
        head = self.repo.commit({"b.txt": "y\n"}, "Clean")
        code, _, _ = run(["--root", str(self.repo.root), "--range", f"{base}..{head}"], ENV)
        self.assertEqual(code, 0)

    def test_pull_request_title_and_body(self) -> None:
        self.repo.commit({"a.txt": "x\n"})
        env = {**ENV, "PR_TITLE": f"About {TERM}", "PR_BODY": f"fine\n\nand {OTHER}"}
        code, out, err = run(["--root", str(self.repo.root)], env)
        self.assertEqual(code, 1)
        self.assertIn("pull request title", err)
        self.assertIn("pull request body line 3", err)
        self.assertNotIn(OTHER, (out + err).lower())

    def test_missing_secret_warns_and_passes_on_every_event(self) -> None:
        self.repo.commit({"a.txt": "x\n"})
        for event in ("push", "merge_group", "workflow_dispatch", "pull_request", ""):
            for env in ({}, {"CONFIDENTIAL_TERMS": ""}, {"CONFIDENTIAL_TERMS": "\n \n"}):
                code, out, _ = run(["--root", str(self.repo.root), "--event", event], env)
                self.assertEqual(code, 0, event)
                self.assertIn("::warning::", out)

    def test_unavailable_secret_on_fork_warns_and_passes(self) -> None:
        code, out, _ = run(["--root", str(self.repo.root), "--event", "pull_request", "--secrets-unavailable"], {})
        self.assertEqual(code, 0)
        self.assertIn("not available", out)

    def test_secrets_unavailable_flag_does_not_skip_when_terms_exist(self) -> None:
        self.repo.commit({"a.txt": f"{OTHER}\n"})
        argv = ["--root", str(self.repo.root), "--event", "pull_request", "--secrets-unavailable"]
        self.assertEqual(run(argv, ENV)[0], 1)


if __name__ == "__main__":
    unittest.main()
