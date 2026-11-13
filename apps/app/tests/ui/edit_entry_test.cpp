// SPDX-License-Identifier: GPL-3.0-or-later
// EditEntry (docs/plan/stage-4.md, 4.10): edit.fields, edit.preview, edit.save_cancel, edit.delete, edit.time_order,
// edit.dst, edit.header_change_entry, edit.validation, edit.layout, edit.time_sheet and the cross-cutting checks. The
// sheet is part of the Review screen and is opened the way the screen opens it, from a row; the time is chosen on the
// second, shorter sheet (EditTimeSheet) that the screen declares after it.
#include "review_test_support.hpp"
// The other headers of the tests come after the project's, as the support header says.
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QMetaObject>
#include <QQuickItem>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::review;
using ej::viewmodel::TimelineModel;
using namespace std::chrono_literals;

namespace
{

/// The Review screen with its Edit entry sheet, and what a test does in the sheet.
struct Editor : Screen
{
    using Screen::Screen;

    void open(std::uint64_t entry_number)
    {
        REQUIRE(QMetaObject::invokeMethod(m_Review, "openEdit", Q_ARG(QString, id_text(entry_number))));
        m_Rig.advance(std::chrono::milliseconds(500));
    }

    [[nodiscard]] QQuickItem* sheet(const char* name = "editSheet")
    {
        QQuickItem* found = named(name);
        REQUIRE(found != nullptr);
        return found;
    }

    [[nodiscard]] QQuickItem* in(const char* sheet_name, const char* name)
    {
        return require_in(m_Rig, QString::fromLatin1(name), sheet(sheet_name));
    }

    [[nodiscard]] bool is_open()
    {
        return sheet()->isVisible();
    }

    /// The page in front: "main" or "type".
    [[nodiscard]] QString page()
    {
        return sheet()->property("page").toString();
    }

    /// The time sheet is up, over the Edit entry sheet.
    [[nodiscard]] bool time_open()
    {
        return sheet()->property("timeOpen").toBool();
    }

    [[nodiscard]] QString text(const char* name)
    {
        QQuickItem* item = named(name, sheet());
        return item != nullptr ? item->property("text").toString() : QString();
    }

    [[nodiscard]] QString preview()
    {
        return text("editPreview");
    }

    /// The text of the Note field.
    void set_note(const QString& note)
    {
        QQuickItem* field = in("editSheet", "editNote");
        REQUIRE(field != nullptr);
        field->setProperty("text", note);
        m_Rig.advance(std::chrono::milliseconds(50));
    }

    void tap_in(const char* sheet_name, const char* name)
    {
        QQuickItem* item = in(sheet_name, name);
        REQUIRE(item != nullptr);
        m_Rig.tap(item);
        m_Rig.advance(std::chrono::milliseconds(500));
    }

    void save()
    {
        tap_in("editSheet", "navTrailing");
    }

    void cancel()
    {
        tap_in("editSheet", "navLeading");
    }

    [[nodiscard]] bool can_save()
    {
        return in("editSheet", "navTrailing")->isEnabled();
    }

    /// Chooses the error type named `name` in the type sheet.
    void choose_type(const QString& name)
    {
        tap_in("editSheet", "editType");
        REQUIRE(page() == QStringLiteral("type"));
        for (QQuickItem* row : all_named(m_Rig, QStringLiteral("typeRow"), sheet()))
        {
            if (row->property("text").toString() == name)
            {
                m_Rig.tap(row);
                m_Rig.advance(std::chrono::milliseconds(500));
                return;
            }
        }
        FAIL("no error type named " << name.toStdString());
    }

    /// Opens the time sheet and sets the hour and the minute with the tumbler's own columns.
    void set_time(int hour, int minute)
    {
        tap_in("editSheet", "editTime");
        REQUIRE(time_open());
        QQuickItem* hours = in("editTimeSheet", "Hours");
        QQuickItem* minutes = in("editTimeSheet", "Minutes");
        REQUIRE(hours != nullptr);
        REQUIRE(minutes != nullptr);
        hours->setProperty("currentIndex", hour);
        minutes->setProperty("currentIndex", minute);
        m_Rig.advance(std::chrono::milliseconds(100));
    }

    void done_time()
    {
        tap_in("editTimeSheet", "navTrailing");
    }

    /// What the time sheet says under its columns.
    [[nodiscard]] QString time_caption()
    {
        return in("editTimeSheet", "editTumbler")->property("caption").toString();
    }

    /// The rows of the screen by entry order, as `timeText + " " + lineText`.
    [[nodiscard]] std::vector<QString> lines()
    {
        return row_lines(m_Rig);
    }
};

QString line_of(const TimelineModel& model, int row)
{
    return model.data(model.index(row), TimelineModel::FULL_LINE).toString();
}

} // namespace

TEST_CASE("edit.fields: the sheet shows the entry and each field edits on its own", "[ui][edit]")
{
    Editor e;
    e.open(4);
    REQUIRE(e.is_open());
    const QString before = prd_example_lines()[3];

    CHECK(e.in("editSheet", "editType")->property("value").toString() == QStringLiteral("Wrong Track Played"));
    CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("11:02"));
    CHECK(e.in("editSheet", "editNote")->property("text").toString() == QStringLiteral("asked for Evening Song"));
    CHECK(e.preview() == before);
    CHECK(e.can_save());

    SECTION("the note")
    {
        e.set_note(QStringLiteral("asked for Morning Medley"));
        CHECK(e.preview() == QStringLiteral("11:02 T| Wrong Track Played – asked for Morning Medley"));
        CHECK(e.in("editSheet", "editType")->property("value").toString() == QStringLiteral("Wrong Track Played"));
        CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("11:02"));
    }

    SECTION("an empty note takes the dash out of the line")
    {
        e.set_note(QString());
        CHECK(e.preview() == QStringLiteral("11:02 T| Wrong Track Played"));
    }

    SECTION("the type")
    {
        e.choose_type(QStringLiteral("Mute Delay"));
        CHECK_FALSE((e.page() == QStringLiteral("type")));
        CHECK(e.is_open());
        CHECK(e.in("editSheet", "editType")->property("value").toString() == QStringLiteral("Mute Delay"));
        CHECK(e.preview() == QStringLiteral("11:02 T| Mute Delay – asked for Evening Song"));
        CHECK(e.in("editSheet", "editNote")->property("text").toString() == QStringLiteral("asked for Evening Song"));
    }

    SECTION("the type sheet marks the current type and Cancel changes nothing")
    {
        e.tap_in("editSheet", "editType");
        int checked = 0;
        for (QQuickItem* row : all_named(e.m_Rig, QStringLiteral("typeRow"), e.sheet()))
        {
            if (row->property("checked").toBool())
            {
                ++checked;
                CHECK(row->property("text").toString() == QStringLiteral("Wrong Track Played"));
            }
        }
        CHECK(checked == 1);
        e.tap_in("editSheet", "navLeading");
        CHECK_FALSE((e.page() == QStringLiteral("type")));
        CHECK(e.in("editSheet", "editType")->property("value").toString() == QStringLiteral("Wrong Track Played"));
    }

    SECTION("the time, with Cancel putting the old time back")
    {
        e.set_time(9, 30);
        CHECK(e.time_caption() == QStringLiteral("Shown in the log as 09:30"));
        e.tap_in("editTimeSheet", "navLeading");
        CHECK_FALSE(e.time_open());
        CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("11:02"));
        CHECK(e.preview() == before);

        e.set_time(9, 30);
        e.done_time();
        CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("09:30"));
        CHECK(e.preview() == QStringLiteral("09:30 T| Wrong Track Played – asked for Evening Song"));
    }
}

TEST_CASE("edit.preview: the line is previewLine of the edit, and saving puts exactly that line in the list",
          "[ui][edit]")
{
    Editor e;
    e.open(2);
    e.choose_type(QStringLiteral("Speech Delay"));
    e.set_note(QStringLiteral("after the greeting"));
    e.set_time(10, 45);
    e.done_time();

    // The model's own answer for the same edit.
    QVariantMap edit;
    edit.insert(QStringLiteral("localDate"), QStringLiteral("2026-11-07"));
    edit.insert(QStringLiteral("localSeconds"), 10 * 3600 + 45 * 60);
    edit.insert(QStringLiteral("errorTypeId"),
                QString::fromStdString(type_named(e.m_World.configuration(), "Speech Delay").format()));
    edit.insert(QStringLiteral("attachedNote"), QStringLiteral("after the greeting"));
    const QString expected = e.m_World.timeline().previewLine(id_text(2), edit);
    REQUIRE_FALSE(expected.isEmpty());
    CHECK(expected == QStringLiteral("10:45 T| Speech Delay – after the greeting"));
    CHECK(e.preview() == expected);

    e.save();
    e.commit();
    CHECK_FALSE(e.is_open());
    // The row moved to its new place, and says what the preview said.
    const std::vector<QString> lines = e.lines();
    CHECK(std::ranges::count(lines, expected) == 1);
    CHECK(lines[static_cast<std::size_t>(e.m_World.timeline().rowOf(id_text(2)))] == expected);
}

TEST_CASE("edit.save_cancel: Cancel writes nothing, Save saves once, and an unchanged sheet is not an edit",
          "[ui][edit]")
{
    Editor e;
    const std::size_t posted = e.m_World.gateway().posted();

    SECTION("Cancel")
    {
        e.open(1);
        e.choose_type(QStringLiteral("Mute Delay"));
        e.set_note(QStringLiteral("not wanted"));
        e.cancel();
        e.commit();
        CHECK_FALSE(e.is_open());
        CHECK(e.m_World.gateway().posted() == posted);
        CHECK(joined(e.lines()) == joined(prd_example_lines()));
        CHECK_FALSE(e.m_World.undo().active());
    }

    SECTION("Save")
    {
        e.open(1);
        e.choose_type(QStringLiteral("Mute Delay"));
        e.save();
        CHECK_FALSE(e.is_open());
        e.commit();
        CHECK(e.m_World.gateway().posted() > posted);
        std::vector<QString> expected = prd_example_lines();
        expected[0] = QStringLiteral("10:05 T| Mute Delay");
        CHECK(joined(e.lines()) == joined(expected));
    }

    SECTION("saving a sheet that was not touched writes nothing")
    {
        e.open(1);
        e.save();
        e.commit();
        CHECK_FALSE(e.is_open());
        CHECK(e.m_World.gateway().posted() == posted);
        CHECK(joined(e.lines()) == joined(prd_example_lines()));
    }

    SECTION("a note entry edits its text")
    {
        e.open(3);
        CHECK(e.in("editSheet", "editType")->isVisible() == false);
        CHECK(e.in("editSheet", "editNote")->property("text").toString() == QStringLiteral("Played Morning Medley"));
        e.set_note(QStringLiteral("Played Evening Song"));
        CHECK(e.preview() == QStringLiteral("10:30 (Free Text) Played Evening Song"));
        e.save();
        e.commit();
        CHECK(e.lines()[2] == QStringLiteral("10:30 (Free Text) Played Evening Song"));
    }
}

TEST_CASE("edit.delete: Delete entry deletes through the undo toast and asks no dialog", "[ui][edit]")
{
    Editor e;
    e.open(2);
    e.tap_in("editSheet", "editDelete");
    CHECK_FALSE(e.is_open());
    // No dialog: nothing but the toast of the UndoController, which the shell shows.
    CHECK(e.named("alertConfirm") == nullptr);
    e.commit();
    std::vector<QString> expected = prd_example_lines();
    expected.erase(expected.begin() + 1);
    CHECK(joined(e.lines()) == joined(expected));
    CHECK(e.m_World.undo().active());
    CHECK(e.m_World.undo().text() == QStringLiteral("Entry deleted"));

    REQUIRE(e.m_World.undo().undo());
    e.commit();
    CHECK(joined(e.lines()) == joined(prd_example_lines()));
}

TEST_CASE("edit.time_order: an earlier time moves the row", "[ui][edit]")
{
    Editor e;
    e.open(4);
    e.set_time(9, 0);
    e.done_time();
    e.save();
    e.commit();
    const std::vector<QString> lines = e.lines();
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == QStringLiteral("09:00 T| Wrong Track Played – asked for Evening Song"));
    CHECK(lines[1] == prd_example_lines()[0]);
    CHECK(e.m_World.timeline().rowOf(id_text(4)) == 0);
}

TEST_CASE("edit.header_change_entry: a header change edits its time and its new value only", "[ui][edit]")
{
    Editor e;
    e.open(5);
    CHECK_FALSE(e.in("editSheet", "editType")->isVisible());
    CHECK(e.in("editSheet", "editNote")->property("text").toString() == QStringLiteral("1.5.0"));
    CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("13:15"));
    CHECK(e.preview() == prd_example_lines()[4]);

    SECTION("a new value")
    {
        e.set_note(QStringLiteral("1.6.0"));
        CHECK(e.preview() == QStringLiteral("13:15 Software Version changed: v1.4.2 → v1.6.0"));
        e.save();
        e.commit();
        CHECK(e.lines()[4] == QStringLiteral("13:15 Software Version changed: v1.4.2 → v1.6.0"));
    }

    SECTION("a new time")
    {
        e.set_time(12, 30);
        e.done_time();
        CHECK(e.preview() == QStringLiteral("12:30 Software Version changed: v1.4.2 → v1.5.0"));
        e.save();
        e.commit();
        CHECK(e.lines()[4] == QStringLiteral("12:30 Software Version changed: v1.4.2 → v1.5.0"));
    }

    SECTION("a value the field refuses is shown with the model's words, and Save is off")
    {
        e.set_note(QStringLiteral("not a version"));
        QVariantMap edit;
        edit.insert(QStringLiteral("localDate"), QStringLiteral("2026-11-07"));
        edit.insert(QStringLiteral("localSeconds"), 13 * 3600 + 15 * 60);
        edit.insert(QStringLiteral("newValue"), QStringLiteral("not a version"));
        const QString refusal = e.m_World.timeline().validateEdit(id_text(5), edit);
        REQUIRE_FALSE(refusal.isEmpty());
        CHECK(e.text("editRefusal") == refusal);
        CHECK_FALSE(e.can_save());
        CHECK(e.preview() != prd_example_lines()[4]);
    }
}

TEST_CASE("edit.validation: an empty note is refused by the model and Save is off", "[ui][edit]")
{
    Editor e;
    e.open(3);
    e.set_note(QString());
    CHECK(e.text("editRefusal") == QStringLiteral("A note cannot be empty."));
    CHECK_FALSE(e.can_save());
    const std::size_t posted = e.m_World.gateway().posted();
    e.save();
    e.commit();
    CHECK(e.is_open());
    CHECK(e.m_World.gateway().posted() == posted);

    e.set_note(QStringLiteral("Played Morning Medley again"));
    CHECK(e.text("editRefusal").isEmpty());
    CHECK(e.can_save());
}

TEST_CASE("edit.time_format: the tumbler has the columns of the time format and the line uses it", "[ui][edit]")
{
    Options options;
    options.time_format = TimeFormat{TimeStyle::H12, true, MeridiemCase::LOWER};
    Editor e(std::move(options));
    const TimelineModel& timeline = e.m_World.timeline();
    const auto shown = [&timeline](int hour, int minute)
    { return timeline.formatLocalSeconds(hour * 3600 + minute * 60); };

    e.open(1);
    CHECK(e.in("editSheet", "editTime")->property("value").toString() == shown(10, 5));
    CHECK(shown(10, 5).endsWith(QStringLiteral(" am")));
    e.tap_in("editSheet", "editTime");
    REQUIRE(e.time_open());
    QQuickItem* tumbler = e.in("editTimeSheet", "editTumbler");
    REQUIRE(tumbler != nullptr);
    CHECK(tumbler->property("twelveHour").toBool());
    CHECK(tumbler->property("showSeconds").toBool());
    for (const char* column : {"Hours", "Minutes", "Seconds", "AM/PM"})
    {
        QQuickItem* item = e.in("editTimeSheet", column);
        REQUIRE(item != nullptr);
        CHECK(item->isVisible());
    }

    // 11:05 pm: the hour column is 12, 1 ... 11, so index 11 is 11, and the meridiem column says pm.
    e.in("editTimeSheet", "Hours")->setProperty("currentIndex", 11);
    e.in("editTimeSheet", "AM/PM")->setProperty("currentIndex", 1);
    e.m_Rig.advance(std::chrono::milliseconds(100));
    CHECK(e.time_caption() == QStringLiteral("Shown in the log as %1").arg(shown(23, 5)));
    e.done_time();
    e.save();
    e.commit();
    const std::vector<QString> lines = e.lines();
    REQUIRE(lines.size() == 6);
    CHECK(lines.back().startsWith(shown(23, 5) + QLatin1Char(' ')));
    CHECK(lines.back().endsWith(QStringLiteral("T| Wrong Response")));
}

TEST_CASE("edit.layout: the swatch, the time chip, the footnote and the delete button are the design's", "[ui][edit]")
{
    Editor e;
    e.open(4);
    const QVariantMap data = e.m_World.timeline().entryForEdit(id_text(4));

    // The Error row has the tile colour of the type as a swatch before its name.
    QQuickItem* type = e.in("editSheet", "editType");
    QQuickItem* swatch = find_in(e.m_Rig, QStringLiteral("rowSwatch"), type);
    REQUIRE(swatch != nullptr);
    CHECK(swatch->isVisible());
    const int slot = data.value(QStringLiteral("colourSlot")).toInt();
    REQUIRE(slot >= 1);
    QColor tile;
    REQUIRE(QMetaObject::invokeMethod(e.m_Rig.theme(), "tileColour", Q_RETURN_ARG(QColor, tile), Q_ARG(int, slot)));
    CHECK(swatch->property("color").value<QColor>() == tile);

    // The Time row shows its value as a chip and has no chevron; the Time row is 52 pt and the Error row at least that.
    QQuickItem* time = e.in("editSheet", "editTime");
    CHECK(time->property("valueChip").toBool());
    QQuickItem* mark = find_in(e.m_Rig, QStringLiteral("rowMark"), time);
    CHECK((mark == nullptr || !mark->isVisible()));
    // (The Error row grows if its value wraps, which the test font can make it do.)
    CHECK(e.m_Rig.geometry(type).height() >= 52);
    CHECK(e.m_Rig.geometry(time).height() == 52);

    // The footnote is the model's, under the line of the shared log.
    const QString footnote = data.value(QStringLiteral("footnote")).toString();
    REQUIRE_FALSE(footnote.isEmpty());
    CHECK(e.text("editFootnote") == footnote);
    CHECK(e.m_Rig.geometry(e.in("editSheet", "editFootnote")).top() >=
          e.m_Rig.geometry(e.in("editSheet", "editPreview")).bottom());

    // "Delete entry" is 52 pt high and rests on the bottom of the sheet's content, over the home indicator.
    QQuickItem* del = e.in("editSheet", "editDelete");
    CHECK(e.m_Rig.geometry(del).height() == 52);
    const QQuickItem* content = e.sheet()->property("contentArea").value<QQuickItem*>();
    REQUIRE(content != nullptr);
    CHECK(e.m_Rig.geometry(del).bottom() == e.m_Rig.geometry(const_cast<QQuickItem*>(content)).bottom());
}

TEST_CASE(
    "edit.time_sheet: the time is chosen on a second, shorter sheet over the first, and Now sets the time of day now",
    "[ui][edit]")
{
    Editor e;
    e.open(4);
    CHECK_FALSE(e.sheet("editTimeSheet")->property("isOpen").toBool());
    e.tap_in("editSheet", "editTime");
    REQUIRE(e.time_open());
    QQuickItem* edit_sheet = e.sheet();
    QQuickItem* time_sheet = e.sheet("editTimeSheet");
    CHECK(time_sheet->property("isOpen").toBool());
    // The Edit entry sheet is still open under it, with its page unchanged.
    CHECK(edit_sheet->property("isOpen").toBool());
    CHECK(e.page() == QStringLiteral("main"));

    // It is declared after the Edit entry sheet in the same layer, so it is over it, and it is shorter.
    REQUIRE(time_sheet->parentItem() == edit_sheet->parentItem());
    const QList<QQuickItem*> siblings = edit_sheet->parentItem()->childItems();
    CHECK(siblings.indexOf(time_sheet) > siblings.indexOf(edit_sheet));
    QQuickItem* edit_panel = find_in(e.m_Rig, QStringLiteral("sheetPanel"), edit_sheet);
    QQuickItem* time_panel = find_in(e.m_Rig, QStringLiteral("sheetPanel"), time_sheet);
    REQUIRE(edit_panel != nullptr);
    REQUIRE(time_panel != nullptr);
    CHECK(e.m_Rig.geometry(time_panel).top() > e.m_Rig.geometry(edit_panel).top() + 100);
    CHECK(e.m_Rig.geometry(time_panel).bottom() >= e.m_Rig.geometry(edit_panel).bottom() - 1);

    // "Now" is the time of day now in the zone of the entry.
    const int now = e.m_World.timeline().nowLocalSeconds(id_text(4));
    REQUIRE(now >= 0);
    REQUIRE(e.in("editTimeSheet", "tumblerNow")->isVisible());
    e.tap_in("editTimeSheet", "tumblerNow");
    CHECK(edit_sheet->property("localSeconds").toInt() == now);
    CHECK(e.in("editSheet", "editTime")->property("value").toString() == e.m_World.timeline().formatLocalSeconds(now));

    // A tap outside the time sheet is Done: the time is kept and the Edit entry sheet is in front again.
    QQuickItem* scrim = find_in(e.m_Rig, QStringLiteral("sheetScrim"), time_sheet);
    REQUIRE(scrim != nullptr);
    e.m_Rig.tap(scrim, QPointF(40, 20));
    e.m_Rig.advance(std::chrono::milliseconds(500));
    CHECK_FALSE(e.time_open());
    CHECK_FALSE(time_sheet->property("isOpen").toBool());
    CHECK(edit_sheet->property("isOpen").toBool());
    CHECK(edit_sheet->property("localSeconds").toInt() == now);
}

TEST_CASE("edit.dst: a time in the hour the clocks skip is refused with the model's message, and never saved",
          "[ui][edit]")
{
    // 4 Oct 2026 in Melbourne: the clocks go from 02:00 to 03:00. The entry is at 01:30 (+10:00), 2026-10-03 15:30 UTC.
    const MelbourneDstZones zones;
    HeaderValues header = test::sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 10 / 4;
    Entry entry;
    entry.m_Id = test::sample_id<EntryId>(1);
    entry.m_Captured = Timestamp{test::utc(2026, 10, 3, 15, 30), MelbourneDstZones::ZONE};
    entry.m_Body = NoteEntry{std::nullopt, "Checked the volume"};
    Options options;
    options.zones = &zones;
    options.entries = std::vector<Entry>{entry};
    options.header = header;
    Editor e(std::move(options));
    REQUIRE(joined(e.lines()) == joined(std::vector<QString>{QStringLiteral("01:30 (Free Text) Checked the volume")}));

    e.open(1);
    CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("01:30"));

    const QString message = e.m_World.timeline()
                                .localToUtc(id_text(1), QStringLiteral("2026-10-04"), 2 * 3600 + 30 * 60)
                                .value(QStringLiteral("message"))
                                .toString();
    REQUIRE_FALSE(message.isEmpty());

    e.set_time(2, 30);
    // The message is the tumbler's error text, and neither Done nor Save can be pressed.
    QQuickItem* tumbler = e.in("editTimeSheet", "editTumbler");
    REQUIRE(tumbler != nullptr);
    CHECK(tumbler->property("errorText").toString() == message);
    CHECK(tumbler->property("invalid").toBool());
    CHECK_FALSE(e.in("editTimeSheet", "navTrailing")->isEnabled());
    CHECK_FALSE(e.can_save());

    // A press on Done does nothing; the time sheet stays, and a tap outside it does not dismiss it either.
    e.done_time();
    CHECK(e.time_open());
    CHECK_FALSE(e.sheet("editTimeSheet")->property("dismissible").toBool());
    // Cancel puts the entry's own time back and nothing was written.
    const std::size_t posted = e.m_World.gateway().posted();
    e.tap_in("editTimeSheet", "navLeading");
    CHECK_FALSE(e.time_open());
    CHECK(e.in("editSheet", "editTime")->property("value").toString() == QStringLiteral("01:30"));
    CHECK(e.can_save());
    e.commit();
    CHECK(e.m_World.gateway().posted() == posted);

    // The hour after the gap is fine, and the saved line is the preview.
    e.set_time(3, 30);
    CHECK(tumbler->property("errorText").toString().isEmpty());
    e.done_time();
    CHECK_FALSE(e.time_open());
    REQUIRE(e.can_save());
    CHECK(e.preview() == QStringLiteral("03:30 (Free Text) Checked the volume"));
    e.save();
    e.commit();
    CHECK(joined(e.lines()) == joined(std::vector<QString>{QStringLiteral("03:30 (Free Text) Checked the volume")}));
}

TEST_CASE("edit.dst: a time in the repeated hour is allowed and says that the first one is used", "[ui][edit]")
{
    // 5 Apr 2026 in Melbourne: the clocks go from 03:00 back to 02:00. The entry is at 01:30 (+11:00).
    const MelbourneDstZones zones;
    HeaderValues header = test::sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 4 / 5;
    Entry entry;
    entry.m_Id = test::sample_id<EntryId>(1);
    entry.m_Captured = Timestamp{test::utc(2026, 4, 4, 14, 30), MelbourneDstZones::ZONE};
    entry.m_Body = NoteEntry{std::nullopt, "Checked the volume"};
    Options options;
    options.zones = &zones;
    options.entries = std::vector<Entry>{entry};
    options.header = header;
    Editor e(std::move(options));

    e.open(1);
    e.set_time(2, 30);
    const QVariantMap checked =
        e.m_World.timeline().localToUtc(id_text(1), QStringLiteral("2026-04-05"), 2 * 3600 + 30 * 60);
    REQUIRE(checked.value(QStringLiteral("ok")).toBool());
    REQUIRE(checked.value(QStringLiteral("repeated")).toBool());
    CHECK(e.in("editTimeSheet", "editTumbler")->property("errorText").toString().isEmpty());
    CHECK(text_in(e.m_Rig, QStringLiteral("editTimeNote")) == checked.value(QStringLiteral("message")).toString());
    CHECK(e.in("editTimeSheet", "navTrailing")->isEnabled());
    e.done_time();
    CHECK(e.can_save());
    e.save();
    e.commit();
    CHECK(joined(e.lines()) == joined(std::vector<QString>{QStringLiteral("02:30 (Free Text) Checked the volume")}));
}

namespace
{

/// The sheets on their own, as the Sheet and Alert tests have theirs: nothing under them, so the checks that read the
/// whole window see the sheets and not a screen that a real sheet would cover. The time sheet is declared after the
/// Edit entry sheet, as the Review screen declares it.
struct Alone
{
    static constexpr const char* HOST = R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            id: host
            required property var timeline
            required property var picker
            required property var settings
            property alias editor: editSheet
            EditEntry { id: editSheet; timeline: host.timeline; picker: host.picker; settings: host.settings }
            EditTimeSheet { id: timeSheet; edit: editSheet; timeline: host.timeline; settings: host.settings }
        }
    )";

    Alone() : m_Host(nullptr), m_Sheet(nullptr)
    {
        m_World.open_prd_example();
        m_Rig.attach(m_World.clock());
        m_Rig.attach(m_World.gateway());
        QVariantMap properties = m_World.properties();
        properties.remove(QStringLiteral("share"));
        m_Host = m_Rig.load_source(QByteArray(HOST), properties);
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Host != nullptr);
        m_Sheet = m_Host->property("editor").value<QQuickItem*>();
        REQUIRE(m_Sheet != nullptr);
    }

    World m_World;
    UiRig m_Rig;
    QQuickItem* m_Host;
    QQuickItem* m_Sheet;

    void open(std::uint64_t entry_number)
    {
        REQUIRE(QMetaObject::invokeMethod(m_Sheet, "edit", Q_ARG(QString, id_text(entry_number))));
        m_Rig.advance(std::chrono::milliseconds(500));
    }

    void tap(const char* name)
    {
        QQuickItem* item = find_in(m_Rig, QString::fromLatin1(name), m_Sheet);
        REQUIRE(item != nullptr);
        m_Rig.tap(item);
        m_Rig.advance(std::chrono::milliseconds(500));
    }

    [[nodiscard]] QString page() const
    {
        return m_Sheet->property("page").toString();
    }
};

} // namespace

TEST_CASE("edit.cross_cutting: names, targets, order and text scale, on every page and sheet, light and dark",
          "[ui][edit]")
{
    Alone a;

    SECTION("the first page")
    {
        a.open(4);
        REQUIRE(a.page() == QStringLiteral("main"));
        expect_clean(check_everything(a.m_Rig));
        a.m_Rig.set_dark(true);
        expect_clean(check_everything(a.m_Rig));
    }

    SECTION("the first page with a refusal")
    {
        a.open(3);
        require_in(a.m_Rig, QStringLiteral("editNote"), a.m_Sheet)->setProperty("text", QString());
        a.m_Rig.advance(std::chrono::milliseconds(50));
        REQUIRE_FALSE(require_in(a.m_Rig, QStringLiteral("navTrailing"), a.m_Sheet)->isEnabled());
        expect_clean(check_everything(a.m_Rig));
    }

    SECTION("a header change")
    {
        a.open(5);
        expect_clean(check_everything(a.m_Rig));
    }

    SECTION("the time sheet")
    {
        a.open(4);
        a.tap("editTime");
        REQUIRE(a.m_Sheet->property("timeOpen").toBool());
        // The Edit entry sheet is under the time sheet, under its scrim and disabled, and the two overlap, so the
        // reading order of the window (which goes by position) is not asked of the pair; each sheet has its own check,
        // the Edit entry sheet above and this one on its own controls, here for names, targets, text scale and the safe
        // area.
        const auto checks = [&a]
        {
            expect_clean(check_names(a.m_Rig));
            expect_clean(check_targets(a.m_Rig));
            expect_clean(check_text_scale(a.m_Rig));
            expect_clean(check_safe_area(a.m_Rig, 0, 0));
        };
        checks();
        a.m_Rig.set_dark(true);
        checks();
    }

    SECTION("the type page")
    {
        a.open(4);
        a.tap("editType");
        REQUIRE(a.page() == QStringLiteral("type"));
        expect_clean(check_everything(a.m_Rig));
        a.m_Rig.set_dark(true);
        expect_clean(check_everything(a.m_Rig));
    }

    SECTION("nothing interactive in the insets of an iPhone 17, nor in those of a taller phone")
    {
        a.open(4);
        a.m_Sheet->setProperty("overrideInsets", true);
        for (const auto& insets : {std::pair{59.0, 34.0}, std::pair{79.0, 54.0}})
        {
            a.m_Sheet->setProperty("overrideTop", insets.first);
            a.m_Sheet->setProperty("overrideBottom", insets.second);
            a.m_Rig.layout();
            expect_clean(check_safe_area(a.m_Rig, insets.first, insets.second));
        }
    }

    SECTION("with Reduce Motion the sheet only fades")
    {
        a.m_Rig.set_reduce_motion(true);
        a.open(4);
        expect_clean(check_motion_reduced(a.m_Rig));
    }
}

TEST_CASE("edit.voiceover: Cancel, Save and Delete entry are buttons, and the sheet is a dialog", "[ui][edit]")
{
    Editor e;
    e.open(4);
    QStringList names;
    for (const Node& node : e.m_Rig.accessible_tree(e.sheet()))
    {
        if (node.m_Role == QAccessible::Button)
        {
            names.push_back(node.m_Name);
        }
    }
    CHECK(names.contains(QStringLiteral("Cancel")));
    CHECK(names.contains(QStringLiteral("Save")));
    CHECK(names.contains(QStringLiteral("Delete entry")));
    CHECK(names.contains(QStringLiteral("Error, Wrong Track Played")));
    CHECK(names.contains(QStringLiteral("Time, 11:02")));
    // The sheet cannot be dismissed by a swipe: Cancel and Save are the ways out.
    CHECK_FALSE(e.sheet()->property("dismissible").toBool());
    CHECK(line_of(e.m_World.timeline(), 3) == prd_example_lines()[3]);
}
