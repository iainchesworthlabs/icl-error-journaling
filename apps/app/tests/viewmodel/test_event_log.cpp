// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/event_log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <vector>

using ej::test::Event;
using ej::test::EventKind;
using ej::test::EventLog;

TEST_CASE("An event log keeps events in the order they were recorded", "[viewmodel][support][eventlog]")
{
    EventLog log;
    log.record(EventKind::HAPTIC, "impact");
    log.record(EventKind::COMMIT, "add_entry a");
    log.record(EventKind::CONFIRMATION, "entry_recorded a");

    const std::vector<Event> expected{{EventKind::HAPTIC, "impact"},
                                      {EventKind::COMMIT, "add_entry a"},
                                      {EventKind::CONFIRMATION, "entry_recorded a"}};
    CHECK(log.events() == expected);
    CHECK(log.size() == 3);
}

TEST_CASE("An event log counts by kind and by kind and detail", "[viewmodel][support][eventlog]")
{
    EventLog log;
    log.record(EventKind::HAPTIC, "impact");
    log.record(EventKind::HAPTIC, "impact");
    log.record(EventKind::HAPTIC, "success");
    log.record(EventKind::TOAST);

    CHECK(log.count(EventKind::HAPTIC) == 3);
    CHECK(log.count(EventKind::HAPTIC, "impact") == 2);
    CHECK(log.count(EventKind::HAPTIC, "success") == 1);
    CHECK(log.count(EventKind::TOAST) == 1);
    CHECK(log.count(EventKind::SHARE) == 0);
    CHECK(log.details(EventKind::HAPTIC) == std::vector<std::string>{"impact", "impact", "success"});
}

TEST_CASE("An event log finds the first and last position of an event", "[viewmodel][support][eventlog]")
{
    EventLog log;
    log.record(EventKind::HAPTIC, "impact");    // 0
    log.record(EventKind::COMMIT, "add_entry"); // 1
    log.record(EventKind::HAPTIC, "impact");    // 2

    CHECK(log.first_index(EventKind::HAPTIC, "impact") == 0);
    CHECK(log.last_index(EventKind::HAPTIC, "impact") == 2);
    CHECK(log.first_index(EventKind::COMMIT, "add_entry") == 1);
    CHECK_FALSE(log.first_index(EventKind::COMMIT, "other").has_value());
    CHECK_FALSE(log.last_index(EventKind::SHARE, "opened").has_value());
}

TEST_CASE("An event log tells which of two events came first", "[viewmodel][support][eventlog]")
{
    EventLog log;
    log.record(EventKind::HAPTIC, "impact");
    log.record(EventKind::COMMIT, "add_entry");
    const Event haptic{EventKind::HAPTIC, "impact"};
    const Event commit{EventKind::COMMIT, "add_entry"};
    const Event missing{EventKind::TOAST, "open"};

    CHECK(log.occurred_before(haptic, commit));
    CHECK_FALSE(log.occurred_before(commit, haptic));
    CHECK_FALSE(log.occurred_before(haptic, missing));
    CHECK_FALSE(log.occurred_before(missing, commit));
}

TEST_CASE("An event log formats one line per event and can be cleared", "[viewmodel][support][eventlog]")
{
    EventLog log;
    log.record(EventKind::HAPTIC, "impact");
    log.record(EventKind::TOAST);

    CHECK(log.format() == "0 HAPTIC impact\n1 TOAST\n");

    log.clear();
    CHECK(log.size() == 0);
    CHECK(log.format().empty());
}

TEST_CASE("An event log can be written from another thread", "[viewmodel][support][eventlog]")
{
    EventLog log;
    std::thread writer(
        [&log]
        {
            for (int i = 0; i < 100; ++i)
            {
                log.record(EventKind::COMMIT, "from the storage thread");
            }
        });
    for (int i = 0; i < 100; ++i)
    {
        log.record(EventKind::HAPTIC, "impact");
    }
    writer.join();

    CHECK(log.count(EventKind::COMMIT) == 100);
    CHECK(log.count(EventKind::HAPTIC) == 100);
}
