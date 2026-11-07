// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/file_journal.hpp"
#include "error_journaling/journal_reader.hpp"
#include "error_journaling/recovery.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "raw_db.hpp"
#include "storage_samples.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

/// A populated store built through the production code, and the helpers the recovery and kill tests share: copy it,
/// damage the copy, recover it, look at the result. All data is synthetic (Sam Example, UNIT, North Site, Wrong
/// Response).
namespace ej::test
{

inline LogId log_id(std::uint64_t n)
{
    return sample_id<LogId>(n);
}

inline EntryId entry_id(std::uint64_t n)
{
    return sample_id<EntryId>(n);
}

inline std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline void write_bytes(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

/// A store open for writing: the file journal and the SQLite repository over it, as the app has them.
class OpenStore
{
public:
    explicit OpenStore(const std::filesystem::path& root, CrashHook repository_hook = {}, CrashHook journal_hook = {})
        : m_Location(root), m_Clock(sample_time(0))
    {
        REQUIRE(m_Location.create_directories().has_value());
        FileJournal::Options journal_options;
        journal_options.m_Hook = std::move(journal_hook);
        m_Journal = std::make_unique<FileJournal>(m_Location, std::move(journal_options));
        SqliteLogRepository::Options options;
        options.m_Hook = std::move(repository_hook);
        auto opened = SqliteLogRepository::open(m_Location.database_path(), m_Clock, *m_Journal, std::move(options));
        const std::string why = opened ? std::string() : describe(opened.error());
        INFO(why);
        REQUIRE(opened.has_value());
        m_Repository = std::move(*opened);
    }

    ~OpenStore()
    {
        m_Repository.reset();
        m_Journal.reset();
    }

    SqliteLogRepository& repo() { return *m_Repository; }
    ScriptedClock& clock() { return m_Clock; }
    [[nodiscard]] const StorageLocation& location() const { return m_Location; }

private:
    StorageLocation m_Location;
    ScriptedClock m_Clock;
    std::unique_ptr<FileJournal> m_Journal;
    std::unique_ptr<SqliteLogRepository> m_Repository;
};

inline Entry mixed_entry(std::uint64_t number, std::int64_t minute)
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

struct Pristine
{
    std::filesystem::path m_Root;            // the store as it ends: three logs, a draft
    std::filesystem::path m_StaleDatabase;   // the database file as it was before the last 200 or so records
    std::map<LogId, StateDigest> m_Digests;  // what each log should be
    std::map<LogId, std::size_t> m_EntryCounts;
    std::string m_Draft = "half a note";
    LogId m_OpenLog = log_id(3);
    LogId m_FirstArchived = log_id(1);
    LogId m_SecondArchived = log_id(2);
    std::uint64_t m_StaleApplied = 0;                   // the open log's applied_seq in the stale file
    static constexpr std::uint64_t ARCHIVED_EDITS = 5;  // records written to the archived logs after the stale copy
    std::uint64_t m_OpenLastSeq = 0;                    // the open log's last journal record
};

/// Builds the store the cases start from: three logs and a draft, through the production code, with a copy of the
/// database from before the last records (it takes seconds, because every write is synced).
inline void build_pristine(const std::filesystem::path& root)
{
    std::uint64_t next_entry = 1;
    std::int64_t minute = 0;
    const auto fill = [&](OpenStore& store, LogId log, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            REQUIRE(store.repo().add_entry(log, mixed_entry(next_entry, minute++ % 1400)).has_value());
            if (next_entry % 11 == 0)
            {
                REQUIRE(store.repo().set_entry_deleted(log, entry_id(next_entry), true).has_value());
            }
            if (next_entry % 13 == 0)
            {
                REQUIRE(store.repo()
                            .edit_entry(log, mixed_entry(next_entry, minute % 1400), sample_time(1500))
                            .has_value());
            }
            ++next_entry;
        }
    };
    HeaderValues header = sample_header();
    {
        OpenStore store(root);
        header.m_LocalDate = std::chrono::year{2026} / 11 / 5;
        store.clock().set(sample_time(1));
        REQUIRE(store.repo().create_log(log_id(1), header).has_value());
        fill(store, log_id(1), 40);
        REQUIRE(store.repo().mark_shared(log_id(1), sample_time(900)).has_value());

        header.m_LocalDate = std::chrono::year{2026} / 11 / 6;
        store.clock().set(sample_time(2));
        REQUIRE(store.repo().start_new_log(log_id(1), log_id(2), header).has_value());
        fill(store, log_id(2), 60);

        header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
        store.clock().set(sample_time(3));
        REQUIRE(store.repo().start_new_log(log_id(2), log_id(3), header).has_value());
        fill(store, log_id(3), 60);
        REQUIRE(store.repo()
                    .add_entry(log_id(3), sample_change(next_entry++, 800, HeaderField::SOFTWARE_VERSION, "1.4.3"))
                    .has_value());
        fill(store, log_id(3), 40);
    }
    std::filesystem::copy_file(root / "ej.db", root / "stale.db");
    {
        OpenStore store(root);
        fill(store, log_id(3), 200);
        // Past logs stay editable, through their own journals (decision 13): an edit, a delete and the latest share.
        REQUIRE(store.repo().edit_entry(log_id(1), mixed_entry(2, 50), sample_time(1550)).has_value());
        REQUIRE(store.repo().set_entry_deleted(log_id(1), entry_id(3), true).has_value());
        REQUIRE(store.repo().edit_entry(log_id(2), mixed_entry(45, 60), sample_time(1555)).has_value());
        REQUIRE(store.repo().mark_shared(log_id(2), sample_time(1560)).has_value());
        REQUIRE(store.repo().mark_shared(log_id(1), sample_time(1570)).has_value());
        REQUIRE(store.repo().correct_header(log_id(3), HeaderField::UNIT_NUMBER, "31").has_value());
        REQUIRE(store.repo().set_draft("half a note", sample_time(1600)).has_value());
    }
}

/// One open log of 300 entries and two archived logs. It is built once for all the test processes that CTest starts
/// (one per case): the first to need it builds it beside the others and renames it into place, and the rest use what is
/// there. Its name carries the time this file was compiled, so a changed fixture is never read from an old directory.
inline const Pristine& pristine()
{
    static const std::unique_ptr<const Pristine> built = []
    {
        auto owned = std::make_unique<Pristine>();
        Pristine& result = *owned;
        const std::filesystem::path base = std::filesystem::current_path() / "ej-test-tmp";
        const std::string stamp = std::to_string(std::hash<std::string>{}(std::string(__DATE__) + __TIME__));
        const std::filesystem::path cached = base / ("pristine-" + stamp);
        if (!std::filesystem::exists(cached / "ready"))
        {
            std::error_code ignored;
            for (const auto& old : std::filesystem::directory_iterator(base, ignored))
            {
                if (old.path().filename().string().starts_with("pristine-") && old.path() != cached)
                {
                    std::filesystem::remove_all(old.path(), ignored);
                }
            }
            TempDir building;
            build_pristine(building.path() / "store");
            write_bytes(building.path() / "store" / "ready", "ready");
            std::filesystem::rename(building.path() / "store", cached, ignored);  // another process may have won
            REQUIRE(std::filesystem::exists(cached / "ready"));
        }
        result.m_Root = cached;
        result.m_StaleDatabase = cached / "stale.db";

        // What the store is, read from a copy of it.
        TempDir measuring;
        std::filesystem::copy(cached, measuring.path() / "store", std::filesystem::copy_options::recursive);
        {
            ScriptedClock clock;
            NullJournal journal;
            auto repo = SqliteLogRepository::open(measuring.path() / "store" / "ej.db", clock, journal);
            REQUIRE(repo.has_value());
            for (const LogId log : {log_id(1), log_id(2), log_id(3)})
            {
                const auto stored = (*repo)->load_log(log);
                REQUIRE(stored.has_value());
                result.m_Digests.emplace(log, digest_of(*stored));
                result.m_EntryCounts.emplace(log, stored->m_Log.entries().size());
            }
        }
        // Read the stale file from the copy: opening a database writes side files, and others are copying the cached
        // one.
        RawDb stale(measuring.path() / "store" / "stale.db");
        result.m_StaleApplied = stale
                                    .scalar(QStringLiteral("SELECT applied_seq FROM day_log WHERE id = ?"),
                                            {QString::fromStdString(log_id(3).format())})
                                    .toULongLong();
        const auto scan = scan_journal_bytes(read_bytes(cached / "journal" / (log_id(3).format() + ".jsonl")));
        REQUIRE(scan.has_value());
        result.m_OpenLastSeq = scan->last_seq();
        return std::unique_ptr<const Pristine>(std::move(owned));
    }();
    return *built;
}

inline std::filesystem::path journal_of(const std::filesystem::path& root, LogId log)
{
    const auto open = root / "journal" / (log.format() + ".jsonl");
    return std::filesystem::exists(open) ? open : root / "journal" / "archive" / (log.format() + ".jsonl");
}

/// Copies the files of a store that is open, as a power loss would leave them: everything but the shared-memory file,
/// which a process holds mapped and which SQLite makes again.
inline void copy_open_store(const std::filesystem::path& from, const std::filesystem::path& to)
{
    for (const auto& entry : std::filesystem::recursive_directory_iterator(from))
    {
        const auto relative = std::filesystem::relative(entry.path(), from);
        if (entry.is_directory())
        {
            std::filesystem::create_directories(to / relative);
        }
        else if (entry.path().extension() != "-shm" && !entry.path().string().ends_with("-shm"))
        {
            std::filesystem::create_directories((to / relative).parent_path());
            std::filesystem::copy_file(entry.path(), to / relative, std::filesystem::copy_options::overwrite_existing);
        }
    }
}

/// A copy of the pristine store in a directory of its own (without the stale snapshot).
class StoreCopy
{
public:
    StoreCopy()
    {
        std::filesystem::copy(pristine().m_Root, m_Dir.path(),
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(m_Dir.path() / "stale.db");
        std::filesystem::remove(m_Dir.path() / "ready");
    }

    [[nodiscard]] const std::filesystem::path& root() const { return m_Dir.path(); }
    [[nodiscard]] std::filesystem::path database() const { return m_Dir.path() / "ej.db"; }
    [[nodiscard]] std::filesystem::path journal(LogId log) const { return journal_of(m_Dir.path(), log); }
    [[nodiscard]] StorageLocation location() const { return StorageLocation(m_Dir.path()); }

private:
    TempDir m_Dir;
};

/// Runs recovery on a store with a clock of its own.
inline Result<RecoveryReport, StorageError> recover(const StorageLocation& location, RecoveryOptions options = {})
{
    static ScriptedClock clock(sample_time(2000));
    clock.advance(std::chrono::seconds(1));
    return Recovery::run(location, clock, std::move(options));
}

/// The whole tree as bytes, relative path to content, quarantine and all: to show that a call changed nothing.
inline std::map<std::string, std::string> tree_of(const std::filesystem::path& root)
{
    return snapshot_of(root);
}

/// After a recovery the store must be a store the app can open and use: every log is what it should be, the checks
/// pass, the journals are clean, and a second recovery finds nothing to do and changes nothing.
inline void require_recovered(const StoreCopy& store, const std::map<LogId, StateDigest>& expected,
                              std::optional<LogId> open_log)
{
    ScriptedClock clock(sample_time(3000));
    {
        FileJournal journal(store.location());
        auto repo = SqliteLogRepository::open(store.database(), clock, journal);
        const std::string why = repo ? std::string() : describe(repo.error());
        INFO(why);
        REQUIRE(repo.has_value());
        const auto quick = (*repo)->quick_check();
        REQUIRE(quick.has_value());
        CHECK(quick->m_Ok);
        for (const auto& [log, digest] : expected)
        {
            const auto stored = (*repo)->load_log(log);
            INFO("log " << log.format());
            REQUIRE(stored.has_value());
            CHECK(digest_of(*stored) == digest);
        }
        CHECK((*repo)->list_logs().value().size() == expected.size());
        const auto open = (*repo)->open_log().value();
        CHECK(open.has_value() == open_log.has_value());
        if (open && open_log)
        {
            CHECK(open->m_Meta.m_Id == *open_log);
        }
        // Every journal is clean, and the next number a write would use is the right one.
        const auto applied = (*repo)->applied_seqs().value();
        for (const auto& [log, seq] : applied)
        {
            const auto path = journal_of(store.root(), log);
            REQUIRE(std::filesystem::exists(path));
            const auto scan = scan_journal_bytes(read_bytes(path), log);
            REQUIRE(scan.has_value());
            CHECK(scan->is_clean());
            CHECK(scan->last_seq() >= seq);
        }
    }
    const auto before = tree_of(store.root());
    const auto again = recover(store.location());
    REQUIRE(again.has_value());
    std::string notes = describe(*again);
    for (const std::string& line : again->m_Notes)
    {
        notes += "\n  " + line;
    }
    for (const std::string& line : again->m_QuarantinedPaths)
    {
        notes += "\n  quarantined " + line;
    }
    INFO(notes);
    CHECK(again->m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(again->m_QuarantinedPaths.empty());
    CHECK(again->m_ReplayedRecords == 0);
    CHECK(again->m_DiscardedRecords == 0);
    CHECK(again->m_DiscardedTailBytes == 0);
    CHECK(tree_of(store.root()) == before);
}

/// Nothing the store held has gone: every file there was before is still there or, if it was replaced, a copy of it
/// with the same bytes is in quarantine. (A journal whose torn tail was cut is the one thing changed in place.)
inline void require_nothing_deleted(const std::map<std::string, std::string>& before,
                                    const std::map<std::string, std::string>& after)
{
    for (const auto& [path, bytes] : before)
    {
        if (path.find("rebuild") != std::string::npos || path.ends_with("-shm"))
        {
            continue;
        }
        if (after.contains(path))
        {
            continue;
        }
        const bool in_quarantine =
            std::any_of(after.begin(), after.end(), [&](const auto& other)
                        { return other.first.starts_with("quarantine/") && other.second == bytes; });
        INFO("lost " << path);
        CHECK(in_quarantine);
    }
}

}  // namespace ej::test
