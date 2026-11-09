// SPDX-License-Identifier: GPL-3.0-or-later
//
// The upgrade from schema version 1 to 2, which adds `day_log.changed_since_share` (the share rule: a log is unshared
// when it changed after its last share). A database from an earlier build has no such column: its logs must read as
// they did, a log that was shared reads as shared, and nothing is lost. All data is synthetic.
#include "error_journaling/clock.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/migrator.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "journal_seed.hpp"
#include "recovery_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 1;

/// A store as the build before the share rule left it: one log, shared at 50, with an entry added after the share
/// (which that build did not remember), made through the production code and then taken back to schema version 1.
class VersionOneStore
{
public:
    VersionOneStore()
    {
        {
            OpenStore store(m_Dir.path());
            REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
            REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
            REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(2, 11)).has_value());
            REQUIRE(store.repo().mark_shared(log_id(LOG), sample_time(50)).has_value());
            REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(3, 20)).has_value());
        }
        RawDb raw(database());
        REQUIRE(raw.is_open());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")));
        REQUIRE(raw.exec(QStringLiteral("ALTER TABLE day_log DROP COLUMN changed_since_share")));
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version = 1")));
    }

    [[nodiscard]] const std::filesystem::path& root() const
    {
        return m_Dir.path();
    }
    [[nodiscard]] std::filesystem::path database() const
    {
        return m_Dir.path() / "ej.db";
    }
    [[nodiscard]] StorageLocation location() const
    {
        return StorageLocation(m_Dir.path());
    }

private:
    TempDir m_Dir;
};

std::int64_t user_version(const std::filesystem::path& database)
{
    RawDb raw(database);
    return raw.scalar(QStringLiteral("PRAGMA user_version")).toLongLong();
}

StoredLog load(const std::filesystem::path& database, LogId id)
{
    ScriptedClock clock;
    NullJournal journal;
    auto repo = SqliteLogRepository::open(database, clock, journal);
    const std::string why = repo ? std::string() : describe(repo.error());
    INFO(why);
    REQUIRE(repo.has_value());
    auto stored = (*repo)->load_log(id);
    REQUIRE(stored.has_value());
    return std::move(*stored);
}

/// The columns of `day_log` as SQLite describes them: name, type, not null, default.
std::vector<std::string> day_log_columns(const std::filesystem::path& database)
{
    RawDb raw(database);
    std::vector<std::string> columns;
    for (int i = 0;; ++i)
    {
        const QString at = QStringLiteral(" ORDER BY cid LIMIT 1 OFFSET %1").arg(i);
        const QVariant name = raw.scalar(QStringLiteral("SELECT name FROM pragma_table_info('day_log')") + at);
        if (!name.isValid())
        {
            return columns;
        }
        const QVariant detail =
            raw.scalar(QStringLiteral("SELECT type || ' ' || \"notnull\" || ' ' || ifnull(dflt_value, '-') "
                                      "FROM pragma_table_info('day_log')") +
                       at);
        columns.push_back(name.toString().toStdString() + " " + detail.toString().toStdString());
    }
}

bool noted(const RecoveryReport& report, const std::string& text)
{
    return std::ranges::any_of(report.m_Notes,
                               [&](const std::string& note) { return note.find(text) != std::string::npos; });
}

std::size_t live_entries(const StoredLog& log)
{
    return log.m_Log.sorted_entries(false).size();
}

} // namespace

TEST_CASE("A version 1 database is migrated at launch and a log that was shared reads as shared", "[migration][share]")
{
    VersionOneStore store;
    REQUIRE(user_version(store.database()) == 1);

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(noted(*report, "migrated from version 1 to " + std::to_string(SQLITE_SCHEMA_VERSION)));
    CHECK(user_version(store.database()) == SQLITE_SCHEMA_VERSION);

    // Nothing is lost, and a backup of the version 1 file was made first.
    const StoredLog log = load(store.database(), log_id(LOG));
    CHECK(log.m_Log.entries().size() == 3);
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(50));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
    CHECK_FALSE(is_unshared(log.m_Meta, live_entries(log)));
    std::size_t backups = 0;
    for (const auto& file : std::filesystem::directory_iterator(store.location().backup_dir()))
    {
        backups += file.path().filename().string().starts_with("ej-v1-") ? 1 : 0;
    }
    CHECK(backups == 1);

    // The launch check compares the database with the journal, which knows of an entry after the share: it does not
    // count that against the database, and does not rewrite it, so the log still reads as shared.
    const auto again = recover(store.location());
    REQUIRE(again.has_value());
    CHECK(again->m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(again->m_ReplayedRecords == 0);
    CHECK_FALSE(load(store.database(), log_id(LOG)).m_Meta.m_ChangedSinceShare);
}

TEST_CASE("A migrated log is unshared again from its first change, and a share clears it", "[migration][share]")
{
    VersionOneStore store;
    REQUIRE(recover(store.location()).has_value());
    {
        OpenStore open(store.root());
        REQUIRE(open.repo().add_entry(log_id(LOG), sample_error(4, 30)).has_value());
    }
    StoredLog log = load(store.database(), log_id(LOG));
    CHECK(log.m_Meta.m_ChangedSinceShare);
    CHECK(is_unshared(log.m_Meta, live_entries(log)));
    {
        OpenStore open(store.root());
        REQUIRE(open.repo().mark_shared(log_id(LOG), sample_time(90)).has_value());
    }
    log = load(store.database(), log_id(LOG));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(90));
}

TEST_CASE("A migrated database has the schema of a new one", "[migration][share]")
{
    VersionOneStore migrated;
    REQUIRE(recover(migrated.location()).has_value());

    TempDir fresh_dir;
    {
        OpenStore fresh(fresh_dir.path());
        REQUIRE(fresh.repo().create_log(log_id(LOG), sample_header()).has_value());
    }
    const auto expected = day_log_columns(fresh_dir.path() / "ej.db");
    REQUIRE_FALSE(expected.empty());
    CHECK(day_log_columns(migrated.database()) == expected);
    CHECK(expected.back() == "changed_since_share INTEGER 1 0");
}

TEST_CASE("The product's migration step is the only one between version 1 and the current schema", "[migration][share]")
{
    VersionOneStore store;
    ScriptedClock clock(sample_time(4000));
    const auto report = Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run();
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::MIGRATED);
    CHECK(report->m_Steps == std::vector<std::string>{"add day_log.changed_since_share"});
    RawDb raw(store.database());
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM day_log WHERE changed_since_share = 0")).toInt() == 1);
}

TEST_CASE("A database rebuilt from the journals knows that a log changed after its share", "[recovery][share]")
{
    // The journals are the log of record and say what the database of an older build could not.
    VersionOneStore store;
    REQUIRE(recover(store.location()).has_value());
    std::filesystem::remove(store.database());
    std::filesystem::remove(store.root() / "ej.db-wal");
    std::filesystem::remove(store.root() / "ej.db-shm");

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::REBUILT);
    const StoredLog log = load(store.database(), log_id(LOG));
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(50));
    CHECK(log.m_Meta.m_ChangedSinceShare);
    CHECK(is_unshared(log.m_Meta, live_entries(log)));
}

TEST_CASE("A journal written again from a log keeps whether it changed after its share", "[recovery][share]")
{
    const auto fold = [](const StoredLog& source)
    {
        const std::vector<JournalRecord> records = seed_records(source);
        REQUIRE_FALSE(records.empty());
        const auto& create = std::get<CreateLogBody>(records.front().m_Body);
        auto stored = make_stored_log(source.m_Meta.m_Id, create);
        REQUIRE(stored.has_value());
        for (std::size_t i = 1; i < records.size(); ++i)
        {
            REQUIRE(apply_record(*stored, records[i], ApplyMode::REPLAY).has_value());
        }
        return std::move(*stored);
    };
    const auto make = [](bool shared, bool changed, std::size_t entries)
    {
        auto stored = make_stored_log(log_id(LOG), CreateLogBody{sample_header(), sample_time(0), std::nullopt});
        REQUIRE(stored.has_value());
        for (std::size_t i = 1; i <= entries; ++i)
        {
            REQUIRE(stored->m_Log.add(sample_error(i, static_cast<std::int64_t>(i))).has_value());
        }
        if (shared)
        {
            stored->m_Meta.m_LastSharedUtc = sample_time(50);
        }
        stored->m_Meta.m_ChangedSinceShare = changed;
        return std::move(*stored);
    };

    SECTION("shared and changed since")
    {
        const StoredLog back = fold(make(true, true, 3));
        CHECK(back.m_Meta.m_ChangedSinceShare);
        CHECK(back.m_Meta.m_LastSharedUtc == sample_time(50));
        CHECK(back.m_Log.entries().size() == 3);
    }
    SECTION("shared and not changed since")
    {
        const StoredLog back = fold(make(true, false, 3));
        CHECK_FALSE(back.m_Meta.m_ChangedSinceShare);
        CHECK(back.m_Meta.m_LastSharedUtc == sample_time(50));
    }
    SECTION("never shared")
    {
        const StoredLog back = fold(make(false, false, 3));
        CHECK_FALSE(back.m_Meta.m_ChangedSinceShare);
        CHECK_FALSE(back.m_Meta.m_LastSharedUtc.has_value());
    }
    SECTION("shared with no entry at all")
    {
        const StoredLog back = fold(make(true, false, 0));
        CHECK(back.m_Meta.m_LastSharedUtc == sample_time(50));
        CHECK_FALSE(back.m_Meta.m_ChangedSinceShare);
    }
}
