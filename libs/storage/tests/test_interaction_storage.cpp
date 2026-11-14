// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interactions in the database and the journals (Stage 14, sections 3.3 to 3.5): the schema step from 3 to 4 (T-I2-1,
// T-I2-2), recovery of interactions and people (T-I2-6) and the rule that no log journal and no entry table ever holds
// a name (T-I2-8). All data is synthetic.
#include "error_journaling/clock.hpp"
#include "error_journaling/migrator.hpp"
#include "error_journaling/people_store.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "migration_steps.hpp"
#include "people_names.hpp"
#include "raw_db.hpp"
#include "recovery_fixture.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

PersonId person(std::uint64_t n)
{
    return sample_id<PersonId>(n);
}

// ---------------------------------------------------------------------------------------------------------------------
// A database as the build before this one left it (schema version 3)
// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* V3_SCHEMA[] = {
    "CREATE TABLE day_log (id TEXT PRIMARY KEY NOT NULL, local_date TEXT NOT NULL, facilitator TEXT NOT NULL,"
    " unit_number TEXT, location_id TEXT, location_text TEXT, software_version TEXT, created_utc INTEGER NOT NULL,"
    " last_shared_utc INTEGER, archived INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)), previous_log_id TEXT,"
    " applied_seq INTEGER NOT NULL DEFAULT 0,"
    " changed_since_share INTEGER NOT NULL DEFAULT 0 CHECK (changed_since_share IN (0, 1)), extra_versions TEXT)",
    "CREATE INDEX day_log_archived ON day_log(archived)",
    "CREATE UNIQUE INDEX day_log_one_open ON day_log(archived) WHERE archived = 0",
    "CREATE TABLE entry (id TEXT PRIMARY KEY NOT NULL, day_log_id TEXT NOT NULL REFERENCES day_log(id) ON DELETE "
    "RESTRICT,"
    " seq INTEGER NOT NULL, kind TEXT NOT NULL CHECK (kind IN ('error', 'note', 'header_change')),"
    " captured_utc INTEGER NOT NULL, captured_tz TEXT NOT NULL, edited_utc INTEGER,"
    " deleted INTEGER NOT NULL DEFAULT 0 CHECK (deleted IN (0, 1)), UNIQUE (day_log_id, seq))",
    "CREATE INDEX entry_by_time ON entry(day_log_id, captured_utc, seq)",
    "CREATE TABLE error_entry (entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " error_type_id TEXT NOT NULL, error_type_label TEXT NOT NULL, attached_note TEXT)",
    "CREATE TABLE note_entry (entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " template_id TEXT, value_text TEXT NOT NULL)",
    "CREATE TABLE header_change_entry (entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " field TEXT NOT NULL, old_value TEXT, new_value TEXT NOT NULL)",
    "CREATE TABLE draft (id INTEGER PRIMARY KEY CHECK (id = 1), text TEXT NOT NULL, updated_utc INTEGER NOT NULL)",
    "CREATE TABLE store_state (id INTEGER PRIMARY KEY CHECK (id = 1), applied_seq INTEGER NOT NULL,"
    " journal_generation INTEGER NOT NULL)",
    "INSERT INTO store_state (id, applied_seq, journal_generation) VALUES (1, 6, 1)",
    // One open log, with an error, a note, a header change, a deleted error and an edited one, in an order that is not
    // the order of their ids.
    "INSERT INTO day_log (id, local_date, facilitator, unit_number, location_id, location_text, software_version,"
    " created_utc, applied_seq) VALUES ('00000000-0000-4000-8000-000000000001', '2026-11-07', 'Sam Example', '27', "
    "NULL,"
    " 'North Site', '1.4.2', 1000, 6)",
    "INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000e3', '00000000-0000-4000-8000-000000000001', 2,"
    " 'error', 1100, 'Australia/Melbourne', NULL, 0)",
    "INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000e1', '00000000-0000-4000-8000-000000000001', 3,"
    " 'note', 1200, 'Australia/Melbourne', NULL, 0)",
    "INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000e2', '00000000-0000-4000-8000-000000000001', 4,"
    " 'header_change', 1300, 'Australia/Melbourne', NULL, 0)",
    "INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000e5', '00000000-0000-4000-8000-000000000001', 5,"
    " 'error', 1400, 'Australia/Melbourne', 1500, 1)",
    "INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000e4', '00000000-0000-4000-8000-000000000001', 6,"
    " 'error', 1450, 'Australia/Melbourne', 1600, 0)",
    "INSERT INTO error_entry VALUES ('00000000-0000-4000-8000-0000000000e3', '00000000-0000-4000-8000-0000000000a1',"
    " 'Wrong Response', NULL)",
    "INSERT INTO error_entry VALUES ('00000000-0000-4000-8000-0000000000e5', '00000000-0000-4000-8000-0000000000a1',"
    " 'Wrong Response', 'the robot paused')",
    "INSERT INTO error_entry VALUES ('00000000-0000-4000-8000-0000000000e4', '00000000-0000-4000-8000-0000000000a1',"
    " 'Other', NULL)",
    "INSERT INTO note_entry VALUES ('00000000-0000-4000-8000-0000000000e1', NULL, 'Played a song')",
    "INSERT INTO header_change_entry VALUES ('00000000-0000-4000-8000-0000000000e2', 'unit_number', '27', '28')",
    "INSERT INTO draft VALUES (1, 'half a note', 1700)",
};

class VersionThreeStore
{
public:
    VersionThreeStore()
    {
        REQUIRE(m_Location.create_directories().has_value());
        RawDb raw(m_Location.database_path());
        REQUIRE(raw.is_open());
        for (const char* statement : V3_SCHEMA)
        {
            INFO(statement);
            REQUIRE(raw.exec(QString::fromLatin1(statement)));
        }
        REQUIRE(raw.exec(QStringLiteral("PRAGMA application_id = %1").arg(SQLITE_APPLICATION_ID)));
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version = 3")));
        REQUIRE(raw.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")));
    }

    [[nodiscard]] const StorageLocation& location() const
    {
        return m_Location;
    }
    [[nodiscard]] std::filesystem::path database() const
    {
        return m_Location.database_path();
    }

private:
    TempDir m_Dir;
    StorageLocation m_Location{m_Dir.path()};
};

/// Every row of `entry` as text, in the order the rows are stored.
std::string entry_rows(const std::filesystem::path& database)
{
    RawDb raw(database);
    return raw
        .scalar(QStringLiteral("SELECT group_concat(id || '|' || day_log_id || '|' || seq || '|' || kind || '|' ||"
                               " captured_utc || '|' || captured_tz || '|' || ifnull(edited_utc, '-') || '|' ||"
                               " deleted, ';') FROM (SELECT * FROM entry ORDER BY rowid)"))
        .toString()
        .toStdString();
}

std::int64_t user_version(const std::filesystem::path& database)
{
    RawDb raw(database);
    return raw.scalar(QStringLiteral("PRAGMA user_version")).toLongLong();
}

/// The columns of a table as SQLite describes them.
std::string table_shape(RawDb& raw, const char* table)
{
    const QString sql = QStringLiteral("SELECT group_concat(name || ' ' || type || ' ' || \"notnull\" || ' ' ||"
                                       " ifnull(dflt_value, '-') || ' ' || pk, ', ') FROM (SELECT * FROM"
                                       " pragma_table_info('%1') ORDER BY cid)")
                            .arg(QLatin1String(table));
    return raw.scalar(sql).toString().toStdString();
}

/// Whether `entry.kind` takes `interaction`: tried inside a transaction that is rolled back, so the database is left as
/// it was.
bool can_insert_interaction(const std::filesystem::path& database)
{
    RawDb raw(database);
    if (!raw.exec(QStringLiteral("BEGIN")))
    {
        return false;
    }
    const bool inserted =
        raw.exec(QStringLiteral("INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000f1',"
                                " '00000000-0000-4000-8000-000000000001', 9, 'interaction', 2000, 'UTC', NULL, 0)"));
    (void)raw.exec(QStringLiteral("ROLLBACK"));
    return inserted;
}

bool table_exists(const std::filesystem::path& database, const char* table)
{
    RawDb raw(database);
    return raw.scalar(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = ?"),
                      {QString::fromLatin1(table)})
               .toInt() == 1;
}

constexpr const char* NEW_TABLES[] = {"interaction_entry", "interaction_entry_person", "interaction_type", "person"};

void copy_tree(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::filesystem::create_directories(to);
    std::filesystem::copy(from, to,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// T-I2-1 migrate.interaction_step
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("M1: the step from 3 to 4 keeps every row of entry, in its order, and adds the interaction tables",
          "[migration][interaction]")
{
    VersionThreeStore store;
    const std::string before = entry_rows(store.database());
    REQUIRE_FALSE(before.empty());
    REQUIRE(user_version(store.database()) == 3);
    REQUIRE_FALSE(can_insert_interaction(store.database())); // the CHECK of version 3
    ScriptedClock clock(sample_time(4000));

    const auto report = Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run();
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == MigrationOutcome::MIGRATED);
    CHECK(report->m_FromVersion == 3);
    CHECK(report->m_ToVersion == SQLITE_SCHEMA_VERSION);
    CHECK(report->m_Steps == std::vector<std::string>{"add interaction entries and people"});

    CHECK(user_version(store.database()) == SQLITE_SCHEMA_VERSION);
    CHECK(entry_rows(store.database()) == before);
    for (const char* table : NEW_TABLES)
    {
        CHECK(table_exists(store.database(), table));
    }
    CHECK_FALSE(table_exists(store.database(), "entry_new"));
    CHECK(can_insert_interaction(store.database()));

    // The other tables are as they were, and their references find the new `entry`.
    RawDb raw(store.database());
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM error_entry")).toInt() == 3);
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM note_entry")).toInt() == 1);
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM header_change_entry")).toInt() == 1);
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM draft")).toInt() == 1);
    CHECK_FALSE(raw.scalar(QStringLiteral("SELECT count(*) FROM pragma_foreign_key_check")).toInt() != 0);
    CHECK(raw.scalar(QStringLiteral("PRAGMA integrity_check")).toString() == "ok");
    CHECK(
        raw.scalar(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'index' AND name = 'entry_by_time'"))
            .toInt() == 1);
    // A kind that is none of the four is still refused.
    CHECK_FALSE(
        raw.exec(QStringLiteral("INSERT INTO entry VALUES ('00000000-0000-4000-8000-0000000000f2',"
                                " '00000000-0000-4000-8000-000000000001', 10, 'bogus', 2000, 'UTC', NULL, 0)")));

    // The repository opens it and reads the log as it was.
    ScriptedClock open_clock(sample_time(5000));
    NullJournal journal;
    auto repository = SqliteLogRepository::open(store.database(), open_clock, journal);
    REQUIRE(repository.has_value());
    const auto log = (*repository)->load_log(sample_id<LogId>(1)).value();
    CHECK(log.m_Log.entries().size() == 5);

    // The backup is the version 3 file.
    std::size_t backups = 0;
    for (const auto& file : std::filesystem::directory_iterator(store.location().backup_dir()))
    {
        if (file.path().filename().string().starts_with("ej-v3-"))
        {
            ++backups;
        }
    }
    CHECK(backups == 1);
}

TEST_CASE("A database migrated from 3 has the schema of a new one", "[migration][interaction]")
{
    VersionThreeStore migrated;
    ScriptedClock clock(sample_time(4000));
    REQUIRE(Migrator(migrated.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run().has_value());

    TempDir fresh_dir;
    {
        OpenStore fresh(fresh_dir.path());
        REQUIRE(fresh.repo().create_log(log_id(1), sample_header()).has_value());
    }
    RawDb fresh_raw(fresh_dir.path() / "ej.db");
    RawDb migrated_raw(migrated.database());
    for (const char* table : {"entry", "error_entry", "note_entry", "header_change_entry", "interaction_entry",
                              "interaction_entry_person", "interaction_type", "person", "day_log"})
    {
        INFO(table);
        const std::string expected = table_shape(fresh_raw, table);
        CHECK_FALSE(expected.empty());
        CHECK(table_shape(migrated_raw, table) == expected);
    }
    const auto indexes = [](RawDb& raw)
    {
        return raw
            .scalar(QStringLiteral("SELECT group_concat(name, ',') FROM (SELECT name FROM sqlite_master WHERE type ="
                                   " 'index' AND name NOT LIKE 'sqlite_%' ORDER BY name)"))
            .toString();
    };
    CHECK(indexes(migrated_raw) == indexes(fresh_raw));
}

TEST_CASE("M2: a later step that fails puts the version 3 database back, byte for byte", "[migration][interaction]")
{
    VersionThreeStore store;
    const std::string before = read_bytes(store.database());
    const std::string rows = entry_rows(store.database());
    ScriptedClock clock(sample_time(4000));

    std::vector<MigrationStep> steps = product_migration_steps();
    steps.push_back(failing_step(SQLITE_SCHEMA_VERSION));
    const auto report = Migrator(store.location(), clock, steps, SQLITE_SCHEMA_VERSION + 1).run();
    REQUIRE_FALSE(report.has_value());

    CHECK(read_bytes(store.database()) == before);
    CHECK(user_version(store.database()) == 3);
    CHECK(entry_rows(store.database()) == rows);
    CHECK_FALSE(can_insert_interaction(store.database()));
    for (const char* table : NEW_TABLES)
    {
        CHECK_FALSE(table_exists(store.database(), table));
    }
    CHECK_FALSE(std::filesystem::exists(store.location().migration_marker()));
}

TEST_CASE("M5: a database of a newer version than this build's is refused and left alone", "[migration][interaction]")
{
    VersionThreeStore store;
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version = %1").arg(SQLITE_SCHEMA_VERSION + 1)));
    }
    const std::string before = read_bytes(store.database());
    ScriptedClock clock(sample_time(4000));
    const auto report = Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION).run();
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().m_Kind == StorageErrorKind::NEWER_SCHEMA);
    CHECK(read_bytes(store.database()) == before);
    const bool has_backup = std::filesystem::exists(store.location().backup_dir()) &&
                            !std::filesystem::is_empty(store.location().backup_dir());
    CHECK_FALSE(has_backup);
}

// ---------------------------------------------------------------------------------------------------------------------
// T-I2-2 migrate.kill_mid_step
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("M3: a kill at any point of the step leaves the old database or the new one, never half",
          "[migration][interaction]")
{
    // The hook is called at each point the harness can end the process. The store is copied as a kill would leave it
    // (the database with its write-ahead log, the marker, the backup) and the migration is then failed, so that the
    // copy is taken at that point and nothing else. Recovery over each copy must give the old database or the new one.
    VersionThreeStore store;
    const std::string rows = entry_rows(store.database());
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
        copy_tree(store.location().root(), snapshots.back().path());
        return {};
    };
    REQUIRE(
        Migrator(store.location(), clock, product_migration_steps(), SQLITE_SCHEMA_VERSION, options).run().has_value());
    REQUIRE(snapshots.size() >= 4); // after the marker, after the step, before the commit, after it

    for (std::size_t i = 0; i < snapshots.size(); ++i)
    {
        INFO("kill point " << i);
        const StorageLocation killed(snapshots[i].path());
        const auto report = recover(killed);
        REQUIRE(report.has_value());
        const std::int64_t version = user_version(killed.database_path());
        if (version == 3)
        {
            CHECK_FALSE(can_insert_interaction(killed.database_path()));
            for (const char* table : NEW_TABLES)
            {
                CHECK_FALSE(table_exists(killed.database_path(), table));
            }
        }
        else
        {
            REQUIRE(version == SQLITE_SCHEMA_VERSION);
            for (const char* table : NEW_TABLES)
            {
                CHECK(table_exists(killed.database_path(), table));
            }
            CHECK(can_insert_interaction(killed.database_path()));
        }
        CHECK(entry_rows(killed.database_path()) == rows); // every row, in order
        CHECK_FALSE(std::filesystem::exists(killed.migration_marker()));
        RawDb raw(killed.database_path());
        CHECK(raw.scalar(QStringLiteral("PRAGMA integrity_check")).toString() == "ok");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// T-I2-6 recovery.interactions
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

/// A store with an open log holding a note, an error and three interactions (one with two people, one with one, one
/// with nobody), and three people in the journal. Closed again, the way the app closes it.
struct InteractionStore
{
    TempDir m_Dir;
    StateDigest m_Digest{};

    InteractionStore()
    {
        OpenStore store(m_Dir.path());
        REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
        PeopleStore::Options options;
        options.m_Cache = &store.repo();
        auto people = PeopleStore::open(store.location(), store.clock(), options);
        REQUIRE(people.has_value());
        store.clock().set(sample_time(5));
        REQUIRE((*people)->add_person(person(1), ALEX).has_value());
        REQUIRE((*people)->add_person(person(2), JORDAN).has_value());
        REQUIRE((*people)->add_person(person(3), PAT).has_value());
        REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
        REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(2, 20)).has_value());
        REQUIRE(store.repo()
                    .edit_entry(log_id(1), sample_interaction_entry(2, 20, {person(2), person(1)}), sample_time(21))
                    .has_value());
        REQUIRE((*people)->touch_person(person(2)).has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(1), sample_interaction_entry(3, 30, {person(1)}, "Morning Medley"))
                    .has_value());
        REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(4, 40)).has_value());
        REQUIRE(store.repo().add_entry(log_id(1), sample_note(5, 50, "Played a song")).has_value());
        m_Digest = digest_of(store.repo().load_log(log_id(1)).value());
    }

    [[nodiscard]] StorageLocation location() const
    {
        return StorageLocation(m_Dir.path());
    }

    [[nodiscard]] StoredLog load() const
    {
        ScriptedClock clock(sample_time(6000));
        NullJournal journal;
        auto repository = SqliteLogRepository::open(location().database_path(), clock, journal);
        REQUIRE(repository.has_value());
        return (*repository)->load_log(log_id(1)).value();
    }

    [[nodiscard]] CachedPeople cached() const
    {
        ScriptedClock clock(sample_time(6000));
        NullJournal journal;
        auto repository = SqliteLogRepository::open(location().database_path(), clock, journal);
        REQUIRE(repository.has_value());
        return (*repository)->read_people().value();
    }
};

/// Everything a report says in words.
std::string words_of(const RecoveryReport& report)
{
    std::string text = describe(report) + " " + report.m_IntegrityMessage;
    for (const std::string& note : report.m_Notes)
    {
        text += " " + note;
    }
    for (const std::string& path : report.m_QuarantinedPaths)
    {
        text += " " + path;
    }
    return text;
}

void remove_database(const StorageLocation& location)
{
    for (const char* suffix : {"", "-wal", "-shm"})
    {
        std::filesystem::path file = location.database_path();
        file += suffix;
        std::filesystem::remove(file);
    }
}

} // namespace

TEST_CASE("A healthy store with interactions and people recovers clean", "[recovery][interaction]")
{
    InteractionStore store;
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::CLEAN);
    CHECK(report->m_PeopleRecovered == 0);
    CHECK(report->m_PeopleLost == 0);
    CHECK(digest_of(store.load()) == store.m_Digest);
    CHECK(store.cached().m_People.size() == 3);
}

TEST_CASE("A database that is gone is rebuilt with its interactions, and the people table from the people journal",
          "[recovery][interaction]")
{
    InteractionStore store;
    remove_database(store.location());

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::REBUILT);
    CHECK(report->m_PeopleRecovered == 3);
    CHECK(report->m_PeopleLost == 0);
    const StoredLog log = store.load();
    CHECK(digest_of(log) == store.m_Digest);
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(2))->m_Body).m_People ==
          std::vector<PersonId>{person(2), person(1)});
    const CachedPeople cached = store.cached();
    REQUIRE(cached.m_People.size() == 3);
    const auto jordan = std::find_if(cached.m_People.begin(), cached.m_People.end(),
                                     [](const Person& p) { return p.m_Id == person(2); });
    REQUIRE(jordan != cached.m_People.end());
    CHECK(jordan->m_DisplayName == JORDAN);
    CHECK(jordan->m_LastUsedUtc.has_value());
    for (const std::string_view name : ALL_NAMES)
    {
        CHECK(words_of(*report).find(name) == std::string::npos);
    }

    // Once made, nothing more to do.
    const auto again = recover(store.location());
    REQUIRE(again.has_value());
    CHECK(again->m_PeopleRecovered == 0);
}

TEST_CASE("A people journal that is missing never loses an entry: they read as someone and the loss is counted",
          "[recovery][interaction]")
{
    InteractionStore store;
    std::filesystem::remove(store.location().people_path());

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleLost == 3); // the three the table and the entries know
    CHECK(report->m_PeopleRecovered == 0);
    CHECK(digest_of(store.load()) == store.m_Digest); // not one entry changed
    CHECK(store.cached().m_People.empty());
    for (const std::string_view name : ALL_NAMES)
    {
        CHECK(words_of(*report).find(name) == std::string::npos);
    }

    // The store is usable: a new people journal is made at the first person.
    OpenStore open(store.location().root());
    PeopleStore::Options options;
    options.m_Cache = &open.repo();
    auto people = PeopleStore::open(open.location(), open.clock(), options);
    REQUIRE(people.has_value());
    REQUIRE((*people)->add_person(person(1), ALEX).has_value()); // the id an entry still holds, named again
}

TEST_CASE("A people journal that is damaged is set aside, what can be read is kept and the rest is counted",
          "[recovery][interaction]")
{
    InteractionStore store;
    std::string bytes = read_bytes(store.location().people_path());
    // Damage the second record: the first person stays, the second and third are lost.
    const std::size_t second = bytes.find(JORDAN);
    REQUIRE(second != std::string::npos);
    bytes.replace(second, 6, "Jorden");
    write_bytes(store.location().people_path(), bytes);

    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleLost == 2);
    CHECK(digest_of(store.load()) == store.m_Digest);
    CHECK(std::ranges::any_of(report->m_QuarantinedPaths,
                              [](const std::string& path) { return path.find("people.jsonl") != std::string::npos; }));
    CHECK(store.cached().m_People.size() == 1);
    const PeopleScan scan = scan_people_journal(read_bytes(store.location().people_path()));
    CHECK(scan.is_clean());
    CHECK(scan.m_State.size() == 1);
    for (const std::string_view name : ALL_NAMES)
    {
        CHECK(words_of(*report).find(name) == std::string::npos);
    }

    // The next launch finds nothing to do.
    const auto again = recover(store.location());
    REQUIRE(again.has_value());
    CHECK(again->m_PeopleLost ==
          1); // the entries still hold the id of the second person, whom the journal does not know
    CHECK(again->m_QuarantinedPaths.empty());
}

TEST_CASE("A people journal that is nothing but damage is moved to quarantine whole", "[recovery][interaction]")
{
    InteractionStore store;
    write_bytes(store.location().people_path(), std::string(400, '\0') + "garbage\n");
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleLost == 3);
    CHECK_FALSE(std::filesystem::exists(store.location().people_path()));
    CHECK(std::ranges::any_of(report->m_QuarantinedPaths,
                              [](const std::string& path) { return path.find("people.jsonl") != std::string::npos; }));
    CHECK(digest_of(store.load()) == store.m_Digest);
}

TEST_CASE("A torn tail of the people journal is cut off and loses nobody", "[recovery][interaction]")
{
    InteractionStore store;
    const std::string whole = read_bytes(store.location().people_path());
    write_bytes(store.location().people_path(), whole + "{\"body\":{\"id\":\"0000");
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleLost == 0);
    CHECK(report->m_DiscardedTailBytes > 0);
    CHECK(read_bytes(store.location().people_path()) == whole);
    CHECK(report->m_QuarantinedPaths.empty());
}

TEST_CASE("A person who was acknowledged in the journal and not yet in the table is put in the table",
          "[recovery][interaction]")
{
    // A kill between the journal and the table: the journal has the person, the table does not.
    InteractionStore store;
    {
        OpenStore open(store.location().root());
        PeopleStore::Options options; // no cache: the journal alone is written
        auto people = PeopleStore::open(open.location(), open.clock(), options);
        REQUIRE(people.has_value());
        REQUIRE((*people)->add_person(person(4), SAM).has_value());
    }
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleRecovered == 1);
    CHECK(report->m_PeopleLost == 0);
    CHECK(store.cached().m_People.size() == 4);
}

TEST_CASE("A location change that is in the journal and not in the table is put in the table",
          "[recovery][interaction][location]")
{
    InteractionStore store;
    const LocationId north = sample_id<LocationId>(7);
    {
        OpenStore open(store.location().root());
        PeopleStore::Options options; // no cache: the journal alone is written
        auto people = PeopleStore::open(open.location(), open.clock(), options);
        REQUIRE(people.has_value());
        REQUIRE((*people)->set_person_location(person(1), north).has_value());
    }
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleRecovered == 1);
    CHECK(report->m_PeopleLost == 0);
    const auto cached = store.cached();
    const auto row = std::ranges::find_if(cached.m_People, [](const Person& p) { return p.m_Id == person(1); });
    REQUIRE(row != cached.m_People.end());
    CHECK(row->m_Location == north);
}

TEST_CASE("A forget that was killed before its rename leaves the old file, and recovery clears the leftover",
          "[recovery][interaction][forget]")
{
    InteractionStore store;
    const std::string old_file = read_bytes(store.location().people_path());
    write_bytes(store.location().root() / "people.jsonl.tmp", "half of a new file");
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK_FALSE(std::filesystem::exists(store.location().root() / "people.jsonl.tmp"));
    CHECK(read_bytes(store.location().people_path()) == old_file);
    CHECK(report->m_PeopleLost == 0);
}

TEST_CASE("A person the table says was forgotten stays forgotten when the people journal is lost",
          "[recovery][interaction][forget]")
{
    InteractionStore store;
    {
        OpenStore open(store.location().root());
        PeopleStore::Options options;
        options.m_Cache = &open.repo();
        auto people = PeopleStore::open(open.location(), open.clock(), options);
        REQUIRE(people.has_value());
        REQUIRE((*people)->forget_person(person(1)).has_value());
    }
    std::filesystem::remove(store.location().people_path());
    const auto report = recover(store.location());
    REQUIRE(report.has_value());
    CHECK(report->m_PeopleLost == 2); // the two who were not forgotten; the third is known as forgotten
    const PeopleScan scan = scan_people_journal(read_bytes(store.location().people_path()));
    CHECK(scan.m_State.is_forgotten(person(1)));
    CHECK(digest_of(store.load()) == store.m_Digest);
}

// ---------------------------------------------------------------------------------------------------------------------
// T-I2-8 journal.no_names
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("After adding, renaming and forgetting people, no log journal and no entry table holds a name",
          "[journal][interaction][privacy]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
        PeopleStore::Options options;
        options.m_Cache = &store.repo();
        auto people = PeopleStore::open(store.location(), store.clock(), options);
        REQUIRE(people.has_value());

        REQUIRE((*people)->add_person(person(1), ALEX).has_value());
        REQUIRE((*people)->add_person(person(2), JORDAN).has_value());
        REQUIRE((*people)->add_person(person(3), "Pat Stand-in Senior").has_value());
        REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(1, 10)).has_value());
        REQUIRE(store.repo()
                    .edit_entry(log_id(1), sample_interaction_entry(1, 10, {person(1), person(2)}), sample_time(11))
                    .has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(1), sample_interaction_entry(2, 20, {person(3)}, "Played a song"))
                    .has_value());
        REQUIRE((*people)->rename_person(person(2), PAT).has_value());
        REQUIRE((*people)->rename_person(person(3), "Jordan Placeholder Junior").has_value());
        REQUIRE((*people)->retire_person(person(3)).has_value());
        REQUIRE((*people)->touch_person(person(1)).has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(1), sample_interaction_entry(3, 30, {person(1), person(2), person(3)}))
                    .has_value());
        REQUIRE(store.repo().start_new_log(log_id(1), log_id(2), sample_header()).has_value());
        REQUIRE(store.repo().add_entry(log_id(2), sample_interaction_entry(4, 40, {person(2)})).has_value());
        REQUIRE((*people)->forget_person(person(2)).has_value());
        REQUIRE((*people)->forget_person(person(3)).has_value());
        REQUIRE(store.repo().checkpoint().has_value());
    }

    // (Sam Example is the tester of the sample header, so he is not one of them.)
    const std::vector<std::string_view> every_name = {ALEX, JORDAN, PAT, "Pat Stand-in Senior",
                                                      "Jordan Placeholder Junior"};

    // No log journal, open or archived.
    std::size_t journals = 0;
    for (const auto& folder : {dir.path() / "journal", dir.path() / "journal" / "archive"})
    {
        for (const auto& file : std::filesystem::directory_iterator(folder))
        {
            if (!file.is_regular_file())
            {
                continue;
            }
            ++journals;
            const std::string bytes = read_bytes(file.path());
            for (const std::string_view name : every_name)
            {
                INFO(file.path().filename().string());
                CHECK(bytes.find(name) == std::string::npos);
            }
        }
    }
    CHECK(journals == 2);

    // No entry table: the person table is taken out of a copy of the database, and nothing of a name is left in it.
    TempDir copy;
    std::filesystem::copy_file(dir.path() / "ej.db", copy.path() / "ej.db");
    {
        RawDb raw(copy.path() / "ej.db");
        REQUIRE(raw.exec(QStringLiteral("DROP TABLE person")));
        REQUIRE(raw.exec(QStringLiteral("VACUUM")));
    }
    const std::string database = read_bytes(copy.path() / "ej.db");
    for (const std::string_view name : every_name)
    {
        CHECK(database.find(name) == std::string::npos);
    }

    // What the people journal holds: the one person who was neither forgotten nor renamed, and nobody else.
    const std::string people_bytes = read_bytes(dir.path() / "people.jsonl");
    CHECK(people_bytes.find(ALEX) != std::string::npos);
    CHECK(people_bytes.find(JORDAN) == std::string::npos);
    CHECK(people_bytes.find(PAT) == std::string::npos);
    CHECK(people_bytes.find("Junior") == std::string::npos);
    CHECK(people_bytes.find("Senior") == std::string::npos);
}
