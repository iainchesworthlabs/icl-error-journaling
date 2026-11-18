# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_site_kit.py, check_site_pages.py and site_shell_hook.py (label: unit). The Field Test pages.

Run with: python3 -m unittest discover -s tools/tests -v
The page checks run on small sites written to a temporary folder; the kit check runs on a copy of the kit.
"""

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ci"))

import check_site_kit as kit_check  # noqa: E402
import check_site_pages as pages_check  # noqa: E402


def load_hook():
    spec = importlib.util.spec_from_file_location("site_shell_hook", ROOT / "tools" / "docs" / "site_shell_hook.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PAGE = """<!doctype html><html lang="en-AU" data-app="field-test"><head><title>T</title>
<link rel="canonical" href="https://example.test/x/"><link rel="stylesheet" href="{css}"></head>
<body><main><h1>Heading</h1>{body}</main></body></html>"""


def page(body: str = "", css: str = "/assets/shell.css") -> str:
    return PAGE.format(body=body, css=css)


class KitCheck(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        shutil.copytree(ROOT / kit_check.KIT, self.root / kit_check.KIT)
        shutil.copyfile(ROOT / kit_check.THEME, self._theme())

    def _theme(self) -> Path:
        path = self.root / kit_check.THEME
        path.parent.mkdir(parents=True, exist_ok=True)
        return path

    def tearDown(self) -> None:
        self.dir.cleanup()

    def test_the_pinned_copy_and_palette_pass(self) -> None:
        self.assertEqual(kit_check.check_sums(self.root), [])
        self.assertEqual(kit_check.check_palette(self.root), [])

    def test_an_edited_kit_file_fails(self) -> None:
        with (self.root / kit_check.KIT / "icl.css").open("a", encoding="utf-8") as handle:
            handle.write("/* edited */\n")
        self.assertTrue(any("icl.css" in p and "edited" in p for p in kit_check.check_sums(self.root)))

    def test_a_missing_and_an_extra_kit_file_fail(self) -> None:
        (self.root / kit_check.KIT / "kit" / "theme.css").unlink()
        (self.root / kit_check.KIT / "extra.css").write_text("x", encoding="utf-8")
        problems = kit_check.check_sums(self.root)
        self.assertTrue(any("kit/theme.css" in p and "missing" in p for p in problems))
        self.assertTrue(any("extra.css" in p for p in problems))

    def test_line_endings_are_not_compared(self) -> None:
        path = self.root / kit_check.KIT / "icl.css"
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(kit_check.check_sums(self.root), [])

    def test_a_palette_that_fails_contrast_is_reported(self) -> None:
        theme = self.root / kit_check.THEME
        theme.write_text(theme.read_text(encoding="utf-8").replace("light-dark(#1e5b7a, #7bb8dd)", "light-dark(#9fc5db, #7bb8dd)", 1),
                         encoding="utf-8")
        problems = kit_check.check_palette(self.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("FAIL", problems[0])

    def test_a_property_that_is_not_a_role_is_reported(self) -> None:
        theme = self.root / kit_check.THEME
        theme.write_text(theme.read_text(encoding="utf-8").replace("}", "  --app-extra: #000000;\n}"), encoding="utf-8")
        self.assertTrue(kit_check.check_palette(self.root))


class PagesCheck(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.site = Path(self.dir.name)
        for name in pages_check.FIXED:
            path = self.site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(page() if name.endswith(".html") else "x", encoding="utf-8")

    def tearDown(self) -> None:
        self.dir.cleanup()

    def problems(self, body: str) -> list[str]:
        (self.site / "faq" / "index.html").write_text(page(body), encoding="utf-8")
        return pages_check.check_site(self.site, ROOT)

    def test_a_clean_site_passes(self) -> None:
        self.assertEqual(self.problems('<a href="../privacy/">p</a> <a href="https://github.com/x">g</a> <a href="/support/">shell</a>'), [])

    def test_a_missing_fixed_address_is_reported(self) -> None:
        (self.site / "privacy" / "index.html").unlink()
        self.assertTrue(any("privacy/index.html" in p for p in pages_check.check_site(self.site, ROOT)))

    def test_a_script_is_reported_but_json_ld_is_not(self) -> None:
        self.assertTrue(any("script" in p for p in self.problems("<script>x</script>")))
        self.assertEqual(self.problems('<script type="application/ld+json">{}</script>'), [])

    def test_a_load_from_another_host_is_reported(self) -> None:
        self.assertTrue(self.problems('<img src="https://example.org/a.png">'))
        self.assertTrue(self.problems('<a href="https://example.org/">x</a>'))

    def test_a_style_attribute_is_reported(self) -> None:
        self.assertTrue(any("style" in p for p in self.problems('<p style="color:red">x</p>')))

    def test_a_broken_link_and_anchor_are_reported(self) -> None:
        self.assertTrue(any("no such file" in p for p in self.problems('<a href="../nope/">x</a>')))
        self.assertTrue(any("no anchor" in p for p in self.problems('<a href="../privacy/#missing">x</a>')))

    def test_an_anchor_that_exists_passes(self) -> None:
        (self.site / "privacy" / "index.html").write_text(page('<h2 id="there">x</h2>'), encoding="utf-8")
        self.assertEqual(self.problems('<a href="../privacy/#there">x</a>'), [])

    def test_a_link_that_names_the_section_is_reported(self) -> None:
        self.assertTrue(any("relative" in p for p in self.problems('<a href="/field-test/faq/">x</a>')))

    def test_an_email_address_is_reported(self) -> None:
        self.assertTrue(any("email" in p for p in self.problems('<a href="mailto:a@b.test">x</a>')))

    def test_the_stale_picture_is_reported(self) -> None:
        picture = self.site / "guide" / "img" / "first-launch.png"
        picture.parent.mkdir(parents=True)
        picture.write_bytes(b"x")
        self.assertTrue(any("old name" in p for p in self.problems('<img src="../guide/img/first-launch.png" alt="">')))

    def test_a_page_needs_one_heading(self) -> None:
        (self.site / "faq" / "index.html").write_text(page().replace("</main>", "<h1>Two</h1></main>"), encoding="utf-8")
        self.assertTrue(any("<h1>" in p for p in pages_check.check_site(self.site, ROOT)))

    def test_a_stylesheet_that_imports_is_reported(self) -> None:
        (self.site / "assets").mkdir()
        (self.site / "assets" / "field-test.css").write_text('@import url("https://example.org/f.css");', encoding="utf-8")
        self.assertTrue(any("field-test.css" in p for p in pages_check.check_site(self.site, ROOT)))


class ShellHook(unittest.TestCase):
    def test_data_app_is_added_once(self) -> None:
        hook = load_hook()
        once = hook.add_app('<!doctype html><html lang="en"><head></head></html>')
        self.assertIn('<html data-app="field-test" lang="en">', once)
        self.assertEqual(hook.add_app(once), once)

    def test_a_page_with_no_html_tag_raises(self) -> None:
        with self.assertRaises(ValueError):
            load_hook().add_app("<p>no page</p>")


if __name__ == "__main__":
    unittest.main()
