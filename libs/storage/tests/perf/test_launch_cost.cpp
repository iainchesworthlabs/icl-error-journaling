// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cost of the launch check (work package 2.6 of docs/plan/stage-2.md, finding F13): an integrity check that grows
// with history could break NFR-04 (the board is up quickly). It builds the store of a facilitator who has used the app
// for a year, about 80,000 entries in 365 logs with a journal for each, then times what happens at launch, on a warm
// cache.
//
// Measured here (and printed, so a CI log shows the numbers) are the quick check alone, the full integrity check, the
// recovery that runs at every launch, and the recovery with the full check that runs after an upgrade. The budget
// applies to the launch itself: `Recovery::run` with its default options, which is the quick check and the comparison
// of the open log with its journal, and which looks at an archived log's journal only at its tail.
#include "error_journaling/recovery.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "journal_seed.hpp"
#include "recovery_fixture.hpp"

#include <QString>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::size_t LOGS = 365;
constexpr std::size_t ENTRIES_PER_LOG = 220;

/// What the launch check may cost on the one-year store, in milliseconds, in an optimised build without
/// instrumentation. The plan proposed 100 ms; the measurement (docs/plan/stage-2.md, section 11) is
/// that SQLite's `quick_check` alone takes 100 to 160 ms on 80,000 entries (34 MB), so the budget is set to about twice
/// what the launch measured, which leaves room for a shared CI runner, and still fails if the launch grows with the
/// number of days kept again. NFR-04 allows 1.5 s from cold start to a usable board.
constexpr double LAUNCH_BUDGET_MS = 400.0;

/// The first launch after an upgrade runs the full integrity check and compares every log with its journal, which reads
/// every journal: about 3 s on a year of logs (measured), well beyond NFR-04's 1.5 s. The figure here is a bound that
/// catches a change for the worse, not a target. Whether that launch should compare only the open log, or do the rest
/// after the board is up, is an open question for the owner (docs/plan/stage-2.md, section 11).
constexpr double UPGRADE_BOUND_MS = 10000.0;

/// A build with sanitizers, coverage or no optimisation is several times slower, and CMake says by how much
/// (`EJ_PERF_BUDGET_FACTOR`). The figures are printed either way.
double budget_factor()
{
    bool parsed = false;
    const double factor = qEnvironmentVariable("EJ_PERF_BUDGET_FACTOR").toDouble(&parsed);
    return parsed ? std::max(1.0, factor) : 1.0;
}

using Clock_ = std::chrono::steady_clock;

double milliseconds_since(Clock_::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock_::now() - start).count();
}

/// The median of `runs` timings of `work`.
template <typename Work>
double median_ms(int runs, Work work)
{
    std::vector<double> timings;
    for (int i = 0; i < runs; ++i)
    {
        const auto start = Clock_::now();
        work();
        timings.push_back(milliseconds_since(start));
    }
    std::sort(timings.begin(), timings.end());
    return timings[timings.size() / 2];
}

/// A year of logs, written in bulk (one transaction for each log) and not through the repository's synced write path,
/// which would take hours: the files are the same the repository and the journal writer would make.
std::size_t build_year(const StorageLocation& location)
{
    REQUIRE(location.create_directories().has_value());
    ScriptedClock clock(sample_time(0));
    NullJournal null_journal;
    auto repository = SqliteLogRepository::open(location.database_path(), clock, null_journal);
    REQUIRE(repository.has_value());

    std::size_t entries = 0;
    std::optional<LogId> previous;
    std::uint64_t number = 1;
    for (std::size_t day = 0; day < LOGS; ++day)
    {
        const LogId id = log_id(day + 1);
        HeaderValues header = sample_header();
        header.m_LocalDate = std::chrono::sys_days{std::chrono::year{2025} / 11 / 7} + std::chrono::days(day);
        auto stored =
            make_stored_log(id, CreateLogBody{header, sample_time(static_cast<std::int64_t>(day) * 1440), previous});
        REQUIRE(stored.has_value());
        std::uint64_t seq = 2;
        for (std::size_t i = 0; i < ENTRIES_PER_LOG; ++i)
        {
            Entry entry = mixed_entry(number++, static_cast<std::int64_t>(i) * 3);
            entry.m_Captured.m_Utc += std::chrono::days(day);
            REQUIRE(apply_record(*stored, JournalRecord{JOURNAL_VERSION, seq++, id, AddEntryBody{std::move(entry)}},
                                 ApplyMode::REPLAY)
                        .has_value());
            ++entries;
        }
        if (day + 1 < LOGS)
        {
            REQUIRE(apply_record(*stored, JournalRecord{JOURNAL_VERSION, seq++, id, ArchiveBody{}}, ApplyMode::REPLAY)
                        .has_value());
        }
        const std::vector<JournalRecord> records = seed_records(*stored);
        REQUIRE((*repository)->replace_log(*stored, records.back().m_Seq).has_value());
        const std::filesystem::path path =
            (day + 1 < LOGS ? location.archived_journal_dir() : location.journal_dir()) / (id.format() + ".jsonl");
        REQUIRE(write_journal_file(path, records).has_value());
        previous = id;
    }
    REQUIRE((*repository)->checkpoint().has_value());
    return entries;
}

} // namespace

TEST_CASE("The launch check on a year of logs fits its budget", "[perf]")
{
    TempDir dir;
    const StorageLocation location(dir.path() / "store");
    const std::size_t entries = build_year(location);
    CHECK(entries >= 80'000);

    const auto database_bytes = std::filesystem::file_size(location.database_path());
    ScriptedClock clock(sample_time(10'000'000));

    // The checks on their own, on a connection of their own.
    double quick_ms = 0;
    double full_ms = 0;
    {
        NullJournal journal;
        auto repository = SqliteLogRepository::open(location.database_path(), clock, journal);
        REQUIRE(repository.has_value());
        quick_ms = median_ms(5,
                             [&]
                             {
                                 const auto quick = (*repository)->quick_check();
                                 REQUIRE(quick.has_value());
                                 REQUIRE(quick->m_Ok);
                             });
        full_ms = median_ms(3,
                            [&]
                            {
                                const auto full = (*repository)->integrity_check();
                                REQUIRE(full.has_value());
                                REQUIRE(full->m_Ok);
                            });
    }

    // The parts of the launch other than the quick check: opening the database (the settings are read back), and the
    // journal folders listed with one small read at the end of each archived journal.
    const double open_ms = median_ms(5,
                                     [&]
                                     {
                                         NullJournal journal;
                                         auto repository =
                                             SqliteLogRepository::open(location.database_path(), clock, journal);
                                         REQUIRE(repository.has_value());
                                     });
    const double tails_ms =
        median_ms(5,
                  [&]
                  {
                      std::size_t files = 0;
                      std::string tail(4096, ' ');
                      for (const auto& file : std::filesystem::directory_iterator(location.archived_journal_dir()))
                      {
                          std::ifstream in(file.path(), std::ios::binary | std::ios::ate);
                          const std::streamoff size = in.tellg();
                          in.seekg(size - 4096);
                          in.read(tail.data(), 4096);
                          ++files;
                      }
                      REQUIRE(files == LOGS - 1);
                  });

    // What runs at every launch, and what runs on the first launch after an upgrade.
    double launch_ms = median_ms(5,
                                 [&]
                                 {
                                     const auto report = Recovery::run(location, clock);
                                     REQUIRE(report.has_value());
                                     REQUIRE(report->m_Outcome == RecoveryOutcome::CLEAN);
                                 });
    RecoveryOptions full;
    full.m_FullCheck = true;
    const double upgrade_ms = median_ms(3,
                                        [&]
                                        {
                                            const auto report = Recovery::run(location, clock, full);
                                            REQUIRE(report.has_value());
                                            REQUIRE(report->m_Outcome == RecoveryOutcome::CLEAN);
                                        });

    std::printf("ej-perf launch check on %zu entries in %zu logs, database %llu KiB: quick_check %.1f ms, "
                "integrity_check %.1f ms, open %.1f ms, 364 journal tails %.1f ms, launch (Recovery::run) %.1f ms, "
                "launch after an upgrade (full check) %.1f ms, "
                "budget %.0f ms\n",
                entries, LOGS, static_cast<unsigned long long>(database_bytes / 1024), quick_ms, full_ms, open_ms,
                tails_ms, launch_ms, upgrade_ms, LAUNCH_BUDGET_MS * budget_factor());
    std::fflush(stdout);
    CHECK(launch_ms <= LAUNCH_BUDGET_MS * budget_factor());
    CHECK(upgrade_ms <= UPGRADE_BOUND_MS * budget_factor());
}
