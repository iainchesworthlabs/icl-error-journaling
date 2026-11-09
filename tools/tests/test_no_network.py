# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for check_no_network.py (label: unit). T11-5 of docs/plan/stage-11.md.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_no_network as network_check  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


class NoNetworkCheckTest(unittest.TestCase):
    def problems(self, rel: str, text: str) -> list[str]:
        return network_check.check_text(rel, text)

    def test_repository_passes(self) -> None:
        self.assertEqual(network_check.check(ROOT), [])

    def test_qt_network_class_fails(self) -> None:
        self.assertTrue(self.problems("libs/x/src/a.cpp", "QNetworkAccessManager manager;"))

    def test_qt_network_include_fails(self) -> None:
        self.assertTrue(self.problems("libs/x/src/a.cpp", "#include <QtNetwork/QTcpSocket>"))

    def test_url_session_fails(self) -> None:
        self.assertTrue(self.problems("libs/x/src/platform/ios/a.mm", "NSURLSession *session = nil;"))

    def test_socket_header_fails(self) -> None:
        self.assertTrue(self.problems("libs/x/src/a.cpp", "#include <sys/socket.h>"))

    def test_network_framework_import_fails(self) -> None:
        self.assertTrue(self.problems("libs/x/src/a.mm", "#import <Network/Network.h>"))

    def test_cmake_link_fails(self) -> None:
        self.assertTrue(self.problems("apps/app/CMakeLists.txt", "target_link_libraries(app PRIVATE Qt6::Network)"))

    def test_cmake_find_package_fails(self) -> None:
        self.assertTrue(self.problems("apps/app/CMakeLists.txt", "find_package(Qt6 REQUIRED COMPONENTS Quick Network)"))

    def test_cmake_comment_passes(self) -> None:
        self.assertEqual(self.problems("apps/app/CMakeLists.txt", "# never Qt6::Network"), [])

    def test_remote_url_in_qml_fails(self) -> None:
        self.assertTrue(self.problems("apps/app/qml/A.qml", 'Image { source: "https://example.com/a.png" }'))

    def test_project_pages_pass(self) -> None:
        for url in ("https://github.com/iainchesworthlabs/icl-error-journaling",
                    "https://iainchesworthlabs.github.io/icl-error-journaling/support/",
                    "https://www.gnu.org/licenses/"):
            self.assertEqual(self.problems("apps/app/qml/A.qml", f'Qt.openUrlExternally("{url}")'), [], url)

    def test_plist_doctype_passes(self) -> None:
        line = '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
        self.assertEqual(self.problems("apps/app/packaging/ios/Info.plist.in", line), [])

    def test_words_alone_pass(self) -> None:
        self.assertEqual(self.problems("libs/x/include/a.hpp", "/// No network, no analytics."), [])

    def test_tests_are_skipped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "libs" / "x" / "tests").mkdir(parents=True)
            (root / "libs" / "x" / "tests" / "a.cpp").write_text("QTcpSocket socket;", encoding="utf-8")
            (root / "libs" / "x" / "src").mkdir(parents=True)
            (root / "libs" / "x" / "src" / "b.cpp").write_text("QTcpSocket socket;", encoding="utf-8")
            problems = network_check.check(root)
            self.assertEqual(len(problems), 1)
            self.assertIn("libs/x/src/b.cpp", problems[0])


if __name__ == "__main__":
    unittest.main()
