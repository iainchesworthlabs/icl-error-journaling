// SPDX-License-Identifier: GPL-3.0-or-later
#include "fake_time_zone_provider.hpp"
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_platform.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/recording_repository.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/history_model.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/share_service.hpp"
#include "viewmodel/timeline_model.hpp"
#include "viewmodel/undo_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QVariant>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::DocumentRegistry;
using viewmodel::HistoryModel;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::SettingsModel;
using viewmodel::ShareService;
using viewmodel::TimelineModel;
using viewmodel::UndoController;
using namespace std::chrono_literals;

// DAY-05, at compile time: the one way to a `DeleteConfirmation` is `HistoryDeleteConfirmation::confirmed`, and the
// repository does not delete a log without one. A model cannot call `hard_delete_log` by accident.
static_assert(!std::is_default_constructible_v<DeleteConfirmation>);
static_assert(!std::is_constructible_v<DeleteConfirmation, LogId>);
static_assert(!std::is_convertible_v<LogId, DeleteConfirmation>);
template <typename... Arguments>
concept can_hard_delete =
    requires(LogRepository& repository, Arguments... arguments) { repository.hard_delete_log(arguments...); };

static_assert(!can_hard_delete<LogId>);
static_assert(!can_hard_delete<LogId, LogId>);
static_assert(can_hard_delete<LogId, DeleteConfirmation>);

namespace
{

constexpr std::uint64_t LAST_YEAR = 1; // 7 Nov 2025
constexpr std::uint64_t YESTERDAY = 2; // 6 Nov 2026
constexpr std::uint64_t MORNING = 3;   // 7 Nov 2026, the first log of the date
constexpr std::uint64_t EVENING = 4;   // 7 Nov 2026, the second (a move to another unit or site)
constexpr std::uint64_t OPEN = 5;      // 8 Nov 2026, the open log

LocationId location_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_Locations, name, &Location::m_Name);
    REQUIRE(it != configuration.m_Locations.end());
    return it->m_Id;
}

QString text_of(std::uint64_t log_number)
{
    return QString::fromStdString(sample_id<LogId>(log_number).format());
}

/// An error entry at `hour:minute` on 7 Nov 2026 in Melbourne.
Entry error_at(std::uint64_t number, int hour, int minute, std::string label = "Wrong Response")
{
    Entry entry = sample_error(number, 0, std::move(label));
    entry.m_Captured = on_day(hour, minute);
    return entry;
}

QVariant cell(const HistoryModel& model, int row, int role)
{
    return model.data(model.index(row), role);
}

QString section(const HistoryModel& model, int row)
{
    return cell(model, row, HistoryModel::SECTION).toString();
}

QString log_id(const HistoryModel& model, int row)
{
    return cell(model, row, HistoryModel::LOG_ID).toString();
}

int row_of(const HistoryModel& model, std::uint64_t log_number)
{
    for (int row = 0; row < model.rowCount(); ++row)
    {
        if (log_id(model, row) == text_of(log_number))
        {
            return row;
        }
    }
    return -1;
}

QString timeline_line(const TimelineModel& model, int row)
{
    return model.data(model.index(row), TimelineModel::FULL_LINE).toString();
}

/// The sample configuration in force, five logs in the in-memory store (four archived, one open), a registry and a
/// share service over the manual gateway, and the model under test.
///
/// Declaration order is destruction order backwards: the models go first, then the registry, the gateway and the store.
class Fixture
{
public:
    explicit Fixture(bool configured = true)
    {
        if (configured)
        {
            m_Storage.config().set_saved(export_configuration(sample_configuration()));
            m_Settings.load();
            m_Gateway.run_all();
            REQUIRE(m_Settings.configured());
        }
        const LocationId north = location_named(sample_configuration(), "North Site");

        // The store holds one open log at a time, so each is archived before the next is made. The clock moves between
        // them so that "created" differs.
        make(LAST_YEAR, 2025, 11, 7, LocationValue{north, {}}, {error_at(11, 9, 0), error_at(12, 9, 10)}, true);
        make(YESTERDAY, 2026, 11, 6, LocationValue{std::nullopt, "South Elsewhere"}, {error_at(21, 9, 0)}, true);
        make(MORNING, 2026, 11, 7, LocationValue{north, {}},
             {error_at(31, 10, 5), error_at(32, 10, 20, "Mute Delay"), error_at(33, 11, 2, "Wrong Track Played"),
              error_at(34, 13, 40, "Speech Delay")},
             true);
        REQUIRE(m_Storage.memory().mark_shared(sample_id<LogId>(MORNING), utc(2026, 11, 7, 21, 5)));
        make(EVENING, 2026, 11, 7, LocationValue{north, {}}, {error_at(41, 16, 0)}, true);
        make(OPEN, 2026, 11, 8, LocationValue{north, {}}, {error_at(51, 9, 0)}, false);

        m_Events.clear();
        m_Storage.repository().clear_records();
        m_World.m_Clock.set_wall(utc(2026, 11, 8, 8, 10)); // 8 Nov 19:10 in Melbourne

        make_model();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void make_model()
    {
        m_History.reset();
        m_Share.reset();
        m_Share =
            std::make_unique<ShareService>(m_World.environment(), m_Open, m_Registry, m_Settings, m_Platform, true);
        m_History = std::make_unique<HistoryModel>(m_World.environment(), m_Gateway, m_Registry, m_Settings, m_Open,
                                                   m_Clipboard, *m_Share);
    }

    /// Runs every job and completion until the gateway is idle.
    void settle()
    {
        m_Gateway.run_all();
    }

    /// `refresh` and let it arrive.
    void load()
    {
        m_History->refresh();
        settle();
    }

    [[nodiscard]] Configuration configuration() const
    {
        return m_Settings.configuration();
    }

    /// Makes log `number` (made by the constructor) the open log of the app: the document the history watches.
    LogDocument& open_document(std::uint64_t number)
    {
        auto loaded = m_Storage.memory().load_log(sample_id<LogId>(number));
        REQUIRE(loaded.has_value());
        m_OpenDocument = m_Registry.make_document(std::move(*loaded));
        m_Open.set(m_OpenDocument.get());
        return *m_OpenDocument;
    }

    /// The open log, replaced by a new log the way a new day does it: the store archives it and starts the next.
    void roll_over(std::uint64_t new_number)
    {
        const LogId old_id = m_OpenDocument->id();
        HeaderValues header = sample_header();
        header.m_LocalDate = std::chrono::year{2026} / 11 / 9;
        REQUIRE(m_Storage.memory().start_new_log(old_id, sample_id<LogId>(new_number), header));
        auto loaded = m_Storage.memory().load_log(sample_id<LogId>(new_number));
        REQUIRE(loaded.has_value());
        m_OpenDocument->replace(std::move(*loaded));
    }

    HistoryModel& history()
    {
        return *m_History;
    }
    ShareService& share()
    {
        return *m_Share;
    }
    TestEnvironment& world()
    {
        return m_World;
    }
    MemoryStorage& storage()
    {
        return m_Storage;
    }
    ManualStorageGateway& gateway()
    {
        return m_Gateway;
    }
    SettingsModel& settings()
    {
        return m_Settings;
    }
    FakePlatform& platform()
    {
        return m_Platform;
    }
    DocumentRegistry& registry()
    {
        return m_Registry;
    }

    /// The stored shared time of a log.
    [[nodiscard]] std::optional<UtcMillis> stored_share(std::uint64_t log_number)
    {
        const auto stored = m_Storage.memory().load_log(sample_id<LogId>(log_number));
        REQUIRE(stored.has_value());
        return stored->m_Meta.m_LastSharedUtc;
    }

    [[nodiscard]] bool stored(std::uint64_t log_number)
    {
        return m_Storage.memory().load_log(sample_id<LogId>(log_number)).has_value();
    }

private:
    void make(std::uint64_t number, int year, unsigned month, unsigned day, LocationValue location,
              std::initializer_list<Entry> entries, bool archive)
    {
        HeaderValues header = sample_header();
        header.m_LocalDate = std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day};
        header.m_Location = std::move(location);
        const LogId id = sample_id<LogId>(number);
        REQUIRE(m_Storage.memory().create_log(id, header));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        if (archive)
        {
            REQUIRE(m_Storage.memory().archive_log(id));
        }
        m_World.m_Clock.advance(1min);
    }

    TestEnvironment m_World;
    EventLog m_Events;
    MemoryStorage m_Storage{m_World.m_Clock, &m_Events};
    ManualStorageGateway m_Gateway{m_Storage.context()};
    SettingsModel m_Settings{m_Gateway, m_World.environment()};
    FakeClipboard m_Clipboard{&m_Events};
    FakePlatform m_Platform{&m_Events};
    FakeOpenLogSource m_Open;
    DocumentRegistry m_Registry{m_World.environment(), m_Gateway};
    std::shared_ptr<LogDocument> m_OpenDocument; // outlives the models that watch it
    std::unique_ptr<ShareService> m_Share;
    std::unique_ptr<HistoryModel> m_History;
};

} // namespace

// -------------------------------------------------------------------------------------------------------- the rows

TEST_CASE("Archived logs are listed newest first, grouped by date, and the open log is not listed",
          "[viewmodel][history][unit]")
{
    Fixture fixture;
    HistoryModel& history = fixture.history();
    CHECK(history.rowCount() == 0); // nothing is read until refresh

    fixture.load();

    REQUIRE(history.rowCount() == 4);
    CHECK(log_id(history, 0) == text_of(EVENING));
    CHECK(log_id(history, 1) == text_of(MORNING));
    CHECK(log_id(history, 2) == text_of(YESTERDAY));
    CHECK(log_id(history, 3) == text_of(LAST_YEAR));
    CHECK(row_of(history, OPEN) == -1);

    // Two logs of one date sit under one heading, next to each other.
    CHECK(section(history, 0) == "7 Nov 2026");
    CHECK(section(history, 1) == "7 Nov 2026");
    CHECK(section(history, 2) == "6 Nov 2026");
    CHECK(section(history, 3) == "7 Nov 2025");
}

TEST_CASE("A row has the date, the unit, the location, the count and the shared state of its log",
          "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();

    const int morning = row_of(history, MORNING);
    REQUIRE(morning >= 0);
    CHECK(cell(history, morning, HistoryModel::DATE_TEXT).toString() == "Sat 7 Nov");
    CHECK(cell(history, morning, Qt::DisplayRole).toString() == "Sat 7 Nov");
    CHECK(cell(history, morning, HistoryModel::UNIT_TEXT).toString() == "UNIT-27");
    CHECK(cell(history, morning, HistoryModel::LOCATION_TEXT).toString() == "North Site");
    CHECK(cell(history, morning, HistoryModel::ENTRY_COUNT).toInt() == 4);
    CHECK(cell(history, morning, HistoryModel::SHARED).toBool());
    // 21:05 UTC on 7 Nov is 08:05 on 8 Nov in Melbourne.
    CHECK(cell(history, morning, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");

    const int evening = row_of(history, EVENING);
    CHECK_FALSE(cell(history, evening, HistoryModel::SHARED).toBool());
    CHECK(cell(history, evening, HistoryModel::SHARED_AT_TEXT).toString().isEmpty());
    CHECK(cell(history, evening, HistoryModel::ENTRY_COUNT).toInt() == 1);

    // A location that was typed in, not picked, is shown as typed.
    CHECK(cell(history, row_of(history, YESTERDAY), HistoryModel::LOCATION_TEXT).toString() == "South Elsewhere");
    CHECK(cell(history, row_of(history, LAST_YEAR), HistoryModel::ENTRY_COUNT).toInt() == 2);
    CHECK(cell(history, row_of(history, LAST_YEAR), HistoryModel::DATE_TEXT).toString() == "Fri 7 Nov");
}

TEST_CASE("The rows count live entries only", "[viewmodel][history][unit]")
{
    Fixture fixture;
    const LogId evening = sample_id<LogId>(EVENING);
    REQUIRE(fixture.storage().memory().set_entry_deleted(evening, sample_id<EntryId>(41), true));

    fixture.load();

    CHECK(cell(fixture.history(), row_of(fixture.history(), EVENING), HistoryModel::ENTRY_COUNT).toInt() == 0);
}

TEST_CASE("The roles have the names the screens bind to", "[viewmodel][history][unit]")
{
    Fixture fixture;
    const auto names = fixture.history().roleNames();

    CHECK(names.size() == 14);
    CHECK(names.value(HistoryModel::LOG_ID) == "logId");
    CHECK(names.value(HistoryModel::DATE_TEXT) == "dateText");
    CHECK(names.value(HistoryModel::SECTION) == "section");
    CHECK(names.value(HistoryModel::UNIT_TEXT) == "unitText");
    CHECK(names.value(HistoryModel::LOCATION_TEXT) == "locationText");
    CHECK(names.value(HistoryModel::ENTRY_COUNT) == "entryCount");
    CHECK(names.value(HistoryModel::SHARED) == "shared");
    CHECK(names.value(HistoryModel::SHARED_AT_TEXT) == "sharedAtText");
    CHECK(names.value(HistoryModel::ENTRIES_TEXT) == "entriesText");
    CHECK(names.value(HistoryModel::VERSION_TEXT) == "versionText");
    CHECK(names.value(HistoryModel::DETAIL_TEXT) == "detailText");
    CHECK(names.value(HistoryModel::SHARED_DATE_TEXT) == "sharedDateText");
    CHECK(names.value(HistoryModel::LONG_DATE_TEXT) == "longDateText");
    CHECK(names.value(HistoryModel::CHANGED_SINCE_SHARE) == "changedSinceShare");
}

TEST_CASE("Summaries only: refresh lists the logs and loads none", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();

    CHECK(fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS) == 1);
    CHECK(fixture.storage().repository().call_count(RepositoryOp::LOAD_LOG) == 0);
    CHECK(fixture.registry().live_count() == 0);
    CHECK(fixture.history().rowCount() == 4);
}

// ---------------------------------------------------------------------------------------------------------- refresh

TEST_CASE("Refresh is asynchronous and resets the model once", "[viewmodel][history][unit]")
{
    Fixture fixture;
    HistoryModel& history = fixture.history();
    QSignalSpy resets(&history, &QAbstractItemModel::modelReset);
    QSignalSpy loading(&history, &HistoryModel::loadingChanged);
    CHECK_FALSE(history.loading());

    history.refresh();

    CHECK(history.loading());
    CHECK(history.rowCount() == 0); // the list has not arrived
    CHECK(resets.count() == 0);
    CHECK(loading.count() == 1);

    fixture.settle();

    CHECK_FALSE(history.loading());
    CHECK(history.rowCount() == 4);
    CHECK(resets.count() == 1);
    CHECK(loading.count() == 2);
}

TEST_CASE("A second refresh while one is loading does not duplicate the rows", "[viewmodel][history][unit]")
{
    Fixture fixture;
    HistoryModel& history = fixture.history();
    QSignalSpy resets(&history, &QAbstractItemModel::modelReset);

    history.refresh();
    history.refresh();
    fixture.settle();

    CHECK(history.rowCount() == 4);
    CHECK(resets.count() == 1);
    CHECK(fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS) == 1);

    // Once it has arrived, a refresh reads again and still shows each log once.
    fixture.load();
    CHECK(history.rowCount() == 4);
    CHECK(resets.count() == 2);
}

TEST_CASE("A refresh sees the logs that are new since the last", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    REQUIRE(fixture.history().rowCount() == 4);

    REQUIRE(fixture.storage().memory().archive_log(sample_id<LogId>(OPEN)));
    fixture.load();

    CHECK(fixture.history().rowCount() == 5);
    CHECK(log_id(fixture.history(), 0) == text_of(OPEN));
    CHECK(section(fixture.history(), 0) == "8 Nov 2026");
}

TEST_CASE("A store error leaves the previous list and reports", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    REQUIRE(history.rowCount() == 4);

    QSignalSpy resets(&history, &QAbstractItemModel::modelReset);
    QSignalSpy failed(&history, &HistoryModel::refreshFailed);
    fixture.storage().repository().fail_next(RepositoryOp::LIST_LOGS);

    history.refresh();
    fixture.settle();

    CHECK(history.rowCount() == 4);
    CHECK(log_id(history, 0) == text_of(EVENING));
    CHECK(resets.count() == 0);
    CHECK_FALSE(history.loading());
    REQUIRE(failed.count() == 1);
    CHECK_FALSE(failed.at(0).at(0).toString().isEmpty());

    // The next refresh works again.
    fixture.load();
    CHECK(history.rowCount() == 4);
    CHECK(resets.count() == 1);
}

TEST_CASE("A store error on the first refresh leaves an empty list", "[viewmodel][history][unit]")
{
    Fixture fixture;
    QSignalSpy failed(&fixture.history(), &HistoryModel::refreshFailed);
    fixture.storage().repository().fail_next(RepositoryOp::LIST_LOGS);

    fixture.load();

    CHECK(fixture.history().rowCount() == 0);
    CHECK(failed.count() == 1);
}

TEST_CASE("A model that is gone is not called back", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.history().refresh();
    REQUIRE(fixture.gateway().pending() == 1);

    // Destroy the model with the list still on its way.
    fixture.make_model();
    fixture.settle();

    CHECK(fixture.history().rowCount() == 0); // the new model has not asked
    fixture.load();
    CHECK(fixture.history().rowCount() == 4);
}

// ----------------------------------------------------------------------------------------------------------- open

TEST_CASE("Opening a log binds a timeline with its lines; closing unbinds", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    QSignalSpy changed(&history, &HistoryModel::reviewModelChanged);
    QSignalSpy failed(&history, &HistoryModel::logOpenFailed);
    CHECK(history.review_model() == nullptr);

    history.openLog(text_of(MORNING));

    CHECK(history.review_model() == nullptr); // loading
    fixture.settle();

    REQUIRE(history.review_model() != nullptr);
    CHECK(changed.count() == 1);
    CHECK(failed.count() == 0);
    const TimelineModel& review = *history.review_model();
    REQUIRE(review.rowCount() == 4);
    CHECK(timeline_line(review, 0) == "10:05 T| Wrong Response");
    CHECK(timeline_line(review, 1) == "10:20 T| Mute Delay");
    CHECK(timeline_line(review, 2) == "11:02 T| Wrong Track Played");
    CHECK(timeline_line(review, 3) == "13:40 T| Speech Delay");
    CHECK(review.entry_count() == 4);
    CHECK(review.ready());
    CHECK(review.header_text().startsWith("Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27"));
    CHECK(history.review_document() != nullptr);

    history.closeLog();

    CHECK(history.review_model() == nullptr);
    CHECK(history.review_document() == nullptr);
    CHECK(changed.count() == 2);
    CHECK(review.rowCount() == 0);
    CHECK_FALSE(review.ready());
}

TEST_CASE("Opening an unknown id reports and leaves nothing open", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    QSignalSpy failed(&history, &HistoryModel::logOpenFailed);

    history.openLog(text_of(99));
    fixture.settle();

    CHECK(failed.count() == 1);
    CHECK_FALSE(failed.at(0).at(0).toString().isEmpty());
    CHECK(history.review_model() == nullptr);

    history.openLog("not an id");
    CHECK(failed.count() == 2); // refused at once, nothing is read
    fixture.settle();
    CHECK(failed.count() == 2);
    CHECK(history.review_model() == nullptr);
}

TEST_CASE("The open log is not reviewed from History", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    QSignalSpy failed(&fixture.history(), &HistoryModel::logOpenFailed);

    fixture.history().openLog(text_of(OPEN));
    fixture.settle();

    CHECK(failed.count() == 1);
    CHECK(fixture.history().review_model() == nullptr);
}

TEST_CASE("A failed read of the log reports", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    QSignalSpy failed(&fixture.history(), &HistoryModel::logOpenFailed);
    fixture.storage().repository().fail_next(RepositoryOp::LOAD_LOG);

    fixture.history().openLog(text_of(MORNING));
    fixture.settle();

    CHECK(failed.count() == 1);
    CHECK(fixture.history().review_model() == nullptr);
}

TEST_CASE("Closing while a log is loading drops it, and a later open wins", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();

    history.openLog(text_of(MORNING));
    history.closeLog();
    fixture.settle();
    CHECK(history.review_model() == nullptr);

    history.openLog(text_of(MORNING));
    history.openLog(text_of(EVENING));
    fixture.settle();
    REQUIRE(history.review_model() != nullptr);
    CHECK(history.review_document()->id() == sample_id<LogId>(EVENING));
    CHECK(history.review_model()->rowCount() == 1);
}

TEST_CASE("Opening another log replaces the one that is open", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    REQUIRE(history.review_model() != nullptr);

    history.openLog(text_of(EVENING));
    CHECK(history.review_model() == nullptr);
    fixture.settle();

    REQUIRE(history.review_model() != nullptr);
    CHECK(history.review_model()->rowCount() == 1);
    CHECK(timeline_line(*history.review_model(), 0) == "16:00 T| Wrong Response");
}

TEST_CASE("reviewOpened and reviewClosing bracket the document of the review", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    std::vector<const LogDocument*> opened;
    std::vector<const LogDocument*> closing;
    QObject::connect(&history, &HistoryModel::reviewOpened, [&](LogDocument* document) { opened.push_back(document); });
    QObject::connect(&history, &HistoryModel::reviewClosing,
                     [&](LogDocument* document)
                     {
                         // Still the document of the review, and still alive, so a controller can stop observing it.
                         CHECK(document == history.review_document());
                         closing.push_back(document);
                     });

    history.openLog(text_of(MORNING));
    fixture.settle();
    REQUIRE(opened.size() == 1);
    CHECK(opened[0] == history.review_document());
    CHECK(opened[0]->id() == sample_id<LogId>(MORNING));

    history.openLog(text_of(EVENING)); // closes the first
    fixture.settle();
    REQUIRE(closing.size() == 1);
    CHECK(closing[0] == opened[0]);
    REQUIRE(opened.size() == 2);

    history.closeLog();
    REQUIRE(closing.size() == 2);
    CHECK(closing[1] == opened[1]);
    history.closeLog(); // nothing is open
    CHECK(closing.size() == 2);
}

TEST_CASE("A model destroyed with a log open lets its observers stop first", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    const LogDocument* closing = nullptr;
    QObject::connect(&fixture.history(), &HistoryModel::reviewClosing,
                     [&](LogDocument* document) { closing = document; });
    fixture.history().openLog(text_of(MORNING));
    fixture.settle();
    const LogDocument* opened = fixture.history().review_document();
    REQUIRE(opened != nullptr);

    fixture.make_model(); // destroys the model that has the log open

    CHECK(closing == opened);
    CHECK(fixture.registry().live_count() == 0); // nothing holds the document now
}

// ------------------------------------------------------------------------------------------ editing a past log

TEST_CASE("Editing and deleting an entry of a past log persists and re-renders", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    TimelineModel& review = *history.review_model();
    const int row = row_of(history, MORNING);
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);

    // Delete.
    const QString entry = review.data(review.index(1), TimelineModel::ENTRY_ID).toString();
    REQUIRE(review.deleteEntry(entry));
    fixture.settle();

    CHECK(review.rowCount() == 3);
    CHECK(timeline_line(review, 1) == "11:02 T| Wrong Track Played");
    const auto stored = fixture.storage().memory().load_log(sample_id<LogId>(MORNING));
    REQUIRE(stored.has_value());
    CHECK(stored->m_Log.find(sample_id<EntryId>(32))->m_Deleted);
    // The count of the row follows, in place.
    CHECK(cell(history, row, HistoryModel::ENTRY_COUNT).toInt() == 3);
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(0).value<QModelIndex>().row() == row);
    CHECK(changed.at(0).at(2).value<QList<int>>() == QList<int>{HistoryModel::ENTRY_COUNT, HistoryModel::SHARED,
                                                                HistoryModel::CHANGED_SINCE_SHARE,
                                                                HistoryModel::ENTRIES_TEXT, HistoryModel::DETAIL_TEXT});

    // Edit: the same entry types, a new attached note.
    const QString first = review.data(review.index(0), TimelineModel::ENTRY_ID).toString();
    QVariantMap edit = review.entryForEdit(first);
    REQUIRE_FALSE(edit.isEmpty());
    edit.insert("attachedNote", "asked for Evening Song");
    REQUIRE(review.saveEntry(first, edit));
    fixture.settle();

    CHECK(timeline_line(review, 0) == "10:05 T| Wrong Response \xE2\x80\x93 asked for Evening Song");
    const auto edited = fixture.storage().memory().load_log(sample_id<LogId>(MORNING));
    REQUIRE(edited.has_value());
    const Entry* stored_entry = edited->m_Log.find(sample_id<EntryId>(31));
    REQUIRE(stored_entry != nullptr);
    CHECK(std::get<ErrorEntry>(stored_entry->m_Body).m_AttachedNote == "asked for Evening Song");
    CHECK(changed.count() == 1); // an edit does not change the row
}

TEST_CASE("The undo toast works on a past log", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    UndoController undo(fixture.world().environment(), fixture.settings());
    QObject::connect(&history, &HistoryModel::reviewOpened, [&](LogDocument* document) { undo.observe(*document); });
    QObject::connect(&history, &HistoryModel::reviewClosing,
                     [&](LogDocument* document) { undo.stop_observing(*document); });

    history.openLog(text_of(MORNING));
    fixture.settle();
    TimelineModel& review = *history.review_model();
    const int row = row_of(history, MORNING);
    const QString entry = review.data(review.index(2), TimelineModel::ENTRY_ID).toString();

    REQUIRE(review.deleteEntry(entry));
    fixture.settle();
    CHECK(undo.active());
    CHECK(undo.text() == "Entry deleted");
    CHECK(review.rowCount() == 3);
    CHECK(cell(history, row, HistoryModel::ENTRY_COUNT).toInt() == 3);

    REQUIRE(undo.undo());
    fixture.settle();

    CHECK_FALSE(undo.active());
    CHECK(review.rowCount() == 4);
    CHECK(timeline_line(review, 2) == "11:02 T| Wrong Track Played");
    CHECK(cell(history, row, HistoryModel::ENTRY_COUNT).toInt() == 4);
    const auto stored = fixture.storage().memory().load_log(sample_id<LogId>(MORNING));
    REQUIRE(stored.has_value());
    CHECK_FALSE(stored->m_Log.find(sample_id<EntryId>(33))->m_Deleted);

    history.closeLog(); // the controller stops observing before the document goes
}

TEST_CASE("The row follows a correction of the header of the log under review", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    const int row = row_of(history, MORNING);
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);

    REQUIRE(history.review_document()->correct_header(HeaderField::UNIT_NUMBER, "28", {}));
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::UNIT_TEXT).toString() == "UNIT-28");
    REQUIRE(changed.count() == 1);
    // The log was shared, so the correction also makes it changed since the share.
    CHECK(changed.at(0).at(2).value<QList<int>>() ==
          QList<int>{HistoryModel::UNIT_TEXT, HistoryModel::SHARED, HistoryModel::CHANGED_SINCE_SHARE});
}

TEST_CASE("The row shows the environment of the unit, and follows a correction of it",
          "[viewmodel][history][unit][environment]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    const int row = row_of(history, MORNING);

    REQUIRE(history.review_document()->correct_header(HeaderField::UNIT_NUMBER, "27-prod", {}));
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::UNIT_TEXT).toString() == "UNIT-27-prod");

    REQUIRE(history.review_document()->correct_header(HeaderField::UNIT_NUMBER, "27", {}));
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::UNIT_TEXT).toString() == "UNIT-27");
    history.closeLog();
}

// ---------------------------------------------------------------------------------------------------------- delete

TEST_CASE("The open log gets no delete token, nor does an id that is not listed", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();

    CHECK(history.requestDelete(text_of(OPEN)).isEmpty());
    CHECK(history.requestDelete(text_of(99)).isEmpty());
    CHECK(history.requestDelete("not an id").isEmpty());
    CHECK(history.requestDelete("").isEmpty());
    CHECK_FALSE(history.requestDelete(text_of(MORNING)).isEmpty());
    CHECK(fixture.storage().repository().call_count(RepositoryOp::HARD_DELETE_LOG) == 0);
}

TEST_CASE("Nothing is listed, so nothing can be deleted, before the first refresh", "[viewmodel][history][unit]")
{
    Fixture fixture;

    CHECK(fixture.history().requestDelete(text_of(MORNING)).isEmpty());
}

TEST_CASE("Deleting a log needs a request and then a confirmation", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    QSignalSpy finished(&history, &HistoryModel::deleteFinished);
    QSignalSpy removed(&history, &QAbstractItemModel::rowsRemoved);

    // A guess is not a token; neither is a log id.
    CHECK_FALSE(history.confirmDelete(text_of(MORNING)));
    CHECK_FALSE(history.confirmDelete("whatever"));
    CHECK_FALSE(history.confirmDelete(""));
    CHECK(fixture.gateway().pending() == 0);

    const QString token = history.requestDelete(text_of(MORNING));
    REQUIRE_FALSE(token.isEmpty());
    CHECK(fixture.storage().repository().call_count(RepositoryOp::HARD_DELETE_LOG) == 0); // asking deletes nothing
    CHECK(history.rowCount() == 4);

    CHECK(history.confirmDelete(token));
    // Queued, not done: the row stays until the store has deleted the log.
    CHECK(history.rowCount() == 4);
    CHECK(fixture.stored(MORNING));
    CHECK(finished.count() == 0);

    fixture.settle();

    CHECK(history.rowCount() == 3);
    CHECK(row_of(history, MORNING) == -1);
    CHECK(removed.count() == 1);
    REQUIRE(finished.count() == 1);
    CHECK(finished.at(0).at(0).toString() == text_of(MORNING));
    CHECK(finished.at(0).at(1).toBool());
    CHECK(fixture.storage().repository().call_count(RepositoryOp::HARD_DELETE_LOG) == 1);
    CHECK_FALSE(fixture.stored(MORNING));
    // The other logs are untouched.
    CHECK(fixture.stored(EVENING));
    CHECK(fixture.stored(YESTERDAY));
    CHECK(fixture.stored(LAST_YEAR));
    CHECK(fixture.stored(OPEN));

    // A refresh agrees: the store no longer lists it.
    fixture.load();
    CHECK(history.rowCount() == 3);
    CHECK(row_of(history, MORNING) == -1);
}

TEST_CASE("A token is used once", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    const QString token = history.requestDelete(text_of(MORNING));
    const QString another = history.requestDelete(text_of(MORNING));
    REQUIRE_FALSE(token.isEmpty());
    CHECK(token != another);

    REQUIRE(history.confirmDelete(token));
    CHECK_FALSE(history.confirmDelete(token));
    CHECK_FALSE(history.confirmDelete(another)); // the log is on its way out; the second token went with the first
    fixture.settle();

    CHECK_FALSE(history.confirmDelete(token));
    CHECK_FALSE(history.confirmDelete(another));
    CHECK(fixture.storage().repository().call_count(RepositoryOp::HARD_DELETE_LOG) == 1);
}

TEST_CASE("A token for one log does not delete another", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    const QString token = history.requestDelete(text_of(YESTERDAY));

    REQUIRE(history.confirmDelete(token));
    fixture.settle();

    CHECK(row_of(history, YESTERDAY) == -1);
    CHECK(history.rowCount() == 3);
    CHECK(fixture.stored(MORNING));
    CHECK(fixture.stored(LAST_YEAR));
}

TEST_CASE("A token expires when History is left", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();

    SECTION("closeLog")
    {
        const QString token = history.requestDelete(text_of(MORNING));
        history.closeLog();
        CHECK_FALSE(history.confirmDelete(token));
    }
    SECTION("leave")
    {
        const QString token = history.requestDelete(text_of(MORNING));
        history.leave();
        CHECK_FALSE(history.confirmDelete(token));
    }
    SECTION("a token from before is not valid for a later request")
    {
        const QString old_token = history.requestDelete(text_of(MORNING));
        history.leave();
        const QString token = history.requestDelete(text_of(MORNING));
        REQUIRE_FALSE(token.isEmpty());
        CHECK_FALSE(history.confirmDelete(old_token));
        CHECK(history.confirmDelete(token));
        fixture.settle();
        CHECK_FALSE(fixture.stored(MORNING));
        return;
    }

    fixture.settle();
    CHECK(history.rowCount() == 4);
    CHECK(fixture.storage().repository().call_count(RepositoryOp::HARD_DELETE_LOG) == 0);
    CHECK(fixture.stored(MORNING));
}

TEST_CASE("A failed delete keeps the row and the log", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    QSignalSpy finished(&history, &HistoryModel::deleteFinished);
    fixture.storage().repository().fail_next(RepositoryOp::HARD_DELETE_LOG);

    REQUIRE(history.confirmDelete(history.requestDelete(text_of(MORNING))));
    fixture.settle();

    CHECK(history.rowCount() == 4);
    CHECK(row_of(history, MORNING) >= 0);
    CHECK(fixture.stored(MORNING));
    REQUIRE(finished.count() == 1);
    CHECK(finished.at(0).at(0).toString() == text_of(MORNING));
    CHECK_FALSE(finished.at(0).at(1).toBool());

    // Asking again works.
    REQUIRE(history.confirmDelete(history.requestDelete(text_of(MORNING))));
    fixture.settle();
    CHECK(row_of(history, MORNING) == -1);
    CHECK_FALSE(fixture.stored(MORNING));
}

TEST_CASE("Deleting the log that is open for review closes the review first", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    REQUIRE(history.review_model() != nullptr);
    int closing = 0;
    QObject::connect(&history, &HistoryModel::reviewClosing, [&](LogDocument*) { ++closing; });
    const QString token = history.requestDelete(text_of(MORNING));
    REQUIRE_FALSE(token.isEmpty());

    REQUIRE(history.confirmDelete(token));

    CHECK(history.review_model() == nullptr);
    CHECK(closing == 1);
    fixture.settle();
    CHECK(row_of(history, MORNING) == -1);
    CHECK_FALSE(fixture.stored(MORNING));
}

TEST_CASE("Deleting one log leaves the review of another open", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(EVENING));
    fixture.settle();

    REQUIRE(history.confirmDelete(history.requestDelete(text_of(MORNING))));
    fixture.settle();

    REQUIRE(history.review_model() != nullptr);
    CHECK(history.review_model()->rowCount() == 1);
}

// ---------------------------------------------------------------------------------------------------------- sharing

TEST_CASE("Sharing a past log marks it shared and the row updates in place with one dataChanged",
          "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    const int row = row_of(history, EVENING);
    REQUIRE_FALSE(cell(history, row, HistoryModel::SHARED).toBool());
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);
    QSignalSpy resets(&history, &QAbstractItemModel::modelReset);
    QSignalSpy recorded(&fixture.share(), &ShareService::shareRecorded);
    fixture.world().m_Clock.set_wall(utc(2026, 11, 7, 21, 5)); // 08:05 on 8 Nov in Melbourne

    REQUIRE(fixture.share().shareLog(text_of(EVENING)));
    fixture.settle();

    REQUIRE(recorded.count() == 1);
    CHECK(fixture.stored_share(EVENING) == utc(2026, 11, 7, 21, 5));
    CHECK(cell(history, row, HistoryModel::SHARED).toBool());
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(0).value<QModelIndex>().row() == row);
    CHECK(changed.at(0).at(1).value<QModelIndex>().row() == row);
    const auto roles = changed.at(0).at(2).value<QList<int>>();
    CHECK(roles.contains(HistoryModel::SHARED));
    CHECK(roles.contains(HistoryModel::SHARED_AT_TEXT));
    CHECK(resets.count() == 0); // no reload, no reorder
    CHECK(log_id(history, row) == text_of(EVENING));
    // The other rows did not move or change.
    CHECK_FALSE(cell(history, row_of(history, YESTERDAY), HistoryModel::SHARED).toBool());
}

// ------------------------------------------------------------------------------------- changed since the last share

TEST_CASE("A log that changed after its share is not shown as shared", "[viewmodel][history][unit][share]")
{
    Fixture fixture;
    // The morning log was shared and then had an entry added, one edited, and one deleted in Review (the store keeps
    // past logs editable). The evening log was never shared.
    const LogId morning = sample_id<LogId>(MORNING);
    SECTION("an entry edited after the share")
    {
        REQUIRE(fixture.storage().memory().edit_entry(morning, error_at(31, 10, 6), utc(2026, 11, 8, 1, 0)));
    }
    SECTION("an entry deleted after the share")
    {
        REQUIRE(fixture.storage().memory().set_entry_deleted(morning, sample_id<EntryId>(32), true));
    }
    SECTION("a header corrected after the share")
    {
        REQUIRE(fixture.storage().memory().correct_header(morning, HeaderField::UNIT_NUMBER, "31"));
    }
    fixture.load();
    HistoryModel& history = fixture.history();

    const int row = row_of(history, MORNING);
    CHECK_FALSE(cell(history, row, HistoryModel::SHARED).toBool()); // the ring, as a log never shared has
    CHECK(cell(history, row, HistoryModel::CHANGED_SINCE_SHARE).toBool());
    // The time of the last share is still said: it is when the log last left the phone.
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");
    CHECK_FALSE(cell(history, row, HistoryModel::SHARED_DATE_TEXT).toString().isEmpty());

    // The other rows are as they were: never shared, and (no share, no change) not changed since a share.
    CHECK_FALSE(cell(history, row_of(history, EVENING), HistoryModel::SHARED).toBool());
    CHECK_FALSE(cell(history, row_of(history, EVENING), HistoryModel::CHANGED_SINCE_SHARE).toBool());
}

TEST_CASE("A log that was shared and not changed since is shown as shared", "[viewmodel][history][unit][share]")
{
    Fixture fixture;
    fixture.load();
    const int row = row_of(fixture.history(), MORNING);
    CHECK(cell(fixture.history(), row, HistoryModel::SHARED).toBool());
    CHECK_FALSE(cell(fixture.history(), row, HistoryModel::CHANGED_SINCE_SHARE).toBool());
}

TEST_CASE("An edit made in Review changes the row at once, and sharing again puts the check back",
          "[viewmodel][history][unit][share]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    const int row = row_of(history, MORNING);
    REQUIRE(cell(history, row, HistoryModel::SHARED).toBool());
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);

    REQUIRE(history.review_document()->set_deleted(sample_id<EntryId>(32), true, LogChange::Origin::USER, {}));
    fixture.settle();

    CHECK_FALSE(cell(history, row, HistoryModel::SHARED).toBool());
    CHECK(cell(history, row, HistoryModel::CHANGED_SINCE_SHARE).toBool());
    REQUIRE(changed.count() >= 1);
    const auto roles = changed.at(changed.count() - 1).at(2).value<QList<int>>();
    CHECK(roles.contains(HistoryModel::SHARED));
    CHECK(roles.contains(HistoryModel::CHANGED_SINCE_SHARE));

    fixture.world().m_Clock.set_wall(utc(2026, 11, 8, 3, 30)); // 14:30 on 8 Nov in Melbourne
    REQUIRE(fixture.share().shareLog(text_of(MORNING)));
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::SHARED).toBool());
    CHECK_FALSE(cell(history, row, HistoryModel::CHANGED_SINCE_SHARE).toBool());
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "14:30");
}

TEST_CASE("Copying a past log does not share it", "[viewmodel][history][unit][share]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(EVENING));
    fixture.settle();
    const int row = row_of(history, EVENING);

    REQUIRE(history.review_model()->copyAll());
    REQUIRE(history.review_model()->copyLine(0));
    fixture.settle();

    CHECK_FALSE(cell(history, row, HistoryModel::SHARED).toBool());
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString().isEmpty());
    CHECK(fixture.stored_share(EVENING) == std::nullopt);
}

TEST_CASE("Sharing again moves the shared time of a row that was shared", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    const int row = row_of(history, MORNING);
    REQUIRE(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);
    fixture.world().m_Clock.set_wall(utc(2026, 11, 8, 3, 30)); // 14:30 on 8 Nov in Melbourne

    REQUIRE(fixture.share().shareLog(text_of(MORNING)));
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "14:30");
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(2).value<QList<int>>().contains(HistoryModel::SHARED_AT_TEXT));
}

TEST_CASE("A share that is not recorded changes no row", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);
    fixture.platform().answer_with(FakePlatform::share_outcome::CANCELLED);

    REQUIRE(fixture.share().shareLog(text_of(EVENING)));
    fixture.settle();

    CHECK(changed.count() == 0);
    CHECK_FALSE(cell(history, row_of(history, EVENING), HistoryModel::SHARED).toBool());
}

TEST_CASE("Sharing the log that is open for review updates its row once", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(EVENING));
    fixture.settle();
    const int row = row_of(history, EVENING);
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);
    fixture.world().m_Clock.set_wall(utc(2026, 11, 7, 21, 5));

    REQUIRE(fixture.share().shareLog(text_of(EVENING)));
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::SHARED).toBool());
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");
    CHECK(changed.count() == 1); // the document's SHARED change and shareRecorded say the same thing
    CHECK(history.review_document()->last_shared() == utc(2026, 11, 7, 21, 5));
}

TEST_CASE("A share of a log that is not listed changes no row", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    QSignalSpy changed(&fixture.history(), &QAbstractItemModel::dataChanged);

    REQUIRE(fixture.share().shareLog(text_of(99))); // accepted, and ends as "failed": the store has no such log
    fixture.settle();

    CHECK(changed.count() == 0);
    CHECK(fixture.history().rowCount() == 4);
}

// ---------------------------------------------------------------------------------- display follows the preferences

TEST_CASE("The strings follow the time format, for logs written before it was changed (PRF-07)",
          "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    const int row = row_of(history, MORNING);
    REQUIRE(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);
    QSignalSpy resets(&history, &QAbstractItemModel::modelReset);

    CHECK(fixture.settings().setTimeFormat("12h", false, "upper").isEmpty());
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05"); // the save has not committed
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05 AM");
    REQUIRE(changed.count() == 1); // one dataChanged over the rows
    CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
    CHECK(changed.at(0).at(1).value<QModelIndex>().row() == history.rowCount() - 1);
    CHECK(changed.at(0).at(2).value<QList<int>>() == QList<int>{HistoryModel::SHARED_AT_TEXT});
    CHECK(resets.count() == 0);
    // The row says what the settings model says, which is what the screens use for every other time.
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() ==
          fixture.settings().formatTime(utc(2026, 11, 7, 21, 5).time_since_epoch().count(), "Australia/Melbourne"));

    CHECK(fixture.settings().setTimeFormat("12h", true, "lower").isEmpty());
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05:00 am");

    CHECK(fixture.settings().setTimeFormat("24h", false, "upper").isEmpty());
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");
    CHECK(changed.count() == 3);
}

TEST_CASE("A change that alters no row emits nothing", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    QSignalSpy changed(&fixture.history(), &QAbstractItemModel::dataChanged);

    CHECK(fixture.settings().setTester("Pat Example").isEmpty());
    fixture.settle();

    CHECK(changed.count() == 0);
}

TEST_CASE("The unit prefix and the location names follow the configuration", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    const int row = row_of(history, MORNING);
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);

    CHECK(fixture.settings().setUnitPrefix("BOX").isEmpty());
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::UNIT_TEXT).toString() == "BOX-27");
    CHECK(changed.count() == 1);

    const LocationId north = location_named(fixture.configuration(), "North Site");
    CHECK(fixture.settings().renameLocation(QString::fromStdString(north.format()), "North Site Annex").isEmpty());
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::LOCATION_TEXT).toString() == "North Site Annex");
    CHECK(cell(history, row_of(history, YESTERDAY), HistoryModel::LOCATION_TEXT).toString() == "South Elsewhere");
    CHECK(changed.count() == 2);
}

TEST_CASE("A retired location still has its name on a past log", "[viewmodel][history][unit]")
{
    Fixture fixture;
    const LocationId north = location_named(fixture.configuration(), "North Site");
    CHECK(fixture.settings().removeLocation(QString::fromStdString(north.format())).isEmpty());
    fixture.settle();

    fixture.load();

    CHECK(cell(fixture.history(), row_of(fixture.history(), MORNING), HistoryModel::LOCATION_TEXT).toString() ==
          "North Site");
}

TEST_CASE("The shared time is shown where the device is", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.world().m_DeviceZone.set("Australia/Perth");

    fixture.load();

    // 21:05 UTC is 05:05 in Perth (+08:00).
    CHECK(cell(fixture.history(), row_of(fixture.history(), MORNING), HistoryModel::SHARED_AT_TEXT).toString() ==
          "05:05");
}

TEST_CASE("With no configuration the rows still list", "[viewmodel][history][unit]")
{
    Fixture fixture(false);

    fixture.load();

    REQUIRE(fixture.history().rowCount() == 4);
    const int row = row_of(fixture.history(), MORNING);
    CHECK(cell(fixture.history(), row, HistoryModel::SHARED_AT_TEXT).toString() == "08:05"); // the default format
    CHECK(cell(fixture.history(), row, HistoryModel::LOCATION_TEXT).toString().isEmpty());   // the name is unknown
    CHECK(section(fixture.history(), row) == "7 Nov 2026");
}

// ------------------------------------------------------------------------------------------------------ a new day

TEST_CASE("When the open log rolls over, the day that was archived appears in the list", "[viewmodel][history][unit]")
{
    Fixture fixture;
    LogDocument& open = fixture.open_document(OPEN);
    fixture.load();
    HistoryModel& history = fixture.history();
    REQUIRE(history.rowCount() == 4);
    QSignalSpy resets(&history, &QAbstractItemModel::modelReset);
    const std::size_t reads = fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS);

    fixture.roll_over(6); // the same document, now holding the next day's log (REPLACED)

    CHECK(&open == fixture.registry().find(sample_id<LogId>(6)).get()); // no new document pointer
    CHECK(fixture.gateway().pending() == 1);
    fixture.settle();

    REQUIRE(history.rowCount() == 5);
    CHECK(log_id(history, 0) == text_of(OPEN));
    CHECK(section(history, 0) == "8 Nov 2026");
    CHECK(cell(history, 0, HistoryModel::ENTRY_COUNT).toInt() == 1);
    CHECK(row_of(history, 6) == -1); // the new day's log is the open one
    CHECK(resets.count() == 1);
    CHECK(fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS) == reads + 1);
}

TEST_CASE("A roll-over during a read is followed by one more read", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.open_document(OPEN);
    HistoryModel& history = fixture.history();

    history.refresh(); // under way
    fixture.roll_over(6);
    CHECK(fixture.gateway().pending() == 1); // no second read yet
    fixture.settle();

    CHECK(fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS) == 2);
    CHECK(history.rowCount() == 5);
    CHECK(log_id(history, 0) == text_of(OPEN));
    CHECK_FALSE(history.loading());
}

TEST_CASE("The first load of the open log and edits to it do not read the list", "[viewmodel][history][unit]")
{
    Fixture fixture;
    LogDocument& open = fixture.open_document(OPEN);
    fixture.load();
    const std::size_t reads = fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS);

    REQUIRE(open.add_entry(error_at(52, 9, 30), LogChange::Origin::USER, {}));
    fixture.settle();

    CHECK(fixture.storage().repository().call_count(RepositoryOp::LIST_LOGS) == reads);
    CHECK(fixture.history().rowCount() == 4);
}

TEST_CASE("A model that is gone stops watching the open log", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.open_document(OPEN);
    fixture.make_model(); // a new model over the same open log; the old one is destroyed

    fixture.roll_over(6); // would call a destroyed model if it were still listening
    fixture.settle();

    CHECK(fixture.history().rowCount() == 5);
}

// ----------------------------------------------------------------------------------- what the visual pass binds to

TEST_CASE("A row reads as the designs do: entries, version, the two joined, and the long date",
          "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();

    const int morning = row_of(history, MORNING);
    CHECK(cell(history, morning, HistoryModel::ENTRIES_TEXT).toString() == "4 entries");
    CHECK(cell(history, morning, HistoryModel::VERSION_TEXT).toString() == "v1.4.2");
    CHECK(cell(history, morning, HistoryModel::DETAIL_TEXT).toString() == "4 entries \xC2\xB7 v1.4.2");
    CHECK(cell(history, morning, HistoryModel::LONG_DATE_TEXT).toString() == "Sat 7 Nov 2026");

    const int evening = row_of(history, EVENING);
    CHECK(cell(history, evening, HistoryModel::ENTRIES_TEXT).toString() == "1 entry");
    CHECK(cell(history, evening, HistoryModel::DETAIL_TEXT).toString() == "1 entry \xC2\xB7 v1.4.2");

    CHECK(cell(history, row_of(history, LAST_YEAR), HistoryModel::LONG_DATE_TEXT).toString() == "Fri 7 Nov 2025");
    CHECK(cell(history, row_of(history, LAST_YEAR), HistoryModel::ENTRIES_TEXT).toString() == "2 entries");
}

TEST_CASE("The shared date is the day of the share, not the date of the log", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();

    // The morning log is of 7 Nov and was shared at 21:05 UTC, which is 8 Nov in Melbourne.
    const int morning = row_of(history, MORNING);
    CHECK(cell(history, morning, HistoryModel::SECTION).toString() == "7 Nov 2026");
    CHECK(cell(history, morning, HistoryModel::SHARED_DATE_TEXT).toString() == "8 Nov");
    CHECK(cell(history, morning, HistoryModel::SHARED_AT_TEXT).toString() == "08:05");

    // A log that was never shared has no date to show.
    CHECK(cell(history, row_of(history, EVENING), HistoryModel::SHARED_DATE_TEXT).toString().isEmpty());
}

TEST_CASE("The shared date is where the device is, and follows a new share", "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.world().m_DeviceZone.set("Australia/Perth");
    fixture.load();
    HistoryModel& history = fixture.history();
    const int row = row_of(history, EVENING);
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);

    // 15:30 UTC on 7 Nov is 23:30 on 7 Nov in Perth (+08:00).
    fixture.world().m_Clock.set_wall(utc(2026, 11, 7, 15, 30));
    REQUIRE(fixture.share().shareLog(text_of(EVENING)));
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::SHARED_DATE_TEXT).toString() == "7 Nov");
    CHECK(cell(history, row, HistoryModel::SHARED_AT_TEXT).toString() == "23:30");
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(2).value<QList<int>>().contains(HistoryModel::SHARED_DATE_TEXT));

    // 17:00 UTC is 01:00 on 8 Nov in Perth: sharing again moves the day with the time.
    fixture.world().m_Clock.set_wall(utc(2026, 11, 7, 17, 0));
    REQUIRE(fixture.share().shareLog(text_of(EVENING)));
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::SHARED_DATE_TEXT).toString() == "8 Nov");
}

TEST_CASE("A log with no version reads as its entries alone, and a correction moves the row",
          "[viewmodel][history][unit]")
{
    Fixture fixture;
    fixture.load();
    HistoryModel& history = fixture.history();
    history.openLog(text_of(MORNING));
    fixture.settle();
    const int row = row_of(history, MORNING);
    QSignalSpy changed(&history, &QAbstractItemModel::dataChanged);

    REQUIRE(history.review_document()->correct_header(HeaderField::SOFTWARE_VERSION, "", {}));
    fixture.settle();

    CHECK(cell(history, row, HistoryModel::VERSION_TEXT).toString().isEmpty());
    CHECK(cell(history, row, HistoryModel::DETAIL_TEXT).toString() == "4 entries");
    REQUIRE(changed.count() == 1);
    const auto roles = changed.at(0).at(2).value<QList<int>>();
    // The morning log was shared: a correction made in Review also makes it changed since the share.
    CHECK(roles == QList<int>{HistoryModel::SHARED, HistoryModel::CHANGED_SINCE_SHARE, HistoryModel::VERSION_TEXT,
                              HistoryModel::DETAIL_TEXT});

    REQUIRE(history.review_document()->correct_header(HeaderField::SOFTWARE_VERSION, "1.5.0", {}));
    fixture.settle();
    CHECK(cell(history, row, HistoryModel::DETAIL_TEXT).toString() == "4 entries \xC2\xB7 v1.5.0");
}
