// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/time_zone_provider.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_platform.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/day_watcher.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/share_service.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace std::chrono_literals;
using viewmodel::DayWatcher;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::LogSession;
using viewmodel::ShareService;
using viewmodel::StartupResult;

namespace
{

const QString WRONG_RESPONSE = QStringLiteral("00000000-0000-4000-8000-0000000000a1");

constexpr auto MELBOURNE = "Australia/Melbourne";
constexpr auto PERTH = "Australia/Perth";

/// A local time in Melbourne on `day` of November 2026 (+11:00 on every day of the tests that use it).
UtcMillis melbourne(unsigned day, int hour, int minute, int second = 0, int millisecond = 0)
{
    return utc(2026, 11, day, 0, 0) + std::chrono::hours{hour - 11} + std::chrono::minutes{minute} +
           std::chrono::seconds{second} + std::chrono::milliseconds{millisecond};
}

HeaderValues header_on(int year, unsigned month, unsigned day)
{
    HeaderValues header = sample_header();
    header.m_LocalDate = std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day};
    return header;
}

struct Options
{
    bool m_ExistingLog = true;
    bool m_Load = true;
    bool m_QtZones = false;
    HeaderValues m_Header = sample_header();  // Saturday 7 November 2026

    // Chained setters, because a designated initializer that leaves a field out is a warning for Clang.
    Options& no_load()
    {
        m_Load = false;
        return *this;
    }
    Options& header(HeaderValues header)
    {
        m_Header = std::move(header);
        return *this;
    }
    Options& qt_zones()
    {
        m_QtZones = true;
        return *this;
    }
};

std::unique_ptr<TimeZoneProvider> make_zones(const Options& options)
{
    return options.m_QtZones ? make_qt_time_zone_provider() : nullptr;
}

/// The session, the share service and the watcher over the in-memory store and the manual gateway, with the sample
/// configuration. The log in the store is dated Saturday 7 November 2026 unless the options say otherwise, the clock
/// starts at 08:55 that day in Melbourne (+11:00) and the device is in Melbourne.
class WatcherRig
{
public:
    explicit WatcherRig(Options options = {})
        : m_Zones(make_zones(options)), m_Env(environment_with(world, m_Zones.get())), storage(world.m_Clock, &events),
          registry(m_Env, gateway)
    {
        if (options.m_ExistingLog)
        {
            REQUIRE(storage.memory().create_log(log_id, options.m_Header));
        }
        config.set(sample_configuration());
        session = std::make_unique<LogSession>(m_Env, gateway, registry, config, feedback, StartupResult{});
        share = std::make_unique<ShareService>(m_Env, *session, registry, config, platform, true);
        watcher = std::make_unique<DayWatcher>(m_Env, *session, *share);
        if (options.m_Load)
        {
            session->load();
            gateway.run_all();
        }
    }

    WatcherRig(const WatcherRig&) = delete;
    WatcherRig& operator=(const WatcherRig&) = delete;

    [[nodiscard]] LogDocument& document()
    {
        REQUIRE(session->open_log() != nullptr);
        return *session->open_log();
    }

    /// Sets the wall clock.
    void at(UtcMillis time) { world.m_Clock.set_wall(time); }

    /// Records one error at `time` and commits it.
    void tap_at(UtcMillis time)
    {
        at(time);
        REQUIRE(session->recordError(WRONG_RESPONSE));
        gateway.run_all();
    }

    [[nodiscard]] StoredLog stored(LogId id)
    {
        auto loaded = storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        return std::move(*loaded);
    }

    [[nodiscard]] static viewmodel::Environment environment_with(TestEnvironment& world, const TimeZoneProvider* zones)
    {
        const viewmodel::Environment base = world.environment();
        return viewmodel::Environment{base.m_Clock, base.m_Scheduler, base.m_Ids, base.m_Zone,
                                      zones != nullptr ? *zones : base.m_Zones};
    }

    TestEnvironment world;
    std::unique_ptr<TimeZoneProvider> m_Zones;
    viewmodel::Environment m_Env;
    EventLog events;
    MemoryStorage storage;
    ManualStorageGateway gateway{storage.context()};
    viewmodel::DocumentRegistry registry;
    FakeConfigurationSource config;
    FakeFeedbackSink feedback{&events};
    FakePlatform platform{&events};
    LogId log_id = sample_id<LogId>(1);
    std::unique_ptr<LogSession> session;
    std::unique_ptr<ShareService> share;
    std::unique_ptr<DayWatcher> watcher;
};

}  // namespace

// ------------------------------------------------------------------------------------------------------- midnight

TEST_CASE("Midnight on the log's date gives no prompt until the next calendar day", "[viewmodel][day]")
{
    WatcherRig rig;
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    rig.at(melbourne(7, 8, 55));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.at(melbourne(7, 23, 59, 59, 999));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(changed.count() == 0);

    rig.at(melbourne(8, 0, 0));
    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Sunday 8 November"));
    CHECK(changed.count() == 1);
}

TEST_CASE("Several days later the prompt names the new date", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.at(melbourne(12, 9, 30));

    rig.watcher->check();

    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Thursday 12 November"));
    CHECK(rig.watcher->open_log_day_text() == QStringLiteral("Saturday"));
}

TEST_CASE("A clock set back gives no prompt, and takes down one that was up", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.at(melbourne(6, 23, 0));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.at(melbourne(8, 8, 0));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());

    rig.world.m_Clock.step_wall_back(48h);
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());
}

TEST_CASE("The prompt is raised by check() and by nothing else", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.at(melbourne(7, 23, 0));
    rig.watcher->check();
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    // A phone left open over midnight: time passes, the timers fire, taps are made, and nobody asks.
    rig.world.m_Clock.advance(2h);
    rig.world.m_Clock.advance(24h);
    rig.gateway.run_all();

    CHECK(rig.world.m_Clock.pending_timers() == 0);
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(changed.count() == 0);

    // Taps still go into the open log.
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.document().id() == rig.log_id);
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
}

// ----------------------------------------------------------------------------------------- a session past midnight

TEST_CASE("A session past midnight keeps its entries; keeping the log open hides the prompt until the next check",
          "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 23, 50));
    rig.tap_at(melbourne(8, 0, 10));  // after midnight, into the open log
    REQUIRE(rig.document().id() == rig.log_id);
    REQUIRE(rig.document().log().entries().size() == 2);
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->open_log_entry_count() == 2);
    CHECK(changed.count() == 1);

    rig.watcher->keepOpen();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(changed.count() == 2);
    CHECK(rig.document().id() == rig.log_id);
    CHECK(rig.document().log().entries().size() == 2);
    CHECK_FALSE(rig.stored(rig.log_id).m_Meta.m_Archived);

    // Hiding twice, or keeping open what is not up, is nothing.
    rig.watcher->keepOpen();
    CHECK(changed.count() == 2);

    // Logging goes on into the open log while the prompt is hidden, and the next foreground raises it again.
    rig.tap_at(melbourne(8, 0, 20));
    CHECK(rig.document().log().entries().size() == 3);
    CHECK(changed.count() == 2);
    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->open_log_entry_count() == 3);
    CHECK(changed.count() == 3);
}

TEST_CASE("Accepting archives the log with the entry made after midnight", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 23, 50));
    rig.tap_at(melbourne(8, 0, 10));
    LogDocument* const document = rig.session->open_log();
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    REQUIRE(rig.watcher->acceptNewLog());

    // Not yet: the new log is made after the commit, and the prompt is up until it has taken the old one's place.
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.document().id() == rig.log_id);

    rig.gateway.run_all();

    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(started.count() == 1);
    CHECK(changed.count() == 2);  // raised, then lowered
    CHECK(rig.session->open_log() == document);
    CHECK(rig.document().id() != rig.log_id);
    CHECK(rig.document().log().entries().empty());

    const StoredLog archived = rig.stored(rig.log_id);
    CHECK(archived.m_Meta.m_Archived);
    REQUIRE(archived.m_Log.entries().size() == 2);
    const auto entries = archived.m_Log.sorted_entries();
    CHECK(entries.back()->m_Captured.m_Utc == melbourne(8, 0, 10));

    // The new log belongs to the new day, so there is nothing to ask about.
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());
}

// --------------------------------------------------------------------------------------------- zones and daylight

TEST_CASE("The 23-hour day rolls over at local midnight, not after 24 hours", "[viewmodel][day][time_zone]")
{
    // 4 October 2026: clocks go forward at 02:00, so the day ends at 13:00 UTC (AEDT, +11:00 from then on).
    WatcherRig rig(Options().header(header_on(2026, 10, 4)).qt_zones());

    rig.at(utc(2026, 10, 4, 12, 59, 59, 999));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.at(utc(2026, 10, 4, 13, 0));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Monday 5 October"));
    CHECK(rig.watcher->open_log_day_text() == QStringLiteral("Sunday"));
}

TEST_CASE("The 25-hour day rolls over at local midnight, not after 24 hours", "[viewmodel][day][time_zone]")
{
    // 5 April 2026: clocks go back at 03:00, so the day starts at 13:00 UTC on the 4th and ends at 14:00 UTC on the
    // 5th.
    WatcherRig rig(Options().header(header_on(2026, 4, 5)).qt_zones());

    rig.at(utc(2026, 4, 5, 13, 0));  // 24 hours after the day began is not yet midnight
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.at(utc(2026, 4, 5, 13, 59, 59, 999));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.at(utc(2026, 4, 5, 14, 0));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Monday 6 April"));
}

TEST_CASE("A log opened in Melbourne and read in Perth waits for Perth's midnight", "[viewmodel][day][time_zone]")
{
    WatcherRig rig;
    rig.world.m_DeviceZone.set(PERTH);

    // 22:30 in Perth on the 7th is 14:30 UTC, which is already 01:30 on the 8th in Melbourne.
    rig.at(utc(2026, 11, 7, 14, 30));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    // 00:30 in Perth on the 8th.
    rig.at(utc(2026, 11, 7, 16, 30));
    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Sunday 8 November"));
}

TEST_CASE("The same traveller case with the zone rules of the platform", "[viewmodel][day][time_zone]")
{
    WatcherRig rig(Options().qt_zones());
    rig.world.m_DeviceZone.set(PERTH);

    rig.at(utc(2026, 11, 7, 14, 30));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());

    rig.at(utc(2026, 11, 7, 16, 30));
    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
}

TEST_CASE("A zone that is not known hides the prompt and is reported once", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.at(melbourne(8, 8, 0));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    QSignalSpy failed(rig.watcher.get(), &DayWatcher::checkFailed);

    rig.world.m_DeviceZone.set("Nowhere/North_Site");
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(failed.count() == 1);
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(failed.count() == 1);

    // Once a check works again, the next failure is reported again.
    rig.world.m_DeviceZone.set(MELBOURNE);
    rig.watcher->check();
    CHECK(rig.watcher->prompt_visible());
    rig.world.m_DeviceZone.set("Nowhere/North_Site");
    rig.watcher->check();
    CHECK(failed.count() == 2);
}

// ------------------------------------------------------------------------------------------------- the check itself

TEST_CASE("A check before the log has loaded does nothing and is made when the session opens", "[viewmodel][day]")
{
    WatcherRig rig(Options().no_load());
    rig.at(melbourne(8, 8, 0));
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    rig.watcher->check();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(changed.count() == 0);

    rig.session->load();
    CHECK_FALSE(rig.watcher->prompt_visible());  // still loading
    rig.gateway.run_all();

    REQUIRE(rig.session->state() == LogSession::State::OPEN);
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Sunday 8 November"));
    CHECK(changed.count() == 1);
}

TEST_CASE("A session that opens without a check waiting raises nothing", "[viewmodel][day]")
{
    WatcherRig rig(Options().no_load());
    rig.at(melbourne(8, 8, 0));

    rig.session->load();
    rig.gateway.run_all();

    CHECK_FALSE(rig.watcher->prompt_visible());
}

TEST_CASE("Repeated checks do not repeat the notification", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 0));
    rig.at(melbourne(8, 8, 5));
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    rig.watcher->check();
    rig.watcher->check();
    rig.world.m_Clock.advance(1min);
    rig.watcher->check();

    CHECK(rig.watcher->prompt_visible());
    CHECK(changed.count() == 1);
}

// -------------------------------------------------------------------------------------------------------- the text

TEST_CASE("The prompt names the new date and the old day", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.tap_at(melbourne(7, 10, 20));
    rig.at(melbourne(8, 8, 5));

    rig.watcher->check();

    CHECK(rig.watcher->new_date_text() == QStringLiteral("Sunday 8 November"));
    CHECK(rig.watcher->open_log_day_text() == QStringLiteral("Saturday"));
    CHECK(rig.watcher->open_log_entry_count() == 2);
}

TEST_CASE("The entry count is the live entries of every kind", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.tap_at(melbourne(7, 10, 6));
    REQUIRE(rig.session->addNote(QStringLiteral("a note")));
    REQUIRE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), 12 * 3600));
    rig.gateway.run_all();
    const Entry first = **rig.document().log().sorted_entries().begin();
    REQUIRE(rig.document().set_deleted(first.m_Id, true, LogChange::Origin::USER, {}));
    rig.gateway.run_all();
    rig.at(melbourne(8, 8, 5));

    rig.watcher->check();

    CHECK(rig.watcher->open_log_entry_count() == 3);
}

TEST_CASE("The count follows the open log while the prompt is up", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    REQUIRE(rig.watcher->open_log_entry_count() == 1);
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);

    // An entry after the prompt is up: counted.
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.watcher->open_log_entry_count() == 2);
    CHECK(changed.count() == 1);

    // And an entry that is deleted is not.
    const Entry newest = **rig.document().log().sorted_entries().rbegin();
    REQUIRE(rig.document().set_deleted(newest.m_Id, true, LogChange::Origin::UNDO, {}));
    rig.gateway.run_all();
    CHECK(rig.watcher->open_log_entry_count() == 1);
    CHECK(changed.count() == 2);
}

TEST_CASE("The prompt is available to QML through its names", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));

    rig.watcher->check();

    CHECK(rig.watcher->property("promptVisible").toBool());
    CHECK(rig.watcher->property("newDateText").toString() == QStringLiteral("Sunday 8 November"));
    CHECK(rig.watcher->property("openLogDayText").toString() == QStringLiteral("Saturday"));
    CHECK(rig.watcher->property("openLogEntryCount").toInt() == 1);
    CHECK(rig.watcher->property("unshared").toBool());

    CHECK(QMetaObject::invokeMethod(rig.watcher.get(), "keepOpen"));
    CHECK_FALSE(rig.watcher->property("promptVisible").toBool());
    QMetaObject::invokeMethod(rig.watcher.get(), "check");
    CHECK(rig.watcher->property("promptVisible").toBool());
    bool accepted = false;
    CHECK(QMetaObject::invokeMethod(rig.watcher.get(), "acceptNewLog", qReturnArg(accepted)));
    CHECK(accepted);
    bool sharing = true;
    rig.gateway.run_all();
    CHECK(QMetaObject::invokeMethod(rig.watcher.get(), "shareFirst", qReturnArg(sharing)));
}

// --------------------------------------------------------------------------------------------------- the unshared flag

TEST_CASE("An unshared log with entries is unshared; an empty log never is", "[viewmodel][day]")
{
    SECTION("with entries")
    {
        WatcherRig rig;
        rig.tap_at(melbourne(7, 10, 5));
        rig.at(melbourne(8, 8, 5));
        rig.watcher->check();
        CHECK(rig.watcher->unshared());
    }
    SECTION("empty")
    {
        WatcherRig rig;
        rig.at(melbourne(8, 8, 5));
        rig.watcher->check();
        REQUIRE(rig.watcher->prompt_visible());
        CHECK(rig.watcher->open_log_entry_count() == 0);
        CHECK_FALSE(rig.watcher->unshared());
    }
    SECTION("with only deleted entries")
    {
        WatcherRig rig;
        rig.tap_at(melbourne(7, 10, 5));
        const Entry only = **rig.document().log().sorted_entries().begin();
        REQUIRE(rig.document().set_deleted(only.m_Id, true, LogChange::Origin::USER, {}));
        rig.gateway.run_all();
        rig.at(melbourne(8, 8, 5));
        rig.watcher->check();
        CHECK(rig.watcher->open_log_entry_count() == 0);
        CHECK_FALSE(rig.watcher->unshared());
    }
}

TEST_CASE("A log that was shared is not unshared, even when it changed after", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 17, 30));
    REQUIRE(rig.share->shareOpenLog());  // the fake sheet reports "shared" at once
    rig.gateway.run_all();
    REQUIRE(rig.stored(rig.log_id).m_Meta.m_LastSharedUtc.has_value());

    rig.tap_at(melbourne(7, 18, 0));  // an edit after sharing does not clear it
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();

    CHECK(rig.watcher->prompt_visible());
    CHECK_FALSE(rig.watcher->unshared());
    CHECK(rig.watcher->open_log_entry_count() == 2);
}

TEST_CASE("Sharing first turns unshared off while the prompt is still up", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.tap_at(melbourne(7, 10, 20));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    REQUIRE(rig.watcher->unshared());
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);
    QSignalSpy finished(rig.share.get(), &ShareService::shareFinished);
    std::optional<bool> unshared_when_recorded;
    QObject::connect(rig.share.get(), &ShareService::shareRecorded, rig.share.get(),
                     [&] { unshared_when_recorded = rig.watcher->unshared(); });

    REQUIRE(rig.watcher->shareFirst());
    CHECK(rig.platform.sheets_presented() == 1);
    CHECK(finished.count() == 1);
    // The sheet is done and the time is not committed yet: still unshared.
    CHECK(rig.watcher->unshared());
    CHECK(changed.count() == 0);

    rig.gateway.run_all();

    REQUIRE(unshared_when_recorded.has_value());
    CHECK_FALSE(*unshared_when_recorded);
    CHECK_FALSE(rig.watcher->unshared());
    CHECK(rig.watcher->prompt_visible());
    CHECK(changed.count() == 1);
    CHECK(rig.watcher->open_log_entry_count() == 2);
}

TEST_CASE("A share the facilitator cancelled leaves the log unshared", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    QSignalSpy changed(rig.watcher.get(), &DayWatcher::promptChanged);
    rig.platform.answer_with(FakePlatform::share_outcome::cancelled);

    REQUIRE(rig.watcher->shareFirst());
    rig.gateway.run_all();

    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->unshared());
    CHECK(changed.count() == 0);
    CHECK_FALSE(rig.stored(rig.log_id).m_Meta.m_LastSharedUtc.has_value());

    // And a second try, which the facilitator completes.
    rig.platform.answer_with(FakePlatform::share_outcome::shared);
    REQUIRE(rig.watcher->shareFirst());
    rig.gateway.run_all();
    CHECK_FALSE(rig.watcher->unshared());
}

TEST_CASE("Sharing is one at a time, and the prompt stays up whatever the outcome", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    rig.platform.hold_completion();

    REQUIRE(rig.watcher->shareFirst());
    CHECK_FALSE(rig.watcher->shareFirst());  // the sheet is open
    CHECK(rig.platform.sheets_presented() == 1);
    CHECK(rig.watcher->prompt_visible());

    REQUIRE(rig.platform.complete_share_sheet(FakePlatform::share_outcome::cancelled));
    CHECK(rig.watcher->prompt_visible());
    CHECK(rig.watcher->unshared());
}

TEST_CASE("The flag is per log: a new log is unshared again once it has entries", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    REQUIRE(rig.watcher->shareFirst());
    rig.gateway.run_all();
    REQUIRE_FALSE(rig.watcher->unshared());
    REQUIRE(rig.watcher->acceptNewLog());
    rig.gateway.run_all();
    REQUIRE_FALSE(rig.watcher->prompt_visible());
    REQUIRE_FALSE(rig.stored(rig.document().id()).m_Meta.m_LastSharedUtc.has_value());

    // The next day: the new log is empty, so there is nothing to share, and then it has an entry.
    rig.at(melbourne(9, 8, 5));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    CHECK(rig.watcher->open_log_entry_count() == 0);
    CHECK_FALSE(rig.watcher->unshared());

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.watcher->open_log_entry_count() == 1);
    CHECK(rig.watcher->unshared());
}

// --------------------------------------------------------------------------------------------------- the new log

TEST_CASE("An empty log from an earlier day still gets the prompt and is archived, never as unshared",
          "[viewmodel][day]")
{
    WatcherRig rig;
    rig.at(melbourne(9, 8, 5));
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);

    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    CHECK(rig.watcher->open_log_entry_count() == 0);
    CHECK_FALSE(rig.watcher->unshared());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Monday 9 November"));

    REQUIRE(rig.watcher->acceptNewLog());
    rig.gateway.run_all();

    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(started.count() == 1);
    const StoredLog archived = rig.stored(rig.log_id);
    CHECK(archived.m_Meta.m_Archived);
    CHECK(archived.m_Log.entries().empty());
    CHECK_FALSE(archived.m_Meta.m_LastSharedUtc.has_value());
}

TEST_CASE("Sharing first and then starting the new log, as on the morning after", "[viewmodel][day]")
{
    // Steps 15 to 17 of the full-day test, on a short log.
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.tap_at(melbourne(7, 10, 20));
    rig.tap_at(melbourne(7, 11, 2));
    rig.at(melbourne(8, 8, 5));
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);

    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());
    CHECK(rig.watcher->new_date_text() == QStringLiteral("Sunday 8 November"));
    CHECK(rig.watcher->open_log_day_text() == QStringLiteral("Saturday"));
    CHECK(rig.watcher->open_log_entry_count() == 3);
    CHECK(rig.watcher->unshared());

    // Share first: the sheet reports "shared" after 20 seconds.
    rig.platform.hold_completion();
    REQUIRE(rig.watcher->shareFirst());
    rig.world.m_Clock.advance(20s);
    REQUIRE(rig.platform.complete_share_sheet(FakePlatform::share_outcome::shared));
    rig.gateway.run_all();
    CHECK(rig.stored(rig.log_id).m_Meta.m_LastSharedUtc == melbourne(8, 8, 5, 20));
    CHECK(rig.watcher->prompt_visible());
    CHECK_FALSE(rig.watcher->unshared());

    // Start the new log.
    rig.world.m_Clock.advance(40s);
    REQUIRE(rig.watcher->acceptNewLog());
    rig.gateway.run_all();

    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(started.count() == 1);
    CHECK(rig.stored(rig.log_id).m_Meta.m_Archived);
    const auto open = rig.storage.memory().open_log();
    REQUIRE(open.has_value());
    REQUIRE(open->has_value());
    CHECK((*open)->m_Meta.m_Id == rig.document().id());
    CHECK(rig.document().id() != rig.log_id);
    CHECK(rig.session->facilitator() == QStringLiteral("Sam Example"));
    CHECK(rig.session->date_text() == QStringLiteral("Sun 8 Nov"));
    CHECK(rig.session->live_entry_count() == 0);
}

TEST_CASE("Accepting is refused while the prompt is not up, and while a new log is on its way", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));

    // Not up.
    rig.at(melbourne(7, 12, 0));
    rig.watcher->check();
    CHECK_FALSE(rig.watcher->acceptNewLog());
    CHECK(rig.gateway.pending() == 0);
    CHECK_FALSE(rig.stored(rig.log_id).m_Meta.m_Archived);

    // Up, and hidden again by keeping the log open.
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    rig.watcher->keepOpen();
    CHECK_FALSE(rig.watcher->acceptNewLog());

    // Up, and a second press while the first is being written.
    rig.watcher->check();
    REQUIRE(rig.watcher->acceptNewLog());
    CHECK_FALSE(rig.watcher->acceptNewLog());
    CHECK(rig.watcher->prompt_visible());
    rig.gateway.run_all();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(rig.storage.repository().call_count(RepositoryOp::START_NEW_LOG) == 1);
}

TEST_CASE("A write that fails keeps the prompt up, and accepting works again", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);
    rig.storage.repository().fail_next(RepositoryOp::START_NEW_LOG);

    REQUIRE(rig.watcher->acceptNewLog());
    rig.gateway.run_all();

    CHECK(rig.watcher->prompt_visible());
    CHECK(started.count() == 0);
    CHECK(failed.count() == 1);
    CHECK(rig.document().id() == rig.log_id);
    CHECK_FALSE(rig.stored(rig.log_id).m_Meta.m_Archived);

    REQUIRE(rig.watcher->acceptNewLog());
    rig.gateway.run_all();
    CHECK_FALSE(rig.watcher->prompt_visible());
    CHECK(started.count() == 1);
}

TEST_CASE("A new log started from anywhere takes the prompt down", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());

    REQUIRE(rig.session->startNewLog());  // "a log for another unit", not the answer to the prompt
    rig.gateway.run_all();

    CHECK_FALSE(rig.watcher->prompt_visible());
}

TEST_CASE("A watcher that goes leaves the session as it was", "[viewmodel][day]")
{
    WatcherRig rig;
    rig.tap_at(melbourne(7, 10, 5));
    rig.at(melbourne(8, 8, 5));
    rig.watcher->check();
    REQUIRE(rig.watcher->prompt_visible());

    rig.watcher.reset();

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    REQUIRE(rig.session->startNewLog());
    rig.gateway.run_all();
    CHECK(rig.document().id() != rig.log_id);
}
