// SPDX-License-Identifier: GPL-3.0-or-later
//
// Schema migrations (work package 2.7, section 2.9 of docs/plan/stage-2.md): M1, M2, M4, M5, M6 and M7, and the states
// a killed migration can leave behind (M3 and K8 run as real kills in the kill harness). The steps are test-only
// (migration_steps.hpp): the test steps start from the schema version the product writes now, and
// the product's own step is tried on a version 1 database in test_schema_upgrade.cpp.
#include "error_journaling/migrator.hpp"
#include "migration_steps.hpp"
#include "recovery_fixture.hpp"

#include <algorithm>

using namespace ej;
using namespace ej::test;

namespace
{

Migrator migrator_for(const StoreCopy& store, const Clock& clock, std::vector<MigrationStep> steps,
                      MigrationOptions options = {}, std::int32_t target = TEST_TARGET_VERSION)
{
    return Migrator(store.location(), clock, std::move(steps), target, std::move(options));
}

std::int64_t count_of(RawDb& db, const char* table)
{
    return db.scalar(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QString::fromLatin1(table))).toLongLong();
}

/// Row counts of the tables a migration must not lose rows from.
std::map<std::string, std::int64_t> counts(const std::filesystem::path& database)
{
    RawDb db(database);
    std::map<std::string, std::int64_t> result;
    for (const char* table :
         {"day_log", "entry", "error_entry", "note_entry", "header_change_entry", "draft", "store_state"})
    {
        result[table] = count_of(db, table);
    }
    return result;
}

std::int64_t user_version(const std::filesystem::path& database)
{
    RawDb db(database);
    return db.scalar(QStringLiteral("PRAGMA user_version")).toLongLong();
}

std::vector<std::filesystem::path> backups_of(const StoreCopy& store)
{
    std::vector<std::filesystem::path> found;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(store.location().backup_dir(), ignored))
    {
        found.push_back(entry.path());
    }
    return found;
}

/// Closes the store the way the app does, so that the database file is whole on its own. The copy of the pristine store
/// already is.
std::string database_bytes(const StoreCopy& store)
{
    return read_bytes(store.database());
}

void write_marker(const StoreCopy& store, std::int32_t from, std::int32_t to, const std::string& backup)
{
    write_bytes(store.location().migration_marker(), "ej-migration 1\nfrom=" + std::to_string(from) +
                                                         "\nto=" + std::to_string(to) + "\nbackup=" + backup + "\n");
}

} // namespace

TEST_CASE("M1: a forward migration keeps every row, adds what the steps add, and leaves a backup", "[migration]")
{
    StoreCopy store;
    const auto before_counts = counts(store.database());
    const std::string before_bytes = database_bytes(store);
    const auto before_journals = snapshot_of(store.root() / "journal");
    ScriptedClock clock(sample_time(4000));

    const auto report = migrator_for(store, clock, test_steps()).run();
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::MIGRATED);
    CHECK(report->m_FromVersion == TEST_FROM_VERSION);
    CHECK(report->m_ToVersion == TEST_TARGET_VERSION);
    CHECK(report->m_Steps == std::vector<std::string>{"add a column", "add a table"});

    CHECK(user_version(store.database()) == TEST_TARGET_VERSION);
    CHECK(counts(store.database()) == before_counts);
    {
        RawDb db(store.database());
        CHECK(db.scalar(QStringLiteral("SELECT COUNT(*) FROM day_log WHERE test_note = 'v2'")).toLongLong() ==
              before_counts.at("day_log"));
        CHECK(count_of(db, "test_audit") == before_counts.at("entry"));
    }
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    // The journals are not touched: they are logical records and mean the same after any schema.
    CHECK(snapshot_of(store.root() / "journal") == before_journals);

    const auto backups = backups_of(store);
    REQUIRE(backups.size() == 1);
    CHECK(backups.front().filename().string().starts_with("ej-v" + std::to_string(TEST_FROM_VERSION) + "-20261109T"));
    CHECK(read_bytes(backups.front()) == before_bytes);
    CHECK(report->m_Backup == "backup/" + backups.front().filename().string());

    // A second run finds nothing to do.
    const auto again = migrator_for(store, clock, test_steps()).run();
    REQUIRE(again.has_value());
    CHECK(again->m_Outcome == MigrationOutcome::UP_TO_DATE);
    CHECK(backups_of(store).size() == 1);
}

TEST_CASE("The product's steps reach the current version, which is then always up to date", "[migration]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    const auto before = tree_of(store.root());
    const auto report = Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run();
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::UP_TO_DATE);
    CHECK(product_migration_steps().size() == static_cast<std::size_t>(SQLITE_SCHEMA_VERSION - 1));
    CHECK(tree_of(store.root()) == before);
}

TEST_CASE("M2: a step that fails restores the previous database byte for byte", "[migration]")
{
    StoreCopy store;
    const std::string before_bytes = database_bytes(store);
    ScriptedClock clock(sample_time(4000));

    const auto report = migrator_for(store, clock, {add_column_step(), failing_step()}).run();
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Kind == StorageErrorKind::DATABASE);
    CHECK(report.error().m_Message.find("restored from the backup copy") != std::string::npos);

    CHECK(database_bytes(store) == before_bytes);
    CHECK(user_version(store.database()) == TEST_FROM_VERSION);
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    {
        RawDb db(store.database());
        CHECK(db.scalar(QStringLiteral("SELECT COUNT(*) FROM pragma_table_info('day_log') WHERE name = 'test_note'"))
                  .toLongLong() == 0);
    }
    // The old database opens with the old schema code and recovers to the logs it held.
    ScriptedClock later(sample_time(5000));
    const auto recovered = Recovery::run(store.location(), later);
    REQUIRE(recovered.has_value());
    CHECK(recovered->m_Outcome == RecoveryOutcome::CLEAN);
    require_recovered(store, pristine().m_Digests, pristine().m_OpenLog);
}

TEST_CASE("M4: a post-check that fails restores, whether it is foreign_key_check or the caller's own check",
          "[migration]")
{
    SECTION("a row without its parent")
    {
        StoreCopy store;
        const std::string before_bytes = database_bytes(store);
        ScriptedClock clock(sample_time(4000));
        const auto report = migrator_for(store, clock, {add_column_step(), orphan_step()}).run();
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().m_Message.find("foreign_key_check") != std::string::npos);
        CHECK(database_bytes(store) == before_bytes);
        CHECK(user_version(store.database()) == TEST_FROM_VERSION);
        CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    }
    SECTION("the caller's check")
    {
        StoreCopy store;
        const std::string before_bytes = database_bytes(store);
        ScriptedClock clock(sample_time(4000));
        MigrationOptions options;
        options.m_ExtraCheck = [](MigrationConnection&) -> Result<void, StorageError>
        { return fail(make_error(StorageErrorKind::DATABASE, "the extra check refused")); };
        const auto report = migrator_for(store, clock, test_steps(), options).run();
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().m_Message.find("the extra check refused") != std::string::npos);
        CHECK(database_bytes(store) == before_bytes);
        CHECK(user_version(store.database()) == TEST_FROM_VERSION);
    }
}

TEST_CASE("A hook that fails before the commit is a failed migration and restores", "[migration]")
{
    for (int fail_at = 1; fail_at <= 4; ++fail_at)
    {
        INFO("the hook fails at its call number " << fail_at);
        StoreCopy store;
        const std::string before_bytes = database_bytes(store);
        ScriptedClock clock(sample_time(4000));
        int calls = 0;
        MigrationOptions options;
        options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
        {
            CHECK(point == CrashPoint::MID_MIGRATION);
            return ++calls == fail_at ? fail(make_error(StorageErrorKind::INTERNAL, "killed"))
                                      : Result<void, StorageError>{};
        };
        const auto report = migrator_for(store, clock, test_steps(), options).run();
        REQUIRE_FALSE(report.has_value());
        CHECK(calls == fail_at);
        CHECK(database_bytes(store) == before_bytes);
        CHECK(user_version(store.database()) == TEST_FROM_VERSION);
        CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    }
}

TEST_CASE("The hook is reached after the marker, after each step, before the commit and after it", "[migration]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    int calls = 0;
    MigrationOptions options;
    options.m_Hook = [&](CrashPoint) -> Result<void, StorageError>
    {
        ++calls;
        return {};
    };
    REQUIRE(migrator_for(store, clock, test_steps(), options).run().has_value());
    CHECK(calls == 1 + 2 + 1 + 1);
}

TEST_CASE("M5: a database from a newer version, or another application's, is refused and left untouched", "[migration]")
{
    SECTION("newer")
    {
        StoreCopy store;
        {
            RawDb db(store.database());
            REQUIRE(db.exec(QStringLiteral("PRAGMA user_version = 99")));
        }
        const auto before = tree_of(store.root());
        ScriptedClock clock(sample_time(4000));
        const auto report = migrator_for(store, clock, test_steps()).run();
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().m_Kind == StorageErrorKind::NEWER_SCHEMA);
        CHECK(tree_of(store.root()) == before);
        CHECK(backups_of(store).empty());
    }
    SECTION("another application")
    {
        StoreCopy store;
        {
            RawDb db(store.database());
            REQUIRE(db.exec(QStringLiteral("PRAGMA application_id = 12345")));
        }
        const auto before = tree_of(store.root());
        ScriptedClock clock(sample_time(4000));
        const auto report = migrator_for(store, clock, test_steps()).run();
        REQUIRE_FALSE(report.has_value());
        CHECK(report.error().m_Kind == StorageErrorKind::NOT_OUR_DATABASE);
        CHECK(tree_of(store.root()) == before);
    }
}

TEST_CASE("A chain with a step missing is refused before anything is touched", "[migration]")
{
    StoreCopy store;
    const auto before = tree_of(store.root());
    ScriptedClock clock(sample_time(4000));
    const auto report = migrator_for(store, clock, {add_table_step()}).run(); // 2 to 3, but the database is at 1
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Kind == StorageErrorKind::INTERNAL);
    CHECK(tree_of(store.root()) == before);
}

TEST_CASE("M6: the backup copy exists and is synced, and the marker names it, before the first step", "[migration]")
{
    StoreCopy store;
    const std::string before_bytes = database_bytes(store);
    ScriptedClock clock(sample_time(4000));
    std::vector<std::string> events;
    MigrationOptions options;
    options.m_Observer = [&](std::string_view event) { events.emplace_back(event); };

    bool step_ran = false;
    MigrationStep probe{TEST_FROM_VERSION, "look at the files",
                        [&](MigrationConnection& sql) -> Result<void, StorageError>
                        {
                            step_ran = true;
                            // What the first step finds: the order of the durable steps, the copy, and the marker.
                            CHECK(events == std::vector<std::string>{"backup_synced", "marker_synced"});
                            const auto backups = backups_of(store);
                            CHECK(backups.size() == 1);
                            if (backups.size() == 1)
                            {
                                CHECK(read_bytes(backups.front()) == before_bytes);
                                const std::string marker = read_bytes(store.location().migration_marker());
                                CHECK(marker.find("from=" + std::to_string(TEST_FROM_VERSION) +
                                                  "\nto=" + std::to_string(TEST_FROM_VERSION + 1) + "\nbackup=backup/" +
                                                  backups.front().filename().string()) != std::string::npos);
                            }
                            return sql.exec("UPDATE day_log SET facilitator = facilitator");
                        }};
    REQUIRE(migrator_for(store, clock, {probe}, options, TEST_FROM_VERSION + 1).run().has_value());
    CHECK(step_ran);
    CHECK(events == std::vector<std::string>{"backup_synced", "marker_synced"});
}

TEST_CASE("The backup names never collide, and an old backup is never overwritten", "[migration]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000)); // the same second for both
    // Two failed runs from the same version at the same clock reading leave two backups.
    for (int i = 0; i < 2; ++i)
    {
        const auto report =
            migrator_for(store, clock, {failing_step(TEST_FROM_VERSION)}, {}, TEST_FROM_VERSION + 1).run();
        REQUIRE_FALSE(report.has_value());
    }
    CHECK(backups_of(store).size() == 2);
}

TEST_CASE("M7: a migrated database still rebuilds from the journals written before the migration", "[migration]")
{
    StoreCopy store;
    const auto journals_before = snapshot_of(store.root() / "journal");
    ScriptedClock clock(sample_time(4000));
    REQUIRE(migrator_for(store, clock, test_steps()).run().has_value());
    REQUIRE(user_version(store.database()) == TEST_TARGET_VERSION);
    CHECK(snapshot_of(store.root() / "journal") == journals_before);

    // The database is lost; the journals rebuild it, at this build's schema.
    write_bytes(store.database(), "");
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::REBUILT);
    CHECK(user_version(store.database()) == SQLITE_SCHEMA_VERSION);
    require_recovered(store, pristine().m_Digests, pristine().m_OpenLog);
}

// ---------------------------------------------------------------------------------------------------------------------
// What a killed migration leaves, and what the next launch does with it
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("A marker with the database still at the old version: the migration never committed", "[migration][resume]")
{
    StoreCopy store;
    const std::string before_bytes = database_bytes(store);
    std::filesystem::create_directories(store.location().backup_dir());
    write_bytes(store.location().backup_dir() / "ej-v1-copy.db", before_bytes);
    write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION, "backup/ej-v1-copy.db");
    ScriptedClock clock(sample_time(4000));

    const auto report = Migrator::resume(store.location(), clock);
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::NOT_COMMITTED);
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    CHECK(database_bytes(store) == before_bytes);
    CHECK(report->m_QuarantinedPaths.empty());
}

TEST_CASE("A marker with the database at the new version and sound: the migration had committed", "[migration][resume]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    REQUIRE(migrator_for(store, clock, test_steps()).run().has_value());
    const auto backups = backups_of(store);
    REQUIRE(backups.size() == 1);
    const std::string migrated = database_bytes(store);
    write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION,
                 "backup/" + backups.front().filename().string()); // killed before it was removed

    const auto report = Migrator::resume(store.location(), clock);
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::COMPLETED);
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    CHECK(database_bytes(store) == migrated);
    CHECK(user_version(store.database()) == TEST_TARGET_VERSION);
}

TEST_CASE("A marker with a database that is neither: the backup copy is put back and the rest is kept",
          "[migration][resume]")
{
    SECTION("the database is damaged")
    {
        StoreCopy store;
        const std::string before_bytes = database_bytes(store);
        ScriptedClock clock(sample_time(4000));
        REQUIRE(migrator_for(store, clock, test_steps()).run().has_value());
        const auto backups = backups_of(store);
        REQUIRE(backups.size() == 1);
        write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION, "backup/" + backups.front().filename().string());
        const std::string damaged = std::string(5000, '\x5A');
        write_bytes(store.database(), damaged);

        const auto report = Migrator::resume(store.location(), clock);
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == MigrationOutcome::RESTORED);
        CHECK(database_bytes(store) == before_bytes);
        CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
        REQUIRE(report->m_QuarantinedPaths.size() == 1);
        CHECK(read_bytes(store.root() / report->m_QuarantinedPaths.front()) == damaged);
        require_recovered(store, pristine().m_Digests, pristine().m_OpenLog);
    }
    SECTION("the database is gone")
    {
        StoreCopy store;
        const std::string before_bytes = database_bytes(store);
        ScriptedClock clock(sample_time(4000));
        write_bytes(store.location().backup_dir() / "ej-v1-copy.db", before_bytes);
        write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION, "backup/ej-v1-copy.db");
        std::filesystem::remove(store.database());

        const auto report = Migrator::resume(store.location(), clock);
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == MigrationOutcome::RESTORED);
        CHECK(database_bytes(store) == before_bytes);
    }
    SECTION("the new version fails its checks")
    {
        StoreCopy store;
        const std::string before_bytes = database_bytes(store);
        ScriptedClock clock(sample_time(4000));
        REQUIRE(migrator_for(store, clock, test_steps()).run().has_value());
        const auto backups = backups_of(store);
        write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION, "backup/" + backups.front().filename().string());
        {
            RawDb db(store.database());
            REQUIRE(db.exec(QStringLiteral("INSERT INTO error_entry (entry_id, error_type_id, error_type_label)"
                                           " VALUES ('no-such-entry', 'x', 'Wrong Response')")));
        }
        const auto report = Migrator::resume(store.location(), clock);
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == MigrationOutcome::RESTORED);
        CHECK(database_bytes(store) == before_bytes);
    }
}

TEST_CASE("A marker whose backup copy is missing, or that cannot be read, changes nothing", "[migration][resume]")
{
    SECTION("no copy, and a database that needs one")
    {
        StoreCopy store;
        write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION, "backup/none.db");
        write_bytes(store.database(), std::string(5000, '\x5A'));
        const auto before = tree_of(store.root());
        ScriptedClock clock(sample_time(4000));
        const auto report = Migrator::resume(store.location(), clock);
        REQUIRE_FALSE(report.has_value());
        CHECK(tree_of(store.root()) == before);
    }
    SECTION("a marker that is not ours")
    {
        StoreCopy store;
        write_bytes(store.location().migration_marker(), "something else\n");
        const auto before = tree_of(store.root());
        ScriptedClock clock(sample_time(4000));
        const auto report = Migrator::resume(store.location(), clock);
        REQUIRE_FALSE(report.has_value());
        CHECK(tree_of(store.root()) == before);
    }
    SECTION("no marker")
    {
        StoreCopy store;
        const auto before = tree_of(store.root());
        ScriptedClock clock(sample_time(4000));
        const auto report = Migrator::resume(store.location(), clock);
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == MigrationOutcome::NO_MARKER);
        CHECK(tree_of(store.root()) == before);
    }
}

TEST_CASE("Recovery deals with a migration marker before it opens anything, and says so", "[migration][resume]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    REQUIRE(migrator_for(store, clock, {add_column_step()}, {}, TEST_FROM_VERSION + 1).run().has_value());
    // Killed after the commit and before the marker was removed; the database is at a version this build refuses, so
    // put the old one back by hand as the "never committed" case.
    const auto backups = backups_of(store);
    REQUIRE(backups.size() == 1);
    write_bytes(store.database(), read_bytes(backups.front()));
    write_marker(store, TEST_FROM_VERSION, TEST_FROM_VERSION + 1, "backup/" + backups.front().filename().string());

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::CLEAN);
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    bool noted = false;
    for (const std::string& note : report->m_Notes)
    {
        noted = noted || note.find("migration from version " + std::to_string(TEST_FROM_VERSION) + " to " +
                                   std::to_string(TEST_FROM_VERSION + 1) + " had been interrupted: not_committed") !=
                             std::string::npos;
    }
    CHECK(noted);
    require_recovered(store, pristine().m_Digests, pristine().m_OpenLog);
}

TEST_CASE("Recovery restores the backup of a migration that left a damaged database, then reports the store as it was",
          "[migration][resume]")
{
    StoreCopy store;
    const std::string before_bytes = database_bytes(store);
    ScriptedClock clock(sample_time(4000));
    REQUIRE(migrator_for(store, clock, {add_column_step()}, {}, TEST_FROM_VERSION + 1).run().has_value());
    const auto backups = backups_of(store);
    write_marker(store, TEST_FROM_VERSION, TEST_FROM_VERSION + 1, "backup/" + backups.front().filename().string());
    write_bytes(store.database(), std::string(9000, '\x33'));

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report->m_QuarantinedPaths.size() == 1); // the damaged file the restore set aside
    CHECK(database_bytes(store) == before_bytes);
    require_recovered(store, pristine().m_Digests, pristine().m_OpenLog);
}

// ---------------------------------------------------------------------------------------------------------------------
// The edges: databases and markers that are not what they should be
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("A store with no database, or an empty one, has nothing to migrate", "[migration]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    SECTION("no database")
    {
        std::filesystem::remove(store.database());
        const auto report = migrator_for(store, clock, test_steps()).run();
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == MigrationOutcome::UP_TO_DATE);
        CHECK_FALSE(std::filesystem::exists(store.database()));
    }
    SECTION("a database nothing was ever written to")
    {
        write_bytes(store.database(), "");
        {
            RawDb db(store.database());
            REQUIRE(db.exec(QStringLiteral("CREATE TABLE unrelated (x INTEGER)")));
            REQUIRE(db.exec(QStringLiteral("PRAGMA user_version = 0")));
        }
        const auto report = migrator_for(store, clock, test_steps()).run();
        REQUIRE(report.has_value());
        CHECK(report->m_Outcome == MigrationOutcome::UP_TO_DATE);
        CHECK(backups_of(store).empty());
    }
}

TEST_CASE("A file that is not a database is refused and left as it was", "[migration]")
{
    StoreCopy store;
    write_bytes(store.database(), std::string(6000, '\x41'));
    const auto before = tree_of(store.root());
    ScriptedClock clock(sample_time(4000));
    const auto report = migrator_for(store, clock, test_steps()).run();
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Kind == StorageErrorKind::CORRUPT);
    CHECK(tree_of(store.root()) == before);
}

TEST_CASE("A marker left by a killed migration is dealt with before the next one starts", "[migration]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    const std::string original = database_bytes(store);
    std::filesystem::create_directories(store.location().backup_dir());
    write_bytes(store.location().backup_dir() / "ej-v1-copy.db", original);
    write_marker(store, TEST_FROM_VERSION, TEST_TARGET_VERSION, "backup/ej-v1-copy.db");

    const auto report = migrator_for(store, clock, test_steps()).run();
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::MIGRATED);
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
    CHECK(user_version(store.database()) == TEST_TARGET_VERSION);
}

TEST_CASE("A marker with a number that is not one cannot be read", "[migration][resume]")
{
    StoreCopy store;
    write_bytes(store.location().migration_marker(), "ej-migration 1\nfrom=one\nto=3\nbackup=backup/x.db\n");
    const auto before = tree_of(store.root());
    ScriptedClock clock(sample_time(4000));
    const auto report = Migrator::resume(store.location(), clock);
    REQUIRE_FALSE(report.has_value());
    CHECK(tree_of(store.root()) == before);
}

TEST_CASE("When even the restore fails, the error says so and the marker stays for the next start", "[migration]")
{
    StoreCopy store;
    ScriptedClock clock(sample_time(4000));
    // A step that fails after taking the backup away, so that there is nothing to put back.
    MigrationStep sabotage{TEST_FROM_VERSION, "removes the backup and fails",
                           [&](MigrationConnection&) -> Result<void, StorageError>
                           {
                               for (const auto& backup : backups_of(store))
                               {
                                   std::filesystem::remove(backup);
                               }
                               return fail(make_error(StorageErrorKind::DATABASE, "the step failed"));
                           }};
    const auto report = migrator_for(store, clock, {sabotage}, {}, TEST_FROM_VERSION + 1).run();
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Message.find("could not be put back") != std::string::npos);
    CHECK(std::filesystem::exists(store.location().migration_marker()));
}

TEST_CASE("The names the report uses for an outcome", "[migration]")
{
    CHECK(migration_outcome_name(MigrationOutcome::UP_TO_DATE) == "up_to_date");
    CHECK(migration_outcome_name(MigrationOutcome::MIGRATED) == "migrated");
    CHECK(migration_outcome_name(MigrationOutcome::NO_MARKER) == "no_marker");
    CHECK(migration_outcome_name(MigrationOutcome::NOT_COMMITTED) == "not_committed");
    CHECK(migration_outcome_name(MigrationOutcome::COMPLETED) == "completed");
    CHECK(migration_outcome_name(MigrationOutcome::RESTORED) == "restored");
}
