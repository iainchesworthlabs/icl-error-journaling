# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_site_address.py and the site_address_hook (label: unit). The site's address in one place.

Run with: python3 -m unittest discover -s tools/tests -v
Each case copies the files the check reads into a temporary folder and changes one of them.
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ci"))

import check_site_address as address_check  # noqa: E402
import site_address  # noqa: E402

FILES = ("mkdocs.yml", "store/appstore/en-GB/support_url.txt", "store/appstore/en-GB/privacy_url.txt",
         "store/appstore/en-GB/marketing_url.txt", address_check.NOTES, "docs/index.md", "docs/guide/setup-file.md")


def load_hook():
    spec = importlib.util.spec_from_file_location("site_address_hook", ROOT / "tools" / "docs" / "site_address_hook.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SiteAddressCheck(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        for name in FILES:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)

    def tearDown(self) -> None:
        self.dir.cleanup()

    def change(self, name: str, old: str, new: str) -> None:
        path = self.root / name
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text, "the test's starting text moved; update the test")
        path.write_text(text.replace(old, new), encoding="utf-8")

    def test_the_repository_agrees_with_itself(self) -> None:
        self.assertEqual(address_check.check(self.root), [])

    def test_a_new_domain_in_mkdocs_alone_is_found_in_the_store_files(self) -> None:
        self.change("mkdocs.yml", site_address.site_url(self.root), "https://guide.example.test/")
        found = address_check.check(self.root)
        self.assertTrue(any("support_url.txt" in p and "guide.example.test/support/" in p for p in found))
        self.assertTrue(any("marketing_url.txt" in p for p in found))
        self.assertTrue(any("notes.txt" in p for p in found))

    def test_a_new_domain_everywhere_passes(self) -> None:
        old = site_address.site_url(self.root)
        for name in ("mkdocs.yml", "store/appstore/en-GB/support_url.txt", "store/appstore/en-GB/privacy_url.txt",
                     "store/appstore/en-GB/marketing_url.txt", address_check.NOTES):
            self.change(name, old, "https://guide.example.test/")
        self.assertEqual(address_check.check(self.root), [])

    def test_a_page_that_writes_the_host_out_is_found(self) -> None:
        self.change("docs/index.md", "{{ site_url }}", site_address.site_url(self.root))
        self.assertTrue(any("docs/index.md" in p for p in address_check.check(self.root)))

    def test_a_missing_site_url_is_not_a_pass(self) -> None:
        self.change("mkdocs.yml", "site_url:", "site_address:")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(address_check.main(["--root", str(self.root)]), 2)

    def test_a_plain_http_address_is_refused(self) -> None:
        self.change("mkdocs.yml", "site_url: https://", "site_url: http://")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(address_check.main(["--root", str(self.root)]), 2)

    def test_main_reports_ok_and_failure(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(address_check.main(["--root", str(self.root)]), 0)
        self.assertIn("ok", out.getvalue())
        self.change("store/appstore/en-GB/support_url.txt", "support/", "help/")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(address_check.main(["--root", str(self.root)]), 1)


class Hook(unittest.TestCase):
    def test_the_token_becomes_the_address(self) -> None:
        hook = load_hook()
        self.assertEqual(hook.fill("[a]({{ site_url }}demo/x)", "https://guide.example.test"), "[a](https://guide.example.test/demo/x)")

    def test_a_page_without_the_token_is_unchanged(self) -> None:
        self.assertEqual(load_hook().fill("Write `{{` or `}}`.", "https://guide.example.test/"), "Write `{{` or `}}`.")


if __name__ == "__main__":
    unittest.main()
