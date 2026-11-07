// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden_scenarios.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_config_store.hpp"
#include "support/fake_platform.hpp"
#include "support/fake_sources.hpp"
#include "support/sequential_ids.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/feedback_sink.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
namespace bridge = error_journaling::bridge;
using viewmodel::LogChange;

// ---------------------------------------------------------------------------------------------------------- platform

TEST_CASE("The fake platform ends a sheet at once, as the desktop stub does", "[viewmodel][support][platform]")
{
    EventLog events;
    FakePlatform platform(&events);
    platform.answer_with(bridge::share_outcome::cancelled);
    std::vector<std::string> outcomes;

    platform.present_share_sheet("Sam Example logged a Wrong Response",
                                 [&](const bridge::share_result& result)
                                 {
                                     outcomes.emplace_back(share_outcome_name(result.outcome));
                                     CHECK_FALSE(platform.sheet_open());
                                 });

    CHECK(outcomes == std::vector<std::string>{"cancelled"});
    CHECK(platform.shared_texts() == std::vector<std::string>{"Sam Example logged a Wrong Response"});
    CHECK(events.details(EventKind::SHARE) == std::vector<std::string>{"opened", "outcome cancelled"});
}

TEST_CASE("The fake platform can hold a sheet open until the test ends it, as iOS does",
          "[viewmodel][support][platform]")
{
    FakePlatform platform;
    platform.hold_completion();
    int calls = 0;
    bridge::share_result seen;

    platform.present_share_sheet("text",
                                 [&](const bridge::share_result& result)
                                 {
                                     ++calls;
                                     seen = result;
                                 });
    CHECK(calls == 0);
    CHECK(platform.sheet_open());

    CHECK(platform.complete_share_sheet(bridge::share_outcome::shared, "com.apple.UIKit.activity.CopyToPasteboard"));
    CHECK(calls == 1);
    CHECK(seen.outcome == bridge::share_outcome::shared);
    CHECK(seen.activity_type == "com.apple.UIKit.activity.CopyToPasteboard");
    CHECK_FALSE(platform.sheet_open());
    CHECK_FALSE(platform.complete_share_sheet(bridge::share_outcome::shared));
}

TEST_CASE("The fake platform records haptics and wake-ups", "[viewmodel][support][platform]")
{
    EventLog events;
    FakePlatform platform(&events);

    platform.prepare_haptics();
    platform.fire_haptic(bridge::haptic_kind::impact);
    platform.fire_haptic(bridge::haptic_kind::success);
    platform.fire_haptic(bridge::haptic_kind::impact);

    CHECK(platform.prepared() == 1);
    CHECK(platform.haptic_count(bridge::haptic_kind::impact) == 2);
    CHECK(platform.haptic_count(bridge::haptic_kind::success) == 1);
    CHECK(events.details(EventKind::HAPTIC) == std::vector<std::string>{"impact", "success", "impact"});
}

// ---------------------------------------------------------------------------------------------------------- feedback

TEST_CASE("The product feedback sink fires the impact haptic for a touch and the success haptic for a note",
          "[viewmodel][support][feedback]")
{
    FakePlatform platform;
    viewmodel::PlatformFeedbackSink sink(platform);
    const EntryId entry = test_id<EntryId>(1);

    sink.prepare_haptics();
    sink.acknowledge_touch();
    sink.entry_recorded(entry);
    CHECK(platform.prepared() == 1);
    CHECK(platform.haptics() == std::vector<bridge::haptic_kind>{bridge::haptic_kind::impact});

    sink.note_saved(entry);
    CHECK(platform.haptics() ==
          std::vector<bridge::haptic_kind>{bridge::haptic_kind::impact, bridge::haptic_kind::success});
}

TEST_CASE("A recorded error entry makes no haptic of its own", "[viewmodel][support][feedback]")
{
    FakePlatform platform;
    viewmodel::PlatformFeedbackSink sink(platform);

    sink.entry_recorded(test_id<EntryId>(1));

    CHECK(platform.haptics().empty());
}

TEST_CASE("The null feedback sink accepts every call and does nothing", "[viewmodel][support][feedback]")
{
    viewmodel::NullFeedbackSink sink;
    sink.prepare_haptics();
    sink.acknowledge_touch();
    sink.entry_recorded(test_id<EntryId>(1));
    sink.note_saved(test_id<EntryId>(2));
    SUCCEED("no effect to observe");
}

TEST_CASE("The fake feedback sink puts the touch and the confirmations in order in the event log",
          "[viewmodel][support][feedback]")
{
    EventLog events;
    FakeFeedbackSink sink(&events);
    const EntryId first = test_id<EntryId>(1);
    const EntryId second = test_id<EntryId>(2);

    sink.prepare_haptics();
    sink.acknowledge_touch();
    events.record(EventKind::COMMIT, "add_entry");
    sink.entry_recorded(first);
    sink.note_saved(second);

    CHECK(sink.prepared() == 1);
    CHECK(sink.touches() == 1);
    CHECK(sink.recorded() == std::vector<EntryId>{first});
    CHECK(sink.saved_notes() == std::vector<EntryId>{second});
    CHECK(events.occurred_before({EventKind::HAPTIC, "impact"}, {EventKind::COMMIT, "add_entry"}));
    CHECK(events.occurred_before({EventKind::COMMIT, "add_entry"},
                                 {EventKind::CONFIRMATION, "entry_recorded " + first.format()}));
    CHECK(events.count(EventKind::CONFIRMATION, "note_saved " + second.format()) == 1);
}

// --------------------------------------------------------------------------------------------------------- clipboard

TEST_CASE("The fake clipboard keeps the text byte for byte and the history", "[viewmodel][support][clipboard]")
{
    EventLog events;
    FakeClipboard clipboard(&events);
    CHECK(clipboard.text().empty());
    CHECK(clipboard.copies() == 0);

    const std::string multi_line = "10:05 T| Wrong Response\n13:15 Software Version changed: 1.4.2 \xE2\x86\x92 1.5.0";
    clipboard.set_text("first");
    clipboard.set_text(multi_line);

    CHECK(clipboard.text() == multi_line);
    CHECK(clipboard.history() == std::vector<std::string>{"first", multi_line});
    CHECK(events.count(EventKind::CLIPBOARD) == 2);
}

// ------------------------------------------------------------------------------------------- ids, zone, environment

TEST_CASE("Sequential ids count and match the ids a test names by number", "[viewmodel][support][ids]")
{
    SequentialIds ids(1000);

    CHECK(ids.peek() == 1000);
    CHECK(ids.next_as<EntryId>() == test_id<EntryId>(1000));
    CHECK(ids.next_as<LogId>() == test_id<LogId>(1001));
    CHECK(ids.next() == Uuid::from_random(0, 1002));
    CHECK(ids.made() == 3);
    CHECK(ids.peek() == 1003);
}

TEST_CASE("Sequential ids keep clear of the numbers of the golden scenarios by default", "[viewmodel][support][ids]")
{
    SequentialIds ids;
    CHECK(ids.peek() > 99);
}

TEST_CASE("The fake device zone answers what it was last given", "[viewmodel][support][ids]")
{
    FakeDeviceZone zone;
    CHECK(zone.current() == "Australia/Melbourne");
    zone.set("Australia/Perth");
    CHECK(zone.current() == "Australia/Perth");
}

TEST_CASE("The test environment hands out references to its clock, ids, zone and zones", "[viewmodel][support][ids]")
{
    TestEnvironment world;
    const viewmodel::Environment environment = world.environment();

    CHECK(environment.m_Clock.now() == world.m_Clock.now());
    CHECK(environment.m_Zone.current() == "Australia/Melbourne");
    const auto local = environment.m_Zones.to_local({environment.m_Clock.now(), "Australia/Melbourne"});
    REQUIRE(local.has_value());
    CHECK(local->m_SinceMidnight == std::chrono::hours(8) + std::chrono::minutes(55));
    CHECK(local->m_Date == std::chrono::year{2026} / 11 / 7);

    const auto timer = environment.m_Scheduler.after(std::chrono::seconds(1), [] {});
    CHECK(static_cast<bool>(timer));
    CHECK(world.m_Clock.pending_timers() == 1);
    CHECK(environment.m_Ids.next_as<EntryId>() == test_id<EntryId>(1000));
}

TEST_CASE("The sample configuration is Sam Example's and imports cleanly", "[viewmodel][support][ids]")
{
    const Configuration configuration = sample_configuration();

    CHECK(configuration.m_Facilitator == "Sam Example");
    CHECK(configuration.m_UnitPrefix == "UNIT");
    CHECK_FALSE(configuration.m_ErrorTypes.empty());
    CHECK_FALSE(configuration.m_Locations.empty());
}

// ------------------------------------------------------------------------------------------------------- config store

TEST_CASE("The fake config store keeps the saved text and fails once when asked", "[viewmodel][support][configstore]")
{
    FakeConfigStore store;
    const auto first = store.load();
    REQUIRE(first.has_value());
    CHECK_FALSE(first->has_value());

    REQUIRE(store.save("{\"a\":1}"));
    CHECK(store.load()->value() == "{\"a\":1}");

    store.fail_next_save(make_error(StorageErrorKind::DISK_FULL, "full"));
    const auto failed = store.save("{\"a\":2}");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(store.saved().value() == "{\"a\":1}");
    CHECK(store.save("{\"a\":3}").has_value());
    CHECK(store.saves().size() == 2);

    store.fail_next_load(make_error(StorageErrorKind::IO, "gone"));
    CHECK_FALSE(store.load().has_value());
    CHECK(store.load().has_value());
}

TEST_CASE("The fake config store reads the files a person picks within a size limit",
          "[viewmodel][support][configstore]")
{
    FakeConfigStore store;
    store.add_file("/picked/config.json", "twelve bytes");

    CHECK(store.read_file("/picked/config.json", 100).value() == "twelve bytes");
    CHECK(store.read_file("/picked/config.json", 5).error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(store.read_file("/picked/missing.json", 100).error().m_Kind == StorageErrorKind::NOT_FOUND);

    REQUIRE(store.write_file("/out/export.json", "exported"));
    CHECK(store.files().at("/out/export.json") == "exported");
}

// -------------------------------------------------------------------------------------------------- source fakes

TEST_CASE("The fake configuration source tells its observers once per change", "[viewmodel][support][sources]")
{
    EventLog events;
    FakeConfigurationSource source;
    FakeConfigurationObserver observer(&events);
    source.add_observer(observer);
    CHECK_FALSE(source.configured());
    CHECK(source.observer_count() == 1);

    source.set(sample_configuration());
    CHECK(source.configured());
    CHECK(source.configuration().m_Facilitator == "Sam Example");
    CHECK(observer.changes() == 1);
    CHECK(observer.last_source() == &source);

    source.clear();
    CHECK_FALSE(source.configured());
    CHECK(source.configuration().m_ErrorTypes.empty());
    CHECK(observer.changes() == 2);

    source.remove_observer(observer);
    source.set(sample_configuration());
    CHECK(observer.changes() == 2);
    CHECK(events.details(EventKind::CONFIGURATION) == std::vector<std::string>{"configured", "unconfigured"});
}

TEST_CASE("The fake configuration editor returns a new id at once and applies the type when committed",
          "[viewmodel][support][sources]")
{
    SequentialIds ids;
    FakeConfigurationSource source;
    source.set(sample_configuration());
    const std::size_t before = source.configuration().m_ErrorTypes.size();
    FakeConfigurationObserver observer;
    source.add_observer(observer);
    FakeConfigurationEditor editor(ids, &source);

    const auto added = editor.add_error_type("Wrong Greeting");
    REQUIRE(added.has_value());
    CHECK(*added == test_id<ErrorTypeId>(1000));
    CHECK(editor.pending() == 1);
    CHECK(source.configuration().m_ErrorTypes.size() == before);
    CHECK(observer.changes() == 0);

    editor.commit();
    REQUIRE(source.configuration().m_ErrorTypes.size() == before + 1);
    CHECK(source.configuration().m_ErrorTypes.back().m_Name == "Wrong Greeting");
    CHECK(source.configuration().m_ErrorTypes.back().m_Id == *added);
    CHECK(observer.changes() == 1);
    CHECK(editor.pending() == 0);
    editor.commit();
    CHECK(observer.changes() == 1);
}

TEST_CASE("The fake configuration editor can refuse", "[viewmodel][support][sources]")
{
    SequentialIds ids;
    FakeConfigurationEditor editor(ids);

    const auto empty = editor.add_error_type("");
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().m_Code == viewmodel::ConfigEditError::Code::REFUSED);

    editor.refuse_next({viewmodel::ConfigEditError::Code::QUEUE_FULL, "Try again."});
    const auto busy = editor.add_error_type("Wrong Greeting");
    REQUIRE_FALSE(busy.has_value());
    CHECK(busy.error().m_Code == viewmodel::ConfigEditError::Code::QUEUE_FULL);

    CHECK(editor.add_error_type("Wrong Greeting").has_value());
    CHECK(ids.made() == 1);
}

TEST_CASE("The fake open-log source tells its observers each time the open log changes",
          "[viewmodel][support][sources]")
{
    FakeOpenLogSource source;
    FakeOpenLogObserver observer;
    source.add_observer(observer);
    CHECK(source.open_log() == nullptr);

    source.set(nullptr);
    source.set(nullptr);

    CHECK(observer.documents().size() == 2);
    source.remove_observer(observer);
    source.set(nullptr);
    CHECK(observer.documents().size() == 2);
    CHECK(source.observer_count() == 0);
}

TEST_CASE("The fake log observer records each change in the event log", "[viewmodel][support][sources]")
{
    EventLog events;
    FakeLogObserver observer(&events);
    const EntryId entry = test_id<EntryId>(5);

    observer.note(nullptr, LogChange{LogChange::Kind::ENTRY_ADDED, LogChange::Origin::USER, entry});
    observer.note(nullptr, LogChange{LogChange::Kind::ENTRY_DELETED, LogChange::Origin::UNDO, entry});
    observer.note(nullptr, LogChange{LogChange::Kind::REPLACED, LogChange::Origin::USER, std::nullopt});

    CHECK(observer.changes().size() == 3);
    CHECK(observer.changes()[1].m_Origin == LogChange::Origin::UNDO);
    CHECK(events.details(EventKind::LOG_CHANGE) ==
          std::vector<std::string>{"ENTRY_ADDED " + entry.format(), "ENTRY_DELETED " + entry.format() + " (undo)",
                                   "REPLACED"});
}

TEST_CASE("Every kind of log change has a name", "[viewmodel][support][sources]")
{
    for (const auto kind :
         {LogChange::Kind::LOADED, LogChange::Kind::REPLACED, LogChange::Kind::ENTRY_ADDED,
          LogChange::Kind::ENTRY_EDITED, LogChange::Kind::ENTRY_DELETED, LogChange::Kind::ENTRY_RESTORED,
          LogChange::Kind::HEADER_CORRECTED, LogChange::Kind::HEADER_CHANGED, LogChange::Kind::SHARED})
    {
        CHECK(describe_log_change(LogChange{kind, LogChange::Origin::USER, std::nullopt}) != "UNKNOWN");
    }
}
