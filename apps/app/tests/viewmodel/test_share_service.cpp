// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.hpp"
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_platform.hpp"
#include "support/fake_sources.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/share_service.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QSignalSpy>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

using namespace ej;
using namespace ej::test;
using viewmodel::DocumentRegistry;
using viewmodel::export_text;
using viewmodel::ExportContextBuilder;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::ShareService;
using Origin = LogChange::Origin;
using namespace std::chrono_literals;

namespace
{

constexpr std::uint64_t DAY = 2;      // the log of the PRD example
constexpr std::uint64_t NEXT_DAY = 3; // the log that follows it

LocationId location_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_Locations, name, &Location::m_Name);
    REQUIRE(it != configuration.m_Locations.end());
    return it->m_Id;
}

ErrorTypeId type_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_ErrorTypes, name, &ErrorType::m_Name);
    REQUIRE(it != configuration.m_ErrorTypes.end());
    return it->m_Id;
}

Entry error_at(std::uint64_t number, int hour, int minute, ErrorTypeId type, std::string label,
               std::optional<std::string> note = std::nullopt)
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(number);
    entry.m_Captured = on_day(hour, minute);
    entry.m_Body = ErrorEntry{type, std::move(note), std::move(label)};
    return entry;
}

QString text_of(const QVariant& value)
{
    return value.toString();
}

/// The sample configuration in force, the log of the PRD example (id DAY) in the store and as the open document, a
/// registry over the manual gateway, and a service over the fake platform.
struct Fixture
{
    explicit Fixture(bool configured = true, bool available = true) : m_Available(available)
    {
        if (configured)
        {
            m_Configuration.set(sample_configuration());
        }
        HeaderValues header = sample_header();
        header.m_Location = LocationValue{location_named(sample_configuration(), "North Site"), {}};
        REQUIRE(m_Rig.storage().memory().start_new_log(m_Rig.log_id(), day(), header));
        document = m_Registry.make_document(m_Rig.storage().memory().load_log(day()).value());
        m_Open.set(document.get());

        if (configured)
        {
            const Configuration sample = sample_configuration();
            REQUIRE(document->add_entry(error_at(1, 10, 5, type_named(sample, "Wrong Response"), "Wrong Response"),
                                        Origin::USER, {}));
            REQUIRE(document->add_entry(error_at(2, 10, 20, type_named(sample, "Mute Delay"), "Mute Delay"),
                                        Origin::USER, {}));
            Entry note;
            note.m_Id = sample_id<EntryId>(3);
            note.m_Captured = on_day(10, 30);
            note.m_Body = NoteEntry{std::nullopt, "Played Morning Medley"};
            REQUIRE(document->add_entry(std::move(note), Origin::USER, {}));
            REQUIRE(document->add_entry(error_at(4, 11, 2, type_named(sample, "Wrong Track Played"),
                                                 "Wrong Track Played", "asked for Evening Song"),
                                        Origin::USER, {}));
            REQUIRE(document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(13, 15),
                                            sample_id<EntryId>(5), {}));
            REQUIRE(document->add_entry(error_at(6, 13, 40, type_named(sample, "Speech Delay"), "Speech Delay"),
                                        Origin::USER, {}));
            m_Rig.gateway().run_all();
        }
        m_Rig.events().clear();
        make_service();
    }

    void make_service()
    {
        service = std::make_unique<ShareService>(m_Rig.world().environment(), m_Open, m_Registry, m_Configuration,
                                                 m_Platform, m_Available);
    }

    [[nodiscard]] static LogId day()
    {
        return sample_id<LogId>(DAY);
    }
    [[nodiscard]] static LogId next_day()
    {
        return sample_id<LogId>(NEXT_DAY);
    }
    [[nodiscard]] static QString day_text()
    {
        return QString::fromStdString(day().format());
    }

    /// The PRD example by the renderer, from the log as it is now.
    [[nodiscard]] std::string expected_text()
    {
        const auto text =
            export_text(document->log(), ExportContextBuilder(m_Configuration, m_Rig.world().m_Zones).build());
        REQUIRE(text.has_value());
        return *text;
    }

    [[nodiscard]] std::optional<UtcMillis> stored_share(LogId id)
    {
        const auto stored = m_Rig.storage().memory().load_log(id);
        REQUIRE(stored.has_value());
        return stored->m_Meta.m_LastSharedUtc;
    }

    /// Ends the day: the log is archived, the next one is open, and the document of the day is not held by anyone.
    void archive_day()
    {
        open_next();
        document.reset();
    }

    /// Starts the next log in the store, and returns it loaded.
    StoredLog open_next()
    {
        REQUIRE(m_Rig.storage().memory().start_new_log(day(), next_day(), sample_header()));
        return m_Rig.storage().memory().load_log(next_day()).value();
    }

    bool m_Available;
    DocumentRig m_Rig{false};
    FakePlatform m_Platform{&m_Rig.events()};
    FakeConfigurationSource m_Configuration;
    FakeOpenLogSource m_Open;
    DocumentRegistry m_Registry{m_Rig.world().environment(), m_Rig.gateway()};
    std::shared_ptr<LogDocument> document;
    std::unique_ptr<ShareService> service;
};

} // namespace

// ------------------------------------------------------------------------------------------------------- the text

TEST_CASE("The text on the sheet is the PRD example, byte for byte", "[viewmodel][share][golden]")
{
    Fixture fixture;
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

    REQUIRE(fixture.service->shareOpenLog());

    REQUIRE(fixture.m_Platform.sheets_presented() == 1);
    check_golden("prd-example", fixture.m_Platform.shared_texts()[0]);
    CHECK(fixture.m_Platform.shared_texts()[0] == fixture.expected_text());
    REQUIRE(finished.count() == 1);
    CHECK(text_of(finished.at(0).at(0)) == Fixture::day_text());
    CHECK(text_of(finished.at(0).at(1)) == "shared");
}

TEST_CASE("The text is what the screen showed: a committed edit is in it, one still waiting is not",
          "[viewmodel][share]")
{
    Fixture fixture;
    const Configuration sample = sample_configuration();
    REQUIRE(fixture.document->add_entry(error_at(7, 14, 3, type_named(sample, "Track Delay"), "Track Delay"),
                                        Origin::USER, {}));
    REQUIRE(fixture.document->pending() == 1);

    fixture.m_Platform.answer_with(FakePlatform::share_outcome::CANCELLED);
    REQUIRE(fixture.service->shareOpenLog());
    REQUIRE(fixture.m_Platform.sheets_presented() == 1);
    CHECK(fixture.m_Platform.shared_texts()[0].find("14:03") == std::string::npos); // the entry has not committed

    fixture.m_Rig.gateway().run_all();
    REQUIRE(fixture.service->shareOpenLog());
    REQUIRE(fixture.m_Platform.sheets_presented() == 2);
    CHECK(fixture.m_Platform.shared_texts()[1].find("\n14:03 T| Track Delay") != std::string::npos);
}

// -------------------------------------------------------------------------------------------- what counts as shared

TEST_CASE("A share records the time the sheet completed, and only after the commit", "[viewmodel][share]")
{
    Fixture fixture;
    TestClock& clock = fixture.m_Rig.world().m_Clock;
    fixture.m_Platform.hold_completion();
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);
    QSignalSpy recorded(fixture.service.get(), &ShareService::shareRecorded);
    QSignalSpy failed(fixture.service.get(), &ShareService::writeFailed);

    REQUIRE(fixture.service->shareOpenLog());
    const UtcMillis opened = clock.now();
    CHECK(fixture.service->sharing());
    CHECK(fixture.m_Platform.sheet_open());

    clock.advance(20s);
    const UtcMillis completed = clock.now();
    REQUIRE(completed != opened);
    // The sheet's own Copy action reports as shared, with its activity type.
    REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::SHARED,
                                                    "com.apple.UIKit.activity.CopyToPasteboard"));

    CHECK_FALSE(fixture.service->sharing());
    REQUIRE(finished.count() == 1);
    CHECK(text_of(finished.at(0).at(1)) == "shared");
    CHECK(recorded.count() == 0); // nothing is committed yet
    CHECK(fixture.document->last_shared() == std::nullopt);
    CHECK(fixture.stored_share(Fixture::day()) == std::nullopt);
    REQUIRE(fixture.m_Rig.gateway().pending() == 1);

    clock.advance(5s); // a slow commit does not move the share time
    fixture.m_Rig.gateway().run_all();

    CHECK(fixture.stored_share(Fixture::day()) == completed);
    CHECK(fixture.document->last_shared() == completed);
    CHECK(recorded.count() == 1);
    CHECK(failed.count() == 0);
    CHECK(fixture.m_Rig.events().occurred_before(Event{EventKind::SHARE, "outcome shared"},
                                                 Event{EventKind::COMMIT, "mark_shared " + Fixture::day().format()}));
    CHECK(fixture.m_Rig.events().count(EventKind::COMMIT) == 1);
}

TEST_CASE("A share that is not shared records nothing", "[viewmodel][share]")
{
    Fixture fixture;
    const auto outcome = GENERATE(FakePlatform::share_outcome::CANCELLED, FakePlatform::share_outcome::UNAVAILABLE,
                                  FakePlatform::share_outcome::FAILED);
    fixture.m_Platform.answer_with(outcome);
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);
    QSignalSpy recorded(fixture.service.get(), &ShareService::shareRecorded);
    QSignalSpy failed(fixture.service.get(), &ShareService::writeFailed);

    REQUIRE(fixture.service->shareOpenLog());
    fixture.m_Rig.gateway().run_all();

    REQUIRE(finished.count() == 1);
    CHECK(text_of(finished.at(0).at(1)) == QString::fromStdString(std::string(share_outcome_name(outcome))));
    CHECK(recorded.count() == 0);
    CHECK(failed.count() == 0);
    CHECK(fixture.m_Rig.storage().repository().call_count(RepositoryOp::MARK_SHARED) == 0);
    CHECK(fixture.stored_share(Fixture::day()) == std::nullopt);
    CHECK(fixture.document->last_shared() == std::nullopt);
    CHECK_FALSE(fixture.service->sharing());
}

TEST_CASE("A share never fires a haptic", "[viewmodel][share]")
{
    Fixture fixture;
    fixture.m_Platform.hold_completion();

    REQUIRE(fixture.service->shareOpenLog());
    REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::SHARED));
    fixture.m_Rig.gateway().run_all();
    REQUIRE(fixture.service->shareOpenLog());
    REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::CANCELLED));

    CHECK(fixture.m_Platform.haptics().empty());
    CHECK(fixture.m_Platform.prepared() == 0);
    CHECK(fixture.m_Rig.events().count(EventKind::HAPTIC) == 0);
}

TEST_CASE("A share does not clear on an edit and a later share moves the time", "[viewmodel][share]")
{
    Fixture fixture;
    TestClock& clock = fixture.m_Rig.world().m_Clock;
    REQUIRE(fixture.service->shareOpenLog());
    fixture.m_Rig.gateway().run_all();
    const UtcMillis first = clock.now();
    REQUIRE(fixture.document->last_shared() == first);

    clock.advance(1min);
    Entry edited = error_at(1, 10, 6, type_named(sample_configuration(), "Wrong Response"), "Wrong Response");
    REQUIRE(fixture.document->edit_entry(sample_id<EntryId>(1), std::move(edited), {}));
    fixture.m_Rig.gateway().run_all();
    CHECK(fixture.document->last_shared() == first);

    clock.advance(1min);
    REQUIRE(fixture.service->shareOpenLog());
    fixture.m_Rig.gateway().run_all();
    CHECK(fixture.document->last_shared() == clock.now());
    CHECK(fixture.stored_share(Fixture::day()) == clock.now());
}

// ---------------------------------------------------------------------------------------- when the sheet completes

TEST_CASE("A completion before the call returns and one after it both work", "[viewmodel][share]")
{
    Fixture fixture;
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

    SECTION("before: the desktop stub")
    {
        fixture.m_Platform.answer_with(FakePlatform::share_outcome::UNAVAILABLE);
        REQUIRE(fixture.service->shareOpenLog());
        CHECK_FALSE(fixture.service->sharing());
        REQUIRE(finished.count() == 1);
        CHECK(text_of(finished.at(0).at(1)) == "unavailable");
    }

    SECTION("after: a phone")
    {
        fixture.m_Platform.hold_completion();
        REQUIRE(fixture.service->shareOpenLog());
        CHECK(fixture.service->sharing());
        CHECK(finished.count() == 0);
        REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::CANCELLED));
        CHECK_FALSE(fixture.service->sharing());
        REQUIRE(finished.count() == 1);
        CHECK(text_of(finished.at(0).at(1)) == "cancelled");
    }
}

TEST_CASE("A completion after the service is destroyed is ignored", "[viewmodel][share]")
{
    Fixture fixture;
    fixture.m_Platform.hold_completion();
    REQUIRE(fixture.service->shareOpenLog());
    const auto posted = fixture.m_Rig.gateway().posted();

    fixture.service.reset();
    REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::SHARED));

    CHECK(fixture.m_Rig.gateway().posted() == posted); // nothing was recorded
    CHECK(fixture.stored_share(Fixture::day()) == std::nullopt);
}

TEST_CASE("The sheet of a log that a new day replaced records on the log that was shared", "[viewmodel][share]")
{
    Fixture fixture;
    TestClock& clock = fixture.m_Rig.world().m_Clock;
    fixture.m_Platform.hold_completion();
    REQUIRE(fixture.service->shareOpenLog());

    // The new day starts while the sheet is up: the open document takes the next log.
    StoredLog next = fixture.open_next();
    fixture.document->replace(std::move(next));
    clock.advance(10s);
    const UtcMillis completed = clock.now();

    REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::SHARED));
    CHECK(fixture.m_Rig.gateway().pending() == 1); // the shared log is loaded for the write
    fixture.m_Rig.gateway().run_all();

    CHECK(fixture.stored_share(Fixture::day()) == completed);
    CHECK(fixture.stored_share(Fixture::next_day()) == std::nullopt);
    CHECK(fixture.document->id() == Fixture::next_day());
    CHECK(fixture.document->last_shared() == std::nullopt);
}

// -------------------------------------------------------------------------------------------------- one at a time

TEST_CASE("A second call during a share returns false, and works again after the outcome", "[viewmodel][share]")
{
    Fixture fixture;
    fixture.m_Platform.hold_completion();
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);
    QSignalSpy changed(fixture.service.get(), &ShareService::sharingChanged);

    REQUIRE(fixture.service->shareOpenLog());
    REQUIRE(changed.count() == 1);

    CHECK_FALSE(fixture.service->shareOpenLog());
    CHECK_FALSE(fixture.service->shareLog(Fixture::day_text()));
    CHECK_FALSE(fixture.service->shareLog(QStringLiteral("not an id")));
    CHECK(fixture.m_Platform.sheets_presented() == 1); // no second sheet
    CHECK(finished.count() == 0);                      // and no signal from the refusals
    CHECK(changed.count() == 1);
    CHECK(fixture.service->sharing());

    REQUIRE(fixture.m_Platform.complete_share_sheet(FakePlatform::share_outcome::CANCELLED));
    CHECK(finished.count() == 1); // the first share was not disturbed
    CHECK(changed.count() == 2);
    CHECK_FALSE(fixture.service->sharing());

    REQUIRE(fixture.service->shareOpenLog());
    CHECK(fixture.m_Platform.sheets_presented() == 2);
    CHECK(fixture.m_Platform.sheet_open());
}

TEST_CASE("A second call while a past log loads returns false", "[viewmodel][share]")
{
    Fixture fixture;
    fixture.archive_day();
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

    REQUIRE(fixture.service->shareLog(Fixture::day_text()));
    CHECK(fixture.service->sharing());
    CHECK(fixture.m_Rig.gateway().pending() == 1);
    CHECK(fixture.m_Platform.sheets_presented() == 0);
    CHECK_FALSE(fixture.service->shareLog(Fixture::day_text()));

    fixture.m_Rig.gateway().run_all();
    CHECK(fixture.m_Platform.sheets_presented() == 1);
    CHECK(finished.count() == 1);
}

// -------------------------------------------------------------------------------------------------- failures

TEST_CASE("Text that cannot be rendered shows no sheet and reports failed", "[viewmodel][share]")
{
    SECTION("an unknown location id")
    {
        Fixture fixture(false);
        // A configuration that does not know the location of the log.
        Configuration other = sample_configuration();
        std::erase_if(other.m_Locations, [](const Location& location) { return location.m_Name == "North Site"; });
        fixture.m_Configuration.set(other);
        QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

        REQUIRE(fixture.service->shareOpenLog());

        CHECK(fixture.m_Platform.sheets_presented() == 0);
        REQUIRE(finished.count() == 1);
        CHECK(text_of(finished.at(0).at(1)) == "failed");
        CHECK_FALSE(fixture.service->sharing());
        CHECK(fixture.m_Rig.gateway().pending() == 0);
    }

    SECTION("no configuration")
    {
        Fixture fixture(false);
        QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

        REQUIRE(fixture.service->shareOpenLog());

        CHECK(fixture.m_Platform.sheets_presented() == 0);
        REQUIRE(finished.count() == 1);
        CHECK(text_of(finished.at(0).at(1)) == "failed");
    }
}

TEST_CASE("There is no open log", "[viewmodel][share]")
{
    Fixture fixture;
    fixture.m_Open.set(nullptr);
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

    CHECK_FALSE(fixture.service->shareOpenLog());

    CHECK(fixture.m_Platform.sheets_presented() == 0);
    REQUIRE(finished.count() == 1);
    CHECK(text_of(finished.at(0).at(0)).isEmpty());
    CHECK(text_of(finished.at(0).at(1)) == "failed");
    CHECK_FALSE(fixture.service->sharing());
}

TEST_CASE("A failing mark-shared commit emits writeFailed and a later share records it", "[viewmodel][share]")
{
    Fixture fixture;
    TestClock& clock = fixture.m_Rig.world().m_Clock;
    fixture.m_Rig.storage().repository().fail_next(RepositoryOp::MARK_SHARED);
    QSignalSpy recorded(fixture.service.get(), &ShareService::shareRecorded);
    QSignalSpy failed(fixture.service.get(), &ShareService::writeFailed);

    REQUIRE(fixture.service->shareOpenLog());
    fixture.m_Rig.gateway().run_all();

    REQUIRE(failed.count() == 1);
    CHECK(text_of(failed.at(0).at(0)) == "markShared");
    CHECK_FALSE(text_of(failed.at(0).at(1)).isEmpty());
    CHECK(recorded.count() == 0);
    CHECK(fixture.document->last_shared() == std::nullopt);
    CHECK(fixture.stored_share(Fixture::day()) == std::nullopt);

    clock.advance(1min);
    REQUIRE(fixture.service->shareOpenLog());
    fixture.m_Rig.gateway().run_all();

    CHECK(failed.count() == 1);
    CHECK(recorded.count() == 1);
    CHECK(fixture.document->last_shared() == clock.now());
    CHECK(fixture.stored_share(Fixture::day()) == clock.now());
}

// ------------------------------------------------------------------------------------------------------- History

TEST_CASE("Sharing a past log loads it, shares the golden text and records the time", "[viewmodel][share][golden]")
{
    Fixture fixture;
    const std::string golden = fixture.expected_text();
    fixture.archive_day(); // the day's document goes away; the log is in the store only
    TestClock& clock = fixture.m_Rig.world().m_Clock;
    clock.advance(8h);
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);
    QSignalSpy recorded(fixture.service.get(), &ShareService::shareRecorded);
    REQUIRE(fixture.m_Registry.find(Fixture::day()) == nullptr);

    REQUIRE(fixture.service->shareLog(Fixture::day_text()));
    CHECK(fixture.m_Platform.sheets_presented() == 0); // the load has not run
    fixture.m_Rig.gateway().run_next();                // the load, and then the sheet
    REQUIRE(fixture.m_Platform.sheets_presented() == 1);
    CHECK(fixture.m_Platform.shared_texts()[0] == golden);
    check_golden("prd-example", fixture.m_Platform.shared_texts()[0]);

    CHECK(fixture.stored_share(Fixture::day()) == std::nullopt);
    fixture.m_Rig.gateway().run_all(); // the share is committed to an archived log

    CHECK(fixture.stored_share(Fixture::day()) == clock.now());
    REQUIRE(recorded.count() == 1);
    CHECK(text_of(recorded.at(0).at(0)) == Fixture::day_text());
    REQUIRE(finished.count() == 1);
    CHECK(text_of(finished.at(0).at(1)) == "shared");
}

TEST_CASE("A re-share of a past log tells the document's observers, which is the History row", "[viewmodel][share]")
{
    Fixture fixture;
    fixture.archive_day();
    // History holds the document of the past log it shows.
    std::shared_ptr<LogDocument> held;
    fixture.m_Registry.acquire(Fixture::day(), [&held](auto loaded) { held = std::move(*loaded); });
    fixture.m_Rig.gateway().run_all();
    REQUIRE(held);
    FakeLogObserver observer;
    held->add_observer(observer);
    fixture.m_Rig.world().m_Clock.advance(1h);

    REQUIRE(fixture.service->shareLog(Fixture::day_text()));
    CHECK(fixture.m_Rig.gateway().pending() == 1); // no load, the document is live: only the write is queued
    fixture.m_Rig.gateway().run_all();

    REQUIRE(observer.changes().size() == 1);
    CHECK(observer.changes()[0].m_Kind == LogChange::Kind::SHARED);
    CHECK(held->last_shared() == fixture.m_Rig.world().m_Clock.now());
    CHECK(fixture.m_Rig.storage().repository().call_count(RepositoryOp::LOAD_LOG) == 1); // only History's load
    held->remove_observer(observer);
}

TEST_CASE("Sharing an unknown or a malformed id reports and shows no sheet", "[viewmodel][share]")
{
    Fixture fixture;
    QSignalSpy finished(fixture.service.get(), &ShareService::shareFinished);

    SECTION("an id the store does not have")
    {
        const QString stranger = QString::fromStdString(sample_id<LogId>(777).format());
        REQUIRE(fixture.service->shareLog(stranger));
        CHECK(fixture.service->sharing());
        fixture.m_Rig.gateway().run_all();

        REQUIRE(finished.count() == 1);
        CHECK(text_of(finished.at(0).at(0)) == stranger);
        CHECK(text_of(finished.at(0).at(1)) == "failed");
        CHECK_FALSE(fixture.service->sharing());
    }

    SECTION("text that is not an id")
    {
        CHECK_FALSE(fixture.service->shareLog(QStringLiteral("yesterday")));

        REQUIRE(finished.count() == 1);
        CHECK(text_of(finished.at(0).at(0)) == "yesterday");
        CHECK(text_of(finished.at(0).at(1)) == "failed");
        CHECK_FALSE(fixture.service->sharing());
    }

    CHECK(fixture.m_Platform.sheets_presented() == 0);
}

// ------------------------------------------------------------------------------------------------------- misc

TEST_CASE("available is what the composition root says and does not stop a share", "[viewmodel][share]")
{
    Fixture stub(true, false);
    CHECK_FALSE(stub.service->available());
    stub.m_Platform.answer_with(FakePlatform::share_outcome::UNAVAILABLE);
    QSignalSpy finished(stub.service.get(), &ShareService::shareFinished);

    CHECK(stub.service->shareOpenLog());

    REQUIRE(finished.count() == 1);
    CHECK(text_of(finished.at(0).at(1)) == "unavailable");

    Fixture phone(true, true);
    CHECK(phone.service->available());
}
