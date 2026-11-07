// SPDX-License-Identifier: GPL-3.0-or-later
//
// The file journal on a real file system, and the repository over it: the write path of section 2.6 with a journal that
// is really a file (W1, W2, W6, W7), the lock (I4), and the archive folder.
#include "error_journaling/file_journal.hpp"
#include "error_journaling/journal_reader.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "raw_db.hpp"
#include "storage_samples.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <random>
#include <string>

using namespace ej;
using namespace ej::test;

namespace
{

LogId log_id(std::uint64_t n)
{
    return sample_id<LogId>(n);
}

EntryId entry_id(std::uint64_t n)
{
    return sample_id<EntryId>(n);
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// A journal file that lets a test look at, or break, the calls the journal makes on it.
struct Watch
{
    std::function<void(JournalFile&)> m_OnSync;
    std::optional<StorageError> m_FailAppend;
    int m_Appends = 0;
};

class WatchedFile final : public JournalFile
{
public:
    WatchedFile(std::unique_ptr<JournalFile> inner, Watch& watch) : m_Inner(std::move(inner)), m_Watch(watch) {}

    [[nodiscard]] std::uint64_t size() const override { return m_Inner->size(); }
    Result<std::string, StorageError> read_all() override { return m_Inner->read_all(); }
    Result<void, StorageError> append(std::string_view bytes) override
    {
        ++m_Watch.m_Appends;
        if (m_Watch.m_FailAppend)
        {
            const StorageError error = *m_Watch.m_FailAppend;
            m_Watch.m_FailAppend.reset();
            return fail(error);
        }
        return m_Inner->append(bytes);
    }
    Result<void, StorageError> sync() override
    {
        if (m_Watch.m_OnSync)
        {
            m_Watch.m_OnSync(*m_Inner);
        }
        return m_Inner->sync();
    }
    Result<void, StorageError> truncate(std::uint64_t length) override { return m_Inner->truncate(length); }

private:
    std::unique_ptr<JournalFile> m_Inner;
    Watch& m_Watch;
};

JournalFileOpener watching(Watch& watch)
{
    return [&watch](const std::filesystem::path& path) -> Result<std::unique_ptr<JournalFile>, StorageError>
    {
        auto file = open_journal_file(path);
        if (!file)
        {
            return fail(file.error());
        }
        return std::unique_ptr<JournalFile>(new WatchedFile(std::move(*file), watch));
    };
}

JournalRecord create_record(std::uint64_t log, std::uint64_t seq = 1)
{
    return sample_record(seq, CreateLogBody{sample_header(), sample_time(0), std::nullopt}, log);
}

JournalRecord add_record(std::uint64_t log, std::uint64_t seq)
{
    return sample_record(seq, AddEntryBody{sample_error(seq, static_cast<std::int64_t>(seq))}, log);
}

/// A repository over a file journal in a fresh store.
struct Store
{
    TempDir m_Dir;
    StorageLocation m_Location{m_Dir.path()};
    ScriptedClock m_Clock{sample_time(0)};
    Watch m_Watch;
    std::optional<CrashHook> m_JournalHook;
    std::optional<CrashHook> m_RepositoryHook;
    std::unique_ptr<FileJournal> m_Journal;
    std::unique_ptr<SqliteLogRepository> m_Repository;

    void open()
    {
        REQUIRE(m_Location.create_directories().has_value());
        FileJournal::Options journal_options;
        journal_options.m_Opener = watching(m_Watch);
        if (m_JournalHook)
        {
            journal_options.m_Hook = *m_JournalHook;
        }
        m_Journal = std::make_unique<FileJournal>(m_Location, journal_options);
        SqliteLogRepository::Options options;
        if (m_RepositoryHook)
        {
            options.m_Hook = *m_RepositoryHook;
        }
        auto opened = SqliteLogRepository::open(m_Location.database_path(), m_Clock, *m_Journal, options);
        REQUIRE(opened.has_value());
        m_Repository = std::move(*opened);
    }

    void close()
    {
        m_Repository.reset();
        m_Journal.reset();
    }

    SqliteLogRepository& repo() { return *m_Repository; }
    [[nodiscard]] std::filesystem::path journal_path(std::uint64_t log) const
    {
        return m_Journal->open_path(log_id(log));
    }
    [[nodiscard]] std::filesystem::path archive_path(std::uint64_t log) const
    {
        return m_Journal->archived_path(log_id(log));
    }

    Store() { open(); }

    /// The records of a log's journal, wherever it is, with the voided ones taken out.
    std::vector<JournalRecord> effective_records(std::uint64_t log)
    {
        const auto path = std::filesystem::exists(journal_path(log)) ? journal_path(log) : archive_path(log);
        const auto scan = scan_journal_bytes(read_file(path), log_id(log));
        REQUIRE(scan.has_value());
        CHECK(scan->is_clean());
        return resolve_voids(scan->m_Records).m_Effective;
    }
};

/// The state of a log in one comparable string: its first record and one add record per entry, in order.
std::string describe_log(const StoredLog& log)
{
    std::string text;
    const auto line = [&text](const JournalRecord& record)
    {
        const auto encoded = encode_record(record);
        REQUIRE(encoded.has_value());
        text += *encoded + "\n";
    };
    line(
        sample_record(1, CreateLogBody{log.m_Log.initial_header(), log.m_Meta.m_CreatedUtc, log.m_Meta.m_Previous}, 0));
    std::uint64_t seq = 2;
    for (const Entry& entry : log.m_Log.entries())
    {
        line(sample_record(seq++, AddEntryBody{entry}, 0));
    }
    text += "archived=" + std::to_string(log.m_Meta.m_Archived) + "\n";
    text += "shared=" +
            (log.m_Meta.m_LastSharedUtc ? std::to_string(log.m_Meta.m_LastSharedUtc->time_since_epoch().count())
                                        : std::string("none")) +
            "\n";
    return text;
}

/// Folds the effective records of a journal the way recovery will, into the log they describe.
StoredLog fold(const std::vector<JournalRecord>& records)
{
    REQUIRE_FALSE(records.empty());
    const auto* create = std::get_if<CreateLogBody>(&records.front().m_Body);
    REQUIRE(create != nullptr);
    auto stored = make_stored_log(records.front().m_Log, *create);
    REQUIRE(stored.has_value());
    for (std::size_t i = 1; i < records.size(); ++i)
    {
        const auto applied = apply_record(*stored, records[i]);
        REQUIRE(applied.has_value());
    }
    return std::move(*stored);
}

}  // namespace

TEST_CASE("The first record creates the journal file, and the number after it is 2", "[file-journal]")
{
    TempDir dir;
    FileJournal journal{StorageLocation(dir.path())};
    CHECK(journal.next_seq(log_id(1), 0).value() == 1);
    CHECK_FALSE(std::filesystem::exists(journal.open_path(log_id(1))));  // asking creates nothing

    const auto mark = journal.append(create_record(1));
    REQUIRE(mark.has_value());
    CHECK(mark->m_Seq == 1);
    REQUIRE(std::filesystem::exists(journal.open_path(log_id(1))));
    CHECK(journal.next_seq(log_id(1), 1).value() == 2);
    CHECK(journal.next_seq(log_id(1), 7).value() == 8);  // the database may be ahead of the file

    const std::string content = read_file(journal.open_path(log_id(1)));
    CHECK(content.back() == '\n');
    CHECK(scan_journal_bytes(content)->m_Records.size() == 1);
}

TEST_CASE("A record other than the first cannot start a journal, and a missing journal is reported", "[file-journal]")
{
    TempDir dir;
    FileJournal journal{StorageLocation(dir.path())};
    CHECK(journal.append(add_record(1, 2)).error().m_Kind == StorageErrorKind::JOURNAL);
    CHECK(journal.append(add_record(1, 1)).error().m_Kind == StorageErrorKind::JOURNAL);
    CHECK_FALSE(std::filesystem::exists(journal.open_path(log_id(1))));
    // The database holds records of this log and the journal is gone: recovery seeds it.
    CHECK(journal.next_seq(log_id(1), 5).error().m_Kind == StorageErrorKind::JOURNAL);
}

TEST_CASE("Cancelling a record leaves a void record in the file", "[file-journal]")
{
    TempDir dir;
    FileJournal journal{StorageLocation(dir.path())};
    REQUIRE(journal.append(create_record(1)).has_value());
    const auto mark = journal.append(add_record(1, 2));
    REQUIRE(mark.has_value());
    REQUIRE(journal.cancel(*mark).has_value());

    const auto scan = scan_journal_bytes(read_file(journal.open_path(log_id(1))));
    REQUIRE(scan.has_value());
    CHECK(scan->m_Records.size() == 3);
    CHECK(resolve_voids(scan->m_Records).m_Effective.size() == 1);
    CHECK(journal.next_seq(log_id(1), 1).value() == 4);

    CHECK(journal.cancel(JournalMark{log_id(9), 1, 0}).error().m_Kind == StorageErrorKind::INTERNAL);
}

TEST_CASE("Archiving moves the journal to the archive folder, and a write after that goes on in the archive",
          "[file-journal]")
{
    TempDir dir;
    FileJournal journal{StorageLocation(dir.path())};
    REQUIRE(journal.append(create_record(1)).has_value());
    REQUIRE(journal.append(add_record(1, 2)).has_value());
    REQUIRE(journal.archive(log_id(1)).has_value());
    CHECK_FALSE(std::filesystem::exists(journal.open_path(log_id(1))));
    CHECK(std::filesystem::exists(journal.archived_path(log_id(1))));
    CHECK(journal.archive(log_id(1)).has_value());  // again: nothing to move

    CHECK(journal.next_seq(log_id(1), 2).value() == 3);
    REQUIRE(journal.append(add_record(1, 3)).has_value());
    CHECK(scan_journal_bytes(read_file(journal.archived_path(log_id(1))))->m_Records.size() == 3);
    CHECK_FALSE(std::filesystem::exists(journal.open_path(log_id(1))));
}

TEST_CASE("Removing a journal deletes it wherever it is", "[file-journal]")
{
    TempDir dir;
    FileJournal journal{StorageLocation(dir.path())};
    REQUIRE(journal.append(create_record(1)).has_value());
    REQUIRE(journal.append(create_record(2)).has_value());
    REQUIRE(journal.archive(log_id(2)).has_value());
    REQUIRE(journal.remove(log_id(1)).has_value());
    REQUIRE(journal.remove(log_id(2)).has_value());
    CHECK_FALSE(std::filesystem::exists(journal.open_path(log_id(1))));
    CHECK_FALSE(std::filesystem::exists(journal.archived_path(log_id(2))));
    CHECK(journal.remove(log_id(3)).has_value());  // nothing there: fine
}

TEST_CASE("I4: a second journal on the same folders is refused the file the first one holds", "[file-journal][lock]")
{
    TempDir dir;
    FileJournal first{StorageLocation(dir.path())};
    REQUIRE(first.append(create_record(1)).has_value());

    FileJournal second{StorageLocation(dir.path())};
    CHECK(second.next_seq(log_id(1), 1).error().m_Kind == StorageErrorKind::LOCKED);
    CHECK(second.append(add_record(1, 2)).error().m_Kind == StorageErrorKind::LOCKED);
    CHECK(scan_journal_bytes(read_file(first.open_path(log_id(1))))->m_Records.size() == 1);  // nothing interleaved

    // A different log is no concern of the first.
    CHECK(second.append(create_record(2)).has_value());
}

TEST_CASE("A journal with a torn tail is not appended to; recovery has to see it first", "[file-journal]")
{
    TempDir dir;
    StorageLocation location(dir.path());
    {
        FileJournal journal{location};
        REQUIRE(journal.append(create_record(1)).has_value());
        REQUIRE(journal.append(add_record(1, 2)).has_value());
    }
    {
        std::ofstream out(dir.path() / "journal" / (log_id(1).format() + ".jsonl"), std::ios::binary | std::ios::app);
        out << "{\"v\":1,\"seq\":3,\"op\":\"add_en";
    }
    FileJournal journal{location};
    CHECK(journal.next_seq(log_id(1), 2).error().m_Kind == StorageErrorKind::JOURNAL);
    CHECK(journal.append(add_record(1, 3)).error().m_Kind == StorageErrorKind::JOURNAL);
}

TEST_CASE("W1: an acknowledged write is in the database and in the journal", "[file-journal][write-path]")
{
    Store store;
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_note(2, 11, "a note")).has_value());

    const auto records = store.effective_records(1);
    REQUIRE(records.size() == 3);
    CHECK(journal_op(records[0].m_Body) == JournalOp::CREATE_LOG);
    CHECK(journal_op(records[1].m_Body) == JournalOp::ADD_ENTRY);
    CHECK(std::get<AddEntryBody>(records[2].m_Body).m_Entry.m_Id == entry_id(2));
    CHECK(describe_log(fold(records)) == describe_log(store.repo().load_log(log_id(1)).value()));
}

TEST_CASE("W2: the journal is synced before the database commits", "[file-journal][write-path]")
{
    Store store;
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());

    RawDb other(store.m_Location.database_path());
    std::vector<std::string> events;
    store.m_Watch.m_OnSync = [&](JournalFile& file)
    {
        // At the moment the journal is made durable: its file already holds the record, the database does not.
        const bool journal_has_it = file.read_all().value().find(entry_id(1).format()) != std::string::npos;
        const int entries_in_database = other.scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt();
        events.push_back(std::string("sync: journal ") + (journal_has_it ? "has" : "lacks") +
                         " the record, database has " + std::to_string(entries_in_database) + " entries");
    };
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
    events.push_back("commit done: database has " +
                     std::to_string(other.scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt()) + " entries");

    REQUIRE(events.size() == 2);
    CHECK(events[0] == "sync: journal has the record, database has 0 entries");
    CHECK(events[1] == "commit done: database has 1 entries");
}

TEST_CASE("W6: a commit that fails after the journal sync leaves a void record and no entry after a reopen",
          "[file-journal][write-path]")
{
    Store store;
    store.close();
    bool fail_commit = false;
    store.m_RepositoryHook = [&fail_commit](CrashPoint point) -> Result<void, StorageError>
    {
        if (fail_commit && point == CrashPoint::BEFORE_DB_COMMIT)
        {
            return fail(make_error(StorageErrorKind::DATABASE, "injected commit failure"));
        }
        return {};
    };
    store.open();
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());

    fail_commit = true;
    CHECK_FALSE(store.repo().add_entry(log_id(1), sample_error(2, 11)).has_value());
    fail_commit = false;
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(3, 12)).has_value());

    // The file holds the failed record and the void that cancels it; reading it, the entry is not there.
    const std::string content = read_file(store.journal_path(1));
    CHECK(content.find(entry_id(2).format()) != std::string::npos);
    const auto records = store.effective_records(1);
    REQUIRE(records.size() == 3);  // create, entry 1, entry 3
    CHECK(std::get<AddEntryBody>(records[2].m_Body).m_Entry.m_Id == entry_id(3));

    store.close();
    store.open();
    const StoredLog log = store.repo().load_log(log_id(1)).value();
    CHECK(log.m_Log.find(entry_id(2)) == nullptr);
    CHECK(log.m_Log.entries().size() == 2);
    CHECK(describe_log(fold(store.effective_records(1))) == describe_log(log));
}

TEST_CASE("W7: a full disk on the journal append acknowledges nothing and leaves the file as it was",
          "[file-journal][write-path]")
{
    Store store;
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
    const std::string before = read_file(store.journal_path(1));

    store.m_Watch.m_FailAppend = make_error(StorageErrorKind::DISK_FULL, "no space left on device", 28);
    const auto failed = store.repo().add_entry(log_id(1), sample_error(2, 11));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(read_file(store.journal_path(1)) == before);
    CHECK(store.repo().load_log(log_id(1)).value().m_Log.entries().size() == 1);

    REQUIRE(store.repo().add_entry(log_id(1), sample_error(2, 11)).has_value());  // the store is usable
    CHECK(store.effective_records(1).size() == 3);
}

TEST_CASE("A crash point inside the journal write undoes the append", "[file-journal][write-path]")
{
    for (const CrashPoint point : {CrashPoint::AFTER_JOURNAL_APPEND, CrashPoint::AFTER_JOURNAL_SYNC})
    {
        Store store;
        store.close();
        bool armed = false;
        store.m_JournalHook = [&armed, point](CrashPoint reached) -> Result<void, StorageError>
        {
            if (armed && reached == point)
            {
                return fail(make_error(StorageErrorKind::JOURNAL, "injected"));
            }
            return {};
        };
        store.open();
        REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
        const std::string before = read_file(store.journal_path(1));

        armed = true;
        CHECK_FALSE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
        CHECK(read_file(store.journal_path(1)) == before);
        CHECK(store.repo().load_log(log_id(1)).value().m_Log.entries().empty());
        armed = false;
        CHECK(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
    }
}

TEST_CASE("I4: a second writer on the store gets a lock error, and its write changes nothing", "[file-journal][lock]")
{
    Store store;
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());

    // Another process: its own journal and repository over the same folders.
    FileJournal other_journal{store.m_Location};
    ScriptedClock clock;
    auto other = SqliteLogRepository::open(store.m_Location.database_path(), clock, other_journal);
    REQUIRE(other.has_value());
    const auto refused = (*other)->add_entry(log_id(1), sample_error(2, 11));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().m_Kind == StorageErrorKind::LOCKED);
    CHECK(store.repo().load_log(log_id(1)).value().m_Log.entries().size() == 1);
    CHECK(store.effective_records(1).size() == 2);
}

TEST_CASE("Starting a new log: the old journal moves to the archive, the new one starts naming it", "[file-journal]")
{
    Store store;
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
    REQUIRE(store.repo().start_new_log(log_id(1), log_id(2), sample_header()).has_value());

    CHECK_FALSE(std::filesystem::exists(store.journal_path(1)));
    CHECK(std::filesystem::exists(store.archive_path(1)));
    CHECK(std::filesystem::exists(store.journal_path(2)));

    const auto old_records = store.effective_records(1);
    CHECK(journal_op(old_records.back().m_Body) == JournalOp::ARCHIVE);
    const auto new_records = store.effective_records(2);
    REQUIRE(new_records.size() == 1);
    CHECK(std::get<CreateLogBody>(new_records[0].m_Body).m_Previous == log_id(1));

    // The archived log can still be written to; its journal goes on in the archive.
    REQUIRE(store.repo().set_entry_deleted(log_id(1), entry_id(1), true).has_value());
    CHECK(journal_op(store.effective_records(1).back().m_Body) == JournalOp::SET_DELETED);
    CHECK_FALSE(std::filesystem::exists(store.journal_path(1)));
}

TEST_CASE("A hard delete removes the archived journal with the rows", "[file-journal]")
{
    Store store;
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().archive_log(log_id(1)).has_value());
    REQUIRE(std::filesystem::exists(store.archive_path(1)));
    REQUIRE(store.repo().hard_delete_log(log_id(1), HistoryDeleteConfirmation::confirmed(log_id(1))).has_value());
    CHECK_FALSE(std::filesystem::exists(store.archive_path(1)));
    CHECK_FALSE(std::filesystem::exists(store.journal_path(1)));
    CHECK(store.repo().list_logs().value().empty());
}

TEST_CASE("Folding the journal of every log gives the log the database holds, after a long mixed workload",
          "[file-journal][workload]")
{
    Store store;
    std::mt19937_64 rng(20261107);
    std::uint64_t next_log = 1;
    std::uint64_t next_entry = 1;
    std::vector<std::uint64_t> logs;
    const auto start = [&]
    {
        const std::uint64_t id = next_log++;
        if (logs.empty())
        {
            REQUIRE(store.repo().create_log(log_id(id), sample_header()).has_value());
        }
        else
        {
            REQUIRE(store.repo().start_new_log(log_id(logs.back()), log_id(id), sample_header()).has_value());
        }
        logs.push_back(id);
    };
    start();

    std::vector<std::pair<std::uint64_t, std::uint64_t>> entries;  // log, entry
    std::int64_t minute = 0;
    for (int step = 0; step < 150; ++step)
    {
        const std::uint64_t log = logs.back();
        const auto roll = rng() % 100;
        if (roll < 45)
        {
            const std::uint64_t id = next_entry++;
            if (store.repo()
                    .add_entry(log_id(log), sample_error(id, minute++ % 600, rng() % 2 ? "Wrong Response" : "Other"))
                    .has_value())
            {
                entries.emplace_back(log, id);
            }
        }
        else if (roll < 60)
        {
            const std::uint64_t id = next_entry++;
            if (store.repo()
                    .add_entry(log_id(log), sample_note(id, minute++ % 600, "note " + std::to_string(id)))
                    .has_value())
            {
                entries.emplace_back(log, id);
            }
        }
        else if (roll < 65)
        {
            const std::uint64_t id = next_entry++;
            if (store.repo()
                    .add_entry(log_id(log), sample_change(id, minute++ % 600, HeaderField::UNIT_NUMBER,
                                                          std::to_string(100 + rng() % 5)))
                    .has_value())
            {
                entries.emplace_back(log, id);
            }
        }
        else if (roll < 75 && !entries.empty())
        {
            const auto [owner, id] = entries[rng() % entries.size()];
            (void)store.repo().edit_entry(log_id(owner), sample_error(id, minute++ % 600, "Edited"),
                                          sample_time(700 + step));
        }
        else if (roll < 85 && !entries.empty())
        {
            const auto [owner, id] = entries[rng() % entries.size()];
            (void)store.repo().set_entry_deleted(log_id(owner), entry_id(id), rng() % 2 == 0);
        }
        else if (roll < 90)
        {
            (void)store.repo().correct_header(log_id(log), HeaderField::SOFTWARE_VERSION,
                                              "2." + std::to_string(rng() % 9) + ".0");
        }
        else if (roll < 94)
        {
            (void)store.repo().mark_shared(log_id(logs[rng() % logs.size()]), sample_time(800 + step));
        }
        else if (roll < 97)
        {
            start();
        }
        else
        {
            (void)store.repo().set_draft("draft " + std::to_string(step), sample_time(step));
        }
    }
    CHECK(logs.size() > 1);

    for (const std::uint64_t log : logs)
    {
        INFO("log " << log);
        const StoredLog expected = store.repo().load_log(log_id(log)).value();
        CHECK(describe_log(fold(store.effective_records(log))) == describe_log(expected));
    }
    // Exactly one journal is in the open folder, and it is the open log's.
    std::size_t open_files = 0;
    for (const auto& file : std::filesystem::directory_iterator(store.m_Location.journal_dir()))
    {
        open_files += file.is_regular_file() ? 1 : 0;
    }
    CHECK(open_files == 1);
    CHECK(std::filesystem::exists(store.journal_path(logs.back())));
}

TEST_CASE("A journal folder that cannot be made is reported, and writes nothing", "[file-journal]")
{
    TempDir dir;
    {
        std::ofstream blocker(dir.path() / "journal");  // a file where the folder should be
        blocker << "in the way";
    }
    FileJournal journal{StorageLocation(dir.path())};
    const auto refused = journal.append(create_record(1));
    REQUIRE_FALSE(refused.has_value());
    CHECK((refused.error().m_Kind == StorageErrorKind::IO || refused.error().m_Kind == StorageErrorKind::PERMISSION));
}

TEST_CASE("A journal that cannot be moved to the archive stays where it is", "[file-journal]")
{
    TempDir dir;
    FileJournal journal{StorageLocation(dir.path())};
    REQUIRE(journal.append(create_record(1)).has_value());
    {
        std::ofstream blocker(dir.path() / "journal" / "archive");  // a file where the archive folder should be
        blocker << "in the way";
    }
    const auto refused = journal.archive(log_id(1));
    REQUIRE_FALSE(refused.has_value());
    CHECK(std::filesystem::exists(journal.open_path(log_id(1))));
}
