// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/crash_point.hpp"
#include "error_journaling/journal.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::test;

TEST_CASE("Every crash point has a name that reads back to it", "[crash-point]")
{
    for (int i = 0; i <= static_cast<int>(CrashPoint::MID_MIGRATION); ++i)
    {
        const auto point = static_cast<CrashPoint>(i);
        const auto name = crash_point_name(point);
        INFO(name);
        CHECK_FALSE(name.empty());
        CHECK(crash_point_from_name(name) == point);
    }
    CHECK(crash_point_name(CrashPoint::AFTER_JOURNAL_SYNC) == "after_journal_sync");
    CHECK(crash_point_name(CrashPoint::MID_ARCHIVE_PAIR) == "mid_archive_pair");
}

TEST_CASE("A name that is not a crash point reads back to nothing", "[crash-point]")
{
    CHECK_FALSE(crash_point_from_name("").has_value());
    CHECK_FALSE(crash_point_from_name("after_the_fact").has_value());
    CHECK_FALSE(crash_point_from_name("AFTER_JOURNAL_SYNC").has_value());
    CHECK(crash_point_name(static_cast<CrashPoint>(99)).empty());
}

TEST_CASE("The null journal keeps nothing and hands out the next sequence number", "[journal]")
{
    NullJournal journal;
    const LogId log = sample_id<LogId>(1);
    CHECK(journal.next_seq(log, 0).value() == 1);
    CHECK(journal.next_seq(log, 41).value() == 42);

    const JournalRecord record = sample_record(7, ArchiveBody{}, 1);
    const auto mark = journal.append(record);
    REQUIRE(mark.has_value());
    CHECK(mark->m_Seq == 7);
    CHECK(mark->m_Log == log);
    CHECK(journal.cancel(*mark).has_value());
    CHECK(journal.archive(log).has_value());
    CHECK(journal.remove(log).has_value());
}
