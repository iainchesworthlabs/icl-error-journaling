// SPDX-License-Identifier: GPL-3.0-or-later
#include "fake_time_zone_provider.hpp"
#include "golden.hpp"
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/recording_repository.hpp"
#include "support/sequential_ids.hpp"
#include "support/test_clock.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/timeline_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::TimelineModel;
using Origin = LogChange::Origin;
using namespace std::chrono_literals;

namespace
{

constexpr auto MELBOURNE = "Australia/Melbourne";

// ----------------------------------------------------------------------------------------------- the zone with DST

/// Melbourne with its two changes of 2026, for the tests of the skipped and the repeated hour: daylight time (+11:00)
/// until 2026-04-04 16:00 UTC, standard time (+10:00) until 2026-10-03 16:00 UTC, daylight time again after that. So
/// 2026-10-04 has 23 hours (02:00 to 03:00 does not exist) and 2026-04-05 has 25 (02:00 to 03:00 happens twice). A
/// local time in the repeated hour resolves to the earlier instant, one in the gap to the instant after the gap, as the
/// provider contract of Stage 1 says and the Qt provider does.
class MelbourneDstZones final : public TimeZoneProvider
{
public:
    [[nodiscard]] Result<LocalTime, Error> to_local(const Timestamp& timestamp) const override
    {
        if (timestamp.m_Zone != MELBOURNE)
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const auto local = std::chrono::floor<std::chrono::seconds>(timestamp.m_Utc + offset_at(timestamp.m_Utc));
        const auto days = std::chrono::floor<std::chrono::days>(local);
        return LocalTime{std::chrono::year_month_day{days}, local - days};
    }

    [[nodiscard]] Result<UtcMillis, Error> to_utc(const LocalTime& local, std::string_view zone) const override
    {
        if (zone != MELBOURNE)
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const std::chrono::sys_days days{local.m_Date};
        std::optional<UtcMillis> earliest;
        for (const auto offset : {std::chrono::minutes{660}, std::chrono::minutes{600}})
        {
            const UtcMillis candidate{
                std::chrono::floor<std::chrono::milliseconds>(days + local.m_SinceMidnight - offset)};
            if (offset_at(candidate) == offset && (!earliest || candidate < *earliest))
            {
                earliest = candidate;
            }
        }
        if (earliest)
        {
            return *earliest;
        }
        // In the gap: the offset in force before the change, which is the instant after the gap.
        return UtcMillis{std::chrono::floor<std::chrono::milliseconds>(days + local.m_SinceMidnight - 600min)};
    }

private:
    static std::chrono::minutes offset_at(UtcMillis instant)
    {
        if (instant < utc(2026, 4, 4, 16, 0))
        {
            return 660min;
        }
        return instant < utc(2026, 10, 3, 16, 0) ? 600min : 660min;
    }
};

// ------------------------------------------------------------------------------------------------------------ the rig

/// The model under test, the store behind it and the fakes it reads: the clock, the configuration, the clipboard. A
/// log is made in the in-memory store and shown through a `LogDocument` over the manual gateway, so a test can look at
/// the state between "submitted" and "committed".
class Rig
{
public:
    explicit Rig(const TimeZoneProvider* zones = nullptr, Configuration configuration = sample_configuration())
        : m_Zones(zones != nullptr ? zones : &m_Fixed)
    {
        m_Config.set(std::move(configuration));
        m_Model = std::make_unique<TimelineModel>(environment(), m_Config, m_Clipboard);
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    [[nodiscard]] viewmodel::Environment environment()
    {
        return viewmodel::Environment{m_Clock, m_Clock, m_Ids, m_Device, *m_Zones};
    }

    /// A log in the store with `entries`, loaded into a document (not shown yet).
    [[nodiscard]] std::shared_ptr<LogDocument> make(std::uint64_t number, HeaderValues header,
                                                    const std::vector<Entry>& entries = {})
    {
        const LogId id = sample_id<LogId>(number);
        REQUIRE(m_Storage.memory().create_log(id, header));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        return std::make_shared<LogDocument>(environment(), m_Gateway, std::move(*loaded));
    }

    /// A past log: made, filled, archived in the store, and loaded as an archived document. Make it before the open
    /// log, because the store holds one open log at a time.
    [[nodiscard]] std::shared_ptr<LogDocument> make_archived(std::uint64_t number, HeaderValues header,
                                                             const std::vector<Entry>& entries)
    {
        const LogId id = sample_id<LogId>(number);
        REQUIRE(m_Storage.memory().create_log(id, header));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        REQUIRE(m_Storage.memory().archive_log(id));
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        auto document = std::make_shared<LogDocument>(environment(), m_Gateway, std::move(*loaded));
        document->set_archived();
        return document;
    }

    /// A new day: the store archives log `old_number` and opens log `number`, which is loaded.
    [[nodiscard]] std::shared_ptr<LogDocument> make_next(std::uint64_t old_number, std::uint64_t number,
                                                         HeaderValues header)
    {
        const LogId id = sample_id<LogId>(number);
        REQUIRE(m_Storage.memory().start_new_log(sample_id<LogId>(old_number), id, header));
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        return std::make_shared<LogDocument>(environment(), m_Gateway, std::move(*loaded));
    }

    /// `make` and show it in the model.
    std::shared_ptr<LogDocument> start(const std::vector<Entry>& entries = {}, HeaderValues header = sample_header())
    {
        m_Document = make(1, std::move(header), entries);
        m_Model->bind(m_Document);
        return m_Document;
    }

    void commit()
    {
        m_Gateway.run_all();
    }

    /// The text of the export as the renderer makes it now, the one `copyAll` must put on the clipboard.
    [[nodiscard]] std::string export_now(const Log& log)
    {
        const ExportContext context = viewmodel::ExportContextBuilder(m_Config, *m_Zones).build();
        const auto text = viewmodel::export_text(log, context);
        REQUIRE(text.has_value());
        return *text;
    }

    [[nodiscard]] TimelineModel& model()
    {
        return *m_Model;
    }
    [[nodiscard]] TestClock& clock()
    {
        return m_Clock;
    }
    [[nodiscard]] FakeConfigurationSource& config()
    {
        return m_Config;
    }
    [[nodiscard]] FakeClipboard& clipboard()
    {
        return m_Clipboard;
    }
    [[nodiscard]] MemoryStorage& storage()
    {
        return m_Storage;
    }
    [[nodiscard]] ManualStorageGateway& gateway()
    {
        return m_Gateway;
    }
    [[nodiscard]] LogDocument& document()
    {
        return *m_Document;
    }

private:
    TestClock m_Clock;
    SequentialIds m_Ids;
    FakeDeviceZone m_Device;
    FakeTimeZoneProvider m_Fixed = make_fixed_zones();
    const TimeZoneProvider* m_Zones;
    EventLog m_Events;
    MemoryStorage m_Storage{m_Clock, &m_Events};
    ManualStorageGateway m_Gateway{m_Storage.context()};
    FakeConfigurationSource m_Config;
    FakeClipboard m_Clipboard{&m_Events};
    std::shared_ptr<LogDocument> m_Document;
    std::unique_ptr<TimelineModel> m_Model; // last, so it goes first: it listens to the document and the configuration
};

// ----------------------------------------------------------------------------------------------------------- helpers

QString text_of(EntryId id)
{
    return QString::fromStdString(id.format());
}

QString text_of(std::uint64_t entry_number)
{
    return text_of(sample_id<EntryId>(entry_number));
}

QString row_text(const TimelineModel& model, int row, int role)
{
    return model.data(model.index(row), role).toString();
}

QString full_line(const TimelineModel& model, int row)
{
    return row_text(model, row, TimelineModel::FULL_LINE);
}

std::vector<QString> full_lines(const TimelineModel& model)
{
    std::vector<QString> lines;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        lines.push_back(full_line(model, row));
    }
    return lines;
}

/// The timeline lines of an export: what follows the header block and the blank line, up to the next blank line.
std::vector<QString> timeline_of(const std::string& export_text)
{
    std::vector<QString> lines;
    const auto start = export_text.find("\n\n");
    if (start == std::string::npos)
    {
        return lines;
    }
    std::size_t at = start + 2;
    while (at <= export_text.size())
    {
        const auto end = export_text.find('\n', at);
        const std::string line = export_text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (line.empty())
        {
            break;
        }
        lines.push_back(QString::fromStdString(line));
        if (end == std::string::npos)
        {
            break;
        }
        at = end + 1;
    }
    return lines;
}

ErrorTypeId type_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_ErrorTypes, name, &ErrorType::m_Name);
    REQUIRE(it != configuration.m_ErrorTypes.end());
    return it->m_Id;
}

/// 7 Nov 2026 in Melbourne at `hh:mm`; the numbers are the entry's.
Entry error_at(std::uint64_t number, int hour, int minute, std::string label = "Wrong Response",
               std::optional<std::string> note = std::nullopt)
{
    Entry entry = sample_error(number, 0, std::move(label), std::move(note));
    entry.m_Captured = on_day(hour, minute);
    return entry;
}

Entry note_at(std::uint64_t number, int hour, int minute, std::string text)
{
    Entry entry = sample_note(number, 0, std::move(text));
    entry.m_Captured = on_day(hour, minute);
    return entry;
}

Entry change_at(std::uint64_t number, int hour, int minute, HeaderField field, std::string value)
{
    Entry entry = sample_change(number, 0, field, std::move(value));
    entry.m_Captured = on_day(hour, minute);
    return entry;
}

int seconds_of(int hour, int minute, int second = 0)
{
    return hour * 3600 + minute * 60 + second;
}

QVariantMap time_edit(int seconds)
{
    return QVariantMap{{"localSeconds", seconds}};
}

std::int64_t millis_of(UtcMillis instant)
{
    return instant.time_since_epoch().count();
}

/// Lets a model go of the document it follows at the end of a test, whatever happens before.
struct Unbinder
{
    TimelineModel& m_Model;
    ~Unbinder()
    {
        m_Model.unbind();
    }
};

/// A configuration that gives the names of a golden scenario, whose ids are the scenario's own.
Configuration configuration_for(const ExportContext& context)
{
    Configuration configuration;
    configuration.m_Facilitator = "Sam Example";
    configuration.m_UnitPrefix = std::string(context.m_UnitPrefix);
    configuration.m_TimeFormat = context.m_TimeFormat;
    configuration.m_IncludeTotals = context.m_IncludeTotals;
    for (std::uint64_t number = ERROR_WRONG_RESPONSE; number <= ERROR_ECHO_DELAY_RETIRED; ++number)
    {
        ErrorType type;
        type.m_Id = test_id<ErrorTypeId>(number);
        type.m_Name = std::string(context.m_ErrorTypeName(type.m_Id));
        type.m_Retired = number == ERROR_ECHO_DELAY_RETIRED;
        configuration.m_ErrorTypes.push_back(type);
    }
    for (const std::uint64_t number : {LOCATION_NORTH_SITE, LOCATION_SOUTH_SITE})
    {
        Location location;
        location.m_Id = test_id<LocationId>(number);
        location.m_Name = std::string(context.m_LocationName(location.m_Id));
        configuration.m_Locations.push_back(location);
    }
    for (const std::uint64_t number : {TEMPLATE_PLAYED, TEMPLATE_VISITOR, TEMPLATE_REQUESTED})
    {
        configuration.m_Templates.push_back(*context.m_Template(test_id<NoteTemplateId>(number)));
    }
    return configuration;
}

} // namespace

// -------------------------------------------------------------------------------------------------------- the rows

TEST_CASE("Rows follow time order, and entries at the same time keep insertion order", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 30, "Third"), error_at(2, 11, 10, "First"), error_at(3, 11, 20, "Second"),
               note_at(4, 11, 20, "Same minute"), error_at(5, 11, 20, "Last of the tie")});

    REQUIRE(rig.model().rowCount() == 5);
    CHECK(row_text(rig.model(), 0, TimelineModel::ENTRY_ID) == text_of(2));
    CHECK(row_text(rig.model(), 1, TimelineModel::ENTRY_ID) == text_of(3));
    CHECK(row_text(rig.model(), 2, TimelineModel::ENTRY_ID) == text_of(4));
    CHECK(row_text(rig.model(), 3, TimelineModel::ENTRY_ID) == text_of(5));
    CHECK(row_text(rig.model(), 4, TimelineModel::ENTRY_ID) == text_of(1));
    CHECK(full_line(rig.model(), 0) == "11:10 T| First");
    CHECK(full_line(rig.model(), 2) == "11:20 (Free Text) Same minute");
    CHECK(row_text(rig.model(), 0, TimelineModel::KIND) == "error");
    CHECK(row_text(rig.model(), 2, TimelineModel::KIND) == "note");
    CHECK(rig.model().rowOf(text_of(5)) == 3);
    CHECK(rig.model().rowOf(text_of(99)) == -1);
    CHECK(rig.model().rowOf("not an id") == -1);
}

TEST_CASE("The roles have the names QML binds to", "[viewmodel][timeline]")
{
    Rig rig;
    const auto names = rig.model().roleNames();
    CHECK(names.value(TimelineModel::ENTRY_ID) == "entryId");
    CHECK(names.value(TimelineModel::TIME_TEXT) == "timeText");
    CHECK(names.value(TimelineModel::LINE_TEXT) == "lineText");
    CHECK(names.value(TimelineModel::FULL_LINE) == "fullLine");
    CHECK(names.value(TimelineModel::KIND) == "kind");
    CHECK(names.value(TimelineModel::PREFIX_TEXT) == "prefixText");
    CHECK(names.value(TimelineModel::REST_TEXT) == "restText");
    CHECK(names.size() == 7);
}

TEST_CASE("An entry edited to an earlier time moves with one rowsMoved", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B"), error_at(3, 11, 30, "C")});
    QSignalSpy moved(&rig.model(), &QAbstractItemModel::rowsMoved);
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&rig.model(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);
    QSignalSpy changed(&rig.model(), &QAbstractItemModel::dataChanged);

    SECTION("an earlier time moves the row up")
    {
        REQUIRE(rig.model().saveEntry(text_of(3), time_edit(seconds_of(11, 5))));
        CHECK(moved.count() == 0); // nothing changes before the commit
        rig.commit();

        CHECK(moved.count() == 1);
        CHECK(changed.count() == 1);
        CHECK(inserted.count() == 0);
        CHECK(removed.count() == 0);
        CHECK(reset.count() == 0);
        CHECK(full_lines(rig.model()) == std::vector<QString>{"11:05 T| C", "11:10 T| A", "11:20 T| B"});
        CHECK(rig.model().rowOf(text_of(3)) == 0);
    }

    SECTION("a later time moves the row down")
    {
        REQUIRE(rig.model().saveEntry(text_of(1), time_edit(seconds_of(11, 25))));
        rig.commit();

        CHECK(moved.count() == 1);
        CHECK(full_lines(rig.model()) == std::vector<QString>{"11:20 T| B", "11:25 T| A", "11:30 T| C"});
        CHECK(rig.model().rowOf(text_of(1)) == 1);
    }

    SECTION("a time that keeps the place moves nothing")
    {
        REQUIRE(rig.model().saveEntry(text_of(2), time_edit(seconds_of(11, 22))));
        rig.commit();

        CHECK(moved.count() == 0);
        CHECK(changed.count() == 1);
        CHECK(full_line(rig.model(), 1) == "11:22 T| B");
    }
}

TEST_CASE("Deleted entries are absent, and come back on restore at their place", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B"), error_at(3, 11, 30, "C")});
    QSignalSpy removed(&rig.model(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);

    REQUIRE(rig.model().deleteEntry(text_of(2)));
    CHECK(rig.model().rowCount() == 3); // the row leaves after the commit
    CHECK(removed.count() == 0);
    rig.commit();

    CHECK(removed.count() == 1);
    CHECK(full_lines(rig.model()) == std::vector<QString>{"11:10 T| A", "11:30 T| C"});
    CHECK(rig.model().entry_count() == 2);
    CHECK(rig.model().rowOf(text_of(2)) == -1);
    CHECK(rig.model().entryForEdit(text_of(2)).value("kind") == "error"); // soft: the entry is still in the log

    SECTION("restore puts the row back at its place")
    {
        REQUIRE(rig.document().set_deleted(sample_id<EntryId>(2), false, Origin::UNDO, {}));
        rig.commit();

        CHECK(inserted.count() == 1);
        CHECK(full_lines(rig.model()) == std::vector<QString>{"11:10 T| A", "11:20 T| B", "11:30 T| C"});
        CHECK(rig.model().rowOf(text_of(2)) == 1);
    }

    SECTION("a deleted entry cannot be deleted again, and an unknown one cannot be deleted")
    {
        CHECK_FALSE(rig.model().deleteEntry(text_of(2)));
        CHECK_FALSE(rig.model().deleteEntry(text_of(99)));
        CHECK_FALSE(rig.model().deleteEntry("not an id"));
        CHECK(rig.gateway().pending() == 0);
    }
}

TEST_CASE("Deleting while the queue is full is refused", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A")});
    QSignalSpy failed(&rig.model(), &TimelineModel::writeFailed);

    // One command in flight and `MAX_WAITING` behind it.
    for (std::uint64_t i = 0; i < LogDocument::MAX_WAITING + 1; ++i)
    {
        REQUIRE(rig.document().add_entry(error_at(100 + i, 12, 0), Origin::USER, {}));
    }

    CHECK_FALSE(rig.model().deleteEntry(text_of(1)));
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == "deleteEntry");
    CHECK_FALSE(failed.at(0).at(1).toString().isEmpty());

    rig.commit();
    CHECK(rig.model().rowCount() == 1 + static_cast<int>(LogDocument::MAX_WAITING) + 1);
    CHECK(rig.model().deleteEntry(text_of(1))); // the queue has drained
}

// ------------------------------------------------------------------------------------------------ the wording

TEST_CASE("Every row is its time and its line, in every time format", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 9, 5, "Wrong Response"), error_at(2, 13, 20, "Mute Delay", "after the break"),
               note_at(3, 13, 25, "  Robot\n restarted "), change_at(4, 14, 5, HeaderField::SOFTWARE_VERSION, "1.5.0"),
               error_at(5, 23, 59)});
    const TimeFormat formats[] = {
        {TimeStyle::H24, false, MeridiemCase::UPPER}, {TimeStyle::H24, true, MeridiemCase::UPPER},
        {TimeStyle::H12, false, MeridiemCase::UPPER}, {TimeStyle::H12, false, MeridiemCase::LOWER},
        {TimeStyle::H12, true, MeridiemCase::UPPER},  {TimeStyle::H12, true, MeridiemCase::LOWER}};

    for (const TimeFormat& format : formats)
    {
        Configuration configuration = sample_configuration();
        configuration.m_TimeFormat = format;
        rig.config().set(configuration);

        const std::vector<QString> expected = timeline_of(rig.export_now(rig.document().log()));
        REQUIRE(expected.size() == 5);
        REQUIRE(static_cast<std::size_t>(rig.model().rowCount()) == expected.size());
        for (int row = 0; row < rig.model().rowCount(); ++row)
        {
            const QString time = row_text(rig.model(), row, TimelineModel::TIME_TEXT);
            const QString line = row_text(rig.model(), row, TimelineModel::LINE_TEXT);
            CHECK_FALSE(time.isEmpty());
            CHECK(time + " " + line == full_line(rig.model(), row));
            CHECK(full_line(rig.model(), row) == expected[static_cast<std::size_t>(row)]);
        }
    }
    CHECK(full_line(rig.model(), 0) == "09:05:00 am T| Wrong Response");
}

TEST_CASE("Every entry of every golden scenario reads as the export does", "[viewmodel][timeline][golden]")
{
    for (const auto name : golden_names())
    {
        DYNAMIC_SECTION(name)
        {
            const Scenario scenario = make_scenario(name);
            Rig rig(scenario.m_Context.m_Zones, configuration_for(scenario.m_Context));
            const LogId id = sample_id<LogId>(7);
            auto document = std::make_shared<LogDocument>(
                rig.environment(), rig.gateway(),
                StoredLog{LogMeta{id, UtcMillis{}, std::nullopt, false, std::nullopt}, scenario.m_Log});
            rig.model().bind(document);

            const auto reference = render_export(scenario.m_Log, scenario.m_Context);
            REQUIRE(reference.has_value());
            const std::vector<QString> expected = timeline_of(*reference);

            REQUIRE(static_cast<std::size_t>(rig.model().rowCount()) == expected.size());
            for (int row = 0; row < rig.model().rowCount(); ++row)
            {
                CHECK(full_line(rig.model(), row) == expected[static_cast<std::size_t>(row)]);
                CHECK(row_text(rig.model(), row, TimelineModel::TIME_TEXT) + " " +
                          row_text(rig.model(), row, TimelineModel::LINE_TEXT) ==
                      full_line(rig.model(), row));
            }
            CHECK(rig.model().header_text().toStdString() ==
                  reference->substr(0, reference->find("\n\n") == std::string::npos ? reference->size()
                                                                                    : reference->find("\n\n")));

            REQUIRE(rig.model().copyAll());
            CHECK(rig.clipboard().text() == *reference);
            check_golden(name, rig.clipboard().text());
        }
    }
}

// ------------------------------------------------------------------------------------------------- the summary

TEST_CASE("The summary: count, last line, header text and title", "[viewmodel][timeline]")
{
    Rig rig;

    SECTION("an empty log")
    {
        rig.start();
        CHECK(rig.model().ready());
        CHECK(rig.model().entry_count() == 0);
        CHECK(rig.model().last_line().isEmpty());
        CHECK(rig.model().title_text() == "Sat 7 Nov \xC2\xB7 0 entries");
        CHECK(rig.model().header_text() ==
              "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\nLocation: North Site\n"
              "Software Version: v1.4.2");
    }

    SECTION("the last line follows the latest entry, and returns to the previous one after an undo")
    {
        rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B")});
        QSignalSpy summary(&rig.model(), &TimelineModel::summaryChanged);
        CHECK(rig.model().entry_count() == 2);
        CHECK(rig.model().last_line() == "11:20 T| B");
        CHECK(rig.model().title_text() == "Sat 7 Nov \xC2\xB7 2 entries");

        REQUIRE(rig.document().add_entry(error_at(3, 11, 30, "C"), Origin::USER, {}));
        rig.commit();
        CHECK(rig.model().last_line() == "11:30 T| C");
        CHECK(rig.model().entry_count() == 3);
        CHECK(summary.count() == 1);

        REQUIRE(rig.document().set_deleted(sample_id<EntryId>(3), true, Origin::UNDO, {}));
        rig.commit();
        CHECK(rig.model().last_line() == "11:20 T| B");
        CHECK(rig.model().entry_count() == 2);
        CHECK(summary.count() == 2);
    }

    SECTION("one entry is '1 entry'")
    {
        rig.start({error_at(1, 11, 10)});
        CHECK(rig.model().title_text() == "Sat 7 Nov \xC2\xB7 1 entry");
    }

    SECTION("the start values stay when a change is made, and the change is a row")
    {
        rig.start({error_at(1, 11, 10, "A")});
        const QString before = rig.model().header_text();
        REQUIRE(rig.document().change_header(HeaderField::UNIT_NUMBER, "28", on_day(12, 0), sample_id<EntryId>(2), {}));
        rig.commit();

        CHECK(rig.model().header_text() == before);
        CHECK(rig.model().header_text().contains("UNIT-27"));
        CHECK(rig.model().entry_count() == 2);
        CHECK(rig.model().last_line() == "12:00 UNIT # changed: UNIT-27 \xE2\x86\x92 UNIT-28");
        CHECK(row_text(rig.model(), 1, TimelineModel::KIND) == "header_change");
    }
}

TEST_CASE("A model with nothing bound is empty and not ready", "[viewmodel][timeline]")
{
    Rig rig;
    CHECK_FALSE(rig.model().ready());
    CHECK(rig.model().rowCount() == 0);
    CHECK(rig.model().header_text().isEmpty());
    CHECK(rig.model().title_text().isEmpty());
    CHECK_FALSE(rig.model().copyAll());
    CHECK_FALSE(rig.model().copyLine(0));
    CHECK_FALSE(rig.model().deleteEntry(text_of(1)));
    CHECK_FALSE(rig.model().saveEntry(text_of(1), {}));
    CHECK(rig.model().entryForEdit(text_of(1)).isEmpty());
    CHECK(rig.clipboard().copies() == 0);
}

// ---------------------------------------------------------------------------------------------------------- edit

TEST_CASE("Edit: the error type, the attached note and the note text each re-render", "[viewmodel][timeline]")
{
    Rig rig;
    const Configuration configuration = sample_configuration();
    rig.start({error_at(1, 11, 10, "Wrong Response"), error_at(2, 11, 20, "Mute Delay", "first note"),
               note_at(3, 11, 30, "Robot restarted")});

    SECTION("the error type takes a new name snapshot")
    {
        const ErrorTypeId type = type_named(configuration, "Sequence Out of Order");
        REQUIRE(rig.model().saveEntry(text_of(1), QVariantMap{{"errorTypeId", QString::fromStdString(type.format())}}));
        rig.commit();

        CHECK(full_line(rig.model(), 0) == "11:10 T| Sequence Out of Order");
        const auto* stored = rig.document().log().find(sample_id<EntryId>(1));
        REQUIRE(stored != nullptr);
        CHECK(std::get<ErrorEntry>(stored->m_Body).m_ErrorType == type);
        CHECK(std::get<ErrorEntry>(stored->m_Body).m_ErrorTypeLabel == "Sequence Out of Order");
    }

    SECTION("the same type keeps the snapshot it has, even when the type was renamed since")
    {
        // An entry of a type the configuration has, tapped as "Wrong Response" before the type was renamed.
        Entry tapped = error_at(4, 11, 40, "Wrong Response");
        std::get<ErrorEntry>(tapped.m_Body).m_ErrorType = type_named(configuration, "Wrong Response");
        REQUIRE(rig.document().add_entry(tapped, Origin::USER, {}));
        rig.commit();
        Configuration renamed = configuration;
        for (ErrorType& type : renamed.m_ErrorTypes)
        {
            if (type.m_Name == "Wrong Response")
            {
                type.m_Name = "Wrong Answer";
            }
        }
        rig.config().set(renamed);

        // The sheet sends the type back as it was: that is not an edit, and the snapshot stays.
        const QVariantMap sheet = rig.model().entryForEdit(text_of(4));
        CHECK(sheet.value("errorTypeName") == "Wrong Response");
        const std::size_t posted = rig.gateway().posted();
        CHECK(rig.model().saveEntry(text_of(4), sheet));
        CHECK(rig.gateway().posted() == posted);
        CHECK(full_line(rig.model(), 3) == "11:40 T| Wrong Response");

        // The sheet of an entry whose type is not in the configuration at all round-trips too.
        CHECK(rig.model().saveEntry(text_of(1), rig.model().entryForEdit(text_of(1))));
        CHECK(rig.gateway().posted() == posted);

        // Another type, and back: the snapshot is the new name, and then the old type's current name.
        const QString other = QString::fromStdString(type_named(configuration, "Mute Delay").format());
        REQUIRE(rig.model().saveEntry(text_of(4), QVariantMap{{"errorTypeId", other}}));
        rig.commit();
        CHECK(full_line(rig.model(), 3) == "11:40 T| Mute Delay");
        const QString original = QString::fromStdString(type_named(configuration, "Wrong Response").format());
        REQUIRE(rig.model().saveEntry(text_of(4), QVariantMap{{"errorTypeId", original}}));
        rig.commit();
        CHECK(full_line(rig.model(), 3) == "11:40 T| Wrong Answer");
    }

    SECTION("the attached note is set, changed and cleared")
    {
        REQUIRE(rig.model().saveEntry(text_of(1), QVariantMap{{"attachedNote", "asked for Evening Song"}}));
        rig.commit();
        CHECK(full_line(rig.model(), 0) == "11:10 T| Wrong Response \xE2\x80\x93 asked for Evening Song");

        REQUIRE(rig.model().saveEntry(text_of(2), QVariantMap{{"attachedNote", "second note"}}));
        rig.commit();
        CHECK(full_line(rig.model(), 1) == "11:20 T| Mute Delay \xE2\x80\x93 second note");

        REQUIRE(rig.model().saveEntry(text_of(2), QVariantMap{{"attachedNote", ""}}));
        rig.commit();
        CHECK(full_line(rig.model(), 1) == "11:20 T| Mute Delay");
        CHECK_FALSE(
            std::get<ErrorEntry>(rig.document().log().find(sample_id<EntryId>(2))->m_Body).m_AttachedNote.has_value());
    }

    SECTION("the note text, and a template for it")
    {
        REQUIRE(rig.model().saveEntry(text_of(3), QVariantMap{{"noteText", "Robot rebooted"}}));
        rig.commit();
        CHECK(full_line(rig.model(), 2) == "11:30 (Free Text) Robot rebooted");

        const QString played = QString::fromStdString(configuration.m_Templates[0].m_Id.format());
        REQUIRE(rig.model().saveEntry(text_of(3), QVariantMap{{"noteText", "Morning Medley"}, {"templateId", played}}));
        rig.commit();
        CHECK(full_line(rig.model(), 2) == "11:30 (Free Text) Played Morning Medley");
        CHECK(rig.model().entryForEdit(text_of(3)).value("templateId") == played);
        CHECK(rig.model().entryForEdit(text_of(3)).value("noteText") == "Morning Medley");
    }

    SECTION("an edit keeps the id and the deleted flag, and takes its edit time from the clock")
    {
        rig.clock().advance(90s);
        const UtcMillis now = rig.clock().now();
        REQUIRE(rig.model().deleteEntry(text_of(3)));
        rig.commit();
        REQUIRE(rig.model().saveEntry(text_of(3), QVariantMap{{"noteText", "Edited while deleted"}}));
        rig.commit();

        const Entry* stored = rig.document().log().find(sample_id<EntryId>(3));
        REQUIRE(stored != nullptr);
        CHECK(stored->m_Deleted);
        CHECK(stored->m_EditedUtc == now);
        CHECK(rig.model().rowCount() == 2);
    }
}

TEST_CASE("Edit: what the sheet shows", "[viewmodel][timeline]")
{
    Rig rig;
    const Configuration configuration = sample_configuration();
    const ErrorTypeId type = type_named(configuration, "Mute Delay");
    Entry error = error_at(1, 11, 10, "Mute Delay", "note one");
    std::get<ErrorEntry>(error.m_Body).m_ErrorType = type;
    rig.start(
        {error, note_at(2, 14, 5, "Robot restarted"), change_at(3, 15, 0, HeaderField::SOFTWARE_VERSION, "1.5.0")});

    const QVariantMap e = rig.model().entryForEdit(text_of(1));
    CHECK(e.value("entryId") == text_of(1));
    CHECK(e.value("kind") == "error");
    CHECK(e.value("errorTypeId") == QString::fromStdString(type.format()));
    CHECK(e.value("errorTypeName") == "Mute Delay");
    CHECK(e.value("attachedNote") == "note one");
    CHECK(e.value("localDate") == "2026-11-07");
    CHECK(e.value("localSeconds") == seconds_of(11, 10));
    CHECK(e.value("zone") == MELBOURNE);
    CHECK(e.value("timeText") == "11:10");

    const QVariantMap n = rig.model().entryForEdit(text_of(2));
    CHECK(n.value("kind") == "note");
    CHECK(n.value("noteText") == "Robot restarted");
    CHECK(n.value("templateId") == "");
    CHECK(n.value("localSeconds") == seconds_of(14, 5));

    const QVariantMap c = rig.model().entryForEdit(text_of(3));
    CHECK(c.value("kind") == "header_change");
    CHECK(c.value("fieldKey") == "software_version");
    CHECK(c.value("newValue") == "1.5.0");

    CHECK(rig.model().entryForEdit(text_of(99)).isEmpty());
}

TEST_CASE("Edit: refusals give a plain reason and write nothing", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10), note_at(2, 11, 20, "Robot restarted"),
               change_at(3, 11, 30, HeaderField::SOFTWARE_VERSION, "1.5.0")});
    QSignalSpy any(&rig.model(), &QAbstractItemModel::dataChanged);

    const auto refused = [&](std::uint64_t entry, const QVariantMap& edit)
    {
        CHECK_FALSE(rig.model().validateEdit(text_of(entry), edit).isEmpty());
        CHECK(rig.model().previewLine(text_of(entry), edit).isEmpty());
        CHECK_FALSE(rig.model().saveEntry(text_of(entry), edit));
    };

    refused(2, QVariantMap{{"noteText", "   \n "}});
    refused(1, QVariantMap{{"errorTypeId", "00000000-0000-4000-8000-0000000000ff"}});
    refused(1, QVariantMap{{"errorTypeId", "nonsense"}});
    refused(1, QVariantMap{{"localSeconds", -1}});
    refused(1, QVariantMap{{"localSeconds", 86400}});
    refused(1, QVariantMap{{"localSeconds", "noon"}});
    refused(1, QVariantMap{{"localDate", "7 Nov 2026"}});
    refused(1, QVariantMap{{"localDate", "2026-02-30"}});
    refused(1, QVariantMap{{"zone", "Australia/Perth"}});
    refused(1, QVariantMap{{"kind", "note"}});
    refused(2, QVariantMap{{"templateId", "00000000-0000-4000-8000-0000000000ff"}});
    refused(3, QVariantMap{{"newValue", "1.4"}});
    refused(3, QVariantMap{{"newValue", "not a version"}});
    refused(2, QVariantMap{{"noteText", QString(10'001, QChar('x'))}});
    CHECK_FALSE(rig.model().validateEdit("not an id", {}).isEmpty());
    CHECK_FALSE(rig.model().validateEdit(text_of(99), {}).isEmpty());

    CHECK(rig.model().validateEdit(text_of(1), {}).isEmpty());
    CHECK(rig.gateway().posted() == 0);
    CHECK(any.count() == 0);
}

TEST_CASE("Edit: previewLine is the saved line, and writes nothing", "[viewmodel][timeline]")
{
    Rig rig;
    const Configuration configuration = sample_configuration();
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B"),
               change_at(3, 11, 30, HeaderField::SOFTWARE_VERSION, "1.5.0"),
               change_at(4, 11, 40, HeaderField::SOFTWARE_VERSION, "1.6.0")});

    const std::vector<std::pair<std::uint64_t, QVariantMap>> edits = {
        {1, QVariantMap{{"errorTypeId", QString::fromStdString(type_named(configuration, "Mute Delay").format())}}},
        {2, QVariantMap{{"attachedNote", "asked for Evening Song"}, {"localSeconds", seconds_of(11, 45)}}},
        {1, QVariantMap{{"localSeconds", seconds_of(11, 5)}}}};
    for (const auto& [entry, edit] : edits)
    {
        const std::size_t posted = rig.gateway().posted();
        const std::size_t writes = rig.storage().repository().mutating_call_count();
        const QString preview = rig.model().previewLine(text_of(entry), edit);
        CHECK_FALSE(preview.isEmpty());
        CHECK(rig.gateway().posted() == posted);
        CHECK(rig.storage().repository().mutating_call_count() == writes);
        REQUIRE(rig.model().saveEntry(text_of(entry), edit));
        rig.commit();
        CHECK(full_line(rig.model(), rig.model().rowOf(text_of(entry))) == preview);
    }

    // A header change: its preview shows the old side the edit would leave.
    const QVariantMap correction{{"newValue", "1.4.5"}};
    const QString preview = rig.model().previewLine(text_of(3), correction);
    CHECK(preview == "11:30 Software Version changed: v1.4.2 \xE2\x86\x92 v1.4.5");
    REQUIRE(rig.model().saveEntry(text_of(3), correction));
    rig.commit();
    CHECK(full_line(rig.model(), rig.model().rowOf(text_of(3))) == preview);
}

TEST_CASE("Edit: saving an unchanged sheet changes nothing and is not an edit", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), note_at(2, 11, 20, "B"),
               change_at(3, 11, 30, HeaderField::SOFTWARE_VERSION, "1.5.0")});
    // An entry with milliseconds: the sheet shows whole seconds, and an untouched sheet keeps the instant exactly.
    Entry precise = error_at(4, 11, 40, "C");
    precise.m_Captured.m_Utc += 456ms;
    REQUIRE(rig.document().add_entry(precise, Origin::USER, {}));
    rig.commit();

    QSignalSpy changed(&rig.model(), &QAbstractItemModel::dataChanged);
    QSignalSpy moved(&rig.model(), &QAbstractItemModel::rowsMoved);
    QSignalSpy summary(&rig.model(), &TimelineModel::summaryChanged);
    const std::size_t posted = rig.gateway().posted();
    const std::size_t writes = rig.storage().repository().mutating_call_count();

    for (const std::uint64_t entry : std::initializer_list<std::uint64_t>{1, 2, 3, 4})
    {
        const QVariantMap sheet = rig.model().entryForEdit(text_of(entry));
        CHECK(rig.model().validateEdit(text_of(entry), sheet).isEmpty());
        CHECK(rig.model().saveEntry(text_of(entry), sheet));
    }
    CHECK(rig.model().saveEntry(text_of(1), {}));

    CHECK(rig.gateway().posted() == posted);
    CHECK(rig.storage().repository().mutating_call_count() == writes); // no edit reached the store
    CHECK(changed.count() == 0);
    CHECK(moved.count() == 0);
    CHECK(summary.count() == 0);
    for (const Entry& entry : rig.document().log().entries())
    {
        CHECK_FALSE(entry.m_EditedUtc.has_value());
    }
    CHECK(rig.document().log().find(sample_id<EntryId>(4))->m_Captured.m_Utc == precise.m_Captured.m_Utc);
}

TEST_CASE("Edit: correcting a header change updates its line and the old side of the next one", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), change_at(2, 11, 30, HeaderField::SOFTWARE_VERSION, "1.5.0"),
               change_at(3, 11, 50, HeaderField::SOFTWARE_VERSION, "1.6.0")});
    const QString header = rig.model().header_text();
    CHECK(full_line(rig.model(), 1) == "11:30 Software Version changed: v1.4.2 \xE2\x86\x92 v1.5.0");
    CHECK(full_line(rig.model(), 2) == "11:50 Software Version changed: v1.5.0 \xE2\x86\x92 v1.6.0");
    QSignalSpy changed(&rig.model(), &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);

    SECTION("the new value")
    {
        REQUIRE(rig.model().saveEntry(text_of(2), QVariantMap{{"newValue", "1.4.5"}}));
        rig.commit();

        CHECK(full_line(rig.model(), 1) == "11:30 Software Version changed: v1.4.2 \xE2\x86\x92 v1.4.5");
        CHECK(full_line(rig.model(), 2) == "11:50 Software Version changed: v1.4.5 \xE2\x86\x92 v1.6.0");
        CHECK(changed.count() == 1); // one dataChanged over every row
        CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
        CHECK(changed.at(0).at(1).value<QModelIndex>().row() == 2);
        CHECK(inserted.count() == 0);
        CHECK(reset.count() == 0);
        CHECK(rig.model().header_text() == header); // the header block does not change
    }

    SECTION("the time, and the value, in one edit")
    {
        REQUIRE(
            rig.model().saveEntry(text_of(2), QVariantMap{{"newValue", "1.4.5"}, {"localSeconds", seconds_of(12, 0)}}));
        rig.commit();

        CHECK(full_lines(rig.model()) ==
              std::vector<QString>{"11:10 T| A", "11:50 Software Version changed: v1.4.2 \xE2\x86\x92 v1.6.0",
                                   "12:00 Software Version changed: v1.6.0 \xE2\x86\x92 v1.4.5"});
        CHECK(rig.model().header_text() == header);
    }

    SECTION("deleting the first change changes the old side of the second")
    {
        REQUIRE(rig.model().deleteEntry(text_of(2)));
        rig.commit();
        CHECK(full_line(rig.model(), 1) == "11:50 Software Version changed: v1.4.2 \xE2\x86\x92 v1.6.0");
    }
}

// ------------------------------------------------------------------------------------------------------ the clipboard

TEST_CASE("Copy: copyLine and copyAll put exactly the expected bytes on the clipboard", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), note_at(2, 11, 20, "B"), error_at(3, 11, 30, "C", "with a note"),
               error_at(4, 11, 40, "D")});

    REQUIRE(rig.model().copyLine(3));
    CHECK(rig.clipboard().text() == "11:40 T| D");
    REQUIRE(rig.model().copyLine(2));
    CHECK(rig.clipboard().text() == "11:30 T| C \xE2\x80\x93 with a note");
    CHECK(rig.clipboard().copies() == 2);

    const std::string expected = rig.export_now(rig.document().log());
    REQUIRE(rig.model().copyAll());
    CHECK(rig.clipboard().text() == expected);
    CHECK(rig.clipboard().text() ==
          "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\nLocation: North Site\nSoftware Version: v1.4.2\n"
          "\n11:10 T| A\n11:20 (Free Text) B\n11:30 T| C \xE2\x80\x93 with a note\n11:40 T| D");

    CHECK_FALSE(rig.model().copyLine(-1));
    CHECK_FALSE(rig.model().copyLine(4));
    CHECK(rig.clipboard().copies() == 3);
}

TEST_CASE("Copy changes nothing in the store and does not mark the log as shared", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B")});
    const std::size_t calls = rig.storage().repository().calls().size();

    REQUIRE(rig.model().copyLine(0));
    REQUIRE(rig.model().copyAll());

    CHECK(rig.gateway().posted() == 0);
    CHECK(rig.storage().repository().calls().size() == calls);
    CHECK(rig.storage().repository().mutating_call_count() == 0);
    CHECK_FALSE(rig.document().last_shared().has_value());
    CHECK(rig.document().pending() == 0);
    const auto stored = rig.storage().memory().load_log(rig.document().id());
    REQUIRE(stored.has_value());
    CHECK_FALSE(stored->m_Meta.m_LastSharedUtc.has_value());
}

TEST_CASE("Copying a log that was shared and has changed since does not make it shared again", "[viewmodel][timeline]")
{
    // Only Share counts (REV-05): "Copy all" and copying a line leave the log as unshared as it was.
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B")});
    REQUIRE(rig.document().mark_shared(sample_time(500), {}));
    rig.commit();
    REQUIRE(rig.document().add_entry(error_at(3, 11, 30, "C"), Origin::USER, {}));
    rig.commit();
    REQUIRE(rig.document().meta().m_ChangedSinceShare);

    REQUIRE(rig.model().copyLine(0));
    REQUIRE(rig.model().copyAll());

    CHECK(rig.document().meta().m_ChangedSinceShare);
    CHECK(rig.document().last_shared() == sample_time(500));
    const auto stored = rig.storage().memory().load_log(rig.document().id());
    REQUIRE(stored.has_value());
    CHECK(stored->m_Meta.m_ChangedSinceShare);
    CHECK(stored->m_Meta.m_LastSharedUtc == sample_time(500));
}

TEST_CASE("Copy all refuses a log that cannot be rendered, and copies no part of it", "[viewmodel][timeline]")
{
    Rig rig;
    HeaderValues header = sample_header();
    header.m_Location = LocationValue{sample_id<LocationId>(9999), {}}; // a location the configuration does not have
    rig.start({error_at(1, 11, 10, "A")}, header);

    CHECK_FALSE(rig.model().copyAll());
    CHECK(rig.clipboard().copies() == 0);
    CHECK(rig.model().header_text().isEmpty());
    REQUIRE(rig.model().rowCount() == 1);
    CHECK(rig.model().copyLine(0)); // the line itself is fine
}

TEST_CASE("A row that cannot be rendered is shown as such and cannot be copied", "[viewmodel][timeline]")
{
    Rig rig;
    Entry unnamed = error_at(1, 11, 10, "");
    std::get<ErrorEntry>(unnamed.m_Body).m_ErrorType = sample_id<ErrorTypeId>(9999);
    // Older data has no snapshot, and storage would not take a new entry without one, so the document is made from a
    // stored log directly.
    Log log(sample_header());
    REQUIRE(log.add(unnamed));
    auto document = std::make_shared<LogDocument>(
        rig.environment(), rig.gateway(),
        StoredLog{LogMeta{sample_id<LogId>(5), UtcMillis{}, std::nullopt, false, std::nullopt}, std::move(log)});
    rig.model().bind(document);

    REQUIRE(rig.model().rowCount() == 1);
    CHECK(full_line(rig.model(), 0).isEmpty());
    CHECK_FALSE(row_text(rig.model(), 0, TimelineModel::LINE_TEXT).isEmpty());
    CHECK_FALSE(rig.model().copyLine(0));
    CHECK_FALSE(rig.model().copyAll());
    CHECK(rig.clipboard().copies() == 0);
}

// -------------------------------------------------------------------------------------------------------- signals

TEST_CASE("A tap is one rowsInserted and nothing else", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A")});
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&rig.model(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(&rig.model(), &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);
    QSignalSpy changed(&rig.model(), &QAbstractItemModel::dataChanged);
    QSignalSpy summary(&rig.model(), &TimelineModel::summaryChanged);

    REQUIRE(rig.document().add_entry(error_at(2, 11, 20, "B"), Origin::USER, {}));
    CHECK(inserted.count() == 0); // not before the commit
    rig.commit();

    REQUIRE(inserted.count() == 1);
    CHECK(inserted.at(0).at(1).toInt() == 1);
    CHECK(inserted.at(0).at(2).toInt() == 1);
    CHECK(removed.count() == 0);
    CHECK(moved.count() == 0);
    CHECK(reset.count() == 0);
    CHECK(changed.count() == 0);
    CHECK(summary.count() == 1);
    CHECK(rig.model().entry_count() == 2);
}

TEST_CASE("A loaded log is one modelReset, however many entries it has", "[viewmodel][timeline]")
{
    Rig rig;
    Log log(sample_header());
    for (std::uint64_t i = 0; i < 500; ++i)
    {
        Entry entry = sample_error(i + 1, 0, "Wrong Response");
        entry.m_Captured.m_Utc += std::chrono::minutes(static_cast<int>(i));
        REQUIRE(log.add(std::move(entry)));
    }
    const LogId id = sample_id<LogId>(3);
    auto document = std::make_shared<LogDocument>(rig.environment(), rig.gateway(), id);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    rig.model().bind(document); // not loaded yet
    CHECK_FALSE(rig.model().ready());
    CHECK(rig.model().rowCount() == 0);
    CHECK(reset.count() == 1);

    document->load(StoredLog{LogMeta{id, UtcMillis{}, std::nullopt, false, std::nullopt}, std::move(log)});

    CHECK(reset.count() == 2);
    CHECK(inserted.count() == 0);
    CHECK(rig.model().rowCount() == 500);
    CHECK(rig.model().ready());
    CHECK(rig.model().entry_count() == 500);

    SECTION("a replaced log is one more")
    {
        document->replace(StoredLog{LogMeta{sample_id<LogId>(4), UtcMillis{}, std::nullopt, false, std::nullopt},
                                    Log(sample_header())});
        CHECK(reset.count() == 3);
        CHECK(rig.model().rowCount() == 0);
        CHECK(rig.model().ready());
    }
}

TEST_CASE("A change of the time format is one dataChanged, and a change that adds nothing to the wording is none",
          "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B"), error_at(3, 11, 30, "C")});
    QSignalSpy changed(&rig.model(), &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    QSignalSpy summary(&rig.model(), &TimelineModel::summaryChanged);

    SECTION("the time format")
    {
        Configuration configuration = sample_configuration();
        configuration.m_TimeFormat = {TimeStyle::H12, false, MeridiemCase::LOWER};
        rig.config().set(configuration);

        REQUIRE(changed.count() == 1);
        CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
        CHECK(changed.at(0).at(1).value<QModelIndex>().row() == 2);
        CHECK(reset.count() == 0);
        CHECK(inserted.count() == 0);
        CHECK(full_line(rig.model(), 0) == "11:10 am T| A");
        CHECK(full_line(rig.model(), 2) == "11:30 am T| C");
        CHECK(rig.model().last_line() == "11:30 am T| C");
        CHECK(summary.count() == 1);
    }

    SECTION("the unit prefix changes the header and not the rows")
    {
        Configuration configuration = sample_configuration();
        configuration.m_UnitPrefix = "RACK";
        rig.config().set(configuration);

        CHECK(changed.count() == 0);
        CHECK(rig.model().header_text().contains("RACK-27"));
        CHECK(summary.count() == 1);
    }

    SECTION("the error prefix is in every error row, is one dataChanged when it changes, and may be empty (PRF-13)")
    {
        Configuration configuration = sample_configuration();
        configuration.m_ErrorPrefix = "ERR:";
        rig.config().set(configuration);

        REQUIRE(changed.count() == 1);
        CHECK(reset.count() == 0);
        CHECK(full_line(rig.model(), 0) == "11:10 ERR: A");
        CHECK(rig.model().last_line() == "11:30 ERR: C");
        // The two columns of the design: the prefix, and the rest without it.
        CHECK(rig.model().data(rig.model().index(0), TimelineModel::PREFIX_TEXT).toString() == "ERR:");
        CHECK(rig.model().data(rig.model().index(0), TimelineModel::REST_TEXT).toString() == "A");

        configuration.m_ErrorPrefix = "";
        rig.config().set(configuration);
        CHECK(full_line(rig.model(), 0) == "11:10 A");
        CHECK(rig.model().data(rig.model().index(0), TimelineModel::PREFIX_TEXT).toString().isEmpty());
        CHECK(rig.model().data(rig.model().index(0), TimelineModel::REST_TEXT).toString() == "A");
    }

    SECTION("a change that touches nothing the model shows")
    {
        Configuration configuration = sample_configuration();
        configuration.m_Facilitator = "Someone Else";
        rig.config().set(configuration);

        CHECK(changed.count() == 0);
        CHECK(reset.count() == 0);
        CHECK(summary.count() == 0);
    }

    SECTION("the time formatter follows the preference")
    {
        CHECK(rig.model().formatLocalSeconds(seconds_of(14, 5, 9)) == "14:05");
        Configuration configuration = sample_configuration();
        configuration.m_TimeFormat = {TimeStyle::H12, true, MeridiemCase::LOWER};
        rig.config().set(configuration);
        CHECK(rig.model().formatLocalSeconds(seconds_of(14, 5, 9)) == "02:05:09 pm");
        configuration.m_TimeFormat = {TimeStyle::H12, false, MeridiemCase::UPPER};
        rig.config().set(configuration);
        CHECK(rig.model().formatLocalSeconds(seconds_of(0, 5)) == "12:05 AM");
        CHECK(rig.model().formatLocalSeconds(-1).isEmpty());
        CHECK(rig.model().formatLocalSeconds(86400).isEmpty());
        CHECK(rig.model().formatLocalSeconds(86399) == "11:59 PM");
    }
}

TEST_CASE("A failed write emits no model signal, and reports through writeFailed", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B")});
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&rig.model(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(&rig.model(), &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);
    QSignalSpy changed(&rig.model(), &QAbstractItemModel::dataChanged);
    QSignalSpy summary(&rig.model(), &TimelineModel::summaryChanged);
    QSignalSpy failed(&rig.model(), &TimelineModel::writeFailed);
    const auto nothing_changed = [&]
    {
        CHECK(inserted.count() == 0);
        CHECK(removed.count() == 0);
        CHECK(moved.count() == 0);
        CHECK(reset.count() == 0);
        CHECK(changed.count() == 0);
        CHECK(summary.count() == 0);
        CHECK(rig.model().rowCount() == 2);
    };

    SECTION("a tap")
    {
        rig.storage().repository().fail_next(RepositoryOp::ADD_ENTRY);
        REQUIRE(rig.document().add_entry(error_at(3, 11, 30, "C"), Origin::USER, {}));
        rig.commit();
        nothing_changed();
        CHECK(failed.count() == 0); // the model did not write it: whoever tapped reports it
    }

    SECTION("an edit")
    {
        rig.storage().repository().fail_next(RepositoryOp::EDIT_ENTRY);
        REQUIRE(rig.model().saveEntry(text_of(1), time_edit(seconds_of(11, 25))));
        rig.commit();
        nothing_changed();
        REQUIRE(failed.count() == 1);
        CHECK(failed.at(0).at(0).toString() == "saveEntry");
        CHECK_FALSE(failed.at(0).at(1).toString().isEmpty());
        CHECK(full_line(rig.model(), 0) == "11:10 T| A");

        // The next try works.
        REQUIRE(rig.model().saveEntry(text_of(1), time_edit(seconds_of(11, 25))));
        rig.commit();
        CHECK(full_line(rig.model(), 0) == "11:20 T| B");
        CHECK(failed.count() == 1);
    }

    SECTION("a delete")
    {
        rig.storage().repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);
        REQUIRE(rig.model().deleteEntry(text_of(1)));
        rig.commit();
        nothing_changed();
        REQUIRE(failed.count() == 1);
        CHECK(failed.at(0).at(0).toString() == "deleteEntry");
    }

    SECTION("a correction of a header change")
    {
        // A refused edit never reaches the store.
        CHECK_FALSE(rig.model().saveEntry(text_of(1), QVariantMap{{"noteText", "x"}, {"errorTypeId", "bad"}}));
        nothing_changed();
        CHECK(failed.count() == 0);
    }
}

// --------------------------------------------------------------------------------- an archived log, and the open log

TEST_CASE("A bound archived document shows its own entries and is unaffected by the open log's taps",
          "[viewmodel][timeline]")
{
    Rig rig;
    HeaderValues past = sample_header();
    past.m_LocalDate = std::chrono::year{2026} / 11 / 6;
    const auto archived = rig.make_archived(2, past, {error_at(11, 9, 0, "Past one"), error_at(12, 9, 30, "Past two")});
    const auto open = rig.make(1, sample_header(), {error_at(1, 11, 10, "Open one")});

    TimelineModel open_model(rig.environment(), rig.config(), rig.clipboard());
    TimelineModel past_model(rig.environment(), rig.config(), rig.clipboard());
    open_model.bind(open);
    past_model.bind(archived);
    QSignalSpy past_changes(&past_model, &QAbstractItemModel::rowsInserted);
    QSignalSpy past_resets(&past_model, &QAbstractItemModel::modelReset);
    QSignalSpy past_data(&past_model, &QAbstractItemModel::dataChanged);
    QSignalSpy past_summary(&past_model, &TimelineModel::summaryChanged);
    QSignalSpy open_changes(&open_model, &QAbstractItemModel::rowsInserted);

    CHECK(full_lines(past_model) == std::vector<QString>{"09:00 T| Past one", "09:30 T| Past two"});
    CHECK(past_model.title_text() == "Fri 6 Nov \xC2\xB7 2 entries");
    CHECK(full_lines(open_model) == std::vector<QString>{"11:10 T| Open one"});

    REQUIRE(open->add_entry(error_at(2, 11, 20, "Open two"), Origin::USER, {}));
    rig.commit();

    CHECK(open_changes.count() == 1);
    CHECK(open_model.rowCount() == 2);
    CHECK(past_changes.count() == 0);
    CHECK(past_resets.count() == 0);
    CHECK(past_data.count() == 0);
    CHECK(past_summary.count() == 0);
    CHECK(past_model.rowCount() == 2);
    CHECK(full_lines(past_model) == std::vector<QString>{"09:00 T| Past one", "09:30 T| Past two"});

    SECTION("edits go through the same invokables")
    {
        REQUIRE(past_model.saveEntry(text_of(12),
                                     QVariantMap{{"attachedNote", "late"}, {"localSeconds", seconds_of(8, 0)}}));
        rig.commit();
        CHECK(full_lines(past_model) ==
              std::vector<QString>{"08:00 T| Past two \xE2\x80\x93 late", "09:00 T| Past one"});

        REQUIRE(past_model.deleteEntry(text_of(11)));
        rig.commit();
        CHECK(past_model.rowCount() == 1);
        CHECK(open_model.rowCount() == 2);

        // An archived log takes no new entry, but its edits are journalled in the store.
        CHECK_FALSE(archived->add_entry(error_at(13, 10, 0), Origin::USER, {}));
        const auto stored = rig.storage().memory().load_log(archived->id());
        REQUIRE(stored.has_value());
        CHECK(stored->m_Log.find(sample_id<EntryId>(11))->m_Deleted);
        CHECK(past_model.copyAll());
        CHECK(rig.clipboard().text().find("08:00 T| Past two") != std::string::npos);
        CHECK(rig.clipboard().text().find("Date: 6 Nov 2026") != std::string::npos);
    }

    SECTION("an entry id of the other log is not an entry of this one")
    {
        CHECK_FALSE(past_model.deleteEntry(text_of(1)));
        CHECK_FALSE(open_model.deleteEntry(text_of(11)));
        CHECK(past_model.entryForEdit(text_of(1)).isEmpty());
    }
}

TEST_CASE("Following the open log: a new log takes the place of the old one with one reset", "[viewmodel][timeline]")
{
    FakeOpenLogSource source; // first: it outlives the model, which unregisters from it
    Rig rig;
    const auto first = rig.make(1, sample_header(), {error_at(1, 11, 10, "Day one")});
    HeaderValues next_day = sample_header();
    next_day.m_LocalDate = std::chrono::year{2026} / 11 / 8;
    const auto second = rig.make_next(1, 2, next_day);
    source.set(first.get());
    const Unbinder unbinder{rig.model()}; // the model follows raw pointers; let go before the documents go

    rig.model().follow(source);
    QSignalSpy reset(&rig.model(), &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&rig.model(), &QAbstractItemModel::rowsInserted);
    CHECK(source.observer_count() == 1);
    CHECK(rig.model().document() == first.get());
    CHECK(full_lines(rig.model()) == std::vector<QString>{"11:10 T| Day one"});

    SECTION("the source reports another document")
    {
        source.set(second.get());
        CHECK(reset.count() == 1);
        CHECK(rig.model().rowCount() == 0);
        CHECK(rig.model().title_text() == "Sun 8 Nov \xC2\xB7 0 entries");

        // The old document no longer moves the model.
        QSignalSpy removed(&rig.model(), &QAbstractItemModel::rowsRemoved);
        REQUIRE(first->set_deleted(sample_id<EntryId>(1), true, Origin::USER, {}));
        rig.commit();
        CHECK(removed.count() == 0);
        CHECK(inserted.count() == 0);
        REQUIRE(second->add_entry(error_at(3, 12, 0, "Day two"), Origin::USER, {}));
        rig.commit();
        CHECK(inserted.count() == 1);
        CHECK(rig.model().last_line() == "12:00 T| Day two");
    }

    SECTION("the same document reported again changes nothing")
    {
        source.set(first.get());
        CHECK(reset.count() == 0);
    }

    SECTION("no open log")
    {
        source.set(nullptr);
        CHECK(reset.count() == 1);
        CHECK_FALSE(rig.model().ready());
    }

    SECTION("a document replaced in place (a new day) is one reset")
    {
        first->replace(
            StoredLog{LogMeta{sample_id<LogId>(9), UtcMillis{}, std::nullopt, false, std::nullopt}, Log(next_day)});
        CHECK(reset.count() == 1);
        CHECK(rig.model().rowCount() == 0);
    }

    SECTION("bind stops the following")
    {
        rig.model().bind(second);
        CHECK(source.observer_count() == 0);
        CHECK(reset.count() == 1);
        source.set(first.get());
        CHECK(rig.model().document() == second.get());
    }

    SECTION("unbind shows nothing")
    {
        rig.model().unbind();
        CHECK(source.observer_count() == 0);
        CHECK(rig.model().document() == nullptr);
        CHECK(rig.model().rowCount() == 0);
    }
}

TEST_CASE("The model stops listening when it is destroyed", "[viewmodel][timeline]")
{
    FakeOpenLogSource source;
    Rig rig;
    const auto document = rig.make(1, sample_header(), {error_at(1, 11, 10)});
    source.set(document.get());
    const std::size_t config_observers = rig.config().observer_count();
    {
        TimelineModel model(rig.environment(), rig.config(), rig.clipboard());
        model.follow(source);
        CHECK(rig.config().observer_count() == config_observers + 1);
        CHECK(source.observer_count() == 1);
    }
    CHECK(rig.config().observer_count() == config_observers);
    CHECK(source.observer_count() == 0);

    // A commit that completes after the model has gone reaches nobody.
    TimelineModel* gone = nullptr;
    {
        auto model = std::make_unique<TimelineModel>(rig.environment(), rig.config(), rig.clipboard());
        model->bind(document);
        REQUIRE(model->deleteEntry(text_of(1)));
        gone = model.get();
    }
    (void)gone;
    rig.commit(); // must not call back into the destroyed model
    CHECK(document->log().find(sample_id<EntryId>(1))->m_Deleted);
}

// ------------------------------------------------------------------------------------------ local time to UTC

namespace
{

QVariantMap convert(Rig& rig, std::uint64_t entry, const QString& date, int seconds)
{
    return rig.model().localToUtc(text_of(entry), date, seconds);
}

/// The checks of the daylight-saving days of Melbourne, for any provider that follows the contract of Stage 1.
void check_melbourne_days(const TimeZoneProvider& zones)
{
    Rig rig(&zones);
    HeaderValues header = sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 10 / 4;
    Entry entry = error_at(1, 0, 0, "Wrong Response");
    entry.m_Captured = Timestamp{utc(2026, 10, 3, 15, 50), MELBOURNE}; // 01:50 AEST on 4 October
    rig.start({entry}, header);

    SECTION("2026-10-04 has 23 hours: the day is one hour short")
    {
        const auto start = convert(rig, 1, "2026-10-04", 0);
        const auto end = convert(rig, 1, "2026-10-05", 0);
        REQUIRE(start.value("ok").toBool());
        REQUIRE(end.value("ok").toBool());
        CHECK(end.value("utcMillis").toLongLong() - start.value("utcMillis").toLongLong() ==
              std::chrono::milliseconds(23h).count());
        CHECK(start.value("utcMillis").toLongLong() == millis_of(utc(2026, 10, 3, 14, 0)));
    }

    SECTION("2026-10-04: the skipped hour is refused with a message")
    {
        for (const int seconds : {seconds_of(2, 0), seconds_of(2, 30), seconds_of(2, 59, 59)})
        {
            const auto result = convert(rig, 1, "2026-10-04", seconds);
            CHECK_FALSE(result.value("ok").toBool());
            CHECK_FALSE(result.value("message").toString().isEmpty());
            CHECK(result.value("utcMillis").toLongLong() == 0);
        }
        // The times each side of the gap exist.
        const auto before = convert(rig, 1, "2026-10-04", seconds_of(1, 59, 59));
        REQUIRE(before.value("ok").toBool());
        CHECK(before.value("utcMillis").toLongLong() == millis_of(utc(2026, 10, 3, 15, 59, 59)));
        CHECK_FALSE(before.value("repeated").toBool());
        const auto after = convert(rig, 1, "2026-10-04", seconds_of(3, 0));
        REQUIRE(after.value("ok").toBool());
        CHECK(after.value("utcMillis").toLongLong() == millis_of(utc(2026, 10, 3, 16, 0)));
        CHECK(after.value("message").toString().isEmpty());
    }

    SECTION("saving into the skipped hour resolves to the instant after the gap, as the goldens do")
    {
        Entry later = error_at(2, 0, 0, "Mute Delay");
        later.m_Captured = Timestamp{utc(2026, 10, 3, 15, 20), MELBOURNE};
        REQUIRE(rig.document().add_entry(later, Origin::USER, {}));
        Entry last = error_at(3, 0, 0, "Speech Delay");
        last.m_Captured = Timestamp{utc(2026, 10, 3, 16, 5), MELBOURNE}; // 03:05 AEDT
        REQUIRE(rig.document().add_entry(last, Origin::USER, {}));
        rig.commit();

        REQUIRE(rig.model().saveEntry(text_of(2), time_edit(seconds_of(2, 30))));
        rig.commit();

        CHECK(full_lines(rig.model()) ==
              std::vector<QString>{"01:50 T| Wrong Response", "03:05 T| Speech Delay", "03:30 T| Mute Delay"});
        REQUIRE(rig.model().copyAll());
        check_golden("melbourne-dst-spring", rig.clipboard().text());
    }
}

void check_melbourne_autumn_days(const TimeZoneProvider& zones)
{
    Rig rig(&zones);
    HeaderValues header = sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 4 / 5;
    Entry entry = error_at(1, 0, 0, "Wrong Response");
    entry.m_Captured = Timestamp{utc(2026, 4, 4, 14, 50), MELBOURNE}; // 01:50 AEDT on 5 April
    Entry second = error_at(2, 0, 0, "Mute Delay");
    second.m_Captured = Timestamp{utc(2026, 4, 4, 14, 0), MELBOURNE}; // 01:00 AEDT, to be edited into the repeated hour
    Entry repeat = error_at(3, 0, 0, "Speech Delay");
    repeat.m_Captured = Timestamp{utc(2026, 4, 4, 16, 30), MELBOURNE}; // 02:30 AEST, the second 02:30
    Entry last = error_at(4, 0, 0, "Wrong Track Played");
    last.m_Captured = Timestamp{utc(2026, 4, 4, 17, 10), MELBOURNE};
    rig.start({entry, second, repeat, last}, header);

    SECTION("2026-04-05 has 25 hours: the day is one hour long")
    {
        const auto start = convert(rig, 1, "2026-04-05", 0);
        const auto end = convert(rig, 1, "2026-04-06", 0);
        REQUIRE(start.value("ok").toBool());
        REQUIRE(end.value("ok").toBool());
        CHECK(end.value("utcMillis").toLongLong() - start.value("utcMillis").toLongLong() ==
              std::chrono::milliseconds(25h).count());
    }

    SECTION("2026-04-05: the repeated hour resolves to the earlier occurrence")
    {
        const auto result = convert(rig, 1, "2026-04-05", seconds_of(2, 30));
        REQUIRE(result.value("ok").toBool());
        CHECK(result.value("utcMillis").toLongLong() == millis_of(utc(2026, 4, 4, 15, 30))); // 02:30 AEDT
        CHECK(result.value("repeated").toBool());
        CHECK_FALSE(result.value("message").toString().isEmpty()); // it says the first one is used

        for (const int seconds : {seconds_of(2, 0), seconds_of(2, 59, 59)})
        {
            CHECK(convert(rig, 1, "2026-04-05", seconds).value("repeated").toBool());
        }
        CHECK_FALSE(convert(rig, 1, "2026-04-05", seconds_of(1, 59, 59)).value("repeated").toBool());
        CHECK_FALSE(convert(rig, 1, "2026-04-05", seconds_of(3, 0)).value("repeated").toBool());
    }

    SECTION("saving into the repeated hour takes the earlier occurrence, as the goldens do")
    {
        REQUIRE(rig.model().saveEntry(text_of(2), time_edit(seconds_of(2, 30))));
        rig.commit();

        CHECK(rig.document().log().find(sample_id<EntryId>(2))->m_Captured.m_Utc == utc(2026, 4, 4, 15, 30));
        CHECK(full_lines(rig.model()) == std::vector<QString>{"01:50 T| Wrong Response", "02:30 T| Mute Delay",
                                                              "02:30 T| Speech Delay", "03:10 T| Wrong Track Played"});
        REQUIRE(rig.model().copyAll());
        check_golden("melbourne-dst-autumn", rig.clipboard().text());
    }

    SECTION("the second 02:30 stays the second when its sheet is saved untouched")
    {
        const QVariantMap sheet = rig.model().entryForEdit(text_of(3));
        CHECK(sheet.value("localSeconds") == seconds_of(2, 30));
        const std::size_t posted = rig.gateway().posted();
        CHECK(rig.model().saveEntry(text_of(3), sheet));
        CHECK(rig.gateway().posted() == posted);
        CHECK(rig.document().log().find(sample_id<EntryId>(3))->m_Captured.m_Utc == utc(2026, 4, 4, 16, 30));
    }
}

} // namespace

TEST_CASE("Local time to UTC with the Melbourne spring change (fake zones)", "[viewmodel][timeline][zone]")
{
    const MelbourneDstZones zones;
    check_melbourne_days(zones);
}

TEST_CASE("Local time to UTC with the Melbourne autumn change (fake zones)", "[viewmodel][timeline][zone]")
{
    const MelbourneDstZones zones;
    check_melbourne_autumn_days(zones);
}

TEST_CASE("Local time to UTC with the Melbourne spring change (the platform's zone data)",
          "[viewmodel][timeline][zone]")
{
    const auto zones = make_qt_time_zone_provider();
    check_melbourne_days(*zones);
}

TEST_CASE("Local time to UTC with the Melbourne autumn change (the platform's zone data)",
          "[viewmodel][timeline][zone]")
{
    const auto zones = make_qt_time_zone_provider();
    check_melbourne_autumn_days(*zones);
}

TEST_CASE("Local time to UTC: an ordinary day, the entry's own zone, and refusals", "[viewmodel][timeline][zone]")
{
    Rig rig;
    Entry perth = error_at(2, 0, 0, "Wrong Response");
    perth.m_Captured = Timestamp{utc(2026, 11, 7, 2, 0), "Australia/Perth"};
    Entry nowhere = error_at(3, 0, 0, "Wrong Response");
    nowhere.m_Captured = Timestamp{utc(2026, 11, 7, 2, 0), "Nowhere/North_Site"};
    rig.start({error_at(1, 11, 10), perth});

    const auto melbourne = convert(rig, 1, "2026-11-07", seconds_of(11, 0));
    REQUIRE(melbourne.value("ok").toBool());
    CHECK(melbourne.value("utcMillis").toLongLong() == millis_of(utc(2026, 11, 7, 0, 0))); // +11:00
    CHECK_FALSE(melbourne.value("repeated").toBool());
    CHECK(melbourne.value("message").toString().isEmpty());

    // The same wall clock in the zone of the other entry.
    const auto in_perth = convert(rig, 2, "2026-11-07", seconds_of(11, 0));
    REQUIRE(in_perth.value("ok").toBool());
    CHECK(in_perth.value("utcMillis").toLongLong() == millis_of(utc(2026, 11, 7, 3, 0))); // +08:00

    // The date is free: the time editor can move an entry across midnight.
    const auto yesterday = convert(rig, 1, "2026-11-06", seconds_of(23, 59, 59));
    REQUIRE(yesterday.value("ok").toBool());
    CHECK(yesterday.value("utcMillis").toLongLong() == millis_of(utc(2026, 11, 6, 12, 59, 59)));

    const auto refused = [&](std::uint64_t entry, const QString& date, int seconds)
    {
        const auto result = convert(rig, entry, date, seconds);
        CHECK_FALSE(result.value("ok").toBool());
        CHECK_FALSE(result.value("message").toString().isEmpty());
        CHECK(result.value("utcMillis").toLongLong() == 0);
    };
    refused(99, "2026-11-07", 0);
    refused(1, "7/11/2026", 0);
    refused(1, "2026-13-01", 0);
    refused(1, "2026-11-07", -1);
    refused(1, "2026-11-07", 86400);

    // The model reads nothing and writes nothing.
    CHECK(rig.gateway().posted() == 0);

    SECTION("a zone the provider does not know")
    {
        Rig other;
        other.start({nowhere});
        const auto result = other.model().localToUtc(text_of(3), "2026-11-07", 0);
        CHECK_FALSE(result.value("ok").toBool());
        CHECK_FALSE(result.value("message").toString().isEmpty());
    }
}

TEST_CASE("Local time to UTC and saving agree on an ordinary day", "[viewmodel][timeline][zone]")
{
    Rig rig;
    rig.start({error_at(1, 11, 10, "A"), error_at(2, 11, 20, "B")});

    const auto converted = convert(rig, 1, "2026-11-07", seconds_of(11, 45, 30));
    REQUIRE(converted.value("ok").toBool());
    REQUIRE(rig.model().saveEntry(text_of(1),
                                  QVariantMap{{"localDate", "2026-11-07"}, {"localSeconds", seconds_of(11, 45, 30)}}));
    rig.commit();

    CHECK(millis_of(rig.document().log().find(sample_id<EntryId>(1))->m_Captured.m_Utc) ==
          converted.value("utcMillis").toLongLong());
    CHECK(full_lines(rig.model()) == std::vector<QString>{"11:20 T| B", "11:45 T| A"});
    CHECK(rig.document().log().find(sample_id<EntryId>(1))->m_Captured.m_Zone == MELBOURNE);

    SECTION("a new date moves the entry to that day, and the date is shown back")
    {
        REQUIRE(rig.model().saveEntry(text_of(2),
                                      QVariantMap{{"localDate", "2026-11-08"}, {"localSeconds", seconds_of(0, 5)}}));
        rig.commit();
        CHECK(rig.model().entryForEdit(text_of(2)).value("localDate") == "2026-11-08");
        CHECK(rig.model().rowOf(text_of(2)) == 1);
        CHECK(full_line(rig.model(), 1) == "00:05 T| B");
    }
}

// ----------------------------------------------------------------------------------- what the visual pass binds to

namespace
{

/// The entries as a vector, built by `push_back`.
template <typename... Entries>
std::vector<Entry> list_of(Entries... entries)
{
    std::vector<Entry> out;
    (out.push_back(std::move(entries)), ...);
    return out;
}

} // namespace

TEST_CASE("A row splits its line into the marker and the rest", "[viewmodel][timeline]")
{
    Rig rig;
    Entry unshowable = error_at(5, 12, 0);
    unshowable.m_Captured.m_Zone = "Nowhere/Land";
    rig.start(list_of(error_at(1, 9, 42), error_at(2, 10, 5, "Wrong Track Played", "asked for Evening Song"),
                      note_at(3, 10, 30, "Played Morning Medley"),
                      change_at(4, 11, 0, HeaderField::SOFTWARE_VERSION, "1.5.0"), unshowable));

    CHECK(row_text(rig.model(), 0, TimelineModel::LINE_TEXT) == "T| Wrong Response");
    CHECK(row_text(rig.model(), 0, TimelineModel::PREFIX_TEXT) == "T|");
    CHECK(row_text(rig.model(), 0, TimelineModel::REST_TEXT) == "Wrong Response");

    CHECK(row_text(rig.model(), 1, TimelineModel::PREFIX_TEXT) == "T|");
    CHECK(row_text(rig.model(), 1, TimelineModel::REST_TEXT) ==
          "Wrong Track Played \xE2\x80\x93 asked for Evening Song");

    CHECK(row_text(rig.model(), 2, TimelineModel::PREFIX_TEXT) == "(Free Text)");
    CHECK(row_text(rig.model(), 2, TimelineModel::REST_TEXT) == "Played Morning Medley");

    // A change of the header has no marker: the rest is the whole line.
    CHECK(row_text(rig.model(), 3, TimelineModel::PREFIX_TEXT).isEmpty());
    CHECK_FALSE(row_text(rig.model(), 3, TimelineModel::REST_TEXT).isEmpty());
    CHECK(row_text(rig.model(), 3, TimelineModel::REST_TEXT) == row_text(rig.model(), 3, TimelineModel::LINE_TEXT));

    // A line that cannot be rendered has no marker either.
    CHECK(row_text(rig.model(), 4, TimelineModel::PREFIX_TEXT).isEmpty());
    CHECK(row_text(rig.model(), 4, TimelineModel::REST_TEXT) == "This entry cannot be shown.");
}

TEST_CASE("The marker and the rest make the line again", "[viewmodel][timeline]")
{
    Rig rig;
    rig.start(list_of(error_at(1, 9, 42), note_at(2, 10, 30, "Played Morning Medley")));

    for (int row = 0; row < rig.model().rowCount(); ++row)
    {
        const QString prefix = row_text(rig.model(), row, TimelineModel::PREFIX_TEXT);
        const QString rest = row_text(rig.model(), row, TimelineModel::REST_TEXT);
        CHECK(prefix + " " + rest == row_text(rig.model(), row, TimelineModel::LINE_TEXT));
        CHECK(row_text(rig.model(), row, TimelineModel::TIME_TEXT) + " " + prefix + " " + rest ==
              full_line(rig.model(), row));
    }
}

TEST_CASE("Edit: the sheet has the colour of the type and its name", "[viewmodel][timeline]")
{
    Rig rig;
    const Configuration configuration = sample_configuration();
    Entry mute = error_at(1, 11, 2, "Mute Delay");
    std::get<ErrorEntry>(mute.m_Body).m_ErrorType = type_named(configuration, "Mute Delay");
    Entry stranger = error_at(2, 11, 5, "Gone Type"); // a type the configuration does not have
    rig.start(list_of(mute, stranger, note_at(3, 11, 10, "Robot restarted")));

    // Mute Delay is the eighth type of the sample: slot 8, Periwinkle.
    const QVariantMap sheet = rig.model().entryForEdit(text_of(1));
    CHECK(sheet.value("colourSlot") == 8);
    CHECK(sheet.value("colourName") == "Periwinkle");

    const QVariantMap unknown = rig.model().entryForEdit(text_of(2));
    CHECK(unknown.value("colourSlot") == 0);
    CHECK(unknown.value("colourName") == "");

    // Only an error has a colour.
    CHECK_FALSE(rig.model().entryForEdit(text_of(3)).contains("colourSlot"));
}

TEST_CASE("Edit: the footnote says when the entry was logged and on which unit and version", "[viewmodel][timeline]")
{
    SECTION("the unit and the version in force")
    {
        Rig rig;
        rig.start(list_of(error_at(1, 11, 2, "Mute Delay"), change_at(2, 12, 0, HeaderField::SOFTWARE_VERSION, "1.5.0"),
                          note_at(3, 13, 0, "Robot restarted")));

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02 on UNIT-27, version 1.4.2.");
        // After the change of the version, the version of the day is the new one.
        CHECK(rig.model().entryForEdit(text_of(3)).value("footnote") == "Logged at 13:00 on UNIT-27, version 1.5.0.");
    }
    SECTION("the time follows the time format")
    {
        Rig rig;
        rig.start(list_of(error_at(1, 14, 5)));
        Configuration configuration = sample_configuration();
        configuration.m_TimeFormat.m_Style = TimeStyle::H12;
        rig.config().set(configuration);

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") ==
              "Logged at 02:05 PM on UNIT-27, version 1.4.2.");
    }
    SECTION("the unit alone, the version alone, and neither")
    {
        HeaderValues header = sample_header();
        header.m_SoftwareVersion.reset();
        Rig unit_only;
        unit_only.start(list_of(error_at(1, 11, 2)), header);
        CHECK(unit_only.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02 on UNIT-27.");

        header = sample_header();
        header.m_UnitNumber.reset();
        Rig version_only;
        version_only.start(list_of(error_at(1, 11, 2)), header);
        CHECK(version_only.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02, version 1.4.2.");

        header.m_SoftwareVersion.reset();
        Rig neither;
        neither.start(list_of(error_at(1, 11, 2)), header);
        CHECK(neither.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02.");
    }
    SECTION("the environment of the unit is part of it, and follows a change of it")
    {
        HeaderValues header = sample_header();
        header.m_UnitNumber = UnitNumber::parse("27", "prod").value();
        Rig rig;
        rig.start(list_of(error_at(1, 11, 2, "Mute Delay"), change_at(2, 12, 0, HeaderField::UNIT_NUMBER, "27-dev"),
                          note_at(3, 13, 0, "Robot restarted")),
                  header);

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") ==
              "Logged at 11:02 on UNIT-27-prod, version 1.4.2.");
        CHECK(rig.model().entryForEdit(text_of(3)).value("footnote") ==
              "Logged at 13:00 on UNIT-27-dev, version 1.4.2.");
    }
    SECTION("the commit follows the version in brackets, and follows a change of it")
    {
        HeaderValues header = sample_header();
        header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2", "a1b2c3d").value();
        Rig rig;
        rig.start(list_of(error_at(1, 11, 2, "Mute Delay"),
                          change_at(2, 12, 0, HeaderField::SOFTWARE_VERSION, "1.5.0+e4f5a6b"),
                          change_at(3, 13, 0, HeaderField::SOFTWARE_VERSION, "1.5.0"),
                          note_at(4, 14, 0, "Robot restarted")),
                  header);

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") ==
              "Logged at 11:02 on UNIT-27, version 1.4.2 (a1b2c3d).");
        // A new version with its own commit, then the same version with none: the footnote is what was in force.
        CHECK(rig.model().entryForEdit(text_of(2)).value("footnote") ==
              "Logged at 12:00 on UNIT-27, version 1.5.0 (e4f5a6b).");
        CHECK(rig.model().entryForEdit(text_of(4)).value("footnote") == "Logged at 14:00 on UNIT-27, version 1.5.0.");
    }
    SECTION("the commit with no unit")
    {
        HeaderValues header = sample_header();
        header.m_UnitNumber.reset();
        header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2", "a1b2c3d").value();
        Rig rig;
        rig.start(list_of(error_at(1, 11, 2)), header);

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02, version 1.4.2 (a1b2c3d).");
    }
    SECTION("with no configuration the unit is its number")
    {
        Rig rig;
        rig.start(list_of(error_at(1, 11, 2)));
        rig.config().clear();

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02 on 27, version 1.4.2.");
    }
    SECTION("with no configuration the unit is its stored text, environment and all")
    {
        HeaderValues header = sample_header();
        header.m_UnitNumber = UnitNumber::parse("27", "prod").value();
        Rig rig;
        rig.start(list_of(error_at(1, 11, 2)), header);
        rig.config().clear();

        CHECK(rig.model().entryForEdit(text_of(1)).value("footnote") == "Logged at 11:02 on 27-prod, version 1.4.2.");
    }
}

TEST_CASE("Now is the clock in the zone of the entry", "[viewmodel][timeline]")
{
    Rig rig;
    Entry perth = error_at(2, 11, 2);
    perth.m_Captured.m_Zone = "Australia/Perth";
    Entry nowhere = error_at(3, 11, 3);
    nowhere.m_Captured.m_Zone = "Nowhere/Land";
    rig.start(list_of(error_at(1, 11, 1), perth, nowhere));

    // The clock of the rig is 08:55 in Melbourne (+11:00), which is 05:55 in Perth (+08:00).
    CHECK(rig.model().nowLocalSeconds(text_of(1)) == seconds_of(8, 55));
    CHECK(rig.model().nowLocalSeconds(text_of(2)) == seconds_of(5, 55));
    // No entry, or one that is not in the log: the zone of the device, Melbourne.
    CHECK(rig.model().nowLocalSeconds() == seconds_of(8, 55));
    CHECK(rig.model().nowLocalSeconds("not an id") == seconds_of(8, 55));
    CHECK(rig.model().nowLocalSeconds(text_of(99)) == seconds_of(8, 55));
    // A zone the provider does not know has no local time.
    CHECK(rig.model().nowLocalSeconds(text_of(3)) == -1);

    // It moves with the clock.
    rig.clock().advance(5min + 7s);
    CHECK(rig.model().nowLocalSeconds(text_of(1)) == seconds_of(9, 0, 7));
}
