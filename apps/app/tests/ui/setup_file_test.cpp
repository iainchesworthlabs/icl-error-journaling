// SPDX-License-Identifier: GPL-3.0-or-later
// SetupFile (docs/plan/stage-4.md, 4.13): the name of a setup file and the files an import accepts, which the shell
// hands to the file dialog and the Stage 5 document picker.
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QStringList>
#include <QVariant>

using namespace ej::ui;

namespace
{

constexpr auto PROBE = R"(
    import QtQuick
    import ErrorJournalingApp
    Item {
        function accepts(name) { return SetupFile.accepts(name); }
        property string defaultFileName: SetupFile.defaultFileName
        property string exportExtension: SetupFile.exportExtension
        property var extensions: SetupFile.extensions
        property var patterns: SetupFile.patterns
        property string dialogFilter: SetupFile.dialogFilter
    })";

bool accepts(QQuickItem* probe, const QString& name)
{
    QVariant result;
    REQUIRE(QMetaObject::invokeMethod(probe, "accepts", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, name)));
    return result.toBool();
}

} // namespace

TEST_CASE("import.file: a setup file is named cs-bingo-board-setup.ejconfig", "[ui][import]")
{
    UiRig rig;
    QQuickItem* probe = rig.load_source(QByteArray(PROBE));
    REQUIRE(probe != nullptr);

    CHECK(probe->property("defaultFileName").toString() == QStringLiteral("cs-bingo-board-setup.ejconfig"));
    CHECK(probe->property("exportExtension").toString() == QStringLiteral("ejconfig"));
    CHECK(probe->property("dialogFilter").toString() == QStringLiteral("Setup files (*.ejconfig *.json)"));
    CHECK(probe->property("extensions").toStringList() ==
          QStringList{QStringLiteral("ejconfig"), QStringLiteral("json")});
    CHECK(probe->property("patterns").toStringList() ==
          QStringList{QStringLiteral("*.ejconfig"), QStringLiteral("*.json")});
}

TEST_CASE("import.file: .ejconfig and .json are accepted with any name, and nothing else is", "[ui][import]")
{
    UiRig rig;
    QQuickItem* probe = rig.load_source(QByteArray(PROBE));
    REQUIRE(probe != nullptr);

    CHECK(accepts(probe, QStringLiteral("cs-bingo-board-setup.ejconfig")));
    CHECK(accepts(probe, QStringLiteral("anything at all.json")));
    CHECK(accepts(probe, QStringLiteral("Setup.EJCONFIG")));
    CHECK(accepts(probe, QStringLiteral("setup (2).JSON")));
    CHECK(accepts(probe, QStringLiteral("a.b.c.json")));

    CHECK_FALSE(accepts(probe, QStringLiteral("setup.txt")));
    CHECK_FALSE(accepts(probe, QStringLiteral("setup.json.bak")));
    CHECK_FALSE(accepts(probe, QStringLiteral("ejconfig")));
    CHECK_FALSE(accepts(probe, QStringLiteral("setup.jsonx")));
    CHECK_FALSE(accepts(probe, QString()));
}
