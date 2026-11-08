// SPDX-License-Identifier: GPL-3.0-or-later
// VersionSheet, the Software version sheet (docs/plan/stage-4.md, 4.9): version.choices, version.change,
// version.correct, version.preview, version.validation, version.time_default and the cross-cutting checks. The models
// are the real ones of Stage 3 over the in-memory store.
#include "header_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
#include "support/recording_repository.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QMetaObject>
#include <QQuickItem>

using namespace ej::ui;
using namespace ej::ui::header;
using ej::test::RepositoryOp;

namespace
{

// The version sheet over a log that has UNIT-27, North Site and version 1.4.2, opened and finished presenting. By
// default the sheet is loaded alone. `in_screen` loads Today's details and opens its sheet, so that the time sheet that
// stacks over it is there too (the checks of reading order cannot tell the page under a sheet from the sheet).
QQuickItem* open_sheet(UiRig& rig, HeaderWorld& world, bool in_screen = false)
{
    world.attach(rig);
    QQuickItem* sheet = nullptr;
    if (in_screen)
    {
        REQUIRE(rig.load(QStringLiteral("HeaderScreen"), world.models()) != nullptr);
        sheet = need(rig, QStringLiteral("versionSheet"));
    }
    else
    {
        sheet = rig.load(QStringLiteral("VersionSheet"), world.models());
    }
    INFO(rig.errors().toStdString());
    REQUIRE(sheet != nullptr);
    REQUIRE(QMetaObject::invokeMethod(sheet, "show"));
    play(rig, 500);
    rig.settle();
    REQUIRE(sheet->property("isOpen").toBool());
    return sheet;
}

/// The time sheet over the version sheet, opened by the Time row and finished presenting.
QQuickItem* open_time_sheet(UiRig& rig)
{
    QQuickItem* time_sheet = need(rig, QStringLiteral("versionTimeSheet"));
    rig.tap(need(rig, QStringLiteral("versionTime")));
    play(rig, 500);
    rig.settle();
    REQUIRE(time_sheet->property("isOpen").toBool());
    return time_sheet;
}

QString preview_of(UiRig& rig)
{
    QQuickItem* line = need(rig, QStringLiteral("versionPreview"));
    return line != nullptr ? line->property("text").toString() : QString();
}

bool save_enabled(UiRig& rig)
{
    return need(rig, QStringLiteral("versionSave"))->isEnabled();
}

int selected_cards(UiRig& rig)
{
    int count = 0;
    for (const char* name : {"versionChanged", "versionTypo"})
    {
        count += need(rig, QString::fromLatin1(name))->property("selected").toBool() ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("version.choices: two plain choices, exactly one selected, in the facilitator's words", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    open_sheet(rig, world);
    QQuickItem* changed = need(rig, QStringLiteral("versionChanged"));
    QQuickItem* typo = need(rig, QStringLiteral("versionTypo"));
    REQUIRE(changed != nullptr);
    REQUIRE(typo != nullptr);

    CHECK(changed->property("title").toString() == QStringLiteral("It changed during the day"));
    CHECK(changed->property("description").toString() ==
          QStringLiteral("For example an over-the-air update. Adds a timed entry to the log."));
    CHECK(typo->property("title").toString() == QStringLiteral("I typed it wrong"));
    CHECK(typo->property("description").toString() ==
          QStringLiteral("Corrects the value. Nothing is added to the log."));
    CHECK(selected_cards(rig) == 1);
    CHECK(changed->property("selected").toBool());
    CHECK(need(rig, QStringLiteral("versionFrom"))->property("value").toString() == QStringLiteral("1.4.2"));

    rig.tap(typo);
    CHECK(selected_cards(rig) == 1);
    CHECK(typo->property("selected").toBool());
    // A Correct has no time.
    CHECK_FALSE(need(rig, QStringLiteral("versionTime"))->isVisible());
    CHECK(need(rig, QStringLiteral("versionSave"))->property("text").toString() ==
          QStringLiteral("Correct the version"));

    rig.tap(changed);
    CHECK(selected_cards(rig) == 1);
    CHECK(need(rig, QStringLiteral("versionTime"))->isVisible());
    CHECK(need(rig, QStringLiteral("versionSave"))->property("text").toString() == QStringLiteral("Log the change"));
}

TEST_CASE("version.change: \"It changed during the day\" records one entry at the time chosen", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    REQUIRE(world.timeline().entry_count() == 0);
    CHECK_FALSE(save_enabled(rig));

    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
    REQUIRE(save_enabled(rig));
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    play(rig, 500);

    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.storage().repository().call_count(RepositoryOp::ADD_ENTRY) == 1);
    CHECK(world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER) == 0);
    REQUIRE(world.timeline().entry_count() == 1);
    CHECK(world.timeline().last_line().contains(QStringLiteral("Software Version changed: 1.4.2 → 1.5.0")));
    CHECK(world.session().version_text() == QStringLiteral("1.5.0"));
}

TEST_CASE("version.layout: From, To and Time are one card, and the question is plain text", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));

    // "What happened?" in 15 pt, in the words of the design and not in capitals.
    QQuickItem* question = need(rig, QStringLiteral("versionQuestion"));
    CHECK(question->property("text").toString() == QStringLiteral("What happened?"));
    CHECK(question->property("font").value<QFont>().pixelSize() == 15);

    // From, To and Time are rows of one group.
    QQuickItem* group = need(rig, QStringLiteral("versionGroup"));
    for (const char* name : {"versionFrom", "versionTo", "versionTime"})
    {
        QQuickItem* row = need(rig, QString::fromLatin1(name));
        bool inside = false;
        for (QQuickItem* up = row->parentItem(); up != nullptr; up = up->parentItem())
        {
            inside = inside || up == group;
        }
        CHECK(inside);
    }
    // The value is typed at the right of its row, in 18/600, and the time is a chip.
    const QFont typed = input_of(rig, need(rig, QStringLiteral("versionTo")))->property("font").value<QFont>();
    CHECK(typed.pixelSize() == 18);
    CHECK(typed.weight() == QFont::DemiBold);
    CHECK(need(rig, QStringLiteral("versionTime"))->property("valueChip").toBool());
    CHECK(rig.geometry(need(rig, QStringLiteral("versionTo"))).height() >= 52.0);

    // The button ends at the bottom of the sheet's content area.
    QQuickItem* save = need(rig, QStringLiteral("versionSave"));
    CHECK(save->height() >= 54.0);
    CHECK(rig.geometry(save).bottom() == rig.geometry(need(rig, QStringLiteral("sheetContent"), sheet)).bottom());
}

TEST_CASE("version.change: the time is the one the tumbler gives, and it may be earlier than now",
          "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world, true);
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));

    QQuickItem* time_sheet = open_time_sheet(rig);
    QQuickItem* tumbler = need(rig, QStringLiteral("versionTumbler"));
    REQUIRE(tumbler != nullptr);
    CHECK(tumbler->isVisible());
    QQuickItem* hours = need(rig, QStringLiteral("Hours"), tumbler);
    REQUIRE(hours != nullptr);
    // 08:55 is now; the change happened at 07:55.
    REQUIRE(sheet->property("timeSeconds").toInt() / 3600 == 8);
    hours->setProperty("currentIndex", 7);
    play(rig, 64);
    CHECK(sheet->property("timeSeconds").toInt() / 3600 == 7);

    // Done keeps the time, and the version sheet is under it as it was.
    rig.tap(need(rig, QStringLiteral("navTrailing"), time_sheet));
    play(rig, 500);
    CHECK_FALSE(time_sheet->property("isOpen").toBool());
    CHECK(sheet->property("isOpen").toBool());
    CHECK_FALSE(
        need(rig, QStringLiteral("versionTime"))->property("value").toString().startsWith(QStringLiteral("Now")));

    const QString shown = preview_of(rig);
    CHECK(shown.contains(QStringLiteral("07:55")));
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    REQUIRE(world.timeline().entry_count() == 1);
    CHECK(world.timeline().last_line() == shown);
}

TEST_CASE("version.correct: \"I typed it wrong\" corrects the value and adds no entry", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    rig.tap(need(rig, QStringLiteral("versionTypo")));

    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.4.3"));
    REQUIRE(save_enabled(rig));
    // A Correct shows no log line.
    CHECK(preview_of(rig).isEmpty());
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    play(rig, 500);

    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().version_text() == QStringLiteral("1.4.3"));
    CHECK(world.timeline().entry_count() == 0);
    CHECK(world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER) == 1);
    CHECK(world.storage().repository().call_count(RepositoryOp::ADD_ENTRY) == 0);
}

TEST_CASE("version.preview: the exact line, byte for byte, updating as the fields change", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    QQuickItem* to = need(rig, QStringLiteral("versionTo"));
    const int now = world.session().now_local_seconds();
    const QString key = QStringLiteral("software_version");

    CHECK(preview_of(rig).isEmpty());
    retype(rig, to, QStringLiteral("1.5.0"));
    QString shown = preview_of(rig);
    CHECK_FALSE(shown.isEmpty());
    CHECK(shown == world.session().previewHeaderChange(key, QStringLiteral("1.5.0"), now));

    retype(rig, to, QStringLiteral("1.6.0"));
    CHECK(preview_of(rig) == world.session().previewHeaderChange(key, QStringLiteral("1.6.0"), now));
    CHECK(preview_of(rig) != shown);

    // A time chosen changes the line.
    sheet->setProperty("timeSeconds", 7 * 3600);
    rig.settle();
    shown = preview_of(rig);
    CHECK(shown == world.session().previewHeaderChange(key, QStringLiteral("1.6.0"), 7 * 3600));
    CHECK(shown.contains(QStringLiteral("07:00")));

    // What is saved is the line that was shown.
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    REQUIRE(world.timeline().entry_count() == 1);
    CHECK(world.timeline().last_line() == shown);
}

TEST_CASE("version.preview: an unchanged value shows the refusal and cannot be saved", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    open_sheet(rig, world);

    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.4.2"));
    CHECK(preview_of(rig) == world.session().previewHeaderChange(QStringLiteral("software_version"),
                                                                 QStringLiteral("1.4.2"),
                                                                 world.session().now_local_seconds()));
    CHECK(preview_of(rig) == QStringLiteral("That is already the current value."));
    CHECK_FALSE(save_enabled(rig));
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    CHECK(world.timeline().entry_count() == 0);
}

TEST_CASE("version.validation: a refused version is flagged inline and Save stays disabled until valid",
          "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    open_sheet(rig, world);
    QQuickItem* to = need(rig, QStringLiteral("versionTo"));

    for (const char* refused : {"1.4", "1.4.2-beta"})
    {
        retype(rig, to, QString::fromLatin1(refused));
        const QString message =
            world.session().validateHeaderValue(QStringLiteral("software_version"), QString::fromLatin1(refused));
        REQUIRE_FALSE(message.isEmpty());
        CHECK(to->property("errorText").toString() == message);
        CHECK_FALSE(save_enabled(rig));
        CHECK(preview_of(rig).isEmpty());
        rig.tap(need(rig, QStringLiteral("versionSave")));
        CHECK(world.timeline().entry_count() == 0);
    }
    retype(rig, to, QStringLiteral("1.5.0"));
    CHECK(to->property("errorText").toString().isEmpty());
    CHECK(save_enabled(rig));
}

TEST_CASE("version.time_default: the time starts at now", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world, true);

    const int now = world.session().now_local_seconds();
    CHECK(sheet->property("timeSeconds").toInt() == now);
    const QString clock = world.timeline().formatLocalSeconds(now);
    CHECK(need(rig, QStringLiteral("versionTime"))->property("value").toString() ==
          QStringLiteral("Now · %1").arg(clock));

    // The tumbler opens on that time, on a sheet of its own.
    QQuickItem* time_sheet = open_time_sheet(rig);
    QQuickItem* tumbler = need(rig, QStringLiteral("versionTumbler"));
    REQUIRE(tumbler != nullptr);
    CHECK(tumbler->isVisible());
    CHECK(tumbler->property("localSeconds").toInt() == now);
    rig.tap(need(rig, QStringLiteral("navTrailing"), time_sheet));
    play(rig, 500);

    // Opening the sheet again reads the clock again.
    rig.tap(need(rig, QStringLiteral("navLeading"), sheet));
    play(rig, 500);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    world.clock().advance(std::chrono::minutes(10));
    REQUIRE(QMetaObject::invokeMethod(sheet, "show"));
    play(rig, 500);
    CHECK(sheet->property("timeSeconds").toInt() == world.session().now_local_seconds());
    CHECK(sheet->property("timeSeconds").toInt() >= now + 600);
}

TEST_CASE("version.cancel: Cancel leaves the log as it was", "[ui][version_sheet]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));

    rig.tap(need(rig, QStringLiteral("navLeading"), sheet));
    play(rig, 500);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().version_text() == QStringLiteral("1.4.2"));
    CHECK(world.timeline().entry_count() == 0);
}

TEST_CASE("version.a11y: names, targets, order and text scale, in light and dark", "[ui][version_sheet]")
{
    // A window tall enough to hold the sheet with its time picker open: the checks compare positions in the window and
    // know nothing of a scroller.
    HeaderWorld world(true);
    UiRig rig(QSize(402, 1600));
    open_sheet(rig, world);

    SECTION("light, with a preview")
    {
        retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
        expect_clean(check_everything(rig));
    }

    SECTION("dark")
    {
        rig.set_dark(true);
        retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
        expect_clean(check_everything(rig));
    }

    SECTION("a Correct")
    {
        rig.tap(need(rig, QStringLiteral("versionTypo")));
        expect_clean(check_everything(rig));
    }

    SECTION("with Reduce Motion nothing moves for long")
    {
        rig.set_reduce_motion(true);
        expect_clean(check_motion_reduced(rig));
    }
}
