// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rename of Interactions to Observations in what the app stores: a store written by the build before the rename
// (journals at version 2 with `"kind":"interaction"`, a version 4 database) opens in this build with no entry lost,
// keeps working, and recovers from the damage of Stage 2. The old files are fixtures written by that build
// (`legacy_store.hpp`). All data is synthetic.
#include "error_journaling/clock.hpp"
#include "error_journaling/file_journal.hpp"
#include "error_journaling/journal_reader.hpp"
#include "error_journaling/migrator.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "legacy_store.hpp"
#include "migration_steps.hpp"
#include "raw_db.hpp"
#include "recovery_fixture.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <filesystem>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

/// A copy of the old store in a directory of its own.
class LegacyCopy
{
public:
    LegacyCopy()
    {
        copy_legacy_store(m_Dir.path());
    }

    [[nodiscard]] const std::filesystem::path& root() const
    {
        return m_Dir.path();
    }
    [[nodiscard]] StorageLocation location() const
    {
        return StorageLocation(m_Dir.path());
    }
    [[nodiscard]] std::filesystem::path database() const
    {
        return m_Dir.path() / "ej.db";
    }

private:
    TempDir m_Dir;
};

std::int64_t user_version(const std::filesystem::path& database)
{
    RawDb raw(database);
    return raw.scalar(QStringLiteral("PRAGMA user_version")).toLongLong();
}

std::int64_t count_of(const std::filesystem::path& database, const QString& sql)
{
    RawDb raw(database);
    return raw.scalar(sql).toLongLong();
}

std::string journal_bytes(const std::filesystem::path& root, LogId log)
{
    return read_bytes(journal_of(root, log));
}

/// The journal of the old store, as the old build wrote it.
std::string old_journal(LogId log)
{
    return journal_bytes(legacy_store_fixture(), log);
}

void lose_database(const LegacyCopy& store)
{
    std::filesystem::remove(store.database());
    std::filesystem::remove(store.root() / "ej.db-wal");
    std::filesystem::remove(store.root() / "ej.db-shm");
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The database: version 4 to 5
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("legacy.db_upgrade: a version 4 database of the old build migrates with every row and every journal kept",
          "[migration][interaction][legacy]")
{
    LegacyCopy store;
    REQUIRE(user_version(store.database()) == 4);
    REQUIRE(count_of(store.database(), QStringLiteral("SELECT count(*) FROM entry WHERE kind = 'interaction'")) == 5);
    const auto before = snapshot_of(store.root() / "journal");
    const auto expected = reference_digests();

    ScriptedClock clock(sample_time(4000));
    const auto report = Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run();
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::MIGRATED);
    CHECK(report->m_FromVersion == 4);
    CHECK(report->m_ToVersion == 5);
    CHECK(report->m_Steps == std::vector<std::string>{"rename interactions to observations"});

    CHECK(user_version(store.database()) == 5);
    CHECK(count_of(store.database(), QStringLiteral("SELECT count(*) FROM entry WHERE kind = 'interaction'")) == 0);
    CHECK(count_of(store.database(), QStringLiteral("SELECT count(*) FROM entry WHERE kind = 'observation'")) == 5);
    CHECK(count_of(store.database(), QStringLiteral("SELECT count(*) FROM entry")) == 8);
    CHECK(count_of(store.database(), QStringLiteral("SELECT count(*) FROM observation_entry")) == 5);
    CHECK(count_of(store.database(), QStringLiteral("SELECT count(*) FROM observation_entry_person")) == 6);
    CHECK(count_of(store.database(),
                   QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name LIKE 'interaction%'")) == 0);
    {
        RawDb raw(store.database());
        CHECK(raw.scalar(QStringLiteral("PRAGMA integrity_check")).toString() == "ok");
        CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM pragma_foreign_key_check")).toInt() == 0);
    }
    check_legacy_content(store.location(), expected);
    // A migration never touches a journal: every old record stays as the old build wrote it.
    CHECK(snapshot_of(store.root() / "journal") == before);
}

TEST_CASE("legacy.db_upgrade: the database of a migrated store has the schema of a new one",
          "[migration][interaction][legacy]")
{
    LegacyCopy migrated;
    ScriptedClock clock(sample_time(4000));
    REQUIRE(Migrator(migrated.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run().has_value());

    TempDir fresh_dir;
    {
        OpenStore fresh(fresh_dir.path());
        REQUIRE(fresh.repo().create_log(log_id(1), sample_header()).has_value());
    }
    RawDb fresh_raw(fresh_dir.path() / "ej.db");
    RawDb migrated_raw(migrated.database());
    const auto tables = [](RawDb& raw)
    {
        return raw
            .scalar(QStringLiteral("SELECT group_concat(name || ' ' || type || ' ' || \"notnull\" || ' ' ||"
                                   " ifnull(dflt_value, '-') || ' ' || pk, ', ') FROM (SELECT m.name AS tname, p.* FROM"
                                   " sqlite_master m, pragma_table_info(m.name) p WHERE m.type = 'table' AND m.name"
                                   " NOT LIKE 'sqlite_%' ORDER BY m.name, p.cid)"))
            .toString();
    };
    CHECK(tables(migrated_raw) == tables(fresh_raw));
    const auto names = [](RawDb& raw)
    {
        return raw
            .scalar(QStringLiteral("SELECT group_concat(name, ',') FROM (SELECT name FROM sqlite_master WHERE name NOT"
                                   " LIKE 'sqlite_%' ORDER BY name)"))
            .toString();
    };
    CHECK(names(migrated_raw) == names(fresh_raw));
}

TEST_CASE("legacy.db_upgrade: a later step that fails puts the old database back byte for byte",
          "[migration][interaction][legacy]")
{
    LegacyCopy store;
    const std::string before = read_bytes(store.database());
    ScriptedClock clock(sample_time(4000));
    std::vector<MigrationStep> steps = product_migration_steps();
    steps.push_back(failing_step(SQLITE_SCHEMA_VERSION));
    REQUIRE_FALSE(Migrator(store.location(), clock, steps, SQLITE_SCHEMA_VERSION + 1).run().has_value());
    CHECK(read_bytes(store.database()) == before);
    CHECK(user_version(store.database()) == 4);
    CHECK(count_of(store.database(), QStringLiteral("SELECT count(*) FROM entry WHERE kind = 'interaction'")) == 5);
}

TEST_CASE("legacy.db_upgrade: a kill at any point of the rename leaves version 4 or 5, never half, and no entry lost",
          "[migration][interaction][legacy]")
{
    LegacyCopy store;
    const auto expected = reference_digests();
    ScriptedClock clock(sample_time(4000));
    std::deque<TempDir> snapshots;
    MigrationOptions options;
    options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
    {
        if (point != CrashPoint::MID_MIGRATION)
        {
            return {};
        }
        snapshots.emplace_back();
        std::filesystem::copy(store.root(), snapshots.back().path(),
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        return {};
    };
    REQUIRE(
        Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION, options).run().has_value());
    REQUIRE(snapshots.size() >= 4);

    for (std::size_t i = 0; i < snapshots.size(); ++i)
    {
        INFO("kill point " << i);
        const StorageLocation killed(snapshots[i].path());
        REQUIRE(recover(killed).has_value());
        // Recovery migrates a version 4 database before it opens it, so the app always reads the new names.
        CHECK(user_version(killed.database_path()) == SQLITE_SCHEMA_VERSION);
        CHECK(count_of(killed.database_path(), QStringLiteral("SELECT count(*) FROM entry")) == 8);
        CHECK(count_of(killed.database_path(),
                       QStringLiteral("SELECT count(*) FROM entry WHERE kind = 'observation'")) == 5);
        check_legacy_content(killed, expected);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Recovery with old-format data: Stage 2 cases over a journal that holds old records
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("legacy.recovery: the launch path migrates the old store and opens it whole",
          "[recovery][interaction][legacy]")
{
    LegacyCopy store;
    const auto expected = reference_digests();
    REQUIRE(recover(store.location()).has_value());
    check_legacy_content(store.location(), expected);
    CHECK(user_version(store.database()) == SQLITE_SCHEMA_VERSION);
    // The next launch finds nothing to do and changes nothing.
    const auto tree = tree_of(store.root());
    REQUIRE(recover(store.location()).has_value());
    CHECK(tree_of(store.root()) == tree);
}

TEST_CASE("legacy.recovery: a lost database is rebuilt from journals written in the old spelling",
          "[recovery][corruption][interaction][legacy]")
{
    const auto expected = reference_digests();
    for (const char* damage : {"missing", "empty", "garbage"})
    {
        INFO(damage);
        LegacyCopy store;
        lose_database(store);
        if (std::string(damage) == "empty")
        {
            write_bytes(store.database(), "");
        }
        else if (std::string(damage) == "garbage")
        {
            write_bytes(store.database(), std::string(8192, '\x5a'));
        }
        REQUIRE(recover(store.location()).has_value());
        check_legacy_content(store.location(), expected);
        CHECK(user_version(store.database()) == SQLITE_SCHEMA_VERSION);
        // Reading the old journals rewrote nothing.
        CHECK(journal_bytes(store.root(), log_id(1)) == old_journal(log_id(1)));
        CHECK(journal_bytes(store.root(), log_id(2)) == old_journal(log_id(2)));
    }
}

TEST_CASE("legacy.recovery: a torn tail on the open log's old journal is cut and no acknowledged entry is lost",
          "[recovery][corruption][interaction][legacy]")
{
    const auto expected = reference_digests();
    LegacyCopy store;
    const std::string old = old_journal(log_id(2));
    // Half a record, as a kill during an append leaves it, and a lost database on top of it.
    write_bytes(journal_of(store.root(), log_id(2)), old + old.substr(old.rfind("{\"v\":2"), 40));
    lose_database(store);
    REQUIRE(recover(store.location()).has_value());
    check_legacy_content(store.location(), expected);
    CHECK(journal_bytes(store.root(), log_id(2)) == old); // the torn bytes are gone, the records are untouched
}

TEST_CASE("legacy.recovery: a bad record in an archived old journal costs no entry",
          "[recovery][corruption][interaction][legacy]")
{
    const auto expected = reference_digests();
    LegacyCopy store;
    REQUIRE(recover(store.location()).has_value()); // migrated: the database is healthy and holds everything
    std::string bytes = journal_bytes(store.root(), log_id(1));
    const auto at = bytes.find("\"kind\":\"interaction\"");
    REQUIRE(at != std::string::npos);
    bytes[at + 8] = 'X'; // inside a record, so its checksum no longer matches
    write_bytes(journal_of(store.root(), log_id(1)), bytes);
    REQUIRE(recover(store.location()).has_value());
    // The database is the record of what was acknowledged: the damaged journal line costs no entry.
    check_legacy_content(store.location(), expected);
}

TEST_CASE("legacy.mixed_journal: new observations are appended under old records, which never change",
          "[recovery][interaction][legacy]")
{
    LegacyCopy store;
    REQUIRE(recover(store.location()).has_value());
    const std::string old_open = old_journal(log_id(2));
    {
        FileJournal journal(store.location());
        ScriptedClock clock(sample_time(5000));
        auto repo = SqliteLogRepository::open(store.database(), clock, journal);
        REQUIRE(repo.has_value());
        REQUIRE((*repo)
                    ->add_entry(log_id(2), sample_interaction_entry(9, 35, {legacy_person(5)}, std::string("new")))
                    .has_value());
        // An old entry, edited by this build, is written new and replaces the old one.
        REQUIRE((*repo)
                    ->edit_entry(log_id(2), sample_interaction_entry(8, 25, {legacy_person(3), legacy_person(5)}),
                                 sample_time(5001))
                    .has_value());
    }
    const std::string mixed = journal_bytes(store.root(), log_id(2));
    REQUIRE(mixed.size() > old_open.size());
    CHECK(mixed.substr(0, old_open.size()) == old_open); // nothing above the first new record changes, byte for byte
    const std::string appended = mixed.substr(old_open.size());
    CHECK(appended.starts_with("{\"v\":3,"));
    CHECK(appended.find("\"kind\":\"observation\"") != std::string::npos);
    CHECK(appended.find("interaction") == std::string::npos);
    CHECK(old_open.find("\"v\":2") != std::string::npos);

    // Lose the database: the log is rebuilt from the mixed journal, old records and new, in order.
    lose_database(store);
    REQUIRE(recover(store.location()).has_value());
    ScriptedClock clock(sample_time(6000));
    NullJournal journal;
    auto repo = SqliteLogRepository::open(store.database(), clock, journal);
    REQUIRE(repo.has_value());
    const auto log = (*repo)->load_log(log_id(2));
    REQUIRE(log.has_value());
    CHECK(log->m_Log.entries().size() == 3);
    CHECK(std::get<InteractionEntry>(log->m_Log.find(entry_id(8))->m_Body).m_People ==
          std::vector<PersonId>{legacy_person(3), legacy_person(5)});
    CHECK(std::get<InteractionEntry>(log->m_Log.find(entry_id(9))->m_Body).m_Value == "new");
    const auto scan = scan_journal_bytes(journal_bytes(store.root(), log_id(2)), log_id(2));
    REQUIRE(scan.has_value());
    CHECK(scan->is_clean());
}
