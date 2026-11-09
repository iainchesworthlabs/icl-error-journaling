# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_qml_platform.py, check_qml_literals.py, check_qml_strings.py, check_qml_tests.py and
check_qml_motion.py (label: unit).

Each check is shown a file that follows the rule and a file that breaks it, because a check that cannot fail proves
nothing. Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_qml_literals as literals  # noqa: E402
import check_qml_motion as motion  # noqa: E402
import check_qml_platform as platform  # noqa: E402
import check_qml_strings as strings  # noqa: E402
import check_qml_tests as tests  # noqa: E402
import qml_source as source  # noqa: E402


def make(root: Path, files: dict[str, str]) -> None:
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def component(body: str) -> str:
    return f"import QtQuick\nimport ErrorJournalingApp\nItem {{\n{body}\n}}\n"


class SourceTest(unittest.TestCase):
    def test_comments_are_blanked_and_strings_kept(self) -> None:
        text = 'a: "x // not a comment" // a comment\n/* block\nover lines */ b: 1\n'
        blanked = source.blank_comments(text)
        self.assertEqual(len(blanked), len(text))
        self.assertEqual(blanked.count("\n"), text.count("\n"))
        self.assertIn('"x // not a comment"', blanked)
        self.assertNotIn("a comment\n", blanked.replace('"x // not a comment"', ""))
        self.assertNotIn("block", blanked)

    def test_strings_are_blanked_and_quotes_kept(self) -> None:
        blanked = source.blank_strings('text: "Sam #FF0000" + \'x\'\n')
        self.assertNotIn("Sam", blanked)
        self.assertEqual(blanked.count('"'), 2)

    def test_snake_case(self) -> None:
        self.assertEqual(source.snake_case("TabBar"), "tab_bar")
        self.assertEqual(source.snake_case("Theme"), "theme")
        self.assertEqual(source.snake_case("SafeFrame"), "safe_frame")
        self.assertEqual(source.snake_case("HeaderCard2"), "header_card2")

    def test_the_theme_is_set_apart(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make(root, {"apps/app/qml/Main.qml": "", "apps/app/qml/theme/Theme.qml": "",
                        "apps/app/qml/components/Tile.qml": ""})
            names = lambda **kw: [p.name for p in source.qml_files(root, **kw)]  # noqa: E731
            self.assertEqual(sorted(names()), ["Main.qml", "Tile.qml"])
            self.assertEqual(sorted(names(include_theme=True)), ["Main.qml", "Theme.qml", "Tile.qml"])


class PlatformTest(unittest.TestCase):
    def test_a_file_that_asks_the_platform_fails(self) -> None:
        for line in ['visible: Qt.platform.os === "ios"', "property bool phone: Qt.platform.os == 'ios'",
                     "visible: Qt.application.platform === 1"]:
            with self.subTest(line=line):
                self.assertEqual(len(platform.check_text("A.qml", component(line))), 1)

    def test_a_file_that_does_not_passes(self) -> None:
        text = component('visible: bridge.shareAvailable\n// Qt.platform.os would be wrong here\ntext: "Qt.platform"')
        self.assertEqual(platform.check_text("A.qml", text), [])

    def test_the_line_number_is_reported(self) -> None:
        problems = platform.check_text("A.qml", component("x: 1\nvisible: Qt.platform.os"))
        self.assertTrue(problems[0].startswith("A.qml:5:"), problems)


class LiteralsTest(unittest.TestCase):
    def found(self, body: str) -> list[str]:
        return literals.check_text("A.qml", component(body))[0]

    def test_colours(self) -> None:
        self.assertEqual(len(self.found('color: "#1E5B7A"')), 1)
        self.assertEqual(len(self.found('border.color: "#fff"')), 1)
        self.assertEqual(len(self.found('color: Qt.rgba(0, 0, 0, 0.5)')), 1)
        self.assertEqual(len(self.found('color: Qt.darker(Theme.colour.primary)')), 1)
        self.assertEqual(len(self.found('color: "red"')), 1)
        self.assertEqual(len(self.found('color: pressed ? "#000000" : Theme.colour.surface')), 1)

    def test_colours_that_are_allowed(self) -> None:
        self.assertEqual(self.found('color: "transparent"'), [])
        self.assertEqual(self.found("color: Theme.colour.primary"), [])
        self.assertEqual(self.found('text: qsTr("Entry #12")'), [])
        self.assertEqual(self.found('objectName: "red"'), [])
        self.assertEqual(self.found("// color: \"#123456\" was the old colour"), [])

    def test_sizes(self) -> None:
        for line in ["width: 120", "height: parent.height - 32", "radius: 8", "anchors.margins: 16", "spacing: 4",
                     "font.pixelSize: 16", "border.width: 2", "duration: 120", "Layout.preferredWidth: 88",
                     "property real cornerRadius: 12", "property real radius: 12", "width: Math.max(Theme.size.badge, 24)",
                     "width: -8", "x: parent.width + 4"]:
            with self.subTest(line=line):
                self.assertEqual(len(self.found(line)), 1, line)

    def test_sizes_that_are_allowed(self) -> None:
        for line in ["width: 0", "x: 0", "width: parent.width / 2", "width: parent.width * 0.5", "height: 2 * Theme.space.tileGap",
                     "radius: Theme.radius.tile", "duration: Theme.motion.toastMs", "anchors.margins: Theme.space.screenMargin",
                     "width: Math.min(parent.width, Theme.size.tileWidth)", "opacity: 0.5", "z: 2", "lineHeight: 1.2",
                     "maximumLineCount: 4", "model: 12", "from: 0", "scale: 0.96"]:
            with self.subTest(line=line):
                self.assertEqual(self.found(line), [], line)

    def test_an_allowed_literal_needs_a_reason(self) -> None:
        problems, allowed = literals.check_text("A.qml", component("width: 3 // literal-ok: hairline the OS draws"))
        self.assertEqual(problems, [])
        self.assertEqual(len(allowed), 1)
        problems, allowed = literals.check_text("A.qml", component("width: 3 // literal-ok"))
        self.assertEqual(len(problems), 2)
        self.assertEqual(allowed, [])

    def test_the_theme_is_where_literals_live(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make(root, {"apps/app/qml/theme/Theme.qml": component('color: "#123456"\nwidth: 40'),
                        "apps/app/qml/components/Tile.qml": component("width: 40")})
            problems, _ = literals.check(root)
            self.assertEqual(len(problems), 1)
            self.assertIn("components/Tile.qml", problems[0])


class StringsTest(unittest.TestCase):
    def found(self, body: str) -> list[str]:
        return strings.check_text("A.qml", component(body))

    def test_a_literal_is_not_translated(self) -> None:
        self.assertEqual(len(self.found('text: "Undo"')), 1)
        self.assertEqual(len(self.found('placeholderText: "Search"')), 1)
        self.assertEqual(len(self.found('Accessible.name: "Add"')), 1)
        self.assertEqual(len(self.found('title: cond ? qsTr("A") : "B"')), 1)
        self.assertEqual(len(self.found('Component.onCompleted: label.text = "Sam Example"')), 1)

    def test_concatenation_is_refused(self) -> None:
        self.assertEqual(len(self.found('text: qsTr("Undo ") + name')), 1)
        self.assertEqual(len(self.found('text: name + qsTr(" logged")')), 1)
        self.assertEqual(len(self.found('text: qsTr("a" + "b")')), 1)
        self.assertEqual(len(self.found('text: qsTr("%1 logged").arg(name) + qsTr("!")')), 2)
        self.assertEqual(len(self.found('text: qsTr(name)')), 1)

    def test_a_continued_line_is_one_statement(self) -> None:
        self.assertEqual(len(self.found('text: qsTr("a")\n    + extra')), 1)
        self.assertEqual(self.found('text: qsTr("%1 logged at %2")\n    .arg(name)\n    .arg(time)'), [])

    def test_translated_text_passes(self) -> None:
        for line in ['text: qsTr("Undo")', 'text: qsTr("Undo %1").arg(label)', 'text: qsTr("%1 on %2").arg(a).arg(b)',
                     'text: "×"', 'text: ": "', 'text: model.label', 'text: ""', 'Accessible.name: root.accessibleName',
                     'text: qsTr("a +b")', 'text: qsTr("Delete %1").arg(a + b)', 'title: qsTrId("id_title")',
                     'objectName: "Undo"', 'text: root.count > 0 ? qsTr("%n entries", "", root.count) : qsTr("None")']:
            with self.subTest(line=line):
                self.assertEqual(self.found(line), [], line)

    def test_a_comment_or_a_string_is_not_a_binding(self) -> None:
        self.assertEqual(self.found('// text: "Undo"\nobjectName: "text: Undo"'), [])


class TestFilesTest(unittest.TestCase):
    def check(self, files: dict[str, str]) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            make(Path(tmp), files)
            return tests.check(Path(tmp))

    CASE = '#include <catch2/catch_test_macros.hpp>\nTEST_CASE("x", "[ui]") {}\n'

    def test_a_component_with_its_test_passes(self) -> None:
        self.assertEqual(self.check({"apps/app/qml/components/TabBar.qml": "", "apps/app/tests/ui/tab_bar_test.cpp": self.CASE,
                                     "apps/app/qml/theme/Theme.qml": "", "apps/app/tests/ui/theme_test.cpp": self.CASE}), [])

    def test_a_component_without_one_fails(self) -> None:
        problems = self.check({"apps/app/qml/components/TabBar.qml": ""})
        self.assertEqual(len(problems), 1)
        self.assertIn("tab_bar_test.cpp", problems[0])

    def test_the_theme_needs_a_test_too(self) -> None:
        self.assertEqual(len(self.check({"apps/app/qml/theme/SafeFrame.qml": ""})), 1)

    def test_a_test_file_with_no_case_fails(self) -> None:
        problems = self.check({"apps/app/qml/screens/log/LogScreen.qml": "", "apps/app/tests/ui/log_screen_test.cpp": "// todo\n"})
        self.assertEqual(len(problems), 1)
        self.assertIn("TEST_CASE", problems[0])

    def test_a_private_part_is_tested_through_its_neighbour(self) -> None:
        self.assertEqual(self.check({"apps/app/qml/screens/log/private/Strip.qml": ""}), [])

    def test_the_window_and_the_shell_are_tested_by_the_app_flow(self) -> None:
        files = {"apps/app/qml/Main.qml": "", "apps/app/qml/shell/AppShell.qml": ""}
        self.assertEqual(len(self.check(files)), 2)
        tested = {**files, "apps/app/tests/ui/app_flow_test.cpp": "TEST_CASE(x)\n"}
        self.assertEqual(self.check(tested), [])


class MotionTest(unittest.TestCase):
    def found(self, body: str) -> list[str]:
        return motion.check_text("A.qml", component(body))

    def test_property_animations_on_the_four_properties_are_found(self) -> None:
        for body in [
            "NumberAnimation on x { from: 0; to: 10 }",
            "NumberAnimation on opacity { from: 0; to: 1 }",
            "PropertyAnimation on scale { to: 2 }",
            "SmoothedAnimation on y { to: 2 }",
            "NumberAnimation { target: box; property: \"y\"; to: 4 }",
            "NumberAnimation { target: box\n property: 'opacity'\n to: 1 }",
            "NumberAnimation { targets: [a, b]; properties: \"x, scale\"; to: 1 }",
            "SpringAnimation { property: \"x\" }",
            "Transition { NumberAnimation { property: \"opacity\"; from: 0; to: 1 } }",
            "ParallelAnimation { NumberAnimation { property: \"x\" } OpacityAnimator { to: 1 } }",
            "Behavior on opacity { OpacityAnimator { duration: Theme.motion.toastMs } }",
            "Behavior on box.x { NumberAnimation { duration: Theme.motion.toastMs } }",
        ]:
            with self.subTest(body=body):
                self.assertGreaterEqual(len(self.found(body)), 1, body)

    def test_the_message_names_the_animator_to_use(self) -> None:
        self.assertIn("OpacityAnimator", self.found("NumberAnimation on opacity { to: 1 }")[0])
        self.assertIn("XAnimator", self.found("NumberAnimation { property: \"x\" }")[0])
        self.assertIn("A.qml:", self.found("NumberAnimation { property: \"x\" }")[0])

    def test_animators_and_other_properties_pass(self) -> None:
        for body in [
            "XAnimator on x { to: 10 }",
            "YAnimator { target: box; to: 4 }",
            "ScaleAnimator { target: box; to: 0.96 }",
            "OpacityAnimator { target: box; to: 1 }",
            "Transition { XAnimator { from: 10; to: 0 } }",
            "NumberAnimation { property: \"height\"; to: 4 }",
            "NumberAnimation on width { to: 4 }",
            "PropertyAnimation { properties: \"width, height\"; to: 4 }",
            "ColorAnimation { property: \"color\" }",
            "Behavior on height { NumberAnimation { duration: Theme.motion.toastMs } }",
            "Behavior on opacityTarget { NumberAnimation { duration: Theme.motion.toastMs } }",
            "// NumberAnimation on x { to: 1 } was the old way",
            "property string note: \"NumberAnimation on x {\"",
        ]:
            with self.subTest(body=body):
                self.assertEqual(self.found(body), [], body)

    def test_only_the_animation_that_breaks_the_rule_is_reported(self) -> None:
        body = "XAnimator on x { to: 1 }\nNumberAnimation { property: \"width\" }\nNumberAnimation on scale { to: 2 }"
        problems = self.found(body)
        self.assertEqual(len(problems), 1)
        self.assertIn("scale", problems[0])

    def test_the_theme_is_checked_too(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make(root, {"apps/app/qml/theme/Theme.qml": component("NumberAnimation on x { to: 1 }"),
                        "apps/app/qml/components/Tile.qml": component("ScaleAnimator on scale { to: 1 }")})
            problems = motion.check(root)
            self.assertEqual(len(problems), 1)
            self.assertIn("theme/Theme.qml", problems[0])


class RepositoryTest(unittest.TestCase):
    """The checks pass on this repository, which is what CI runs them for."""

    ROOT = Path(__file__).resolve().parent.parent.parent

    def test_the_repository_passes_every_check(self) -> None:
        self.assertEqual(platform.check(self.ROOT), [])
        self.assertEqual(literals.check(self.ROOT)[0], [])
        self.assertEqual(strings.check(self.ROOT), [])
        self.assertEqual(tests.check(self.ROOT), [])
        self.assertEqual(motion.check(self.ROOT), [])


if __name__ == "__main__":
    unittest.main()
