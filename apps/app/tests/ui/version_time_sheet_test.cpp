// SPDX-License-Identifier: GPL-3.0-or-later
// VersionTimeSheet, the time of a version change on a second sheet over the Software version sheet
// (docs/design/screens/ TimeTumbler.dc.html, decision 11 of docs/plan/stage-4.md): version.time_sheet and the
// cross-cutting checks. The sheet is declared by Today's details after the version sheet, so the tests load that screen
// and open the version sheet. The models are the real ones of Stage 3 over the in-memory store.
#include "header_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QQuickItem>

using namespace ej::ui;
using namespace ej::ui::header;

namespace
{

// Today's details with the version sheet open and finished presenting; returns the version sheet.
QQuickItem* open_version_sheet(UiRig& rig, HeaderWorld& world)
{
    world.attach(rig);
    REQUIRE(rig.load(QStringLiteral("HeaderScreen"), world.models()) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* sheet = need(rig, QStringLiteral("versionSheet"));
    REQUIRE(QMetaObject::invokeMethod(sheet, "show"));
    play(rig, 500);
    rig.settle();
    REQUIRE(sheet->property("isOpen").toBool());
    return sheet;
}

// The time sheet, opened by the Time row of the version sheet and finished presenting.
QQuickItem* open_time_sheet(UiRig& rig)
{
    QQuickItem* time_sheet = need(rig, QStringLiteral("versionTimeSheet"));
    rig.tap(need(rig, QStringLiteral("versionTime")));
    play(rig, 500);
    rig.settle();
    REQUIRE(time_sheet->property("isOpen").toBool());
    return time_sheet;
}

} // namespace

TEST_CASE("version.time_sheet: Cancel puts the time back, Now sets it to the clock, and Escape keeps what is shown",
          "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_version_sheet(rig, world);
    const int now = sheet->property("timeSeconds").toInt();
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));

    SECTION("Cancel")
    {
        QQuickItem* time_sheet = open_time_sheet(rig);
        QQuickItem* hours = need(rig, QStringLiteral("Hours"), need(rig, QStringLiteral("versionTumbler")));
        hours->setProperty("currentIndex", 6);
        play(rig, 64);
        REQUIRE(sheet->property("timeSeconds").toInt() / 3600 == 6);
        rig.tap(need(rig, QStringLiteral("navLeading"), time_sheet));
        play(rig, 500);
        CHECK_FALSE(time_sheet->property("isOpen").toBool());
        CHECK(sheet->property("timeSeconds").toInt() == now);
        CHECK(need(rig, QStringLiteral("versionTime"))->property("value").toString().startsWith(QStringLiteral("Now")));
    }

    SECTION("Now")
    {
        open_time_sheet(rig);
        QQuickItem* tumbler = need(rig, QStringLiteral("versionTumbler"));
        QQuickItem* hours = need(rig, QStringLiteral("Hours"), tumbler);
        hours->setProperty("currentIndex", 6);
        play(rig, 64);
        REQUIRE(sheet->property("timeSeconds").toInt() != now);
        QMetaObject::invokeMethod(tumbler, "nowRequested");
        play(rig, 64);
        CHECK(sheet->property("timeSeconds").toInt() == now);
    }

    SECTION("Escape is Done")
    {
        QQuickItem* time_sheet = open_time_sheet(rig);
        QQuickItem* hours = need(rig, QStringLiteral("Hours"), need(rig, QStringLiteral("versionTumbler")));
        hours->setProperty("currentIndex", 6);
        play(rig, 64);
        rig.key(Qt::Key_Escape);
        play(rig, 500);
        CHECK_FALSE(time_sheet->property("isOpen").toBool());
        CHECK_FALSE(sheet->property("timeOpen").toBool());
        CHECK(sheet->property("timeSeconds").toInt() / 3600 == 6);
        CHECK(sheet->property("isOpen").toBool());
    }
}

TEST_CASE("version.time_sheet: it has Cancel, Time and Done over a tumbler with a Now chip", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_version_sheet(rig, world);
    QQuickItem* time_sheet = need(rig, QStringLiteral("versionTimeSheet"));
    CHECK_FALSE(time_sheet->property("isOpen").toBool());

    open_time_sheet(rig);
    CHECK(sheet->property("timeOpen").toBool());
    QQuickItem* leading = need(rig, QStringLiteral("navLeading"), time_sheet);
    QQuickItem* trailing = need(rig, QStringLiteral("navTrailing"), time_sheet);
    CHECK(leading->isVisible());
    CHECK(trailing->isVisible());
    CHECK(need(rig, QStringLiteral("navTitle"), time_sheet)->property("text").toString() == QStringLiteral("Time"));
    QQuickItem* tumbler = need(rig, QStringLiteral("versionTumbler"), time_sheet);
    CHECK(tumbler->property("nowText").toString() == QStringLiteral("Now"));
    CHECK(tumbler->property("caption").toString().startsWith(QStringLiteral("Shown in the log as ")));

    // The sheet is shorter than the version sheet and over it: its top is below the version sheet's.
    CHECK(rig.geometry(need(rig, QStringLiteral("sheetPanel"), time_sheet)).top() >
          rig.geometry(need(rig, QStringLiteral("sheetPanel"), sheet)).top());
    // Done closes it and leaves the version sheet open.
    rig.tap(trailing);
    play(rig, 500);
    CHECK_FALSE(time_sheet->property("isOpen").toBool());
    CHECK_FALSE(sheet->property("timeOpen").toBool());
    CHECK(sheet->property("isOpen").toBool());
}

TEST_CASE("version.time_sheet: names, targets and text scale, in light and dark", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig(QSize(402, 1600));
    open_version_sheet(rig, world);
    open_time_sheet(rig);

    // The page and the version sheet under the time sheet are in the tree too, so the order of reading is not checked
    // here; it is checked on each sheet alone.
    expect_clean(check_names(rig));
    expect_clean(check_targets(rig));
    expect_clean(check_text_scale(rig));
    rig.set_dark(true);
    rig.settle();
    expect_clean(check_names(rig));
    expect_clean(check_targets(rig));
    expect_clean(check_text_scale(rig));
}
