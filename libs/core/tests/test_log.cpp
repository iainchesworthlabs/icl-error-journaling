// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/log.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace ej;
using ej::test::test_id;
using namespace std::chrono_literals;

namespace
{

UtcMillis at_minute(int minute)
{
    return std::chrono::sys_days{std::chrono::year{2026} / 11 / 7} + std::chrono::minutes(minute);
}

Entry make_entry(std::uint64_t id, int minute = 0, std::uint64_t error_type = 1)
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = Timestamp{at_minute(minute), "Australia/Melbourne"};
    entry.m_Body = ErrorEntry{test_id<ErrorTypeId>(error_type), std::nullopt};
    return entry;
}

std::vector<std::uint64_t> order_of(const Log& log, bool include_deleted = false)
{
    std::vector<std::uint64_t> ids;
    for (const Entry* entry : log.sorted_entries(include_deleted))
    {
        // test_id puts the number in the last 8 bytes of the id
        std::uint64_t n = 0;
        for (std::size_t i = 8; i < 16; ++i)
        {
            n = (n << 8) | entry->m_Id.uuid().bytes()[i];
        }
        // from_random sets the variant bits in byte 8, which are zero in the low word's top byte
        ids.push_back(n & 0x00FFFFFFFFFFFFFFULL);
    }
    return ids;
}

}  // namespace

TEST_CASE("A log keeps entries in insertion order and rejects a duplicate id", "[log]")
{
    Log log{HeaderValues{}};
    CHECK(log.entries().empty());
    REQUIRE(log.add(make_entry(2)).has_value());
    REQUIRE(log.add(make_entry(1)).has_value());
    REQUIRE(log.entries().size() == 2);
    CHECK(log.entries()[0].m_Id == test_id<EntryId>(2));
    CHECK(log.entries()[1].m_Id == test_id<EntryId>(1));

    const auto duplicate = log.add(make_entry(2));
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error() == Log::Error::DUPLICATE_ID);
    CHECK(log.entries().size() == 2);
}

TEST_CASE("Entries added out of order come back sorted by UTC time, ties by insertion", "[log]")
{
    Log log{HeaderValues{}};
    REQUIRE(log.add(make_entry(1, 30)).has_value());
    REQUIRE(log.add(make_entry(2, 10)).has_value());
    REQUIRE(log.add(make_entry(3, 20)).has_value());
    REQUIRE(log.add(make_entry(4, 10)).has_value());
    REQUIRE(log.add(make_entry(5, 10)).has_value());
    CHECK(order_of(log) == std::vector<std::uint64_t>{2, 4, 5, 3, 1});
}

TEST_CASE("Soft delete hides an entry and restore brings it back", "[log]")
{
    Log log{HeaderValues{}};
    REQUIRE(log.add(make_entry(1, 10, 7)).has_value());
    REQUIRE(log.add(make_entry(2, 20, 7)).has_value());
    CHECK(log.count_for(test_id<ErrorTypeId>(7)) == 2);

    REQUIRE(log.set_deleted(test_id<EntryId>(1), true).has_value());
    CHECK(order_of(log) == std::vector<std::uint64_t>{2});
    CHECK(order_of(log, true) == std::vector<std::uint64_t>{1, 2});
    CHECK(log.count_for(test_id<ErrorTypeId>(7)) == 1);
    CHECK(log.entries().size() == 2);  // never physically removed

    REQUIRE(log.set_deleted(test_id<EntryId>(1), false).has_value());
    CHECK(order_of(log) == std::vector<std::uint64_t>{1, 2});
    CHECK(log.count_for(test_id<ErrorTypeId>(7)) == 2);
}

TEST_CASE("An unknown id is reported by set_deleted, replace and replace_body", "[log]")
{
    Log log{HeaderValues{}};
    REQUIRE(log.add(make_entry(1)).has_value());
    const auto missing = test_id<EntryId>(99);
    CHECK(log.set_deleted(missing, true).error() == Log::Error::UNKNOWN_ENTRY);
    CHECK(log.replace(missing, make_entry(99), at_minute(1)).error() == Log::Error::UNKNOWN_ENTRY);
    CHECK(log.replace_body(missing, NoteEntry{}).error() == Log::Error::UNKNOWN_ENTRY);
    CHECK(log.find(missing) == nullptr);
    CHECK(log.find(test_id<EntryId>(1)) != nullptr);
}

TEST_CASE("replace changes the time, re-sorts and records the edit time", "[log]")
{
    Log log{HeaderValues{}};
    REQUIRE(log.add(make_entry(1, 10)).has_value());
    REQUIRE(log.add(make_entry(2, 20)).has_value());
    REQUIRE(log.set_deleted(test_id<EntryId>(1), true).has_value());

    Entry edit = make_entry(2, 5);
    edit.m_Captured.m_Zone.clear();   // the edit gives no zone: the captured one is kept
    edit.m_Id = test_id<EntryId>(77); // and the id cannot be changed
    REQUIRE(log.replace(test_id<EntryId>(2), edit, at_minute(100)).has_value());

    const Entry* replaced = log.find(test_id<EntryId>(2));
    REQUIRE(replaced != nullptr);
    CHECK(replaced->m_Captured.m_Utc == at_minute(5));
    CHECK(replaced->m_Captured.m_Zone == "Australia/Melbourne");
    CHECK(replaced->m_EditedUtc == at_minute(100));
    CHECK(log.find(test_id<EntryId>(77)) == nullptr);
    CHECK(order_of(log, true) == std::vector<std::uint64_t>{2, 1});

    // an edit that gives a zone replaces it, and the deleted flag survives an edit
    Entry other_zone = make_entry(1, 10);
    other_zone.m_Captured.m_Zone = "Australia/Perth";
    REQUIRE(log.replace(test_id<EntryId>(1), other_zone, at_minute(101)).has_value());
    CHECK(log.find(test_id<EntryId>(1))->m_Captured.m_Zone == "Australia/Perth");
    CHECK(log.find(test_id<EntryId>(1))->m_Deleted);
}

TEST_CASE("replace_body swaps the body and records no edit", "[log]")
{
    Log log{HeaderValues{}};
    REQUIRE(log.add(make_entry(1, 10)).has_value());
    REQUIRE(log.replace_body(test_id<EntryId>(1), NoteEntry{std::nullopt, "text"}).has_value());
    const Entry* entry = log.find(test_id<EntryId>(1));
    REQUIRE(entry != nullptr);
    CHECK(std::holds_alternative<NoteEntry>(entry->m_Body));
    CHECK_FALSE(entry->m_EditedUtc.has_value());
}

TEST_CASE("Counting ignores notes, header changes and deleted entries and counts each type separately", "[log]")
{
    Log log{HeaderValues{}};
    REQUIRE(log.add(make_entry(1, 1, 1)).has_value());
    REQUIRE(log.add(make_entry(2, 2, 1)).has_value());
    REQUIRE(log.add(make_entry(3, 3, 2)).has_value());
    REQUIRE(log.add(make_entry(4, 4, 1)).has_value());
    REQUIRE(log.set_deleted(test_id<EntryId>(4), true).has_value());

    Entry note = make_entry(5, 5);
    note.m_Body = NoteEntry{std::nullopt, "text"};
    REQUIRE(log.add(note).has_value());
    Entry change = make_entry(6, 6);
    change.m_Body = HeaderChangeEntry{HeaderField::SOFTWARE_VERSION, "1.5.0"};
    REQUIRE(log.add(change).has_value());

    CHECK(log.count_for(test_id<ErrorTypeId>(1)) == 2);
    CHECK(log.count_for(test_id<ErrorTypeId>(2)) == 1);
    CHECK(log.count_for(test_id<ErrorTypeId>(3)) == 0);
}

TEST_CASE("A log is copyable and copies diverge", "[log]")
{
    Log original{HeaderValues{}};
    REQUIRE(original.add(make_entry(1)).has_value());
    Log copy = original;
    REQUIRE(copy.add(make_entry(2)).has_value());
    REQUIRE(copy.set_deleted(test_id<EntryId>(1), true).has_value());
    CHECK(original.entries().size() == 1);
    CHECK_FALSE(original.entries()[0].m_Deleted);
    CHECK(copy.entries().size() == 2);
}

TEST_CASE("The PRD example scenario has the expected entry kinds", "[log]")
{
    const Log log = test::make_scenario("prd-example").m_Log;
    REQUIRE(log.entries().size() == 6);
    CHECK(std::holds_alternative<ErrorEntry>(log.entries()[0].m_Body));
    CHECK(std::holds_alternative<NoteEntry>(log.entries()[2].m_Body));
    CHECK(std::holds_alternative<HeaderChangeEntry>(log.entries()[4].m_Body));
    CHECK(log.initial_header().m_Tester == "Sam Example");
}
