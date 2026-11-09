// SPDX-License-Identifier: GPL-3.0-or-later
// The WindowLayout singleton (Stage 8, package 8.1; docs/plan/stage-8.md, "Layout approach" and T8-1): the layout class
// from the window's size, and the sizes that follow from it. The shell's binding of the singleton to its size is
// app.window_layout in app_flow_test.cpp.
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QSize>
#include <QVariant>

using namespace ej::ui;
using Catch::Matchers::WithinAbs;

namespace
{

// The values of the enum WindowLayout.SizeClass.
constexpr int COMPACT = 0;
constexpr int REGULAR = 1;
constexpr int WIDE = 2;

QObject* group(QObject* theme, const char* name)
{
    auto* object = theme->property(name).value<QObject*>();
    REQUIRE(object != nullptr);
    return object;
}

double value(QObject* object, const char* name)
{
    const QVariant v = object->property(name);
    INFO(name);
    REQUIRE(v.isValid());
    return v.toDouble();
}

int size_class(UiRig& rig, QSize size)
{
    rig.set_window_size(size);
    return rig.window_layout()->property("sizeClass").toInt();
}

double tile_height_for(QObject* layout, double tile_width)
{
    double result = 0;
    REQUIRE(
        QMetaObject::invokeMethod(layout, "tileHeightFor", Q_RETURN_ARG(double, result), Q_ARG(double, tile_width)));
    return result;
}

} // namespace

TEST_CASE("layout.classes: the class follows the window's size, at the edges and on every iPad", "[ui][layout]")
{
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("x")}}) != nullptr);
    QObject* layout = rig.window_layout();
    REQUIRE(layout != nullptr);

    SECTION("the phone's design frame, the default, is Compact")
    {
        CHECK(layout->property("sizeClass").toInt() == COMPACT);
        CHECK(layout->property("compact").toBool());
        CHECK_FALSE(layout->property("regular").toBool());
        CHECK_FALSE(layout->property("wide").toBool());
    }

    SECTION("the edges: 600 wide is Regular, 1100 by 640 is Wide")
    {
        CHECK(size_class(rig, QSize(599, 1180)) == COMPACT);
        CHECK(size_class(rig, QSize(600, 1180)) == REGULAR);
        CHECK(size_class(rig, QSize(1099, 820)) == REGULAR);
        CHECK(size_class(rig, QSize(1100, 820)) == WIDE);
        CHECK(size_class(rig, QSize(1100, 640)) == WIDE);
        CHECK(size_class(rig, QSize(1100, 639)) == REGULAR);
        CHECK(size_class(rig, QSize(1400, 500)) == REGULAR);
        CHECK(rig.window_layout()->property("regular").toBool());
    }

    SECTION("phones and narrow Split View windows are Compact")
    {
        for (const QSize size :
             {QSize(402, 874), QSize(375, 667), QSize(440, 956), QSize(320, 1180), QSize(507, 1180), QSize(375, 720)})
        {
            INFO(size.width() << " x " << size.height());
            CHECK(size_class(rig, size) == COMPACT);
        }
    }

    SECTION("every iPad is Regular in portrait and Wide in landscape")
    {
        for (const QSize portrait :
             {QSize(744, 1133), QSize(820, 1180), QSize(834, 1210), QSize(1024, 1366), QSize(1032, 1376)})
        {
            INFO(portrait.width() << " x " << portrait.height());
            CHECK(size_class(rig, portrait) == REGULAR);
            CHECK(size_class(rig, portrait.transposed()) == WIDE);
        }
        // A two-thirds Split View window.
        CHECK(size_class(rig, QSize(694, 820)) == REGULAR);
    }
}

TEST_CASE("layout.compact_is_phone: in a Compact window every derived size is the phone's Theme value", "[ui][layout]")
{
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("x")}}) != nullptr);
    QObject* layout = rig.window_layout();
    QObject* space = group(rig.theme(), "space");
    QObject* size = group(rig.theme(), "size");

    for (const QSize window : {QSize(402, 874), QSize(375, 720), QSize(507, 1180)})
    {
        INFO(window.width() << " x " << window.height());
        rig.set_window_size(window);
        REQUIRE(layout->property("compact").toBool());
        CHECK(value(layout, "screenMargin") == value(space, "screenMargin"));
        CHECK(value(layout, "tileGap") == value(space, "tileGap"));
        const double full = window.width() - 2 * value(space, "screenMargin");
        for (const char* column : {"logColumnWidth", "formColumnWidth", "readingColumnWidth", "firstLaunchColumnWidth"})
        {
            INFO(column);
            CHECK(value(layout, column) == full);
        }
        CHECK(value(layout, "boardPaneWidth") == 0);
        CHECK(value(layout, "logPaneWidth") == 0);
        CHECK(tile_height_for(layout, 118) == value(size, "tileHeight"));
        CHECK(tile_height_for(layout, 300) == value(size, "tileHeight"));
    }
}

TEST_CASE("layout.sizes: the columns, the panes and the tiles of Regular and Wide follow the layout rules",
          "[ui][layout]")
{
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("x")}}) != nullptr);
    QObject* layout = rig.window_layout();
    QObject* rules = group(rig.theme(), "layout");

    SECTION("an iPad Air 11-inch in portrait: the columns are at their limits")
    {
        rig.set_window_size(QSize(820, 1180));
        CHECK(value(layout, "screenMargin") == value(rules, "screenMargin"));
        CHECK(value(layout, "tileGap") == value(rules, "tileGap"));
        CHECK(value(layout, "logColumnWidth") == value(rules, "logColumn"));
        CHECK(value(layout, "formColumnWidth") == value(rules, "formColumn"));
        CHECK(value(layout, "readingColumnWidth") == value(rules, "readingColumn"));
        CHECK(value(layout, "firstLaunchColumnWidth") == value(rules, "firstLaunchColumn"));
        CHECK(value(layout, "boardPaneWidth") == 0);
        // The mock-up's tiles: (680 - 2 x 12) / 3 = 218.7 wide, 0.7 of that capped at 152.
        CHECK(tile_height_for(layout, (680.0 - 24.0) / 3.0) == 152);
    }

    SECTION("a narrow Regular window: the columns are the width less the margins")
    {
        rig.set_window_size(QSize(620, 1180));
        CHECK(value(layout, "logColumnWidth") == 572);
        CHECK(value(layout, "formColumnWidth") == 572);
        CHECK(value(layout, "readingColumnWidth") == 572);
        CHECK(value(layout, "firstLaunchColumnWidth") == value(rules, "firstLaunchColumn"));
        // 0.7 of a narrow tile is under the phone's 104, which is the least.
        CHECK(tile_height_for(layout, 120) == 104);
        CHECK(tile_height_for(layout, 180) == 126);
    }

    SECTION("an iPad Air 11-inch in landscape: the panes of the mock-up")
    {
        rig.set_window_size(QSize(1180, 820));
        REQUIRE(layout->property("wide").toBool());
        // 1180 - 2 x 24 - 20 = 1112; 0.58 of it is 644.96, so 645 for the board and 467 for the log.
        CHECK(value(layout, "panesWidth") == 1112);
        CHECK(value(layout, "boardPaneWidth") == 645);
        CHECK(value(layout, "logPaneWidth") == 467);
    }

    SECTION("an iPad mini in landscape")
    {
        rig.set_window_size(QSize(1133, 744));
        REQUIRE(layout->property("wide").toBool());
        CHECK_THAT(value(layout, "boardPaneWidth") + value(layout, "logPaneWidth"),
                   WithinAbs(1133.0 - 48.0 - 20.0, 1e-9));
        CHECK(value(layout, "boardPaneWidth") == 618);
    }
}

TEST_CASE("layout.side_insets: a SafeFrame keeps clear of simulated left and right insets that it touches",
          "[ui][layout]")
{
    UiRig rig(QSize(1180, 820));
    QQuickItem* root = rig.load_source(R"(
import QtQuick
import ErrorJournalingApp

Item {
    SafeFrame {
        objectName: "full"
        anchors.fill: parent
    }
    SafeFrame {
        objectName: "inner"
        x: 100
        width: 200
        height: 100
    }
}
)");
    REQUIRE(root != nullptr);
    rig.set_safe_area(24, 20);
    rig.set_safe_area_sides(44, 30);
    QObject* full = rig.find(QStringLiteral("full"));
    QObject* inner = rig.find(QStringLiteral("inner"));
    REQUIRE(full != nullptr);
    REQUIRE(inner != nullptr);
    CHECK(value(full, "leftInset") == 44);
    CHECK(value(full, "rightInset") == 30);
    CHECK(value(full, "topInset") == 24);
    CHECK(value(full, "bottomInset") == 20);
    // An item away from the sides is not inset.
    CHECK(value(inner, "leftInset") == 0);
    CHECK(value(inner, "rightInset") == 0);
}
