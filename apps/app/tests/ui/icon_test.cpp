// SPDX-License-Identifier: GPL-3.0-or-later
// Icon (the visual pass): the mock-ups' SVG drawings on the 24 grid, at any size and colour, ignored by a screen
// reader.
#include "input_components_support.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QColor>
#include <QQuickItem>
#include <QStringList>
#include <QVariantMap>

using namespace ej::ui;
using namespace ej::ui::input;
using Catch::Matchers::WithinAbs;

TEST_CASE("icon.kinds: every kind of the design has a path, and an unknown kind draws nothing", "[ui][icon]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QStringLiteral(R"(
        Icon { objectName: "icon"; kind: "chevronRight" })")) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* icon = require_item(rig, QStringLiteral("icon"));

    const QVariantMap paths = icon->property("paths").toMap();
    const QStringList kinds{QStringLiteral("chevronRight"), QStringLiteral("chevronLeft"), QStringLiteral("plus"),
                            QStringLiteral("close"),        QStringLiteral("check"),       QStringLiteral("warning"),
                            QStringLiteral("info"),         QStringLiteral("ring"),        QStringLiteral("search"),
                            QStringLiteral("share"),        QStringLiteral("file"),        QStringLiteral("list"),
                            QStringLiteral("music"),        QStringLiteral("speech"),      QStringLiteral("calendar"),
                            QStringLiteral("grip"),         QStringLiteral("copy"),        QStringLiteral("trash"),
                            QStringLiteral("swap"),         QStringLiteral("board"),       QStringLiteral("sun")};
    for (const QString& kind : kinds)
    {
        INFO(kind.toStdString());
        CHECK_FALSE(paths.value(kind).toString().isEmpty());
        icon->setProperty("kind", kind);
        CHECK(icon->property("known").toBool());
    }
    // The paths are the mock-ups' own.
    CHECK(paths.value(QStringLiteral("chevronRight")).toString() == QStringLiteral("M9 6l6 6-6 6"));
    CHECK(paths.value(QStringLiteral("plus")).toString() == QStringLiteral("M12 5v14M5 12h14"));

    icon->setProperty("kind", QStringLiteral("noSuchIcon"));
    CHECK_FALSE(icon->property("known").toBool());
}

TEST_CASE("icon.size: the square follows `size`, the stroke scales with it, and the default strokes are the design's",
          "[ui][icon]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QStringLiteral(R"(
        Icon { objectName: "thin"; kind: "chevronRight"; size: 14; colour: Theme.colour.borderStrong }
        Icon { objectName: "check"; kind: "check" }
        Icon { objectName: "grip"; kind: "grip" })")) != nullptr);
    QQuickItem* thin = require_item(rig, QStringLiteral("thin"));
    CHECK_THAT(thin->width(), WithinAbs(14, 1e-9));
    CHECK_THAT(thin->height(), WithinAbs(14, 1e-9));
    CHECK_THAT(thin->property("strokeWidth").toDouble(), WithinAbs(2, 1e-9));
    CHECK(thin->property("colour").value<QColor>() == QColor(QStringLiteral("#9AA8B4")));

    QQuickItem* check = require_item(rig, QStringLiteral("check"));
    CHECK_THAT(check->width(), WithinAbs(24, 1e-9));
    CHECK_THAT(check->property("strokeWidth").toDouble(), WithinAbs(2.5, 1e-9));
    CHECK_THAT(require_item(rig, QStringLiteral("grip"))->property("strokeWidth").toDouble(), WithinAbs(3, 1e-9));
}
