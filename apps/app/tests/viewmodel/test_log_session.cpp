// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/log_session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace std::chrono_literals;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::LogSession;
using viewmodel::StartupResult;

namespace
{

const QString WRONG_RESPONSE = QStringLiteral("00000000-0000-4000-8000-0000000000a1");
const QString SEQUENCE_OUT_OF_ORDER = QStringLiteral("00000000-0000-4000-8000-0000000000a2");
const QString NORTH_SITE = QStringLiteral("00000000-0000-4000-8000-0000000000b1");
const QString SOUTH_SITE = QStringLiteral("00000000-0000-4000-8000-0000000000b2");
const QString PLAYED_SONG = QStringLiteral("00000000-0000-4000-8000-0000000000c1");

constexpr int LOCAL_0855 = 8 * 3600 + 55 * 60; // the clock of every rig, in Melbourne

struct Options
{
    bool m_Configured = true;
    bool m_ExistingLog = false;
    bool m_Load = true;
    HeaderValues m_Header = sample_header();
    const TimeZoneProvider* m_Zones = nullptr; // the fixed-offset zones when null
    StartupResult m_Startup;

    // Chained setters, because a designated initializer that leaves a field out is a warning for Clang.
    Options& unconfigured()
    {
        m_Configured = false;
        return *this;
    }
    Options& existing()
    {
        m_ExistingLog = true;
        return *this;
    }
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
    Options& zones(const TimeZoneProvider* zones)
    {
        m_Zones = zones;
        return *this;
    }
    Options& startup(StartupResult startup)
    {
        m_Startup = std::move(startup);
        return *this;
    }
};

/// The environment of a rig with its zone provider replaced.
viewmodel::Environment environment_with(TestEnvironment& world, const TimeZoneProvider* zones)
{
    const viewmodel::Environment base = world.environment();
    return viewmodel::Environment{base.m_Clock, base.m_Scheduler, base.m_Ids, base.m_Zone,
                                  zones != nullptr ? *zones : base.m_Zones};
}

/// A `LogSession` over the in-memory store and the manual gateway, with the sample configuration.
///
/// The clock starts at Sat 7 Nov 2026 08:55 in Melbourne (+11:00).
class SessionRig
{
public:
    explicit SessionRig(Options options = {})
        : m_Env(environment_with(world, options.m_Zones)), storage(world.m_Clock, &events), registry(m_Env, gateway)
    {
        if (options.m_ExistingLog)
        {
            REQUIRE(storage.memory().create_log(log_id, options.m_Header));
        }
        if (options.m_Configured)
        {
            config.set(sample_configuration());
        }
        session = std::make_unique<LogSession>(m_Env, gateway, registry, config, feedback, options.m_Startup);
        connect_events();
        if (options.m_Load)
        {
            session->load();
            gateway.run_all();
        }
    }

    SessionRig(const SessionRig&) = delete;
    SessionRig& operator=(const SessionRig&) = delete;

    [[nodiscard]] LogDocument& document()
    {
        REQUIRE(session->open_log() != nullptr);
        return *session->open_log();
    }

    /// The live entries, in time order.
    [[nodiscard]] std::vector<Entry> entries()
    {
        std::vector<Entry> out;
        for (const Entry* entry : document().log().sorted_entries())
        {
            out.push_back(*entry);
        }
        return out;
    }

    [[nodiscard]] std::size_t calls(RepositoryOp op)
    {
        return storage.repository().call_count(op);
    }

    [[nodiscard]] ExportContext context()
    {
        return viewmodel::ExportContextBuilder(config, m_Env.m_Zones).build();
    }

    TestEnvironment world;
    viewmodel::Environment m_Env;
    EventLog events;
    MemoryStorage storage;
    ManualStorageGateway gateway{storage.context()};
    viewmodel::DocumentRegistry registry;
    FakeConfigurationSource config;
    FakeFeedbackSink feedback{&events};
    LogId log_id = sample_id<LogId>(1);
    std::unique_ptr<LogSession> session;

private:
    void connect_events()
    {
        QObject::connect(session.get(), &LogSession::writeFailed, session.get(),
                         [this](const QString& operation, const QString&)
                         { events.record(EventKind::WRITE_FAILED, operation.toStdString()); });
    }
};

/// Where in the event log the first event of a kind (with a detail that starts with `prefix`) is.
std::optional<std::size_t> index_of(const EventLog& events, EventKind kind, const std::string& prefix = {})
{
    const auto all = events.events();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_Kind == kind && all[i].m_Detail.starts_with(prefix))
        {
            return i;
        }
    }
    return std::nullopt;
}

const NoteEntry* note_of(const Entry& entry)
{
    return std::get_if<NoteEntry>(&entry.m_Body);
}

} // namespace

// ---------------------------------------------------------------------------------------------------- loading

TEST_CASE("A first start with a configuration creates today's log with three empty fields", "[viewmodel][session]")
{
    SessionRig rig(Options().no_load());
    QSignalSpy state(rig.session.get(), &LogSession::stateChanged);
    CHECK(rig.session->state() == LogSession::State::LOADING);

    rig.session->load();
    CHECK(rig.session->state() == LogSession::State::LOADING);
    CHECK(rig.session->open_log() == nullptr);
    rig.gateway.run_all();

    REQUIRE(rig.session->state() == LogSession::State::OPEN);
    CHECK(state.count() == 1);
    CHECK(rig.calls(RepositoryOp::CREATE_LOG) == 1);
    // 21:55 UTC on 6 November is 08:55 on 7 November in Melbourne.
    CHECK(rig.document().log().initial_header().m_LocalDate == std::chrono::year{2026} / 11 / 7);
    CHECK(rig.session->facilitator() == QStringLiteral("Sam Example"));
    CHECK(rig.session->date_text() == QStringLiteral("Sat 7 Nov"));
    CHECK(rig.session->unit_text().isEmpty());
    CHECK(rig.session->location_text().isEmpty());
    CHECK(rig.session->version_text().isEmpty());
    CHECK(rig.session->missing_fields() == QStringList{"unit_number", "location", "software_version"});
    CHECK(rig.session->live_entry_count() == 0);
    CHECK(rig.session->draft().isEmpty());
    CHECK(rig.storage.memory().open_log().value().has_value());
}

TEST_CASE("Without a configuration the session needs setup and creates nothing, until one arrives",
          "[viewmodel][session]")
{
    SessionRig rig(Options().unconfigured());
    CHECK(rig.session->state() == LogSession::State::NEEDS_SETUP);
    CHECK(rig.session->open_log() == nullptr);
    CHECK(rig.calls(RepositoryOp::CREATE_LOG) == 0);
    CHECK_FALSE(rig.session->recordError(WRONG_RESPONSE));
    CHECK_FALSE(rig.session->addNote(QStringLiteral("a note")));

    rig.config.set(sample_configuration());
    CHECK(rig.session->state() == LogSession::State::LOADING);
    rig.gateway.run_all();
    CHECK(rig.session->state() == LogSession::State::OPEN);
    CHECK(rig.calls(RepositoryOp::CREATE_LOG) == 1);
    CHECK(rig.session->facilitator() == QStringLiteral("Sam Example"));
}

TEST_CASE("An open log is opened as it was, with its draft", "[viewmodel][session]")
{
    SessionRig rig(Options().existing().no_load());
    REQUIRE(rig.storage.memory().add_entry(rig.log_id, sample_error(1, 10)));
    REQUIRE(rig.storage.memory().add_entry(rig.log_id, sample_note(2, 11, "a note")));
    REQUIRE(rig.storage.memory().set_draft("half a thought", sample_time(12)));
    QSignalSpy draft(rig.session.get(), &LogSession::draftChanged);

    rig.session->load();
    rig.gateway.run_all();

    REQUIRE(rig.session->state() == LogSession::State::OPEN);
    CHECK(rig.calls(RepositoryOp::CREATE_LOG) == 0);
    CHECK(rig.document().id() == rig.log_id);
    CHECK(rig.session->draft() == QStringLiteral("half a thought"));
    CHECK(draft.count() == 1);
    CHECK(rig.session->unit_text() == QStringLiteral("UNIT-27"));
    CHECK(rig.session->location_text() == QStringLiteral("North Site"));
    CHECK(rig.session->version_text() == QStringLiteral("1.4.2"));
    CHECK(rig.session->missing_fields().isEmpty());
    CHECK(rig.session->live_entry_count() == 2);
    CHECK(rig.entries().size() == 2);
}

TEST_CASE("A location chosen from the list shows its name", "[viewmodel][session]")
{
    HeaderValues header = sample_header();
    header.m_Location = LocationValue{LocationId(Uuid::parse(NORTH_SITE.toStdString()).value()), {}};
    SessionRig rig(Options().existing().header(header));
    CHECK(rig.session->location_text() == QStringLiteral("North Site"));
}

TEST_CASE("A store that cannot be read fails the session and says so", "[viewmodel][session]")
{
    SessionRig rig(Options().no_load());
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);
    rig.storage.repository().fail_next(RepositoryOp::OPEN_LOG);

    rig.session->load();
    rig.gateway.run_all();

    CHECK(rig.session->state() == LogSession::State::FAILED);
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == QStringLiteral("load"));
    CHECK_FALSE(rig.session->recordError(WRONG_RESPONSE));
}

TEST_CASE("The recovery notice comes from the start-up report and is constant", "[viewmodel][session]")
{
    RecoveryReport report;
    report.m_Outcome = RecoveryOutcome::REBUILT;
    report.m_RebuiltRecords = 12;
    SessionRig recovered(Options().startup(StartupResult{report}));
    CHECK(recovered.session->recovery_notice().contains(QStringLiteral("12")));

    SessionRig clean;
    CHECK(clean.session->recovery_notice().isEmpty());
}

TEST_CASE("The open log is announced to the models that follow it", "[viewmodel][session]")
{
    SessionRig rig(Options().no_load());
    FakeOpenLogObserver observer;
    rig.session->add_observer(observer);
    rig.session->load();
    rig.gateway.run_all();
    REQUIRE(observer.documents().size() == 1);
    CHECK(observer.documents().front() == rig.session->open_log());
    rig.session->remove_observer(observer);
}

// -------------------------------------------------------------------------------------------------------- taps

TEST_CASE("A tap is an entry only after the commit, with the time of the tap", "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy recorded(rig.session.get(), &LogSession::entryRecorded);
    QSignalSpy pending(rig.session.get(), &LogSession::pendingWritesChanged);
    const UtcMillis tapped = rig.world.m_Clock.now();

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(2s);

    // Submitted, not committed: no entry, no count, no confirmation.
    CHECK(rig.entries().empty());
    CHECK(rig.session->live_entry_count() == 0);
    CHECK(rig.feedback.recorded().empty());
    CHECK(recorded.count() == 0);
    CHECK(rig.session->pending_writes() == 1);
    CHECK(rig.gateway.pending() == 1);

    REQUIRE(rig.gateway.run_next());

    const auto entries = rig.entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().m_Captured.m_Utc == tapped);
    CHECK(entries.front().m_Captured.m_Zone == "Australia/Melbourne");
    CHECK_FALSE(entries.front().m_Id.uuid().is_nil());
    const auto* error = std::get_if<ErrorEntry>(&entries.front().m_Body);
    REQUIRE(error != nullptr);
    CHECK(error->m_ErrorTypeLabel == "Wrong Response");
    CHECK(rig.session->live_entry_count() == 1);
    CHECK(rig.session->pending_writes() == 0);
    CHECK(pending.count() == 2);

    // One confirmation, one signal, and both after the commit.
    REQUIRE(rig.feedback.recorded().size() == 1);
    CHECK(rig.feedback.recorded().front() == entries.front().m_Id);
    REQUIRE(recorded.count() == 1);
    CHECK(recorded.at(0).at(0).toString().toStdString() == entries.front().m_Id.format());
    CHECK(rig.events.count(EventKind::CONFIRMATION) == 1);
    const auto commit = index_of(rig.events, EventKind::COMMIT);
    const auto confirmation = index_of(rig.events, EventKind::CONFIRMATION, "entry_recorded");
    REQUIRE(commit);
    REQUIRE(confirmation);
    CHECK(*commit < *confirmation);
    // The session never fires the impact haptic: that is touch-down, in QML.
    CHECK(rig.feedback.touches() == 0);
    CHECK(rig.events.count(EventKind::HAPTIC) == 0);
}

TEST_CASE("A failing commit gives no entry, no count and no confirmation, and the next tap works",
          "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy recorded(rig.session.get(), &LogSession::entryRecorded);
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);
    rig.storage.repository().fail_next(RepositoryOp::ADD_ENTRY);

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();

    CHECK(rig.entries().empty());
    CHECK(rig.session->live_entry_count() == 0);
    CHECK(rig.feedback.recorded().empty());
    CHECK(recorded.count() == 0);
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == QStringLiteral("recordError"));
    CHECK_FALSE(failed.at(0).at(1).toString().isEmpty());
    CHECK(rig.session->pending_writes() == 0);
    CHECK(rig.events.count(EventKind::CONFIRMATION) == 0);

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.entries().size() == 1);
    CHECK(recorded.count() == 1);
}

TEST_CASE("The queue holds what it may and refuses the next tap with a message", "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);

    // One tap is in flight and 64 wait behind it: 65 are accepted, the 66th is refused.
    for (int i = 0; i < 65; ++i)
    {
        INFO("tap " << i);
        REQUIRE(rig.session->recordError(WRONG_RESPONSE));
        rig.world.m_Clock.advance(1ms);
    }
    CHECK(rig.session->pending_writes() == 65);
    CHECK_FALSE(rig.session->recordError(WRONG_RESPONSE));
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == QStringLiteral("recordError"));

    rig.gateway.run_all();
    CHECK(rig.entries().size() == 65);
    CHECK(rig.session->pending_writes() == 0);
    CHECK(rig.session->recordError(WRONG_RESPONSE));
}

TEST_CASE("Taps for an unknown or a retired type are refused and write nothing", "[viewmodel][session]")
{
    SessionRig rig;
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[1].m_Retired = true;
    rig.config.set(configuration);
    const auto posted = rig.gateway.posted();

    CHECK_FALSE(rig.session->recordError(SEQUENCE_OUT_OF_ORDER));                                  // retired
    CHECK_FALSE(rig.session->recordError(QStringLiteral("00000000-0000-4000-8000-0000000000ff"))); // unknown
    CHECK_FALSE(rig.session->recordError(QStringLiteral("not an id")));
    CHECK_FALSE(rig.session->recordError(QString{}));
    CHECK(rig.gateway.posted() == posted);
    CHECK(rig.session->pending_writes() == 0);
    CHECK(rig.events.count(EventKind::WRITE_FAILED) == 0);
}

TEST_CASE("A tap while the session is loading is refused", "[viewmodel][session]")
{
    SessionRig rig(Options().no_load());
    CHECK_FALSE(rig.session->recordError(WRONG_RESPONSE));
    rig.session->load();
    CHECK_FALSE(rig.session->recordError(WRONG_RESPONSE)); // still loading: the job has not run
    CHECK(rig.gateway.pending() == 1);
    rig.gateway.run_all();
    CHECK(rig.session->recordError(WRONG_RESPONSE));
}

TEST_CASE("Taps submitted before any commit are committed in order", "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy recorded(rig.session.get(), &LogSession::entryRecorded);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->recordError(SEQUENCE_OUT_OF_ORDER));
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    CHECK(rig.entries().empty());

    rig.gateway.run_all();

    REQUIRE(recorded.count() == 3);
    const auto entries = rig.entries();
    REQUIRE(entries.size() == 3);
    for (int i = 0; i < 3; ++i)
    {
        INFO("entry " << i);
        CHECK(entries.at(static_cast<std::size_t>(i)).m_Id.format() == recorded.at(i).at(0).toString().toStdString());
    }
    CHECK(std::get<ErrorEntry>(entries.at(1).m_Body).m_ErrorTypeLabel == "Sequence Out of Order");
}

TEST_CASE("A date in 1970 and a change of zone between taps are stored as given", "[viewmodel][session]")
{
    SessionRig rig;
    rig.world.m_Clock.set_wall(UtcMillis{});
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_DeviceZone.set("Australia/Perth");
    rig.world.m_Clock.advance(1min);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();

    const auto entries = rig.entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries.at(0).m_Captured.m_Utc == UtcMillis{});
    CHECK(entries.at(0).m_Captured.m_Zone == "Australia/Melbourne");
    CHECK(entries.at(1).m_Captured.m_Utc == UtcMillis{1min});
    CHECK(entries.at(1).m_Captured.m_Zone == "Australia/Perth");
}

TEST_CASE("The count follows commits and undo, and a share changes nothing the header card shows",
          "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy header(rig.session.get(), &LogSession::headerChanged);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.session->live_entry_count() == 2);
    CHECK(header.count() == 2);

    const EntryId second = rig.entries().at(1).m_Id;
    REQUIRE(rig.document().set_deleted(second, true, LogChange::Origin::UNDO, {}));
    rig.gateway.run_all();
    CHECK(rig.session->live_entry_count() == 1);
    REQUIRE(rig.document().set_deleted(second, false, LogChange::Origin::USER, {}));
    rig.gateway.run_all();
    CHECK(rig.session->live_entry_count() == 2);
    CHECK(header.count() == 4);

    REQUIRE(rig.document().mark_shared(rig.world.m_Clock.now(), {}));
    rig.gateway.run_all();
    CHECK(header.count() == 4);
}

// ------------------------------------------------------------------------------------------------------ header

TEST_CASE("An empty header lists its three missing fields and taps still work", "[viewmodel][session]")
{
    SessionRig rig;
    REQUIRE(rig.session->missing_fields().size() == 3);
    CHECK(rig.session->recordError(WRONG_RESPONSE));
    CHECK(rig.session->addNote(QStringLiteral("anything")));
    rig.gateway.run_all();
    CHECK(rig.entries().size() == 2);
}

TEST_CASE("Header values are checked inline and a refused value writes nothing", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    const auto valid = [&rig](const char* key, const char* text)
    { return rig.session->validateHeaderValue(QString::fromLatin1(key), QString::fromUtf8(text)); };

    CHECK(valid("unit_number", "27").isEmpty());
    CHECK(valid("unit_number", " 28 ").isEmpty());
    CHECK_FALSE(valid("unit_number", "027").isEmpty());
    CHECK_FALSE(valid("unit_number", "").isEmpty());
    CHECK_FALSE(valid("unit_number", "2a").isEmpty());
    CHECK_FALSE(valid("unit_number", "1234567890").isEmpty());
    CHECK(valid("software_version", "1.4.2").isEmpty());
    CHECK_FALSE(valid("software_version", "1.4").isEmpty());
    CHECK_FALSE(valid("software_version", "1.4.2-beta").isEmpty());
    CHECK_FALSE(valid("software_version", "").isEmpty());
    CHECK(valid("location", "South Site").isEmpty());
    CHECK(valid("location", SOUTH_SITE.toStdString().c_str()).isEmpty());
    CHECK_FALSE(valid("location", "   ").isEmpty());
    CHECK_FALSE(rig.session->validateHeaderValue(QStringLiteral("location"), QString(201, QChar('x'))).isEmpty());
    CHECK_FALSE(valid("facilitator", "Sam Example").isEmpty());
    // Different fields have different messages.
    CHECK(valid("unit_number", "027") != valid("software_version", "1.4"));

    const auto posted = rig.gateway.posted();
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.4"), LOCAL_0855));
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("unit_number"), QStringLiteral("027"), LOCAL_0855));
    CHECK_FALSE(rig.session->correctHeader(QStringLiteral("software_version"), QStringLiteral("1.4.2-beta")));
    CHECK_FALSE(rig.session->correctHeader(QStringLiteral("facilitator"), QStringLiteral("Sam")));
    CHECK(rig.gateway.posted() == posted);
}

TEST_CASE("Correct rewrites the value in force and adds no entry; a first fill is a Correct", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    QSignalSpy header(rig.session.get(), &LogSession::headerChanged);
    REQUIRE(rig.session->correctHeader(QStringLiteral("software_version"), QStringLiteral("1.4.3")));
    rig.gateway.run_all();
    CHECK(rig.session->version_text() == QStringLiteral("1.4.3"));
    CHECK(rig.entries().empty());
    CHECK(rig.calls(RepositoryOp::CORRECT_HEADER) == 1);
    CHECK(header.count() == 1);
    CHECK(rig.events.count(EventKind::CONFIRMATION) == 0);
    CHECK(rig.events.count(EventKind::HAPTIC) == 0);

    SessionRig empty;
    REQUIRE(empty.session->missing_fields().size() == 3);
    REQUIRE(empty.session->correctHeader(QStringLiteral("unit_number"), QStringLiteral("42")));
    REQUIRE(empty.session->correctHeader(QStringLiteral("location"), SOUTH_SITE));
    empty.gateway.run_all();
    CHECK(empty.session->unit_text() == QStringLiteral("UNIT-42"));
    CHECK(empty.session->location_text() == QStringLiteral("South Site"));
    CHECK(empty.session->missing_fields() == QStringList{"software_version"});
    CHECK(empty.entries().empty());
}

TEST_CASE("Change adds an entry and moves the value in force but not the start value", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);
    rig.world.m_Clock.advance(10min); // 09:05
    const int when = LOCAL_0855 + 10 * 60;

    REQUIRE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), when));
    CHECK(rig.entries().empty());
    rig.gateway.run_all();

    const auto entries = rig.entries();
    REQUIRE(entries.size() == 1);
    const auto* change = std::get_if<HeaderChangeEntry>(&entries.front().m_Body);
    REQUIRE(change != nullptr);
    CHECK(change->m_Field == HeaderField::SOFTWARE_VERSION);
    CHECK(change->m_NewValue == "1.5.0");
    CHECK(entries.front().m_Captured.m_Utc == rig.world.m_Clock.now());
    CHECK(entries.front().m_Captured.m_Zone == "Australia/Melbourne");
    CHECK(rig.session->version_text() == QStringLiteral("1.5.0"));
    CHECK(header_field_text(rig.document().log().initial_header(), HeaderField::SOFTWARE_VERSION) == "1.4.2");
    CHECK(rig.session->live_entry_count() == 1);
    CHECK(failed.count() == 0);
    CHECK(rig.events.count(EventKind::CONFIRMATION) == 0);
    CHECK(rig.events.count(EventKind::HAPTIC) == 0);

    // The value now in force is refused; so is the unit number that is already there.
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), when));
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("unit_number"), QStringLiteral("27"), when));
}

TEST_CASE("A change at an earlier instant sorts before the entry it precedes", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    REQUIRE(rig.session->recordError(WRONG_RESPONSE)); // 08:55
    rig.gateway.run_all();

    REQUIRE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), 8 * 3600 + 50 * 60));
    rig.gateway.run_all();

    const auto entries = rig.entries();
    REQUIRE(entries.size() == 2);
    CHECK(std::holds_alternative<HeaderChangeEntry>(entries.at(0).m_Body));
    CHECK(std::holds_alternative<ErrorEntry>(entries.at(1).m_Body));
}

TEST_CASE("The change at a UTC instant is the same change, and zero is now", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    const auto instant =
        std::chrono::duration_cast<std::chrono::milliseconds>(rig.world.m_Clock.now().time_since_epoch());
    REQUIRE(
        rig.session->changeHeaderAtUtc(QStringLiteral("unit_number"), QStringLiteral("30"), instant.count() - 60'000));
    REQUIRE(rig.session->changeHeaderAtUtc(QStringLiteral("software_version"), QStringLiteral("1.5.0")));
    rig.gateway.run_all();

    const auto entries = rig.entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries.at(0).m_Captured.m_Utc == rig.world.m_Clock.now() - 1min);
    CHECK(entries.at(1).m_Captured.m_Utc == rig.world.m_Clock.now());
    CHECK(rig.session->unit_text() == QStringLiteral("UNIT-30"));
}

// ----------------------------------------------------------------------- local time, preview and the new-log count

TEST_CASE("The local time of the version sheet defaults to the clock", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    CHECK(rig.session->now_local_seconds() == LOCAL_0855);
    rig.world.m_Clock.advance(90s);
    CHECK(rig.session->now_local_seconds() == LOCAL_0855 + 90);

    REQUIRE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"),
                                      rig.session->now_local_seconds()));
    rig.gateway.run_all();
    REQUIRE(rig.entries().size() == 1);
    CHECK(rig.entries().front().m_Captured.m_Utc == rig.world.m_Clock.now());
}

TEST_CASE("A local time out of range is refused", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    const auto posted = rig.gateway.posted();
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), -1));
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), 86'400));
    CHECK_FALSE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), 1'000'000));
    CHECK(rig.gateway.posted() == posted);
    CHECK(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), 0));
    CHECK(rig.session->previewHeaderChange(QStringLiteral("software_version"), QStringLiteral("1.5.0"), 86'400)
              .contains(QStringLiteral("time")));
}

namespace
{

/// The instant a change at `local_seconds` on `date` in Melbourne is stored with, through the real time zone rules.
UtcMillis change_instant(std::chrono::year_month_day date, int local_seconds)
{
    const auto zones = make_qt_time_zone_provider();
    HeaderValues header = sample_header();
    header.m_LocalDate = date;
    SessionRig rig(Options().existing().header(header).zones(zones.get()));
    REQUIRE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"), local_seconds));
    rig.gateway.run_all();
    const auto entries = rig.entries();
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().m_Captured.m_Zone == "Australia/Melbourne");
    return entries.front().m_Captured.m_Utc;
}

UtcMillis utc_at(int month, unsigned day, int hour, int minute)
{
    return UtcMillis{std::chrono::sys_days{std::chrono::year{2026} / std::chrono::month(static_cast<unsigned>(month)) /
                                           std::chrono::day(day)} +
                     std::chrono::hours(hour) + std::chrono::minutes(minute)};
}

} // namespace

TEST_CASE("A local time is resolved in the log's zone either side of the Melbourne changes", "[viewmodel][session]")
{
    using std::chrono::April;
    using std::chrono::October;
    SECTION("2026-10-04: clocks go forward at 02:00 from +10:00 to +11:00")
    {
        const auto date = std::chrono::year{2026} / October / 4;
        // 01:30 is +10:00, 04:00 is +11:00.
        CHECK(change_instant(date, 1 * 3600 + 30 * 60) == utc_at(10, 3, 15, 30));
        CHECK(change_instant(date, 4 * 3600) == utc_at(10, 3, 17, 0));
        // 02:30 does not exist: the provider's answer (on or after the end of the gap, 16:00 UTC) is stored as it is.
        const UtcMillis in_gap = change_instant(date, 2 * 3600 + 30 * 60);
        CHECK(in_gap >= utc_at(10, 3, 16, 0));
        CHECK(in_gap ==
              make_qt_time_zone_provider()->to_utc(LocalTime{date, 2h + 30min}, "Australia/Melbourne").value());
    }
    SECTION("2026-04-05: clocks go back at 03:00 from +11:00 to +10:00")
    {
        const auto date = std::chrono::year{2026} / April / 5;
        // 01:30 is +11:00, 04:00 is +10:00.
        CHECK(change_instant(date, 1 * 3600 + 30 * 60) == utc_at(4, 4, 14, 30));
        CHECK(change_instant(date, 4 * 3600) == utc_at(4, 4, 18, 0));
        // 02:30 happens twice: the earlier instant, at +11:00.
        CHECK(change_instant(date, 2 * 3600 + 30 * 60) == utc_at(4, 4, 15, 30));
    }
}

TEST_CASE("The preview is the line that the change then saves, and writes nothing", "[viewmodel][session]")
{
    const struct
    {
        const char* m_Field;
        const char* m_Value;
    } scenarios[] = {{"software_version", "1.5.0"}, {"unit_number", "42"}, {"location", "South Site"}};
    for (const auto& scenario : scenarios)
    {
        INFO(scenario.m_Field);
        SessionRig rig(Options().existing());
        REQUIRE(rig.session->recordError(WRONG_RESPONSE));
        rig.world.m_Clock.advance(1min);
        rig.gateway.run_all();
        const auto posted = rig.gateway.posted();
        const auto ids = rig.world.m_Ids.made();
        const auto calls = rig.storage.repository().calls().size();
        const auto entries_before = rig.document().log().entries().size();
        const QString field = QString::fromLatin1(scenario.m_Field);
        const QString value = QString::fromLatin1(scenario.m_Value);

        const QString preview = rig.session->previewHeaderChange(field, value, LOCAL_0855 + 60);
        CHECK_FALSE(preview.isEmpty());

        // Nothing was written, queued or made.
        CHECK(rig.gateway.posted() == posted);
        CHECK(rig.world.m_Ids.made() == ids);
        CHECK(rig.storage.repository().calls().size() == calls);
        CHECK(rig.document().log().entries().size() == entries_before);

        REQUIRE(rig.session->changeHeader(field, value, LOCAL_0855 + 60));
        rig.gateway.run_all();
        REQUIRE(rig.document().log().entries().size() == entries_before + 1);
        const Entry& saved = rig.document().log().entries().back();
        const auto line = render_entry_line(rig.document().log(), saved.m_Id, rig.context());
        REQUIRE(line.has_value());
        CHECK(preview.toStdString() == *line);
    }
}

TEST_CASE("The preview shows the old value, the new value and the time", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    const QString line =
        rig.session->previewHeaderChange(QStringLiteral("software_version"), QStringLiteral(" 1.5.0 "), LOCAL_0855);
    CHECK(line.contains(QStringLiteral("08:55")));
    CHECK(line.contains(QStringLiteral("1.4.2")));
    CHECK(line.contains(QStringLiteral("1.5.0")));
    CHECK(rig.session->previewHeaderChange(QStringLiteral("unit_number"), QStringLiteral("42"), 9 * 3600)
              .contains(QStringLiteral("UNIT-27")));
}

TEST_CASE("The preview of an invalid or an unchanged value is the refusal message", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    const auto posted = rig.gateway.posted();
    const QString invalid =
        rig.session->previewHeaderChange(QStringLiteral("software_version"), QStringLiteral("1.4"), LOCAL_0855);
    CHECK_FALSE(invalid.isEmpty());
    CHECK(invalid == rig.session->validateHeaderValue(QStringLiteral("software_version"), QStringLiteral("1.4")));

    const QString unchanged =
        rig.session->previewHeaderChange(QStringLiteral("software_version"), QStringLiteral("1.4.2"), LOCAL_0855);
    CHECK_FALSE(unchanged.isEmpty());
    CHECK(unchanged.toStdString().find("08:55") == std::string::npos);
    CHECK(rig.session->previewHeaderChange(QStringLiteral("unit_number"), QStringLiteral("27"), LOCAL_0855) ==
          unchanged);
    CHECK(rig.gateway.posted() == posted);
}

// ------------------------------------------------------------------------------------------------------ notes

TEST_CASE("Empty and over-long notes are refused", "[viewmodel][session]")
{
    SessionRig rig;
    const auto posted = rig.gateway.posted();
    CHECK_FALSE(rig.session->addNote(QString{}));
    CHECK_FALSE(rig.session->addNote(QStringLiteral("  \n\t ")));
    CHECK_FALSE(rig.session->addNote(QString(10'001, QChar('a'))));
    CHECK(rig.gateway.posted() == posted);

    CHECK(rig.session->addNote(QString(10'000, QChar('a'))));
    rig.gateway.run_all();
    REQUIRE(rig.entries().size() == 1);
    const auto entries = rig.entries();
    const NoteEntry* note = note_of(entries.front());
    REQUIRE(note != nullptr);
    CHECK(note->m_Value.size() == 10'000);
}

TEST_CASE("A note is trimmed and confirmed with the success haptic only after its commit", "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy recorded(rig.session.get(), &LogSession::entryRecorded);
    REQUIRE(rig.session->addNote(QStringLiteral("  Sam Example visited \n")));
    CHECK(rig.feedback.saved_notes().empty());
    rig.gateway.run_all();

    REQUIRE(rig.entries().size() == 1);
    const auto entries = rig.entries();
    REQUIRE(entries.size() == 1);
    const NoteEntry* note = note_of(entries.front());
    REQUIRE(note != nullptr);
    CHECK_FALSE(note->m_Template.has_value());
    CHECK(note->m_Value == "Sam Example visited");
    REQUIRE(rig.feedback.saved_notes().size() == 1);
    CHECK(rig.feedback.saved_notes().front() == rig.entries().front().m_Id);
    CHECK(rig.feedback.recorded().empty());
    CHECK(recorded.count() == 0); // entryRecorded opens the note sheet of a tapped error
    const auto commit = index_of(rig.events, EventKind::COMMIT);
    const auto confirmation = index_of(rig.events, EventKind::CONFIRMATION, "note_saved");
    REQUIRE(commit);
    REQUIRE(confirmation);
    CHECK(*commit < *confirmation);
}

TEST_CASE("A templated note stores the template and the value and the renderer makes the sentence",
          "[viewmodel][session]")
{
    SessionRig rig;
    REQUIRE(rig.session->addTemplatedNote(PLAYED_SONG, QStringLiteral(" Blue Moon ")));
    CHECK_FALSE(rig.session->addTemplatedNote(PLAYED_SONG, QStringLiteral("   ")));
    CHECK_FALSE(
        rig.session->addTemplatedNote(QStringLiteral("00000000-0000-4000-8000-0000000000c9"), QStringLiteral("x")));
    CHECK_FALSE(rig.session->addTemplatedNote(QStringLiteral("nonsense"), QStringLiteral("x")));
    rig.gateway.run_all();

    REQUIRE(rig.entries().size() == 1);
    const auto entries = rig.entries();
    REQUIRE(entries.size() == 1);
    const NoteEntry* note = note_of(entries.front());
    REQUIRE(note != nullptr);
    REQUIRE(note->m_Template.has_value());
    CHECK(note->m_Template->format() == PLAYED_SONG.toStdString());
    CHECK(note->m_Value == "Blue Moon");
    const auto line = render_entry_line(rig.document().log(), rig.entries().front().m_Id, rig.context());
    REQUIRE(line.has_value());
    CHECK(line->find("Played Blue Moon") != std::string::npos);
    CHECK(rig.feedback.saved_notes().size() == 1);
}

TEST_CASE("A note commit clears the draft in the same repository call", "[viewmodel][session]")
{
    SessionRig rig;
    rig.session->set_draft(QStringLiteral("a note being typed"));
    rig.session->flushDraft();
    rig.gateway.run_all();
    REQUIRE(rig.calls(RepositoryOp::SET_DRAFT) == 1);
    REQUIRE(rig.storage.memory().get_draft().value().has_value());

    REQUIRE(rig.session->addNote(QStringLiteral("a note being typed")));
    rig.gateway.run_all();

    CHECK(rig.calls(RepositoryOp::ADD_ENTRY) == 1);
    CHECK(rig.calls(RepositoryOp::SET_DRAFT) == 1); // the clearing was part of the note's own call
    CHECK_FALSE(rig.storage.memory().get_draft().value().has_value());
    CHECK(rig.session->draft().isEmpty());
}

TEST_CASE("A draft submitted as a note is not written again by the debounce", "[viewmodel][session]")
{
    SessionRig rig;
    rig.session->set_draft(QStringLiteral("quick note"));
    REQUIRE(rig.session->addNote(QStringLiteral("quick note")));
    rig.gateway.run_all();
    rig.world.m_Clock.advance(1s);
    rig.gateway.run_all();

    CHECK(rig.calls(RepositoryOp::SET_DRAFT) == 0);
    CHECK_FALSE(rig.storage.memory().get_draft().value().has_value());
    CHECK(rig.session->draft().isEmpty());
}

TEST_CASE("Typing during the commit keeps the newer draft", "[viewmodel][session]")
{
    SessionRig rig;
    rig.session->set_draft(QStringLiteral("abc"));
    REQUIRE(rig.session->addNote(QStringLiteral("abc")));
    rig.session->set_draft(QStringLiteral("abcdef")); // typed while the note commits
    rig.gateway.run_all();

    CHECK(rig.session->draft() == QStringLiteral("abcdef"));
    REQUIRE(rig.storage.memory().get_draft().value().has_value());
    CHECK(rig.storage.memory().get_draft().value()->m_Text == "abcdef");
    rig.world.m_Clock.advance(1s);
    rig.gateway.run_all();
    CHECK(rig.storage.memory().get_draft().value()->m_Text == "abcdef");
}

TEST_CASE("A failed note keeps the draft and writes it again", "[viewmodel][session]")
{
    SessionRig rig;
    rig.session->set_draft(QStringLiteral("keep me"));
    rig.storage.repository().fail_next(RepositoryOp::ADD_ENTRY);
    REQUIRE(rig.session->addNote(QStringLiteral("keep me")));
    rig.gateway.run_all();

    CHECK(rig.entries().empty());
    CHECK(rig.events.count(EventKind::WRITE_FAILED, "addNote") == 1);
    CHECK(rig.feedback.saved_notes().empty());
    CHECK(rig.session->draft() == QStringLiteral("keep me"));
    REQUIRE(rig.storage.memory().get_draft().value().has_value());
    CHECK(rig.storage.memory().get_draft().value()->m_Text == "keep me");
}

TEST_CASE("The draft is shown at once and stored after a quiet moment, or when flushed", "[viewmodel][session]")
{
    SessionRig rig;
    QSignalSpy draft(rig.session.get(), &LogSession::draftChanged);

    rig.session->set_draft(QStringLiteral("a"));
    CHECK(rig.session->draft() == QStringLiteral("a"));
    CHECK(draft.count() == 1);
    CHECK(rig.gateway.pending() == 0);
    rig.world.m_Clock.advance(100ms);
    rig.session->set_draft(QStringLiteral("ab"));
    rig.session->set_draft(QStringLiteral("ab")); // unchanged: no signal, no timer
    CHECK(draft.count() == 2);
    rig.world.m_Clock.advance(299ms);
    CHECK(rig.gateway.pending() == 0);
    rig.world.m_Clock.advance(1ms);
    REQUIRE(rig.gateway.pending() == 1);
    rig.gateway.run_all();
    CHECK(rig.calls(RepositoryOp::SET_DRAFT) == 1);
    CHECK(rig.storage.memory().get_draft().value()->m_Text == "ab");

    rig.session->set_draft(QStringLiteral("abc"));
    rig.session->flushDraft();
    CHECK(rig.gateway.pending() == 1);
    rig.session->flushDraft(); // nothing new to store
    CHECK(rig.gateway.pending() == 1);
    rig.gateway.run_all();
    CHECK(rig.storage.memory().get_draft().value()->m_Text == "abc");
    rig.world.m_Clock.advance(1s);
    CHECK(rig.gateway.pending() == 0);

    // Cleared by the facilitator: the store is told too.
    rig.session->set_draft(QString{});
    rig.session->flushDraft();
    rig.gateway.run_all();
    CHECK_FALSE(rig.storage.memory().get_draft().value().has_value());
}

TEST_CASE("A draft that could not be stored is reported", "[viewmodel][session]")
{
    SessionRig rig;
    rig.storage.repository().fail_next(RepositoryOp::SET_DRAFT);
    rig.session->set_draft(QStringLiteral("x"));
    rig.session->flushDraft();
    rig.gateway.run_all();
    CHECK(rig.events.count(EventKind::WRITE_FAILED, "saveDraft") == 1);
    CHECK(rig.session->draft() == QStringLiteral("x"));
}

// --------------------------------------------------------------------------------------------------- new log

TEST_CASE("A new log archives the open one with all its entries and starts one with the facilitator and the date",
          "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(1min);
    REQUIRE(rig.session->addNote(QStringLiteral("a note")));
    rig.world.m_Clock.advance(1min);
    REQUIRE(rig.session->changeHeader(QStringLiteral("software_version"), QStringLiteral("1.5.0"),
                                      rig.session->now_local_seconds()));
    rig.session->set_draft(QStringLiteral("unsent words"));
    rig.session->flushDraft();
    rig.gateway.run_all();
    REQUIRE(rig.session->live_entry_count() == 3);

    FakeLogObserver observer(&rig.events);
    rig.document().add_observer(observer);
    LogDocument* const document = rig.session->open_log();
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);
    QSignalSpy header(rig.session.get(), &LogSession::headerChanged);
    rig.world.m_Clock.advance(24h); // the next day, 08:57 in Melbourne

    REQUIRE(rig.session->startNewLog());
    // Not yet: the old log is still the open one.
    CHECK(rig.document().id() == rig.log_id);
    CHECK(started.count() == 0);
    CHECK(rig.session->pending_writes() == 1);
    CHECK_FALSE(rig.session->startNewLog()); // one at a time

    rig.gateway.run_all();

    CHECK(started.count() == 1);
    CHECK(rig.session->state() == LogSession::State::OPEN);
    CHECK(rig.session->pending_writes() == 0);
    CHECK(rig.session->open_log() == document); // the same document, now holding the new log
    CHECK(rig.document().id() != rig.log_id);
    REQUIRE(observer.changes().size() == 1);
    CHECK(observer.changes().front().m_Kind == LogChange::Kind::REPLACED);
    CHECK(header.count() == 1);

    // The new log: the facilitator and today's date, nothing else, and no entries.
    CHECK(rig.session->facilitator() == QStringLiteral("Sam Example"));
    CHECK(rig.session->date_text() == QStringLiteral("Sun 8 Nov"));
    CHECK(rig.session->missing_fields().size() == 3);
    CHECK(rig.session->live_entry_count() == 0);
    CHECK(rig.document().log().entries().empty());

    // The old log is archived with everything it had; the draft was not touched.
    const auto old_log = rig.storage.memory().load_log(rig.log_id);
    REQUIRE(old_log.has_value());
    CHECK(old_log->m_Meta.m_Archived);
    CHECK(old_log->m_Log.entries().size() == 3);
    const auto open = rig.storage.memory().open_log();
    REQUIRE(open.has_value());
    REQUIRE(open->has_value());
    CHECK((*open)->m_Meta.m_Id == rig.document().id());
    CHECK(rig.session->draft() == QStringLiteral("unsent words"));
    CHECK(rig.storage.memory().get_draft().value()->m_Text == "unsent words");

    // Never none open and never two, and in one call.
    CHECK(rig.calls(RepositoryOp::START_NEW_LOG) == 1);
    CHECK(rig.calls(RepositoryOp::ARCHIVE_LOG) == 0);
    for (const std::size_t count : rig.storage.repository().open_log_counts())
    {
        CHECK(count == 1);
    }

    // The registry finds the document by its new id; the archived log is a document of its own.
    CHECK(rig.registry.find(rig.document().id()).get() == document);
    CHECK(rig.registry.find(rig.log_id) == nullptr);

    // Edits of the new log leave the archived one alone.
    REQUIRE(rig.session->correctHeader(QStringLiteral("unit_number"), QStringLiteral("55")));
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.session->unit_text() == QStringLiteral("UNIT-55"));
    const auto old_again = rig.storage.memory().load_log(rig.log_id);
    REQUIRE(old_again.has_value());
    CHECK(header_field_text(old_again->m_Log.initial_header(), HeaderField::UNIT_NUMBER) == "27");
    CHECK(old_again->m_Log.entries().size() == 3);
    rig.document().remove_observer(observer);
}

TEST_CASE("A new log waits for the writes that were accepted before it", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE)); // waits behind the first in the document
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->addNote(QStringLiteral("last words of the day")));

    REQUIRE(rig.session->startNewLog());
    CHECK(rig.calls(RepositoryOp::START_NEW_LOG) == 0);
    // While it is on its way a tap is refused with a message, not written into the log being archived.
    CHECK_FALSE(rig.session->recordError(WRONG_RESPONSE));
    CHECK_FALSE(rig.session->addNote(QStringLiteral("too late")));
    CHECK_FALSE(rig.session->correctHeader(QStringLiteral("unit_number"), QStringLiteral("5")));
    CHECK(failed.count() == 3);

    rig.gateway.run_all();
    CHECK(started.count() == 0); // the three writes have committed; the new log looks again after a moment
    CHECK(rig.calls(RepositoryOp::START_NEW_LOG) == 0);
    rig.world.m_Clock.advance(viewmodel::LogSession::NEW_LOG_POLL);
    rig.gateway.run_all();

    CHECK(started.count() == 1);
    const auto old_log = rig.storage.memory().load_log(rig.log_id);
    REQUIRE(old_log.has_value());
    CHECK(old_log->m_Meta.m_Archived);
    CHECK(old_log->m_Log.entries().size() == 3); // nothing accepted was lost
    CHECK(rig.session->live_entry_count() == 0);

    // The session writes again.
    CHECK(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.session->live_entry_count() == 1);
}

TEST_CASE("A new log that fails leaves the old log open", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    QSignalSpy started(rig.session.get(), &LogSession::newLogStarted);
    QSignalSpy failed(rig.session.get(), &LogSession::writeFailed);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    rig.storage.repository().fail_next(RepositoryOp::START_NEW_LOG);

    REQUIRE(rig.session->startNewLog());
    rig.gateway.run_all();

    CHECK(started.count() == 0);
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == QStringLiteral("startNewLog"));
    CHECK(rig.document().id() == rig.log_id);
    CHECK(rig.session->state() == LogSession::State::OPEN);
    CHECK(rig.session->pending_writes() == 0);
    CHECK(rig.session->live_entry_count() == 1);
    CHECK_FALSE(rig.storage.memory().load_log(rig.log_id).value().m_Meta.m_Archived);

    // Taps work, and so does another try.
    CHECK(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(rig.session->live_entry_count() == 2);
    CHECK(rig.session->startNewLog());
    rig.gateway.run_all();
    CHECK(started.count() == 1);
}

TEST_CASE("A new log cannot be started before there is a log", "[viewmodel][session]")
{
    SessionRig unconfigured(Options().unconfigured());
    CHECK_FALSE(unconfigured.session->startNewLog());
    SessionRig loading(Options().no_load());
    CHECK_FALSE(loading.session->startNewLog());
}

TEST_CASE("The new log of an unconfigured session keeps the facilitator of the log it replaces", "[viewmodel][session]")
{
    SessionRig rig(Options().existing());
    rig.config.clear();
    REQUIRE(rig.session->startNewLog());
    rig.gateway.run_all();
    CHECK(rig.session->facilitator() == QStringLiteral("Sam Example"));
}

// ------------------------------------------------------------------------------------------------ lifetime

TEST_CASE("A session that has gone is not called back", "[viewmodel][session]")
{
    SessionRig rig;
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.session->set_draft(QStringLiteral("x"));
    rig.session->flushDraft();
    REQUIRE(rig.session->startNewLog());
    CHECK(rig.gateway.pending() == 2);

    rig.session.reset();
    rig.gateway.run_all();
    rig.world.m_Clock.advance(1s);
    CHECK(rig.world.m_Clock.pending_timers() == 0);
    CHECK(rig.config.observer_count() == 0);
}

// ------------------------------------------------------------------- a model that follows the session (lifetime)

namespace
{

int tile_count(const viewmodel::BoardModel& board, int row)
{
    return board.data(board.index(row), viewmodel::BoardModel::COUNT).toInt();
}

} // namespace

TEST_CASE("A board that follows the session counts the taps and starts again at a new log", "[viewmodel][session]")
{
    SessionRig rig(Options().existing().no_load());
    // The board is made before the log is loaded: it follows the session from no document to the open one.
    viewmodel::BoardModel board(rig.config, *rig.session);
    CHECK(board.total_count() == 0);
    rig.session->load();
    rig.gateway.run_all();
    REQUIRE(rig.session->open_log() != nullptr);

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.world.m_Clock.advance(1s);
    REQUIRE(rig.session->recordError(SEQUENCE_OUT_OF_ORDER));
    CHECK(board.total_count() == 0); // nothing is counted before the commit
    rig.gateway.run_all();
    CHECK(board.total_count() == 3);
    CHECK(tile_count(board, 0) == 2);
    CHECK(tile_count(board, 1) == 1);
    CHECK(rig.feedback.recorded().size() == 3); // the session confirms; the board only reads

    // A new day: the same document takes the new log and the board starts at zero.
    const LogDocument* const before = rig.session->open_log();
    rig.world.m_Clock.advance(24h);
    REQUIRE(rig.session->startNewLog());
    rig.gateway.run_all();
    CHECK(rig.session->open_log() == before);
    CHECK(board.total_count() == 0);
    CHECK(tile_count(board, 0) == 0);

    REQUIRE(rig.session->recordError(WRONG_RESPONSE));
    rig.gateway.run_all();
    CHECK(board.total_count() == 1);
    CHECK(tile_count(board, 0) == 1);

    // And again.
    REQUIRE(rig.session->startNewLog());
    rig.gateway.run_all();
    CHECK(board.total_count() == 0);
    // The board goes first, then the session, then the document (destruction order of this scope).
}

TEST_CASE("A session that goes tells its followers there is no open log before it lets go of the document",
          "[viewmodel][session]")
{
    FakeOpenLogObserver observer;
    {
        SessionRig rig(Options().existing());
        rig.session->add_observer(observer);
        REQUIRE(observer.documents().empty());
        viewmodel::BoardModel board(rig.config, *rig.session);
        REQUIRE(rig.session->recordError(WRONG_RESPONSE));
        rig.gateway.run_all();
        CHECK(board.total_count() == 1);
    }
    REQUIRE(observer.documents().size() == 1);
    CHECK(observer.documents().front() == nullptr);
}
