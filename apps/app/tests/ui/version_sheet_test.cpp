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

// ---------------------------------------------------------------------------------------------------------- commit

TEST_CASE("version.commit: the same optional field as Today's details, a row of the card, under To",
          "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    REQUIRE(world.session().correctVersion(QStringLiteral("1.4.2"), QStringLiteral("a1b2c3d")));
    world.gateway().run_all();
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);

    // The version in force is shown with its commit, and both new fields start empty.
    CHECK(sheet->property("fromText").toString() == QStringLiteral("1.4.2 (a1b2c3d)"));
    CHECK(need(rig, QStringLiteral("versionFrom"))->property("value").toString() == QStringLiteral("1.4.2 (a1b2c3d)"));
    QQuickItem* to = need(rig, QStringLiteral("versionTo"));
    QQuickItem* commit = need(rig, QStringLiteral("versionCommit"));
    CHECK(input_of(rig, to)->property("text").toString().isEmpty());
    CHECK(input_of(rig, commit)->property("text").toString().isEmpty());
    CHECK(commit->property("label").toString() == QStringLiteral("Commit"));
    CHECK(commit->property("placeholderText").toString() == QStringLiteral("Optional"));

    // The row is in the same card, between To and Time, in the same face and as tall as To.
    QQuickItem* group = need(rig, QStringLiteral("versionGroup"));
    bool inside = false;
    for (QQuickItem* up = commit->parentItem(); up != nullptr; up = up->parentItem())
    {
        inside = inside || up == group;
    }
    CHECK(inside);
    CHECK(rig.geometry(commit).top() >= rig.geometry(to).bottom() - 0.5);
    CHECK(rig.geometry(commit).bottom() <= rig.geometry(need(rig, QStringLiteral("versionTime"))).top() + 0.5);
    CHECK(rig.geometry(commit).height() >= 52.0);
    CHECK(input_of(rig, commit)->property("font").value<QFont>().pixelSize() == 18);

    // The hint is under the card, in the same words as Today's details; it gives way to a message.
    QQuickItem* hint = need(rig, QStringLiteral("versionCommitHint"));
    CHECK(hint->isVisible());
    CHECK(hint->property("text").toString() ==
          QString::fromUtf8("The short code (7 to 12 letters and numbers) that identifies the exact build. Leave it "
                            "empty if you can\xE2\x80\x99t get it."));
    retype(rig, commit, QStringLiteral("xyz"));
    CHECK_FALSE(hint->isVisible());
    CHECK(need(rig, QStringLiteral("versionRefusal"))->isVisible());
    retype(rig, commit, QString());
    CHECK(hint->isVisible());
}

TEST_CASE("version.commit: a change takes the commit, and the line and the entry carry it",
          "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    REQUIRE(world.session().correctVersion(QStringLiteral("1.4.2"), QStringLiteral("a1b2c3d")));
    world.gateway().run_all();
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    const int now = world.session().now_local_seconds();

    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
    CHECK(preview_of(rig) == QString::fromUtf8("08:55 Software Version changed: 1.4.2 (a1b2c3d) \xE2\x86\x92 1.5.0"));
    retype(rig, need(rig, QStringLiteral("versionCommit")), QStringLiteral("E4F5A6B"));
    const QString shown = preview_of(rig);
    CHECK(shown == QString::fromUtf8("08:55 Software Version changed: 1.4.2 (a1b2c3d) \xE2\x86\x92 1.5.0 (e4f5a6b)"));
    CHECK(shown == world.session().previewVersionChange(QStringLiteral("1.5.0"), QStringLiteral("E4F5A6B"), now));
    CHECK(need(rig, QStringLiteral("versionExplainer"))->property("text").toString() ==
          QStringLiteral("Entries before 08:55 keep 1.4.2 (a1b2c3d). Entries after it use 1.5.0 (e4f5a6b)."));
    REQUIRE(save_enabled(rig));

    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    play(rig, 500);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.storage().repository().call_count(RepositoryOp::ADD_ENTRY) == 1);
    REQUIRE(world.timeline().entry_count() == 1);
    // What is saved is the line that was shown.
    CHECK(world.timeline().last_line() == shown);
    CHECK(world.session().version_text() == QStringLiteral("1.5.0"));
    CHECK(world.session().commit_text() == QStringLiteral("e4f5a6b"));
}

TEST_CASE("version.commit: a full hash is shortened to 7 characters", "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    REQUIRE(world.session().correctVersion(QStringLiteral("1.4.2"), QStringLiteral("a1b2c3d")));
    world.gateway().run_all();
    UiRig rig;
    open_sheet(rig, world);

    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
    retype(rig, need(rig, QStringLiteral("versionCommit")), QStringLiteral("0123456789ABCDEF0123456789ABCDEF01234567"));
    CHECK(preview_of(rig).endsWith(QString::fromUtf8("1.5.0 (0123456)")));
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    play(rig, 500);
    CHECK(world.session().commit_text() == QStringLiteral("0123456"));
}

TEST_CASE("version.commit: a change of the commit alone is a change; the same pair is not",
          "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    REQUIRE(world.session().correctVersion(QStringLiteral("1.4.2"), QStringLiteral("a1b2c3d")));
    world.gateway().run_all();
    UiRig rig;
    open_sheet(rig, world);

    // The same version and the same commit: refused, and cannot be saved.
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.4.2"));
    retype(rig, need(rig, QStringLiteral("versionCommit")), QStringLiteral("A1B2C3D"));
    CHECK(preview_of(rig) == QStringLiteral("That is already the current value."));
    CHECK_FALSE(save_enabled(rig));

    // The same version with no commit is a change, and so is the same version with another one.
    retype(rig, need(rig, QStringLiteral("versionCommit")), QString());
    CHECK(preview_of(rig) == QString::fromUtf8("08:55 Software Version changed: 1.4.2 (a1b2c3d) \xE2\x86\x92 1.4.2"));
    CHECK(save_enabled(rig));
    retype(rig, need(rig, QStringLiteral("versionCommit")), QStringLiteral("0123456"));
    CHECK(preview_of(rig) ==
          QString::fromUtf8("08:55 Software Version changed: 1.4.2 (a1b2c3d) \xE2\x86\x92 1.4.2 (0123456)"));
    CHECK(save_enabled(rig));
}

TEST_CASE("version.commit: \"I typed it wrong\" corrects the version and the commit together and adds no entry",
          "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* sheet = open_sheet(rig, world);
    rig.tap(need(rig, QStringLiteral("versionTypo")));

    // The row is there in both choices; a Correct shows no line.
    CHECK(need(rig, QStringLiteral("versionCommit"))->isVisible());
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.4.3"));
    retype(rig, need(rig, QStringLiteral("versionCommit")), QStringLiteral("a1b2c3d"));
    REQUIRE(save_enabled(rig));
    CHECK(preview_of(rig).isEmpty());
    rig.tap(need(rig, QStringLiteral("versionSave")));
    rig.settle();
    play(rig, 500);

    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().version_text() == QStringLiteral("1.4.3"));
    CHECK(world.session().commit_text() == QStringLiteral("a1b2c3d"));
    CHECK(world.timeline().entry_count() == 0);
    CHECK(world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER) == 1);
}

TEST_CASE("version.commit: a refused commit is flagged inline, with a plain message, and Save stays disabled",
          "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    UiRig rig;
    open_sheet(rig, world);
    QQuickItem* commit = need(rig, QStringLiteral("versionCommit"));
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
    REQUIRE(save_enabled(rig));

    for (const char* refused : {"a1b2c3", "a1b2c3d4e5f6a", "g1b2c3d"})
    {
        CAPTURE(refused);
        retype(rig, commit, QString::fromLatin1(refused));
        const QString message = world.session().validateCommit(QString::fromLatin1(refused));
        REQUIRE_FALSE(message.isEmpty());
        CHECK(commit->property("errorText").toString() == message);
        CHECK(need(rig, QStringLiteral("versionRefusal"))->property("message").toString() == message);
        CHECK_FALSE(save_enabled(rig));
        CHECK(preview_of(rig).isEmpty());
        rig.tap(need(rig, QStringLiteral("versionSave")));
        CHECK(world.timeline().entry_count() == 0);
    }
    retype(rig, commit, QStringLiteral("a1b2c3d"));
    CHECK(commit->property("errorText").toString().isEmpty());
    CHECK(save_enabled(rig));
}

TEST_CASE("version.commit: a plus sign in the version field is refused, so the commit has one way in",
          "[ui][version_sheet][commit]")
{
    HeaderWorld world(true);
    UiRig rig;
    open_sheet(rig, world);
    QQuickItem* to = need(rig, QStringLiteral("versionTo"));

    retype(rig, to, QStringLiteral("1.5.0+e4f5a6b"));
    CHECK_FALSE(to->property("errorText").toString().isEmpty());
    CHECK_FALSE(save_enabled(rig));
}

TEST_CASE("version.commit: names, targets, order and text scale, in light and dark", "[ui][version_sheet][commit]")
{
    // The window is tall enough to hold the sheet: the checks compare positions in the window and know nothing of a
    // scroller (as in version.a11y).
    HeaderWorld world(true);
    UiRig rig(QSize(402, 1600));
    open_sheet(rig, world);
    retype(rig, need(rig, QStringLiteral("versionTo")), QStringLiteral("1.5.0"));
    retype(rig, need(rig, QStringLiteral("versionCommit")), QStringLiteral("e4f5a6b"));

    // A screen reader hears the row as "Commit", an editable text.
    bool found = false;
    for (const Node& node : rig.tree(need(rig, QStringLiteral("versionCommit"))))
    {
        if (node.m_ObjectName == QStringLiteral("formInput"))
        {
            found = true;
            CHECK(node.m_Name == QStringLiteral("Commit"));
            CHECK(node.m_InAccessibleTree);
            CHECK(node.m_Rect.height() >= 44.0);
        }
    }
    CHECK(found);

    SECTION("light")
    {
        expect_clean(check_everything(rig));
    }

    SECTION("dark")
    {
        rig.set_dark(true);
        rig.settle();
        expect_clean(check_everything(rig));
    }
}
