// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recovery on a populated store (work package 2.4 of docs/plan/stage-2.md): the corruption cases C1 to C16, the journal
// cases J1 to J12, the combined cases X1 to X3, idempotence, and that nothing is ever deleted. Every case damages a
// copy of the same store: one open log of 300 entries and two archived logs, built through the production code.
#include "platform/unreadable_file.hpp"
#include "quarantine.hpp"
#include "recovery_fixture.hpp"

#include <algorithm>
#include <random>

using namespace ej;
using namespace ej::test;

namespace
{

/// The byte offset and length of page `root_page` of a table or index of the database in `database`.
struct PageSpot
{
    std::uint64_t m_Offset = 0;
    std::uint64_t m_Size = 0;
};

PageSpot page_of(const std::filesystem::path& database, const char* name)
{
    RawDb raw(database);
    const std::uint64_t page_size = raw.scalar(QStringLiteral("PRAGMA page_size")).toULongLong();
    const std::uint64_t root =
        raw.scalar(QStringLiteral("SELECT rootpage FROM sqlite_master WHERE name = ?"), {QString::fromLatin1(name)})
            .toULongLong();
    REQUIRE(page_size > 0);
    REQUIRE(root > 0);
    return PageSpot{(root - 1) * page_size, page_size};
}

void overwrite(const std::filesystem::path& file, std::uint64_t offset, const std::string& bytes)
{
    std::string content = read_bytes(file);
    REQUIRE(offset + bytes.size() <= content.size());
    content.replace(static_cast<std::size_t>(offset), bytes.size(), bytes);
    write_bytes(file, content);
}

std::string random_bytes(std::size_t count, std::uint64_t seed)
{
    std::mt19937_64 rng(seed);
    std::string bytes(count, '\0');
    for (char& c : bytes)
    {
        c = static_cast<char>(rng());
    }
    return bytes;
}

/// The open log's journal, and the last full record in it.
std::string open_journal(const StoreCopy& store)
{
    return read_bytes(store.journal(pristine().m_OpenLog));
}

std::string next_record_line(std::uint64_t seq, std::uint64_t entry_number)
{
    const auto line = encode_record(sample_record(seq, AddEntryBody{sample_error(entry_number, 700)}, 3));
    REQUIRE(line.has_value());
    return *line + "\n";
}

const std::map<LogId, StateDigest>& expected()
{
    return pristine().m_Digests;
}

RecoveryReport must_recover(const StoreCopy& store)
{
    const auto report = recover(store.location());
    const std::string why = report ? std::string() : describe(report.error());
    INFO(why);
    REQUIRE(report.has_value());
    return *report;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The store the cases start from
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("A store that was closed cleanly recovers to nothing and changes nothing", "[recovery]")
{
    StoreCopy store;
    const auto before = tree_of(store.root());
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog));
    CHECK(report.m_ReplayedRecords == 0);
    CHECK(report.m_QuarantinedPaths.empty());
    CHECK(report.m_IntegrityMessage == "ok");
    CHECK(tree_of(store.root()) == before);
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("A new store with nothing in it is made, not rebuilt", "[recovery]")
{
    TempDir dir;
    const RecoveryReport report = recover(StorageLocation(dir.path() / "fresh")).value();
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report.m_OpenLogEntries == 0);
    CHECK(std::filesystem::exists(dir.path() / "fresh" / "ej.db"));
    CHECK(report.m_QuarantinedPaths.empty());
}

TEST_CASE("The report holds counts and paths, never entry text", "[recovery]")
{
    StoreCopy store;
    write_bytes(store.database(), "");
    const RecoveryReport report = must_recover(store);
    const std::string text = describe(report);
    for (const std::string& path : report.m_QuarantinedPaths)
    {
        CHECK(path.starts_with("quarantine/"));
    }
    CHECK(text.find("Sam Example") == std::string::npos);
    CHECK(text.find("Wrong Response") == std::string::npos);
    for (const std::string& note : report.m_Notes)
    {
        CHECK(note.find("Sam Example") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// C1 to C16: the database
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("C1: a zero-length database is rebuilt from the journals", "[recovery][corruption]")
{
    StoreCopy store;
    write_bytes(store.database(), "");
    const auto before = tree_of(store.root());
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
    CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog));
    CHECK(report.m_RebuiltRecords > 300);
    // An empty file holds nothing to set aside; the draft lived only there.
    CHECK_FALSE(report.m_DraftRecovered);
    require_recovered(store, expected(), pristine().m_OpenLog);
    require_nothing_deleted(before, tree_of(store.root()));
}

TEST_CASE("C2: a database truncated mid-file is rebuilt, at 60% and at a size that is not a page multiple",
          "[recovery][corruption]")
{
    for (const double keep : {0.6, 0.0})
    {
        StoreCopy store;
        const std::uintmax_t size = std::filesystem::file_size(store.database());
        const std::uintmax_t cut =
            keep > 0 ? static_cast<std::uintmax_t>(static_cast<double>(size) * keep) : size - 1000;
        INFO("cut at " << cut << " of " << size);
        std::filesystem::resize_file(store.database(), cut);
        const auto before = tree_of(store.root());
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
        CHECK_FALSE(report.m_QuarantinedPaths.empty());
        CHECK_FALSE(report.m_IntegrityMessage.empty());
        require_recovered(store, expected(), pristine().m_OpenLog);
        require_nothing_deleted(before, tree_of(store.root()));
    }
}

TEST_CASE("C3: a destroyed header is rebuilt, whether zeros or random bytes", "[recovery][corruption]")
{
    for (const bool zeros : {true, false})
    {
        StoreCopy store;
        overwrite(store.database(), 0, zeros ? std::string(100, '\0') : random_bytes(100, 7));
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
        require_recovered(store, expected(), pristine().m_OpenLog);
    }
}

TEST_CASE(
    "C4: a damaged page of the entry table fails the integrity check and the database is rebuilt, keeping the draft",
    "[recovery][corruption]")
{
    StoreCopy store;
    const PageSpot spot = page_of(store.database(), "entry");
    overwrite(store.database(), spot.m_Offset + 8, random_bytes(16, 11));
    const auto before = tree_of(store.root());
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
    CHECK_FALSE(report.m_IntegrityMessage.empty());
    CHECK(report.m_IntegrityMessage != "ok");
    // The draft is not journalled, but it sat on a page that was not damaged, and it was read back from the old file.
    CHECK(report.m_DraftRecovered);
    require_recovered(store, expected(), pristine().m_OpenLog);
    {
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(store.database(), clock, journal);
        REQUIRE(repo.has_value());
        CHECK((*repo)->get_draft().value()->m_Text == pristine().m_Draft);
    }
    require_nothing_deleted(before, tree_of(store.root()));
    const bool damaged_file_kept = std::any_of(report.m_QuarantinedPaths.begin(), report.m_QuarantinedPaths.end(),
                                               [](const std::string& path) { return path.ends_with("ej.db"); });
    CHECK(damaged_file_kept);
}

TEST_CASE("C5: a damaged page of an index is found, and the result says what was done", "[recovery][corruption]")
{
    StoreCopy store;
    const PageSpot spot = page_of(store.database(), "entry_by_time");
    overwrite(store.database(), spot.m_Offset + 4, random_bytes(24, 13));
    const RecoveryReport report = must_recover(store);
    CHECK((report.m_Outcome == RecoveryOutcome::REBUILT || report.m_Outcome == RecoveryOutcome::REPLAYED));
    CHECK(report.m_IntegrityMessage != "ok");
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("C6: a missing database is rebuilt from the journals", "[recovery][corruption]")
{
    StoreCopy store;
    std::filesystem::remove(store.database());
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
    CHECK(report.m_QuarantinedPaths.empty());  // there was nothing to set aside
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("C7: an orphan write-ahead log is set aside, not applied to the new file", "[recovery][corruption]")
{
    StoreCopy store;
    // Make a write-ahead log with committed pages in it, keep a copy of it, and lose the database it belonged to.
    const std::filesystem::path wal_copy = store.root() / "wal.keep";
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA wal_autocheckpoint = 0")));
        REQUIRE(raw.exec(QStringLiteral("UPDATE draft SET text = 'in the wal only'")));
        std::filesystem::copy_file(store.root() / "ej.db-wal", wal_copy);
    }
    std::filesystem::remove(store.database());
    std::filesystem::remove(store.root() / "ej.db-wal");
    std::filesystem::remove(store.root() / "ej.db-shm");
    std::filesystem::rename(wal_copy, store.root() / "ej.db-wal");
    REQUIRE(std::filesystem::file_size(store.root() / "ej.db-wal") > 0);

    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
    const bool wal_set_aside = std::any_of(report.m_QuarantinedPaths.begin(), report.m_QuarantinedPaths.end(),
                                           [](const std::string& path) { return path.ends_with("ej.db-wal"); });
    CHECK(wal_set_aside);
    // The draft in the orphan was not applied to the new file.
    ScriptedClock clock;
    NullJournal journal;
    auto repo = SqliteLogRepository::open(store.database(), clock, journal);
    REQUIRE(repo.has_value());
    CHECK_FALSE((*repo)->get_draft().value().has_value());
    repo->reset();
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("C8: a torn write-ahead log loses the commits it cannot finish, and the journal restores them",
          "[recovery][corruption]")
{
    StoreCopy store;
    std::filesystem::path image;
    std::size_t added = 0;
    TempDir image_dir;
    {
        // Three more entries are acknowledged; their commits sit in the write-ahead log, not yet in the database file.
        // Take the files as a power loss would leave them (the store is still open), then cut the log in the middle of
        // a frame.
        OpenStore open(store.root());
        for (std::uint64_t n = 5000; n < 5003; ++n)
        {
            REQUIRE(open.repo().add_entry(pristine().m_OpenLog, sample_error(n, 900)).has_value());
            ++added;
        }
        copy_open_store(store.root(), image_dir.path());
    }
    REQUIRE(std::filesystem::exists(image_dir.path() / "ej.db-wal"));
    const std::uintmax_t wal_size = std::filesystem::file_size(image_dir.path() / "ej.db-wal");
    REQUIRE(wal_size > 4096);
    std::filesystem::resize_file(image_dir.path() / "ej.db-wal", wal_size - 1500);  // into the last frame
    std::filesystem::remove(image_dir.path() / "ej.db-shm");

    std::map<LogId, StateDigest> with_added = expected();
    {
        // What the log should be: the pristine log plus the three entries, as the live store holds it.
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(store.database(), clock, journal);
        REQUIRE(repo.has_value());
        with_added[pristine().m_OpenLog] = digest_of((*repo)->load_log(pristine().m_OpenLog).value());
        CHECK((*repo)->load_log(pristine().m_OpenLog).value().m_Log.entries().size() ==
              pristine().m_EntryCounts.at(pristine().m_OpenLog) + added);
    }
    // Recover the image on its own.
    const StorageLocation location(image_dir.path());
    const RecoveryReport report = recover(location).value();
    CHECK((report.m_Outcome == RecoveryOutcome::REPLAYED || report.m_Outcome == RecoveryOutcome::CLEAN));
    CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog) + added);
    ScriptedClock clock;
    NullJournal journal;
    auto repo = SqliteLogRepository::open(image_dir.path() / "ej.db", clock, journal);
    REQUIRE(repo.has_value());
    CHECK(digest_of((*repo)->load_log(pristine().m_OpenLog).value()) == with_added[pristine().m_OpenLog]);
}

TEST_CASE("C9 and J10: a stale database is brought up to date from a complete journal", "[recovery][corruption]")
{
    StoreCopy store;
    std::filesystem::copy_file(pristine().m_StaleDatabase, store.database(),
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(store.root() / "ej.db-wal");
    std::filesystem::remove(store.root() / "ej.db-shm");
    // The integrity check passes on the old file: only the journal knows the difference.
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REPLAYED);
    CHECK(report.m_IntegrityMessage == "ok");
    CHECK(report.m_ReplayedRecords > 200);
    CHECK(report.m_ReplayedRecords == pristine().m_OpenLastSeq - pristine().m_StaleApplied + Pristine::ARCHIVED_EDITS);
    CHECK(report.m_QuarantinedPaths.empty());
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("C10: a missing shared-memory file is made again by SQLite and nothing is wrong", "[recovery][corruption]")
{
    StoreCopy store;
    std::filesystem::path image;
    TempDir image_dir;
    {
        OpenStore open(store.root());
        REQUIRE(open.repo().add_entry(pristine().m_OpenLog, sample_error(5000, 900)).has_value());
        copy_open_store(store.root(), image_dir.path());
    }
    const RecoveryReport report = recover(StorageLocation(image_dir.path())).value();
    CHECK((report.m_Outcome == RecoveryOutcome::CLEAN || report.m_Outcome == RecoveryOutcome::REPLAYED));
    CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog) + 1);
}

TEST_CASE("C11 and C12: a database of zeros or of random bytes is rebuilt", "[recovery][corruption]")
{
    for (const bool zeros : {true, false})
    {
        StoreCopy store;
        const std::size_t size = static_cast<std::size_t>(std::filesystem::file_size(store.database()));
        write_bytes(store.database(), zeros ? std::string(size, '\0') : random_bytes(size, 17));
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
        require_recovered(store, expected(), pristine().m_OpenLog);
    }
}

TEST_CASE("C13: a database that cannot be read is not corrupt: nothing is rebuilt and no file is touched",
          "[recovery][corruption]")
{
    StoreCopy store;
    const auto before = tree_of(store.root());
    const auto outcome = [&]
    {
        UnreadableFile held(store.database());
        if (!held.works())
        {
            return std::optional<StorageError>();
        }
        const auto report = recover(store.location());
        REQUIRE_FALSE(report.has_value());
        return std::optional<StorageError>(report.error());
    }();
    if (!outcome)
    {
        SKIP("this user can read a file whose permission was taken away");
    }
    CHECK((outcome->m_Kind == StorageErrorKind::LOCKED || outcome->m_Kind == StorageErrorKind::PERMISSION));
    CHECK(tree_of(store.root()) == before);
    CHECK_FALSE(std::filesystem::exists(store.root() / "quarantine" / "x"));
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("C14: a database with another application id is refused and left as it was", "[recovery][corruption]")
{
    StoreCopy store;
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA application_id = 12345")));
    }
    const auto before = tree_of(store.root());
    const auto report = recover(store.location());
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Kind == StorageErrorKind::NOT_OUR_DATABASE);
    const auto after = tree_of(store.root());
    CHECK(after.at("ej.db") == before.at("ej.db"));
    for (const auto& [path, bytes] : before)
    {
        if (path.starts_with("journal/"))
        {
            CHECK(after.at(path) == bytes);
        }
    }
    CHECK(std::none_of(after.begin(), after.end(),
                       [](const auto& file) { return file.first.starts_with("quarantine/"); }));
}

TEST_CASE(
    "C15: data changed in place so that every check passes is found by the comparison and repaired from the journal",
    "[recovery][corruption]")
{
    StoreCopy store;
    {
        RawDb raw(store.database());
        REQUIRE(
            raw.exec(QStringLiteral("UPDATE note_entry SET value_text = 'changed behind the back' WHERE entry_id IN "
                                    "(SELECT e.id FROM entry e JOIN note_entry n ON n.entry_id = e.id WHERE "
                                    "e.day_log_id = ? LIMIT 3)"),
                     {QString::fromStdString(pristine().m_OpenLog.format())}));
    }
    {
        // The checks cannot see it.
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(store.database(), clock, journal);
        REQUIRE(repo.has_value());
        CHECK((*repo)->quick_check().value().m_Ok);
        CHECK((*repo)->integrity_check().value().m_Ok);
        CHECK(digest_of((*repo)->load_log(pristine().m_OpenLog).value()) != expected().at(pristine().m_OpenLog));
    }
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REPLAYED);
    CHECK(report.m_IntegrityMessage == "ok");
    CHECK(std::any_of(report.m_Notes.begin(), report.m_Notes.end(), [](const std::string& note)
                      { return note.find("repaired from the journal") != std::string::npos; }));
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("C16: a database from a newer version is refused and left as it was", "[recovery][corruption]")
{
    StoreCopy store;
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version = 2")));
    }
    const auto before = tree_of(store.root());
    const auto report = recover(store.location());
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Kind == StorageErrorKind::NEWER_SCHEMA);
    const auto after = tree_of(store.root());
    CHECK(after.at("ej.db") == before.at("ej.db"));
    CHECK(std::none_of(after.begin(), after.end(),
                       [](const auto& file) { return file.first.starts_with("quarantine/"); }));
}

TEST_CASE("Edits, deletes and shares of an archived log come back when the database is lost", "[recovery][archived]")
{
    StoreCopy store;
    std::filesystem::remove(store.database());
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
    CHECK(report.m_ArchivedLogsLost == 0);
    require_recovered(store, expected(), pristine().m_OpenLog);

    ScriptedClock clock;
    NullJournal journal;
    auto repo = SqliteLogRepository::open(store.database(), clock, journal);
    REQUIRE(repo.has_value());
    const StoredLog first = (*repo)->load_log(pristine().m_FirstArchived).value();
    CHECK(first.m_Meta.m_Archived);
    CHECK(first.m_Log.find(entry_id(2))->m_EditedUtc == sample_time(1550));  // edited after the log was archived
    CHECK(first.m_Log.find(entry_id(3))->m_Deleted);                         // deleted after it
    CHECK(first.m_Meta.m_LastSharedUtc == sample_time(1570));                // shared again after it
    const StoredLog second = (*repo)->load_log(pristine().m_SecondArchived).value();
    CHECK(second.m_Log.find(entry_id(45))->m_EditedUtc == sample_time(1555));
    CHECK(second.m_Meta.m_LastSharedUtc == sample_time(1560));
}

TEST_CASE("A write to an archived log that was journalled but not committed is replayed", "[recovery][archived]")
{
    enum class Change
    {
        EDIT,
        DELETE,
        SHARE
    };
    for (const Change change : {Change::EDIT, Change::DELETE, Change::SHARE})
    {
        StoreCopy store;
        TempDir image;
        {
            // The process dies between the journal sync and the commit: the files at that moment are what is left.
            bool copied = false;
            OpenStore open(store.root(),
                           [&](CrashPoint point) -> Result<void, StorageError>
                           {
                               if (point == CrashPoint::BEFORE_DB_COMMIT && !copied)
                               {
                                   copied = true;
                                   copy_open_store(store.root(), image.path());
                                   return fail(make_error(StorageErrorKind::INTERNAL, "killed"));
                               }
                               return {};
                           });
            const LogId log = pristine().m_FirstArchived;
            switch (change)
            {
            case Change::EDIT:
                CHECK_FALSE(open.repo().edit_entry(log, mixed_entry(4, 77), sample_time(1800)).has_value());
                break;
            case Change::DELETE:
                CHECK_FALSE(open.repo().set_entry_deleted(log, entry_id(5), true).has_value());
                break;
            case Change::SHARE:
                CHECK_FALSE(open.repo().mark_shared(log, sample_time(1900)).has_value());
                break;
            }
            CHECK(copied);
        }
        const RecoveryReport report = recover(StorageLocation(image.path())).value();
        CHECK(report.m_Outcome == RecoveryOutcome::REPLAYED);
        CHECK(report.m_ReplayedRecords == 1);

        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(image.path() / "ej.db", clock, journal);
        REQUIRE(repo.has_value());
        const StoredLog restored = (*repo)->load_log(pristine().m_FirstArchived).value();
        switch (change)
        {
        case Change::EDIT:
            CHECK(restored.m_Log.find(entry_id(4))->m_EditedUtc == sample_time(1800));
            break;
        case Change::DELETE:
            CHECK(restored.m_Log.find(entry_id(5))->m_Deleted);
            break;
        case Change::SHARE:
            CHECK(restored.m_Meta.m_LastSharedUtc == sample_time(1900));
            break;
        }
        // The other logs are as they were.
        CHECK(digest_of((*repo)->load_log(pristine().m_OpenLog).value()) == expected().at(pristine().m_OpenLog));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The draft and a note that was journalled but not committed
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

/// The store as a kill between the journal sync and the commit leaves it, while a note is being added to the open log:
/// the draft ("half a note", written at sample_time(1600)) is in the database and so is the journal record of the note.
void crash_image_of_a_note(const StoreCopy& store, const std::filesystem::path& image, std::int64_t captured_minute)
{
    bool copied = false;
    OpenStore open(store.root(),
                   [&](CrashPoint point) -> Result<void, StorageError>
                   {
                       if (point == CrashPoint::BEFORE_DB_COMMIT && !copied)
                       {
                           copied = true;
                           copy_open_store(store.root(), image);
                           return fail(make_error(StorageErrorKind::INTERNAL, "killed"));
                       }
                       return {};
                   });
    CHECK_FALSE(open.repo()
                    .add_entry(pristine().m_OpenLog, sample_note(9000, captured_minute, pristine().m_Draft))
                    .has_value());
    CHECK(copied);
}

std::optional<Draft> draft_in(const std::filesystem::path& root)
{
    ScriptedClock clock;
    NullJournal journal;
    auto repo = SqliteLogRepository::open(root / "ej.db", clock, journal);
    REQUIRE(repo.has_value());
    return (*repo)->get_draft().value();
}

}  // namespace

TEST_CASE("A note that was journalled but not committed takes its draft with it when it is replayed",
          "[recovery][draft]")
{
    // The commit would have cleared the draft in the same transaction. A kill after the journal sync leaves the record, which
    // recovery applies, and the draft, which is not journalled: both must not be there.
    for (const std::int64_t minute : {1700, 1600})
    {
        INFO("the note was captured at minute " << minute << ", the draft was written at 1600");
        StoreCopy store;
        TempDir image;
        crash_image_of_a_note(store, image.path(), minute);
        REQUIRE(draft_in(image.path()).has_value());  // as the kill left it

        const RecoveryReport report = recover(StorageLocation(image.path())).value();
        CHECK(report.m_Outcome == RecoveryOutcome::REPLAYED);
        CHECK(report.m_ReplayedRecords == 1);
        CHECK_FALSE(draft_in(image.path()).has_value());
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(image.path() / "ej.db", clock, journal);
        REQUIRE(repo.has_value());
        CHECK((*repo)->load_log(pristine().m_OpenLog).value().m_Log.find(entry_id(9000)) != nullptr);
    }
}

TEST_CASE("A draft written after the note was captured is the next note's and stays when the note is replayed",
          "[recovery][draft]")
{
    StoreCopy store;
    TempDir image;
    crash_image_of_a_note(store, image.path(), 100);  // captured long before the draft
    const RecoveryReport report = recover(StorageLocation(image.path())).value();
    CHECK(report.m_Outcome == RecoveryOutcome::REPLAYED);
    const auto draft = draft_in(image.path());
    REQUIRE(draft.has_value());
    CHECK(draft->m_Text == pristine().m_Draft);
}

TEST_CASE("A note that was journalled but not committed does not bring its draft back when the database is rebuilt",
          "[recovery][draft]")
{
    for (const std::int64_t minute : {1700, 100})
    {
        INFO("the note was captured at minute " << minute << ", the draft was written at 1600");
        StoreCopy store;
        TempDir image;
        crash_image_of_a_note(store, image.path(), minute);
        const PageSpot spot = page_of(image.path() / "ej.db", "entry");
        overwrite(image.path() / "ej.db", spot.m_Offset + 8, random_bytes(16, 77));

        const RecoveryReport report = recover(StorageLocation(image.path())).value();
        CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
        // The draft is read back from the damaged file; it stays when it is newer than the note.
        CHECK(report.m_DraftRecovered == (minute == 100));
        CHECK(draft_in(image.path()).has_value() == (minute == 100));
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(image.path() / "ej.db", clock, journal);
        REQUIRE(repo.has_value());
        CHECK((*repo)->load_log(pristine().m_OpenLog).value().m_Log.find(entry_id(9000)) != nullptr);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// J1 to J12: the journal
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("J1: a torn tail is cut off wherever the unacknowledged record was cut", "[recovery][journal]")
{
    const std::uint64_t seq = pristine().m_OpenLastSeq + 1;
    const std::string line = next_record_line(seq, 6000);
    for (const std::size_t cut : {std::size_t{1}, line.size() / 3, line.size() / 2, line.size() - 2, line.size() - 1})
    {
        INFO("cut at " << cut << " of " << line.size());
        StoreCopy store;
        const std::string whole = open_journal(store);
        write_bytes(store.journal(pristine().m_OpenLog), whole + line.substr(0, cut));
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);  // the database has everything that was acknowledged
        CHECK(report.m_DiscardedTailBytes == cut);
        CHECK(report.m_DiscardedRecords == 0);
        CHECK(read_bytes(store.journal(pristine().m_OpenLog)) == whole);
        require_recovered(store, expected(), pristine().m_OpenLog);
    }
}

TEST_CASE("J1: an unacknowledged record that is whole is replayed", "[recovery][journal]")
{
    StoreCopy store;
    const std::string whole = open_journal(store);
    write_bytes(store.journal(pristine().m_OpenLog), whole + next_record_line(pristine().m_OpenLastSeq + 1, 6000));
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::REPLAYED);  // the safe direction: it appears
    CHECK(report.m_ReplayedRecords == 1);
    CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog) + 1);
}

TEST_CASE("J2: a zero-filled tail, and half a record followed by zeros, are dropped", "[recovery][journal]")
{
    const std::string line = next_record_line(pristine().m_OpenLastSeq + 1, 6000);
    for (const std::string& tail : {std::string(4096, '\0'), line.substr(0, line.size() / 2) + std::string(4096, '\0')})
    {
        StoreCopy store;
        const std::string whole = open_journal(store);
        write_bytes(store.journal(pristine().m_OpenLog), whole + tail);
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
        CHECK(report.m_DiscardedTailBytes == tail.size());
        CHECK(read_bytes(store.journal(pristine().m_OpenLog)) == whole);
    }
}

TEST_CASE("J3: a bad record in the middle is counted and the journal is written again from the healthy database",
          "[recovery][journal]")
{
    StoreCopy store;
    std::string bytes = open_journal(store);
    std::size_t at = 0;
    for (int line = 0; line < 120; ++line)
    {
        at = bytes.find('\n', at) + 1;
    }
    bytes[at + 30] = static_cast<char>(bytes[at + 30] ^ 0x01);
    write_bytes(store.journal(pristine().m_OpenLog), bytes);
    const auto before = tree_of(store.root());
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);  // the database is whole, so nothing is lost
    CHECK(report.m_DiscardedRecords >= 1);
    CHECK_FALSE(report.m_QuarantinedPaths.empty());  // a copy of the damaged journal was kept
    require_recovered(store, expected(), pristine().m_OpenLog);
    require_nothing_deleted(before, tree_of(store.root()));
}

TEST_CASE("J4: a missing journal is written again from the healthy database", "[recovery][journal]")
{
    for (const LogId log : {pristine().m_OpenLog, pristine().m_FirstArchived})
    {
        StoreCopy store;
        std::filesystem::remove(store.journal(log));
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
        CHECK(std::any_of(report.m_Notes.begin(), report.m_Notes.end(), [](const std::string& note)
                          { return note.find("written again from the database") != std::string::npos; }));
        CHECK(std::filesystem::exists(store.journal(log)));
        require_recovered(store, expected(), pristine().m_OpenLog);
    }
}

TEST_CASE("J5: a journal shorter than the database is written again from it", "[recovery][journal]")
{
    StoreCopy store;
    const std::string bytes = open_journal(store);
    std::size_t at = 0;
    for (int line = 0; line < 150; ++line)
    {
        at = bytes.find('\n', at) + 1;
    }
    write_bytes(store.journal(pristine().m_OpenLog), bytes.substr(0, at));  // cut at a record boundary
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report.m_ReplayedRecords == 0);
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("J6: copies of the last ten records change nothing, over a healthy and over a stale database",
          "[recovery][journal]")
{
    for (const bool stale : {false, true})
    {
        StoreCopy store;
        std::string bytes = open_journal(store);
        std::size_t at = bytes.size();
        for (int line = 0; line < 11; ++line)
        {
            at = bytes.rfind('\n', at - 1);
        }
        write_bytes(store.journal(pristine().m_OpenLog), bytes + bytes.substr(at + 1));
        if (stale)
        {
            std::filesystem::copy_file(pristine().m_StaleDatabase, store.database(),
                                       std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(store.root() / "ej.db-wal");
        }
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == (stale ? RecoveryOutcome::REPLAYED : RecoveryOutcome::CLEAN));
        if (stale)
        {
            CHECK(report.m_ReplayedRecords ==
                  pristine().m_OpenLastSeq - pristine().m_StaleApplied + Pristine::ARCHIVED_EDITS);
        }
        require_recovered(store, expected(), pristine().m_OpenLog);
    }
}

TEST_CASE("J7: a gap in the sequence is reported, and the log is not carried on as if it were whole",
          "[recovery][journal]")
{
    StoreCopy store;
    const std::string bytes = open_journal(store);
    std::size_t first = 0;
    for (int line = 0; line < 100; ++line)
    {
        first = bytes.find('\n', first) + 1;
    }
    const std::size_t second = bytes.find('\n', first) + 1;
    write_bytes(store.journal(pristine().m_OpenLog), bytes.substr(0, first) + bytes.substr(second));
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);  // the database has the record the journal lost
    CHECK(report.m_DiscardedRecords > 0);
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("J8: lines that are not records are skipped and counted", "[recovery][journal]")
{
    StoreCopy store;
    std::string bytes = open_journal(store);
    const std::size_t at = bytes.find('\n', bytes.size() / 2) + 1;
    bytes.insert(at, std::string("\xFF\xFE not a record \x80\n"));
    write_bytes(store.journal(pristine().m_OpenLog), bytes);
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report.m_DiscardedRecords == 1);
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("J9: another log's journal in place of this one is set aside and this one written again",
          "[recovery][journal]")
{
    StoreCopy store;
    write_bytes(store.journal(pristine().m_OpenLog), read_bytes(store.journal(pristine().m_FirstArchived)));
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(std::any_of(report.m_QuarantinedPaths.begin(), report.m_QuarantinedPaths.end(),
                      [](const std::string& path) { return path.starts_with("quarantine/"); }));
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("J11: a record and its void leave nothing behind", "[recovery][journal]")
{
    StoreCopy store;
    const std::string whole = open_journal(store);
    const std::uint64_t seq = pristine().m_OpenLastSeq + 1;
    const auto voided = encode_record(sample_record(seq + 1, VoidBody{seq}, 3));
    REQUIRE(voided.has_value());
    write_bytes(store.journal(pristine().m_OpenLog), whole + next_record_line(seq, 6000) + *voided + "\n");
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report.m_VoidedRecords == 1);
    CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog));
    require_recovered(store, expected(), pristine().m_OpenLog);
}

TEST_CASE("J12: a ten megabyte line with no newline is cut off without being read into memory", "[recovery][journal]")
{
    StoreCopy store;
    const std::string whole = open_journal(store);
    {
        std::ofstream out(store.journal(pristine().m_OpenLog), std::ios::binary | std::ios::app);
        const std::string chunk(1024 * 1024, 'x');
        for (int i = 0; i < 10; ++i)
        {
            out << chunk;
        }
    }
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report.m_DiscardedTailBytes == 10ULL * 1024 * 1024);
    CHECK(read_bytes(store.journal(pristine().m_OpenLog)) == whole);
}

// ---------------------------------------------------------------------------------------------------------------------
// X1 to X3: combined
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE(
    "X1: a damaged database and a torn tail: rebuilt up to the torn record, and only the unacknowledged write is lost",
    "[recovery][combined]")
{
    for (const bool zero_length : {true, false})
    {
        StoreCopy store;
        if (zero_length)
        {
            write_bytes(store.database(), "");
        }
        else
        {
            const PageSpot spot = page_of(store.database(), "entry");
            overwrite(store.database(), spot.m_Offset + 8, random_bytes(16, 23));
        }
        const std::string whole = open_journal(store);
        const std::string line = next_record_line(pristine().m_OpenLastSeq + 1, 6000);
        write_bytes(store.journal(pristine().m_OpenLog), whole + line.substr(0, line.size() / 2));
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::REBUILT);
        CHECK(report.m_DiscardedTailBytes == line.size() / 2);
        CHECK(report.m_OpenLogEntries == pristine().m_EntryCounts.at(pristine().m_OpenLog));
        require_recovered(store, expected(), pristine().m_OpenLog);
    }
}

TEST_CASE("X1: a damaged database and a bad record mid-journal: rebuilt, and the report says records are gone",
          "[recovery][combined]")
{
    StoreCopy store;
    write_bytes(store.database(), "");
    // Lose a whole record from the middle of the open log's journal: the entry it held cannot come back.
    const std::string bytes = open_journal(store);
    std::size_t first = 0;
    for (int line = 0; line < 150; ++line)
    {
        first = bytes.find('\n', first) + 1;
    }
    const std::size_t second = bytes.find('\n', first) + 1;
    write_bytes(store.journal(pristine().m_OpenLog), bytes.substr(0, first) + bytes.substr(second));
    const RecoveryReport report = must_recover(store);
    CHECK(report.m_Outcome == RecoveryOutcome::PARTIAL_LOSS);
    // The records after the gap that stand alone are kept. The log has all its entries, or all but the one that was
    // lost.
    CHECK(report.m_OpenLogEntries <= pristine().m_EntryCounts.at(pristine().m_OpenLog));
    CHECK(report.m_OpenLogEntries + 1 >= pristine().m_EntryCounts.at(pristine().m_OpenLog));
    // And the store is sound: its checks pass and the journals are clean.
    ScriptedClock clock;
    FileJournal journal(store.location());
    auto repo = SqliteLogRepository::open(store.database(), clock, journal);
    REQUIRE(repo.has_value());
    CHECK((*repo)->quick_check().value().m_Ok);
    CHECK(scan_journal_bytes(read_bytes(store.journal(pristine().m_OpenLog)))->is_clean());
    repo->reset();
    const auto again = recover(store.location());
    REQUIRE(again.has_value());
    CHECK(again->m_Outcome == RecoveryOutcome::CLEAN);
}

TEST_CASE("X2: a database and journals both gone: unrecoverable, a new empty store, nothing deleted",
          "[recovery][combined]")
{
    SECTION("the journals are gone")
    {
        StoreCopy store;
        write_bytes(store.database(), random_bytes(8192, 29));
        for (const LogId log : {log_id(1), log_id(2), log_id(3)})
        {
            std::filesystem::remove(store.journal(log));
        }
        const auto before = tree_of(store.root());
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::UNRECOVERABLE);
        CHECK(report.m_OpenLogEntries == 0);
        CHECK_FALSE(report.m_QuarantinedPaths.empty());
        require_nothing_deleted(before, tree_of(store.root()));
        require_recovered(store, {}, std::nullopt);
    }
    SECTION("the journals are garbage")
    {
        StoreCopy store;
        write_bytes(store.database(), random_bytes(8192, 31));
        for (const LogId log : {log_id(1), log_id(2), log_id(3)})
        {
            write_bytes(store.journal(log), random_bytes(2000, 37 + log.uuid().bytes()[15]));
        }
        const auto before = tree_of(store.root());
        const RecoveryReport report = must_recover(store);
        CHECK(report.m_Outcome == RecoveryOutcome::UNRECOVERABLE);
        CHECK(report.m_ArchivedLogsLost == 2);
        CHECK(report.m_QuarantinedPaths.size() >= 4);  // the database and the three journals
        require_nothing_deleted(before, tree_of(store.root()));
        require_recovered(store, {}, std::nullopt);
    }
}

TEST_CASE("X3: recovery killed at each of its crash points recovers again to the same result", "[recovery][combined]")
{
    for (int kill_at = 1; kill_at <= 3; ++kill_at)
    {
        INFO("killed at crash point " << kill_at);
        StoreCopy store;
        const PageSpot spot = page_of(store.database(), "entry");
        overwrite(store.database(), spot.m_Offset + 8, random_bytes(16, 41));
        const auto before = tree_of(store.root());

        int reached = 0;
        RecoveryOptions options;
        options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
        {
            CHECK(point == CrashPoint::MID_RECOVERY);
            if (++reached == kill_at)
            {
                return fail(make_error(StorageErrorKind::INTERNAL, "killed"));
            }
            return {};
        };
        const auto interrupted = recover(store.location(), options);
        REQUIRE_FALSE(interrupted.has_value());
        CHECK(reached == kill_at);

        const RecoveryReport report = must_recover(store);
        CHECK((report.m_Outcome == RecoveryOutcome::REBUILT || report.m_Outcome == RecoveryOutcome::CLEAN));
        require_recovered(store, expected(), pristine().m_OpenLog);
        require_nothing_deleted(before, tree_of(store.root()));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Everywhere
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("Replaying the journal twice gives the same log", "[recovery][idempotence]")
{
    StoreCopy store;
    std::filesystem::copy_file(pristine().m_StaleDatabase, store.database(),
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(store.root() / "ej.db-wal");
    ScriptedClock clock;
    NullJournal journal;
    auto repo = SqliteLogRepository::open(store.database(), clock, journal);
    REQUIRE(repo.has_value());

    const auto scan = scan_journal_bytes(open_journal(store));
    REQUIRE(scan.has_value());
    const std::vector<JournalRecord> records = resolve_voids(scan->m_Records).m_Effective;
    const auto first = (*repo)->replay(records);
    REQUIRE(first.has_value());
    CHECK(first->m_Applied > 200);
    const StateDigest once = digest_of((*repo)->load_log(pristine().m_OpenLog).value());
    CHECK(once == expected().at(pristine().m_OpenLog));

    const auto second = (*repo)->replay(records);
    REQUIRE(second.has_value());
    CHECK(second->m_Applied == 0);
    CHECK(digest_of((*repo)->load_log(pristine().m_OpenLog).value()) == once);

    // Even from a number far back, applying what is there again changes nothing.
    (void)(*repo)->set_applied_seq(pristine().m_OpenLog, pristine().m_StaleApplied);
    const auto third = (*repo)->replay(records);
    REQUIRE(third.has_value());
    CHECK(digest_of((*repo)->load_log(pristine().m_OpenLog).value()) == once);
}

TEST_CASE("Recovery never deletes a file, over a range of damage", "[recovery][idempotence]")
{
    const auto damage = std::vector<std::function<void(const StoreCopy&)>>{
        [](const StoreCopy& s) { write_bytes(s.database(), ""); },
        [](const StoreCopy& s) { std::filesystem::remove(s.database()); },
        [](const StoreCopy& s) { overwrite(s.database(), 0, std::string(100, 'z')); },
        [](const StoreCopy& s)
        { std::filesystem::resize_file(s.database(), std::filesystem::file_size(s.database()) / 2); },
        [](const StoreCopy& s)
        {
            write_bytes(s.journal(pristine().m_OpenLog), random_bytes(500, 43));
            write_bytes(s.database(), "");
        },
    };
    for (const auto& harm : damage)
    {
        StoreCopy store;
        harm(store);
        const auto before = tree_of(store.root());
        const auto report = recover(store.location());
        REQUIRE(report.has_value());
        require_nothing_deleted(before, tree_of(store.root()));
    }
}

TEST_CASE("Quarantine folders are never reused, however the clock moves, and only the newest five are kept",
          "[recovery][quarantine]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(5000));
    const StorageLocation location = store.location();
    for (int round = 0; round < 8; ++round)
    {
        write_bytes(store.database(), "");  // damaged again, every time
        if (round % 3 == 0)
        {
            clock.set(sample_time(100));  // the clock stepped back
        }
        const auto report =
            Recovery::run(location, clock);  // the same instant for every round: the counter separates them
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == RecoveryOutcome::REBUILT);
    }
    std::size_t folders = 0;
    for (const auto& entry : std::filesystem::directory_iterator(store.root() / "quarantine"))
    {
        if (entry.is_directory())
        {
            ++folders;
        }
    }
    CHECK(folders == 5);
}

TEST_CASE("The quarantine folder name is the UTC time and a counter", "[recovery][quarantine]")
{
    CHECK(Quarantine::folder_name(sample_time(0) + std::chrono::seconds(7), 1) == "20261107T000007Z-1");
    CHECK(Quarantine::folder_name(UtcMillis{}, 12) == "19700101T000000Z-12");
}
