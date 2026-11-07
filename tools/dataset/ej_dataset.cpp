// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writes a year of fictional logs into a data directory, through the real repository: the same files the application
// makes (the database, and a journal for each log, the open log's in journal/ and the others in journal/archive/). The
// people and places are the stand-ins of docs/ (Sam Example, UNIT, North Site, Wrong Response).
//
//     ej_dataset --out <directory> [--logs 365] [--entries 220]
//
// 365 logs of 220 entries is about 80,000 entries. The directory is created and must not already hold a store.
#include "error_journaling/clock.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/journal_record.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "error_journaling/storage_location.hpp"
#include "error_journaling/stored_log.hpp"
#include "journal_seed.hpp"
#include "storage_samples.hpp"

#include <QCoreApplication>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

/// The same mix of entries as the storage tests' year: mostly Wrong Response, some notes, some Other.
Entry mixed_entry(std::uint64_t number, std::int64_t minute)
{
    switch (number % 7)
    {
    case 0:
        return sample_note(number, minute, "Played a song for Sam Example, take " + std::to_string(number));
    case 3:
        return sample_error(number, minute, "Wrong Response", "the robot paused, entry " + std::to_string(number));
    case 5:
        return sample_error(number, minute, "Other");
    default:
        return sample_error(number, minute, "Wrong Response");
    }
}

[[nodiscard]] bool parse_count(std::string_view text, std::size_t& out)
{
    char* end = nullptr;
    const std::string copy(text);
    const unsigned long long value = std::strtoull(copy.c_str(), &end, 10);
    if (end == copy.c_str() || *end != '\0' || value == 0 || value > 100000)
    {
        return false;
    }
    out = static_cast<std::size_t>(value);
    return true;
}

int report_failure(const std::string& why)
{
    std::fprintf(stderr, "ej_dataset: %s\n", why.c_str());
    return 1;
}

} // namespace

int main(int argc, char* argv[])
{
    // Qt SQL loads its drivers through the application object.
    const QCoreApplication application(argc, argv);
    std::filesystem::path out;
    std::size_t logs = 365;
    std::size_t entries_per_log = 220;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--out" && has_value)
        {
            out = argv[++i];
        }
        else if (arg == "--logs" && has_value && parse_count(argv[i + 1], logs))
        {
            ++i;
        }
        else if (arg == "--entries" && has_value && parse_count(argv[i + 1], entries_per_log))
        {
            ++i;
        }
        else
        {
            return report_failure("usage: ej_dataset --out <directory> [--logs N] [--entries N]");
        }
    }
    if (out.empty())
    {
        return report_failure("usage: ej_dataset --out <directory> [--logs N] [--entries N]");
    }

    const StorageLocation location(out);
    if (std::filesystem::exists(location.database_path()))
    {
        return report_failure("the directory already holds a store");
    }
    if (const auto created = location.create_directories(); !created)
    {
        return report_failure("could not create the directories");
    }

    ScriptedClock clock(sample_time(0));
    NullJournal null_journal;
    auto repository = SqliteLogRepository::open(location.database_path(), clock, null_journal);
    if (!repository)
    {
        return report_failure("could not open the database");
    }

    std::size_t entries = 0;
    std::optional<LogId> previous;
    std::uint64_t number = 1;
    for (std::size_t day = 0; day < logs; ++day)
    {
        const LogId id = sample_id<LogId>(day + 1);
        HeaderValues header = sample_header();
        header.m_LocalDate = std::chrono::sys_days{std::chrono::year{2025} / 11 / 7} + std::chrono::days(day);
        auto stored =
            make_stored_log(id, CreateLogBody{header, sample_time(static_cast<std::int64_t>(day) * 1440), previous});
        if (!stored)
        {
            return report_failure("could not make a log");
        }
        std::uint64_t seq = 2;
        for (std::size_t i = 0; i < entries_per_log; ++i)
        {
            Entry entry = mixed_entry(number++, static_cast<std::int64_t>(i) * 3);
            entry.m_Captured.m_Utc += std::chrono::days(day);
            if (!apply_record(*stored, JournalRecord{JOURNAL_VERSION, seq++, id, AddEntryBody{std::move(entry)}},
                              ApplyMode::REPLAY))
            {
                return report_failure("could not add an entry");
            }
            ++entries;
        }
        const bool open_log = day + 1 == logs;
        if (!open_log &&
            !apply_record(*stored, JournalRecord{JOURNAL_VERSION, seq++, id, ArchiveBody{}}, ApplyMode::REPLAY))
        {
            return report_failure("could not archive a log");
        }
        const std::vector<JournalRecord> records = seed_records(*stored);
        if (!(*repository)->replace_log(*stored, records.back().m_Seq))
        {
            return report_failure("could not store a log");
        }
        const std::filesystem::path path =
            (open_log ? location.journal_dir() : location.archived_journal_dir()) / (id.format() + ".jsonl");
        if (!write_journal_file(path, records))
        {
            return report_failure("could not write a journal");
        }
        previous = id;
    }
    if (!(*repository)->checkpoint())
    {
        return report_failure("could not checkpoint the database");
    }
    std::printf("ej_dataset: wrote %zu logs, %zu entries to %s\n", logs, entries, out.string().c_str());
    return 0;
}
