// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/full_day.hpp"

#include "golden.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ej::test
{

namespace vm = ej::viewmodel;
using namespace std::chrono_literals;

namespace
{

// Non-ASCII characters of the wording, as escapes so that the source stays ASCII.
const QString TIMES = QString(QChar(0x00D7));
const QString ARROW = QString(QChar(0x2192));
const QString DASH = QString(QChar(0x2013));

int role_of(const QAbstractItemModel& model, const char* name)
{
    const int role = model.roleNames().key(QByteArray(name), -1);
    REQUIRE(role != -1);
    return role;
}

QString text_in(const QAbstractItemModel& model, int row, const char* role)
{
    return model.data(model.index(row, 0), role_of(model, role)).toString();
}

int number_in(const QAbstractItemModel& model, int row, const char* role)
{
    return model.data(model.index(row, 0), role_of(model, role)).toInt();
}

/// The row of `model` whose `role` has the value `value`, or -1.
int row_with(const QAbstractItemModel& model, const char* role, const QString& value)
{
    for (int row = 0; row < model.rowCount(); ++row)
    {
        if (text_in(model, row, role) == value)
        {
            return row;
        }
    }
    return -1;
}

std::string op_of(const std::string& detail)
{
    return detail.substr(0, detail.find(' '));
}

bool starts_with(const std::string& text, const std::string& prefix)
{
    return text.rfind(prefix, 0) == 0;
}

/// The golden file, split into its lines.
QStringList golden_lines()
{
    const auto bytes = read_bytes(std::filesystem::path(EJ_GOLDEN_DIR) / "prd-example.txt");
    REQUIRE(bytes.has_value());
    return QString::fromUtf8(bytes->data(), static_cast<qsizetype>(bytes->size())).split(QLatin1Char('\n'));
}

class Day
{
public:
    explicit Day(Rig& rig) : m_Rig(rig) {}

    [[nodiscard]] int goldens() const
    {
        return m_Goldens;
    }

    // ----------------------------------------------------------------------------------------------- lookups

    [[nodiscard]] QString board_type(const char* label)
    {
        const int row = row_with(m_Rig.board(), "label", QString::fromLatin1(label));
        REQUIRE(row >= 0);
        return text_in(m_Rig.board(), row, "typeId");
    }

    [[nodiscard]] int board_count(const char* label)
    {
        const int row = row_with(m_Rig.board(), "label", QString::fromLatin1(label));
        REQUIRE(row >= 0);
        return number_in(m_Rig.board(), row, "count");
    }

    [[nodiscard]] int other_count()
    {
        return number_in(m_Rig.board(), vm::OTHER_ROW, "count");
    }

    [[nodiscard]] std::vector<int> board_counts()
    {
        std::vector<int> counts;
        for (int row = 0; row < m_Rig.board().rowCount(); ++row)
        {
            counts.push_back(number_in(m_Rig.board(), row, "count"));
        }
        return counts;
    }

    [[nodiscard]] QStringList timeline_lines()
    {
        QStringList lines;
        for (int row = 0; row < m_Rig.timeline().rowCount(); ++row)
        {
            lines.push_back(text_in(m_Rig.timeline(), row, "fullLine"));
        }
        return lines;
    }

    /// The id of the live entry whose line starts with `prefix` ("10:30 "), the first one at or after `from_row`.
    [[nodiscard]] QString entry_starting(const QString& prefix, int from_row = 0)
    {
        for (int row = from_row; row < m_Rig.timeline().rowCount(); ++row)
        {
            if (text_in(m_Rig.timeline(), row, "fullLine").startsWith(prefix))
            {
                return text_in(m_Rig.timeline(), row, "entryId");
            }
        }
        FAIL("no entry starts with " << prefix.toStdString());
    }

    [[nodiscard]] QString line_of(const QString& entry_id)
    {
        const int row = m_Rig.timeline().rowOf(entry_id);
        REQUIRE(row >= 0);
        return text_in(m_Rig.timeline(), row, "fullLine");
    }

    [[nodiscard]] QString location_id(const char* name)
    {
        const int row = row_with(*m_Rig.settings().locations(), "name", QString::fromLatin1(name));
        REQUIRE(row >= 0);
        return text_in(*m_Rig.settings().locations(), row, "locationId");
    }

    /// What the store holds of an entry, live or deleted, in the open log.
    [[nodiscard]] std::optional<Entry> stored_entry(const StoredLog& log, const QString& entry_id)
    {
        for (const Entry& entry : log.m_Log.entries())
        {
            if (QString::fromStdString(entry.m_Id.format()) == entry_id)
            {
                return entry;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static UtcMillis melbourne(int day, int hour, int minute, int second = 0)
    {
        return utc(2026, 11, static_cast<unsigned>(day), hour, minute, second) - 11h;
    }

    /// Compares `text` with the PRD example, the one contract, byte for byte.
    void golden_check(const std::string& text)
    {
        check_golden("prd-example", text);
        ++m_Goldens;
    }

    // ----------------------------------------------------------------------------------------------- steps

    void step_1_start();
    void step_2_tap_before_header_then_undo();
    void step_3_header();
    void step_4_single_tap();
    void step_5_double_tap();
    void step_6_draft_and_note();
    void step_7_other_sheet();
    void step_8_tap();
    void step_9_restart();
    void step_10_version_change();
    void step_11_wrong_tile();
    void step_12_review_edits();
    void step_13_golden_check_1();
    void step_14_share_cancelled();
    void step_15_new_day();
    void step_16_share_first();
    void step_17_start_the_new_log();
    void step_18_history();
    void step_19_closing_checks();

private:
    Rig& m_Rig;
    int m_Goldens = 0;
    QString m_NoteDraft = QStringLiteral("Played Morning Medly");
    LogId m_FirstLog;
    std::size_t m_HapticsBeforeReview = 0;
};

// ------------------------------------------------------------------------------------------------------- 1

void Day::step_1_start()
{
    Rig& rig = m_Rig;
    // Sat 7 Nov 08:55. The store is empty and nothing is configured.
    CHECK_FALSE(rig.settings().configured());
    rig.settle();
    CHECK(rig.session().state() == vm::LogSession::State::NEEDS_SETUP);

    rig.import_sample_configuration();
    rig.settle();

    CHECK(rig.settings().configured());
    CHECK(rig.session().state() == vm::LogSession::State::OPEN);
    CHECK(rig.session().tester() == QStringLiteral("Sam Example"));
    CHECK(rig.session().date_text() == QStringLiteral("Sat 7 Nov"));
    QStringList missing = rig.session().missing_fields();
    missing.sort();
    CHECK(missing == QStringList{QStringLiteral("location"), QStringLiteral("software_commit"),
                                 QStringLiteral("software_version"), QStringLiteral("unit_number")});
    CHECK(rig.timeline().entry_count() == 0);
    CHECK(rig.timeline().rowCount() == 0);
    CHECK(rig.board().rowCount() == vm::BOARD_ROWS);
    for (const int count : board_counts())
    {
        CHECK(count == 0);
    }
    CHECK(rig.platform().haptics().empty());
    CHECK_FALSE(rig.undo().active());
    CHECK(rig.session().recovery_notice().isEmpty());
}

// ------------------------------------------------------------------------------------------------------- 2

void Day::step_2_tap_before_header_then_undo()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 8, 58);
    const QString wrong_response = board_type("Wrong Response");

    rig.hold_commits();
    REQUIRE(rig.tap(wrong_response));
    rig.pump();
    // Held: the touch-down impact has fired; nothing that says "saved" has appeared.
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::IMPACT) == 1);
    CHECK(rig.session().pending_writes() == 1);
    CHECK(board_count("Wrong Response") == 0);
    CHECK(rig.board().total_count() == 0);
    CHECK(rig.timeline().entry_count() == 0);
    CHECK_FALSE(rig.undo().active());

    rig.release_commits();
    CHECK(board_count("Wrong Response") == 1);
    CHECK(rig.undo().active());
    CHECK(rig.undo().text() == QStringLiteral("Wrong Response logged at 08:58"));
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::IMPACT) == 1);
    CHECK(rig.platform().haptics().size() == 1);

    rig.advance(2s);
    CHECK(rig.undo().undo());
    rig.settle();
    CHECK(board_count("Wrong Response") == 0);
    CHECK_FALSE(rig.undo().active());
    CHECK(rig.platform().haptics().size() == 1);

    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    REQUIRE(stored->m_Log.entries().size() == 1);
    CHECK(stored->m_Log.entries()[0].m_Deleted);
}

// ------------------------------------------------------------------------------------------------------- 3

void Day::step_3_header()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 9, 0);
    rig.settle();
    const std::size_t writes = rig.mutating_calls();

    CHECK_FALSE(rig.session().validateHeaderValue(QStringLiteral("software_version"), QStringLiteral("1.4")).isEmpty());
    CHECK_FALSE(rig.session().correctHeader(QStringLiteral("software_version"), QStringLiteral("1.4")));
    rig.settle();
    CHECK(rig.mutating_calls() == writes); // a refused call writes nothing

    CHECK(rig.session().validateHeaderValue(QStringLiteral("unit_number"), QStringLiteral("28")).isEmpty());
    CHECK(rig.session().correctHeader(QStringLiteral("unit_number"), QStringLiteral("28")));
    rig.settle();
    CHECK(rig.session().unit_text() == QStringLiteral("UNIT-28"));
    CHECK(rig.session().correctHeader(QStringLiteral("unit_number"), QStringLiteral("27")));
    rig.settle();
    CHECK(rig.session().correctHeader(QStringLiteral("location"), location_id("North Site")));
    rig.settle();
    CHECK(rig.session().correctHeader(QStringLiteral("software_version"), QStringLiteral("1.4.2")));
    rig.settle();

    // The commit is optional and is the one thing still to fill.
    CHECK(rig.session().missing_fields() == QStringList{QStringLiteral("software_commit")});
    CHECK(rig.session().unit_text() == QStringLiteral("UNIT-27"));
    CHECK(rig.session().location_text() == QStringLiteral("North Site"));
    CHECK(rig.session().version_text() == QStringLiteral("1.4.2"));
    // Correct adds no entry.
    CHECK(rig.timeline().entry_count() == 0);
    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    for (const Entry& entry : stored->m_Log.entries())
    {
        CHECK_FALSE(std::holds_alternative<HeaderChangeEntry>(entry.m_Body));
    }
}

// ------------------------------------------------------------------------------------------------------- 4

void Day::step_4_single_tap()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 10, 5);
    REQUIRE(rig.tap(board_type("Wrong Response")));
    rig.settle();
    CHECK(board_count("Wrong Response") == 1);
    CHECK(rig.undo().active());
    rig.advance(6s);
    CHECK_FALSE(rig.undo().active());
    CHECK_FALSE(rig.undo().undo());
    CHECK(board_count("Wrong Response") == 1);
}

// ------------------------------------------------------------------------------------------------------- 5

void Day::step_5_double_tap()
{
    Rig& rig = m_Rig;
    const QString mute_delay = board_type("Mute Delay");
    rig.go_to(7, 10, 20, 0);
    REQUIRE(rig.tap(mute_delay));
    rig.settle();
    rig.go_to(7, 10, 20, 1);
    REQUIRE(rig.tap(mute_delay));
    rig.settle();

    CHECK(rig.undo().text() == QStringLiteral("Mute Delay logged ") + TIMES + QStringLiteral("2 at 10:20"));
    CHECK(rig.undo().repeat_count() == 2);
    CHECK(board_count("Mute Delay") == 2);
    rig.advance(5s);
    CHECK_FALSE(rig.undo().active());
    CHECK(board_count("Mute Delay") == 2);
}

// ------------------------------------------------------------------------------------------------------- 6

void Day::step_6_draft_and_note()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 10, 29);
    rig.session().set_draft(m_NoteDraft);
    rig.advance(400ms);
    rig.settle();
    // The draft is in the store before the note is.
    const auto draft = rig.stored_draft();
    REQUIRE(draft.has_value());
    CHECK(QString::fromStdString(draft->m_Text) == m_NoteDraft);

    rig.go_to(7, 10, 30, 5);
    REQUIRE(rig.add_note(m_NoteDraft));
    rig.settle();

    const auto cleared = rig.stored_draft();
    CHECK((!cleared.has_value() || cleared->m_Text.empty()));
    CHECK(rig.session().draft().isEmpty());
    CHECK(rig.undo().text() == QStringLiteral("Note added at 10:30"));
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::SUCCESS) == 1);
    CHECK(rig.timeline().entry_count() == 4);
    CHECK(line_of(entry_starting(QStringLiteral("10:30 "))) ==
          QStringLiteral("10:30 Played Morning Medly"));
}

// ------------------------------------------------------------------------------------------------------- 7

void Day::step_7_other_sheet()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 10, 45);
    CHECK(other_count() == 0);

    const QString added = rig.picker().addType(QStringLiteral("Wrong Greeting")); // the id of the new type
    CHECK_FALSE(added.isEmpty());
    CHECK(rig.picker().add_type_error().isEmpty());
    rig.settle();
    const int row = row_with(rig.picker(), "label", QStringLiteral("Wrong Greeting"));
    REQUIRE(row >= 0);
    CHECK(number_in(rig.picker(), row, "section") == static_cast<int>(vm::ErrorPickerModel::OFF_BOARD));
    const QString greeting = text_in(rig.picker(), row, "typeId");
    CHECK(greeting == added);
    // Adding it does not record it (decision 12).
    CHECK(other_count() == 0);
    CHECK(rig.timeline().entry_count() == 4);

    REQUIRE(rig.tap(greeting));
    rig.settle();
    CHECK(other_count() == 1);
    rig.advance(3s);
    CHECK(rig.undo().undo());
    rig.settle();
    CHECK(other_count() == 0);

    // The entry is soft-deleted, and the type is still in the master list.
    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    int greeting_entries = 0;
    for (const Entry& entry : stored->m_Log.entries())
    {
        if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body); error != nullptr)
        {
            if (error->m_ErrorTypeLabel == "Wrong Greeting")
            {
                ++greeting_entries;
                CHECK(entry.m_Deleted);
            }
        }
    }
    CHECK(greeting_entries == 1);
    CHECK(row_with(*rig.settings().error_types(), "name", QStringLiteral("Wrong Greeting")) >= 0);
}

// ------------------------------------------------------------------------------------------------------- 8

void Day::step_8_tap()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 11, 2, 10);
    REQUIRE(rig.tap(board_type("Wrong Track Played")));
    rig.settle();
    CHECK(board_count("Wrong Track Played") == 1);
    CHECK(rig.timeline().entry_count() == 5);
}

// ------------------------------------------------------------------------------------------------------- 9

void Day::step_9_restart()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 11, 30);
    const QStringList lines_before = timeline_lines();
    const std::vector<int> counts_before = board_counts();
    const QString header_before = rig.timeline().header_text();
    REQUIRE(lines_before.size() == 5);

    rig.session().set_draft(QStringLiteral("Visitor from head office"));
    rig.session().flushDraft();
    rig.settle();
    rig.restart();

    CHECK(rig.session().state() == vm::LogSession::State::OPEN);
    CHECK(rig.session().tester() == QStringLiteral("Sam Example"));
    CHECK(rig.session().unit_text() == QStringLiteral("UNIT-27"));
    CHECK(rig.session().location_text() == QStringLiteral("North Site"));
    CHECK(rig.session().version_text() == QStringLiteral("1.4.2"));
    CHECK(rig.timeline().header_text() == header_before);
    CHECK(timeline_lines() == lines_before);
    CHECK(board_counts() == counts_before);
    CHECK(rig.session().draft() == QStringLiteral("Visitor from head office")); // DS-03, NOTE-03
    CHECK_FALSE(rig.undo().active());
    CHECK(rig.session().recovery_notice().isEmpty());

    // Then the draft is cleared.
    rig.session().set_draft(QString());
    rig.advance(400ms);
    rig.settle();
    const auto draft = rig.stored_draft();
    CHECK((!draft.has_value() || draft->m_Text.empty()));
}

// ------------------------------------------------------------------------------------------------------ 10

void Day::step_10_version_change()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 13, 15);
    rig.settle();
    const std::size_t writes = rig.mutating_calls();

    const QString version = QStringLiteral("software_version");
    CHECK_FALSE(rig.session().changeHeader(version, QStringLiteral("1.4.2"), rig.session().now_local_seconds()));
    rig.settle();
    CHECK(rig.mutating_calls() == writes);

    CHECK(rig.session().changeHeader(version, QStringLiteral("1.5.1"), rig.session().now_local_seconds()));
    rig.settle();
    CHECK(rig.mutating_calls() == writes + 1);
    CHECK(rig.timeline().entry_count() == 6);
    CHECK(rig.timeline().last_line() ==
          QStringLiteral("13:15 Software Version changed: v1.4.2 ") + ARROW + QStringLiteral(" v1.5.1"));
    CHECK(rig.session().version_text() == QStringLiteral("1.5.1"));
    CHECK(rig.timeline().header_text().contains(QStringLiteral("Software Version: v1.4.2")));
}

// ------------------------------------------------------------------------------------------------------ 11

void Day::step_11_wrong_tile()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 13, 41, 30);
    REQUIRE(rig.tap(board_type("Track Delay")));
    rig.settle();
    CHECK(board_count("Track Delay") == 1);
    CHECK(rig.timeline().entry_count() == 7);
}

// ------------------------------------------------------------------------------------------------------ 12

void Day::step_12_review_edits()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 17, 0);
    rig.advance(10s); // the toast of step 11 is long gone; this keeps the next toast checks about review actions only
    m_HapticsBeforeReview = rig.platform().haptics().size();
    CHECK_FALSE(rig.undo().active());

    // a) The typo in the note.
    {
        const QString note = entry_starting(QStringLiteral("10:30 "));
        QVariantMap edit;
        edit.insert(QStringLiteral("noteText"), QStringLiteral("Played Morning Medley"));
        CHECK(rig.timeline().validateEdit(note, edit).isEmpty());
        CHECK(rig.timeline().saveEntry(note, edit));
        rig.settle();
        CHECK(line_of(note) == QStringLiteral("10:30 Played Morning Medley"));
    }
    // b) An attached note on the 11:02 error; the preview is the line that is then saved.
    {
        const QString entry = entry_starting(QStringLiteral("11:02 "));
        QVariantMap edit;
        edit.insert(QStringLiteral("attachedNote"), QStringLiteral("asked for Evening Song"));
        const QString preview = rig.timeline().previewLine(entry, edit);
        CHECK(preview ==
              QStringLiteral("11:02 T| Wrong Track Played ") + DASH + QStringLiteral(" asked for Evening Song"));
        CHECK(rig.timeline().saveEntry(entry, edit));
        rig.settle();
        CHECK(line_of(entry) == preview);
    }
    // c) The version change was a typo too: 1.5.1 should have been 1.5.0.
    {
        const QString change = entry_starting(QStringLiteral("13:15 "));
        QVariantMap edit;
        edit.insert(QStringLiteral("newValue"), QStringLiteral("1.5.0"));
        CHECK(rig.timeline().saveEntry(change, edit));
        rig.settle();
        CHECK(line_of(change) ==
              QStringLiteral("13:15 Software Version changed: v1.4.2 ") + ARROW + QStringLiteral(" v1.5.0"));
        CHECK(rig.session().version_text() == QStringLiteral("1.5.0"));
        CHECK(rig.timeline().header_text().contains(QStringLiteral("Software Version: v1.4.2")));
    }
    // d) The wrong tile: Track Delay at 13:41:30 was Speech Delay at 13:40:00.
    {
        const QString entry = entry_starting(QStringLiteral("13:41 "));
        QVariantMap edit;
        edit.insert(QStringLiteral("errorTypeId"), board_type("Speech Delay"));
        edit.insert(QStringLiteral("localSeconds"), 13 * 3600 + 40 * 60);
        CHECK(rig.timeline().saveEntry(entry, edit));
        rig.settle();
        CHECK(line_of(entry) == QStringLiteral("13:40 T| Speech Delay"));
        CHECK(rig.timeline().last_line() == QStringLiteral("13:40 T| Speech Delay"));
        CHECK(board_count("Track Delay") == 0);
        CHECK(board_count("Speech Delay") == 1);
    }
    // e) Delete the second Mute Delay, undo, delete again and let the window pass.
    {
        const QString first = entry_starting(QStringLiteral("10:20 "));
        const QString second = entry_starting(QStringLiteral("10:20 "), rig.timeline().rowOf(first) + 1);
        REQUIRE(first != second);
        CHECK(rig.timeline().deleteEntry(second));
        rig.settle();
        CHECK(rig.undo().text() == QStringLiteral("Entry deleted"));
        CHECK(board_count("Mute Delay") == 1);
        CHECK(rig.undo().undo());
        rig.settle();
        CHECK(board_count("Mute Delay") == 2);
        CHECK(rig.timeline().rowOf(second) >= 0);

        CHECK(rig.timeline().deleteEntry(second));
        rig.settle();
        CHECK(rig.undo().text() == QStringLiteral("Entry deleted"));
        rig.advance(5s);
        CHECK_FALSE(rig.undo().active());
        CHECK(board_count("Mute Delay") == 1);
        CHECK_FALSE(rig.undo().undo());

        const auto stored = rig.stored_open_log();
        REQUIRE(stored.has_value());
        const auto deleted = stored_entry(*stored, second);
        REQUIRE(deleted.has_value());
        CHECK(deleted->m_Deleted);
    }

    // The edits are marked as edited, and no review action fired a haptic.
    {
        const auto stored = rig.stored_open_log();
        REQUIRE(stored.has_value());
        int edited = 0;
        for (const Entry& entry : stored->m_Log.entries())
        {
            if (entry.m_EditedUtc.has_value())
            {
                ++edited;
            }
        }
        CHECK(edited >= 4);
    }
    CHECK(rig.platform().haptics().size() == m_HapticsBeforeReview);
}

// ------------------------------------------------------------------------------------------------------ 13

void Day::step_13_golden_check_1()
{
    Rig& rig = m_Rig;
    CHECK(rig.timeline().copyAll());
    CHECK(rig.timeline().entry_count() == 6);
    CHECK(rig.timeline().last_line() == QStringLiteral("13:40 T| Speech Delay"));

    const QStringList golden = golden_lines();
    REQUIRE(golden.size() == 12);
    CHECK(rig.timeline().header_text().split(QLatin1Char('\n')) == golden.mid(0, 5));
    golden_check(rig.clipboard().text());
}

// ------------------------------------------------------------------------------------------------------ 14

void Day::step_14_share_cancelled()
{
    Rig& rig = m_Rig;
    rig.go_to(7, 17, 30);
    rig.platform().answer_with(ej::bridge::ShareOutcome::CANCELLED);
    QSignalSpy finished(&rig.share(), &vm::ShareService::shareFinished);
    CHECK(rig.share().shareOpenLog());
    rig.settle();
    REQUIRE(finished.count() == 1);
    CHECK(finished.at(0).at(1).toString() == QStringLiteral("cancelled"));

    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    m_FirstLog = stored->m_Meta.m_Id;
    CHECK_FALSE(stored->m_Meta.m_LastSharedUtc.has_value());

    rig.day().check();
    CHECK_FALSE(rig.day().prompt_visible());
}

// ------------------------------------------------------------------------------------------------------ 15

void Day::step_15_new_day()
{
    Rig& rig = m_Rig;
    rig.go_to(8, 8, 5);
    rig.day().check();
    CHECK(rig.day().prompt_visible());
    CHECK(rig.day().new_date_text() == QStringLiteral("Sunday 8 November"));
    CHECK(rig.day().open_log_day_text() == QStringLiteral("Saturday"));
    CHECK(rig.day().open_log_entry_count() == 6);
    CHECK(rig.day().unshared());
}

// ------------------------------------------------------------------------------------------------------ 16

void Day::step_16_share_first()
{
    Rig& rig = m_Rig;
    rig.platform().answer_with(ej::bridge::ShareOutcome::SHARED);
    rig.platform().hold_completion(true);
    CHECK(rig.day().shareFirst());
    rig.settle();
    REQUIRE(rig.platform().sheets_presented() == 2);
    CHECK(rig.platform().sheet_open());
    golden_check(rig.platform().shared_texts().back());

    rig.advance(20s);
    CHECK(rig.platform().complete_share_sheet(ej::bridge::ShareOutcome::SHARED));
    rig.settle();
    rig.platform().hold_completion(false);

    // The share time is the time the sheet completed, committed after it.
    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    REQUIRE(stored->m_Meta.m_LastSharedUtc.has_value());
    CHECK(*stored->m_Meta.m_LastSharedUtc == melbourne(8, 8, 5, 20));
    const auto outcome = rig.events().first_index(EventKind::MARK, "share shared");
    REQUIRE(outcome.has_value());
    bool committed_after = false;
    const auto events = rig.events().events();
    for (std::size_t i = *outcome; i < events.size(); ++i)
    {
        if (events[i].m_Kind == EventKind::COMMIT && starts_with(events[i].m_Detail, "mark_shared"))
        {
            committed_after = true;
        }
    }
    CHECK(committed_after);

    // The prompt is still up; the log is no longer unshared.
    CHECK(rig.day().prompt_visible());
    CHECK_FALSE(rig.day().unshared());

    CHECK(rig.timeline().copyAll());
    // The clipboard holds the same bytes as the sheet was given.
    CHECK(rig.clipboard().text() == rig.platform().shared_texts().back());
}

// ------------------------------------------------------------------------------------------------------ 17

void Day::step_17_start_the_new_log()
{
    Rig& rig = m_Rig;
    rig.go_to(8, 8, 6);
    QSignalSpy started(&rig.session(), &vm::LogSession::newLogStarted);
    CHECK(rig.day().acceptNewLog());
    rig.settle();

    CHECK_FALSE(rig.day().prompt_visible());
    CHECK(started.count() == 1);

    const auto summaries = rig.stored_summaries();
    REQUIRE(summaries.size() == 2);
    int open = 0;
    for (const LogSummary& summary : summaries)
    {
        if (summary.m_Archived)
        {
            CHECK(summary.m_Id == m_FirstLog);
        }
        else
        {
            ++open;
            CHECK(summary.m_Header.m_Tester == "Sam Example");
            CHECK(summary.m_Header.m_LocalDate == std::chrono::year_month_day{std::chrono::year{2026} / 11 / 8});
        }
    }
    CHECK(open == 1);

    CHECK(rig.timeline().entry_count() == 0);
    CHECK(rig.timeline().rowCount() == 0);
    CHECK_FALSE(rig.undo().active());
    for (const int count : board_counts())
    {
        CHECK(count == 0);
    }
    CHECK(rig.session().tester() == QStringLiteral("Sam Example"));
    CHECK(rig.session().missing_fields().size() == 4);

    // Its export is the header and nothing else.
    CHECK(rig.timeline().copyAll());
    CHECK(rig.clipboard().text() ==
          "Tester: Sam Example\nDate: 8 Nov 2026\nUNIT #:\nLocation:\nSoftware Version:");
}

// ------------------------------------------------------------------------------------------------------ 18

void Day::step_18_history()
{
    Rig& rig = m_Rig;
    rig.go_to(8, 8, 10);
    rig.history().refresh();
    rig.settle();
    REQUIRE(rig.history().rowCount() == 1);
    const QAbstractItemModel& history = rig.history();
    CHECK(text_in(history, 0, "section") == QStringLiteral("7 Nov 2026"));
    CHECK(text_in(history, 0, "unitText") == QStringLiteral("UNIT-27"));
    CHECK(text_in(history, 0, "locationText") == QStringLiteral("North Site"));
    CHECK(number_in(history, 0, "entryCount") == 6);
    CHECK(history.data(history.index(0, 0), role_of(history, "shared")).toBool());
    CHECK(text_in(history, 0, "sharedAtText") == QStringLiteral("08:05"));

    const QString log_id = text_in(history, 0, "logId");
    rig.history().openLog(log_id);
    rig.settle();
    REQUIRE(rig.history().review_model() != nullptr);
    const QAbstractItemModel& review = *rig.history().review_model();
    QStringList lines;
    for (int row = 0; row < review.rowCount(); ++row)
    {
        lines.push_back(text_in(review, row, "fullLine"));
    }
    // The review screen shows the export lines, except that a note has no "(Free Text)" marker on the screen.
    QStringList expected = golden_lines().mid(6, 6);
    expected.replaceInStrings(QStringLiteral(" (Free Text)"), QString());
    CHECK(lines == expected);

    rig.platform().answer_with(ej::bridge::ShareOutcome::SHARED);
    CHECK(rig.share().shareLog(log_id));
    rig.settle();
    golden_check(rig.platform().shared_texts().back());

    const auto stored = rig.stored_log(m_FirstLog);
    REQUIRE(stored.m_Meta.m_LastSharedUtc.has_value());
    CHECK(*stored.m_Meta.m_LastSharedUtc == melbourne(8, 8, 10));
    CHECK(text_in(history, 0, "sharedAtText") == QStringLiteral("08:10"));

    rig.history().closeLog();
    rig.settle();
}

// ------------------------------------------------------------------------------------------------------ 19

void Day::step_19_closing_checks()
{
    Rig& rig = m_Rig;
    rig.settle();

    const std::vector<Event> events = rig.events().events();
    INFO(rig.events().format());

    // Which entries are errors and which are notes, from the store.
    std::set<std::string> error_ids;
    std::set<std::string> note_ids;
    for (const LogSummary& summary : rig.stored_summaries())
    {
        for (const Entry& entry : rig.stored_log(summary.m_Id).m_Log.entries())
        {
            if (std::holds_alternative<ErrorEntry>(entry.m_Body))
            {
                error_ids.insert(entry.m_Id.format());
            }
            else if (std::holds_alternative<NoteEntry>(entry.m_Body))
            {
                note_ids.insert(entry.m_Id.format());
            }
        }
    }

    std::size_t impacts = 0;
    std::size_t error_commits = 0;
    std::size_t note_commits = 0;
    std::size_t new_toasts = 0;
    std::string last_toast;
    std::vector<std::string> ops;
    std::optional<std::string> last_commit;
    for (const Event& event : events)
    {
        switch (event.m_Kind)
        {
        case EventKind::HAPTIC:
            if (event.m_Detail == "impact")
            {
                ++impacts;
            }
            else
            {
                // A success haptic comes directly after the commit of its note.
                REQUIRE(last_commit.has_value());
                REQUIRE(starts_with(*last_commit, "add_entry "));
                CHECK(note_ids.count(last_commit->substr(std::string("add_entry ").size())) == 1);
            }
            break;
        case EventKind::COMMIT:
        {
            last_commit = event.m_Detail;
            const std::string op = op_of(event.m_Detail);
            if (op != "set_draft")
            {
                ops.push_back(op);
            }
            if (op == "add_entry")
            {
                const std::string id = event.m_Detail.substr(op.size() + 1);
                if (error_ids.count(id) == 1)
                {
                    // The impact haptic of its touch came before this commit.
                    CHECK(impacts > error_commits);
                    ++error_commits;
                }
                else if (note_ids.count(id) == 1)
                {
                    ++note_commits;
                }
            }
            break;
        }
        case EventKind::TOAST:
            if (event.m_Detail.find(" logged ") != std::string::npos)
            {
                // The confirmation of a tap is shown only after its commit: a toast that is new (not a refresh of the
                // one showing) needs a commit of its own before it.
                if (event.m_Detail != last_toast)
                {
                    ++new_toasts;
                    CHECK(new_toasts <= error_commits);
                }
            }
            else if (event.m_Detail.find("Note added") != std::string::npos)
            {
                CHECK(note_commits >= 1);
            }
            last_toast = event.m_Detail;
            break;
        default:
            break;
        }
    }
    CHECK(error_commits == impacts);
    CHECK(error_commits == 7);
    CHECK(note_commits == 1);
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::IMPACT) == impacts);
    // The only other haptic of the day is the success haptic of the one note: none for an undo, a review edit, a
    // delete, a header correction or a share.
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::SUCCESS) == 1);
    CHECK(rig.platform().haptics().size() == impacts + 1);

    // Every commit has the expected kind, in order. (Draft writes depend on timing and are left out.)
    const std::vector<std::string> expected = {
        "create_log",        "add_entry",         "set_entry_deleted", // 2: tap, undo
        "correct_header",    "correct_header",    "correct_header",
        "correct_header",                         // 3: unit twice, location, version
        "add_entry",                              // 4
        "add_entry",         "add_entry",         // 5
        "add_entry",                              // 6: the note
        "add_entry",         "set_entry_deleted", // 7: Wrong Greeting and its undo
        "add_entry",                              // 8
        "add_entry",                              // 10: the version change
        "add_entry",                              // 11
        "edit_entry",        "edit_entry",        "edit_entry",
        "edit_entry",                                                  // 12 a to d
        "set_entry_deleted", "set_entry_deleted", "set_entry_deleted", // 12 e: delete, restore, delete
        "mark_shared",                                                 // 16
        "start_new_log",                                               // 17
        "mark_shared"};                                                // 18
    CHECK(ops == expected);

    CHECK(rig.idle());
    CHECK(rig.write_failures() == 0);
    CHECK(m_Goldens == 3);

    if (rig.store_kind() == RigStore::SQLITE)
    {
        // The store is used on its own thread and never on the GUI thread.
        const auto threads = rig.repository_threads();
        CHECK_FALSE(threads.empty());
        CHECK(threads.count(std::this_thread::get_id()) == 0);
    }
}

} // namespace

void full_day(Rig& rig)
{
    Day day(rig);
    day.step_1_start();
    day.step_2_tap_before_header_then_undo();
    day.step_3_header();
    day.step_4_single_tap();
    day.step_5_double_tap();
    day.step_6_draft_and_note();
    day.step_7_other_sheet();
    day.step_8_tap();
    day.step_9_restart();
    day.step_10_version_change();
    day.step_11_wrong_tile();
    day.step_12_review_edits();
    day.step_13_golden_check_1();
    day.step_14_share_cancelled();
    day.step_15_new_day();
    day.step_16_share_first();
    day.step_17_start_the_new_log();
    day.step_18_history();
    day.step_19_closing_checks();
}

} // namespace ej::test
