// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/log.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using ej::test::test_id;

namespace
{

Entry make_entry(std::uint64_t id)
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = Timestamp{UtcMillis{}, "UTC"};
    entry.m_Body = ErrorEntry{test_id<ErrorTypeId>(1), std::nullopt};
    return entry;
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

TEST_CASE("A log is copyable and copies diverge", "[log]")
{
    Log original{HeaderValues{}};
    REQUIRE(original.add(make_entry(1)).has_value());
    Log copy = original;
    REQUIRE(copy.add(make_entry(2)).has_value());
    CHECK(original.entries().size() == 1);
    CHECK(copy.entries().size() == 2);
}

TEST_CASE("The PRD example scenario has the expected entry kinds", "[log]")
{
    const Log log = test::prd_example_log();
    REQUIRE(log.entries().size() == 6);
    CHECK(std::holds_alternative<ErrorEntry>(log.entries()[0].m_Body));
    CHECK(std::holds_alternative<NoteEntry>(log.entries()[2].m_Body));
    CHECK(std::holds_alternative<HeaderChangeEntry>(log.entries()[4].m_Body));
    CHECK(log.initial_header().m_Facilitator == "Sam Example");
}
