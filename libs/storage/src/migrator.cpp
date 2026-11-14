// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/migrator.hpp"

#include "error_journaling/sqlite_log_repository.hpp"
#include "platform/durable_sync.hpp"
#include "protect.hpp"
#include "quarantine.hpp"
#include "schema_v4.hpp"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <system_error>

namespace ej
{

namespace
{

using Failure = std::unexpected<StorageError>;

constexpr std::string_view MARKER_HEADER = "ej-migration 1";

std::atomic<unsigned> g_ConnectionCounter{0};

QString to_qt(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string to_std(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

StorageErrorKind kind_of_sqlite_code(int primary)
{
    switch (primary)
    {
    case 5:
    case 6:
        return StorageErrorKind::LOCKED;
    case 3:
    case 8:
    case 14:
    case 23:
        return StorageErrorKind::PERMISSION;
    case 13:
        return StorageErrorKind::DISK_FULL;
    case 11:
    case 26:
        return StorageErrorKind::CORRUPT;
    case 10:
        return StorageErrorKind::IO;
    default:
        return StorageErrorKind::DATABASE;
    }
}

StorageError from_sql(const QSqlError& error, std::string_view statement)
{
    const int code = error.nativeErrorCode().toInt() & 0xFF;
    std::string message = "migration statement failed: " + std::string(statement.substr(0, 60));
    const std::string detail = to_std(error.databaseText().left(200));
    if (!detail.empty())
    {
        message += " (" + detail + ")";
    }
    return make_error(kind_of_sqlite_code(code), std::move(message), code);
}

StorageError io_error(const char* what, const std::error_code& code)
{
    const bool full = code == std::errc::no_space_on_device;
    const bool denied = code == std::errc::permission_denied || code == std::errc::read_only_file_system;
    return make_error(full     ? StorageErrorKind::DISK_FULL
                      : denied ? StorageErrorKind::PERMISSION
                               : StorageErrorKind::IO,
                      what, code.value());
}

// ---------------------------------------------------------------------------------------------------------------------
// The database
// ---------------------------------------------------------------------------------------------------------------------

/// A connection to the database file of the store, for the steps and the checks. Closed when it goes away.
class Sql final : public MigrationConnection
{
public:
    [[nodiscard]] static Result<std::unique_ptr<Sql>, StorageError> open(const std::filesystem::path& file)
    {
        if (QCoreApplication::instance() == nullptr)
        {
            return Failure(make_error(StorageErrorKind::INTERNAL, "Qt SQL needs a QCoreApplication"));
        }
        auto sql = std::unique_ptr<Sql>(new Sql);
        sql->m_Name = QStringLiteral("ej-migration-%1").arg(++g_ConnectionCounter);
        sql->m_Db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), sql->m_Name);
        sql->m_Db.setDatabaseName(to_qt(file.generic_string()));
        if (!sql->m_Db.open())
        {
            return Failure(from_sql(sql->m_Db.lastError(), "open"));
        }
        if (auto timeout = sql->exec("PRAGMA busy_timeout = 5000"); !timeout)
        {
            return Failure(timeout.error());
        }
        // Off, as SQLite has it by default and as the rebuild of a table asks (a step that drops and renames a table
        // that others refer to). `foreign_key_check` after the steps is what finds a row without its parent.
        if (auto keys = sql->exec("PRAGMA foreign_keys = OFF"); !keys)
        {
            return Failure(keys.error());
        }
        return sql;
    }

    ~Sql() override
    {
        if (m_Db.isValid())
        {
            m_Db.close();
        }
        m_Db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_Name);
    }

    Result<void, StorageError> exec(std::string_view sql) override
    {
        QSqlQuery query(m_Db);
        if (!query.exec(to_qt(sql)))
        {
            return Failure(from_sql(query.lastError(), sql));
        }
        return {};
    }

    Result<std::optional<std::string>, StorageError> scalar(std::string_view sql) override
    {
        QSqlQuery query(m_Db);
        if (!query.exec(to_qt(sql)))
        {
            return Failure(from_sql(query.lastError(), sql));
        }
        if (!query.next() || query.value(0).isNull())
        {
            return std::optional<std::string>{};
        }
        return std::optional<std::string>(to_std(query.value(0).toString()));
    }

    [[nodiscard]] Result<std::int64_t, StorageError> number(std::string_view sql)
    {
        auto value = scalar(sql);
        if (!value)
        {
            return Failure(value.error());
        }
        return value->has_value() ? std::stoll(**value) : 0;
    }

private:
    Sql() = default;
    QString m_Name;
    QSqlDatabase m_Db;
};

/// `foreign_key_check` finds nothing and `integrity_check` says ok.
Result<void, StorageError> post_check(Sql& sql)
{
    auto violation = sql.scalar("PRAGMA foreign_key_check");
    if (!violation)
    {
        return Failure(violation.error());
    }
    if (violation->has_value())
    {
        return Failure(make_error(StorageErrorKind::DATABASE, "foreign_key_check found a row without its parent"));
    }
    auto integrity = sql.scalar("PRAGMA integrity_check");
    if (!integrity)
    {
        return Failure(integrity.error());
    }
    if (integrity->value_or("") != "ok")
    {
        return Failure(make_error(StorageErrorKind::CORRUPT, "integrity_check after the migration: " +
                                                                 integrity->value_or("no answer").substr(0, 200)));
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------
// The marker
// ---------------------------------------------------------------------------------------------------------------------

struct Marker
{
    std::int32_t m_From = 0;
    std::int32_t m_To = 0;
    std::string m_Backup; // relative to the store, with forward slashes
};

std::string marker_text(const Marker& marker)
{
    return std::string(MARKER_HEADER) + "\nfrom=" + std::to_string(marker.m_From) +
           "\nto=" + std::to_string(marker.m_To) + "\nbackup=" + marker.m_Backup + "\n";
}

std::optional<Marker> parse_marker(const std::string& text)
{
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != MARKER_HEADER)
    {
        return std::nullopt;
    }
    Marker marker;
    bool from = false;
    bool to = false;
    bool backup = false;
    while (std::getline(in, line))
    {
        const auto equals = line.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        try
        {
            if (key == "from")
            {
                marker.m_From = static_cast<std::int32_t>(std::stol(value));
                from = true;
            }
            else if (key == "to")
            {
                marker.m_To = static_cast<std::int32_t>(std::stol(value));
                to = true;
            }
            else if (key == "backup")
            {
                marker.m_Backup = value;
                backup = true;
            }
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }
    return from && to && backup ? std::optional<Marker>(marker) : std::nullopt;
}

/// A file written whole or not at all: to a temporary file beside it, synced, then renamed over, and the folder synced.
Result<void, StorageError> write_durably(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::remove(temporary, code);
    {
        auto file = platform::open_for_append(temporary);
        if (!file)
        {
            return Failure(file.error());
        }
        protect_path(temporary);
        if (std::fwrite(text.data(), 1, text.size(), file->get()) != text.size())
        {
            return Failure(make_error(StorageErrorKind::IO, "the migration marker could not be written"));
        }
        if (auto synced = platform::durable_sync(file->get()); !synced)
        {
            return synced;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code)
    {
        return Failure(io_error("the migration marker could not be moved into place", code));
    }
    return platform::sync_directory(path.parent_path());
}

/// A copy of `from` at `to` that is on disk when this returns, with its folder.
Result<void, StorageError> copy_durably(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::error_code code;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, code);
    if (code)
    {
        return Failure(io_error("the database could not be copied", code));
    }
    protect_path(to);
    {
        auto file = platform::open_for_append(to);
        if (!file)
        {
            return Failure(file.error());
        }
        if (auto synced = platform::durable_sync(file->get()); !synced)
        {
            return synced;
        }
    }
    return platform::sync_directory(to.parent_path());
}

bool same_bytes(const std::filesystem::path& a, const std::filesystem::path& b)
{
    std::error_code code;
    if (std::filesystem::file_size(a, code) != std::filesystem::file_size(b, code) || code)
    {
        return false;
    }
    std::ifstream left(a, std::ios::binary);
    std::ifstream right(b, std::ios::binary);
    if (!left || !right)
    {
        return false;
    }
    std::string x(1 << 16, '\0');
    std::string y(1 << 16, '\0');
    while (left && right)
    {
        left.read(x.data(), static_cast<std::streamsize>(x.size()));
        right.read(y.data(), static_cast<std::streamsize>(y.size()));
        if (left.gcount() != right.gcount() || !std::equal(x.begin(), x.begin() + left.gcount(), y.begin()))
        {
            return false;
        }
    }
    return true;
}

std::string time_stamp(UtcMillis time)
{
    const auto days = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::year_month_day date{days};
    const std::chrono::hh_mm_ss<std::chrono::seconds> clock_time{
        std::chrono::duration_cast<std::chrono::seconds>(time - days)};
    char buffer[40];
    const int written =
        std::snprintf(buffer, sizeof buffer, "%04d%02u%02uT%02d%02d%02dZ", static_cast<int>(date.year()),
                      static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                      static_cast<int>(clock_time.hours().count()), static_cast<int>(clock_time.minutes().count()),
                      static_cast<int>(clock_time.seconds().count()));
    return std::string(buffer, static_cast<std::size_t>(written));
}

std::string relative_text(const StorageLocation& location, const std::filesystem::path& path)
{
    return std::filesystem::relative(path, location.root()).generic_string();
}

/// What a migration of a store works on.
class Run
{
public:
    Run(const StorageLocation& location, const Clock& clock) : m_Location(location), m_Quarantine(location, clock) {}

    /// Puts the backup copy back as the database: the side files and a database that differs from the copy go to
    /// quarantine (nothing is deleted), the copy is written beside the database and renamed into place, and the marker
    /// is removed last, so a kill in between leaves the marker for the next launch to finish.
    Result<void, StorageError> restore(const Marker& marker, MigrationReport& report)
    {
        const std::filesystem::path backup = m_Location.root() / marker.m_Backup;
        std::error_code code;
        if (!std::filesystem::exists(backup, code))
        {
            return Failure(make_error(StorageErrorKind::IO,
                                      "the backup copy of the database is missing, and nothing was changed"));
        }
        const std::filesystem::path database = m_Location.database_path();
        for (const char* suffix : {"-wal", "-shm"})
        {
            std::filesystem::path side = database;
            side += suffix;
            if (auto moved = m_Quarantine.take(side); !moved)
            {
                return Failure(moved.error());
            }
        }
        if (!std::filesystem::exists(database, code) || !same_bytes(database, backup))
        {
            if (auto moved = m_Quarantine.take(database); !moved)
            {
                return Failure(moved.error());
            }
            std::filesystem::path staged = database;
            staged += ".restoring";
            if (auto copied = copy_durably(backup, staged); !copied)
            {
                return copied;
            }
            std::filesystem::rename(staged, database, code);
            if (code)
            {
                return Failure(io_error("the backup copy could not be moved into place", code));
            }
            if (auto synced = platform::sync_directory(database.parent_path()); !synced)
            {
                return synced;
            }
        }
        report.m_QuarantinedPaths = m_Quarantine.taken();
        report.m_Backup = marker.m_Backup;
        return remove_marker();
    }

    Result<void, StorageError> remove_marker()
    {
        std::error_code code;
        std::filesystem::remove(m_Location.migration_marker(), code);
        if (code)
        {
            return Failure(io_error("the migration marker could not be removed", code));
        }
        return platform::sync_directory(m_Location.root());
    }

    [[nodiscard]] const std::vector<std::string>& taken() const
    {
        return m_Quarantine.taken();
    }

private:
    const StorageLocation& m_Location;
    Quarantine m_Quarantine;
};

Result<std::string, StorageError> read_text(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return Failure(make_error(StorageErrorKind::IO, "a file could not be read"));
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

namespace
{

/// 3 to 4 (Stage 14, section 3.3): `entry.kind` may be `interaction`, and the interaction and person tables exist. A
/// CHECK cannot be altered in place, so `entry` is built again under another name inside the migration's transaction
/// (create, copy, drop, rename; the order the SQLite documentation gives). The rows are copied as they are, in the
/// order they were stored. Nothing else is rewritten: no log, no entry of another kind.
Result<void, StorageError> add_interactions(MigrationConnection& sql)
{
    std::string create_copy = schema::ENTRY_TABLE;
    const std::string_view prefix = schema::ENTRY_TABLE_PREFIX;
    create_copy.replace(0, prefix.size(), schema::ENTRY_COPY_PREFIX);
    const std::string statements[] = {
        create_copy,
        "INSERT INTO entry_new (id, day_log_id, seq, kind, captured_utc, captured_tz, edited_utc, deleted)"
        " SELECT id, day_log_id, seq, kind, captured_utc, captured_tz, edited_utc, deleted FROM entry ORDER BY rowid",
        "DROP TABLE entry",
        "ALTER TABLE entry_new RENAME TO entry",
        schema::ENTRY_BY_TIME,
    };
    for (const std::string& statement : statements)
    {
        if (auto done = sql.exec(statement); !done)
        {
            return done;
        }
    }
    for (const char* statement : schema::INTERACTION_TABLES)
    {
        if (auto done = sql.exec(statement); !done)
        {
            return done;
        }
    }
    return {};
}

} // namespace

std::vector<MigrationStep> product_migration_steps()
{
    // 1 to 2: whether a shared log has changed since its share. Every log that exists gets 0, so a log that was shared
    // reads as shared; nothing in a version 1 database says otherwise, and the person is not told of changes that
    // were made before the build could know of them.
    // 2 to 3: the other software versions (HDR-10). Every log that exists gets NULL, which reads as none, so its single
    // version stays the robot's and nothing is rewritten.
    // 3 to 4: the interaction entries and the person cache; `entry` is rebuilt (`add_interactions`).
    return {MigrationStep{
        1, "add day_log.changed_since_share", [](MigrationConnection& sql) -> Result<void, StorageError>
        {
            return sql.exec("ALTER TABLE day_log ADD COLUMN changed_since_share INTEGER NOT NULL DEFAULT 0"
                            " CHECK (changed_since_share IN (0, 1))");
        }},
            MigrationStep{2, "add day_log.extra_versions", [](MigrationConnection& sql) -> Result<void, StorageError>
                          { return sql.exec("ALTER TABLE day_log ADD COLUMN extra_versions TEXT"); }},
            MigrationStep{3, "add interaction entries and people", &add_interactions}};
}

std::string_view migration_outcome_name(MigrationOutcome outcome)
{
    switch (outcome)
    {
    case MigrationOutcome::UP_TO_DATE:
        return "up_to_date";
    case MigrationOutcome::MIGRATED:
        return "migrated";
    case MigrationOutcome::NO_MARKER:
        return "no_marker";
    case MigrationOutcome::NOT_COMMITTED:
        return "not_committed";
    case MigrationOutcome::COMPLETED:
        return "completed";
    case MigrationOutcome::RESTORED:
        return "restored";
    }
    return {};
}

Migrator::Migrator(StorageLocation location, const Clock& clock, std::vector<MigrationStep> steps,
                   std::int32_t target_version, MigrationOptions options)
    : m_Location(std::move(location)), m_Clock(clock), m_Steps(std::move(steps)), m_Target(target_version),
      m_Options(std::move(options))
{
}

Result<MigrationReport, StorageError> Migrator::resume(const StorageLocation& location, const Clock& clock)
{
    MigrationReport report;
    report.m_Outcome = MigrationOutcome::NO_MARKER;
    if (!std::filesystem::exists(location.migration_marker()))
    {
        return report;
    }
    auto text = read_text(location.migration_marker());
    if (!text)
    {
        return Failure(text.error());
    }
    const auto marker = parse_marker(*text);
    if (!marker)
    {
        // Written whole or not at all, so this is not a torn write: something else is in its place. Nothing is guessed.
        return Failure(
            make_error(StorageErrorKind::IO, "the migration marker cannot be read, and nothing was changed"));
    }
    report.m_FromVersion = marker->m_From;
    report.m_ToVersion = marker->m_To;
    report.m_Backup = marker->m_Backup;
    Run run(location, clock);

    // What state is the database in? Opening it lets SQLite finish or discard whatever its write-ahead log held.
    enum class Found
    {
        OLD,  // at the version before the migration, and sound
        NEW,  // at the version after it, and sound
        OTHER // anything else: damaged, missing, or half of something
    } found = Found::OTHER;
    if (std::filesystem::exists(location.database_path()))
    {
        if (auto sql = Sql::open(location.database_path()))
        {
            const auto version = (*sql)->number("PRAGMA user_version");
            if (version && *version == marker->m_From)
            {
                const auto quick = (*sql)->scalar("PRAGMA quick_check");
                found = quick && quick->value_or("") == "ok" ? Found::OLD : Found::OTHER;
            }
            else if (version && *version == marker->m_To)
            {
                found = post_check(**sql) ? Found::NEW : Found::OTHER;
            }
        }
    }
    if (found != Found::OTHER)
    {
        if (auto removed = run.remove_marker(); !removed)
        {
            return Failure(removed.error());
        }
        report.m_Outcome = found == Found::OLD ? MigrationOutcome::NOT_COMMITTED : MigrationOutcome::COMPLETED;
        return report;
    }
    if (auto restored = run.restore(*marker, report); !restored)
    {
        return Failure(restored.error());
    }
    report.m_Outcome = MigrationOutcome::RESTORED;
    return report;
}

Result<MigrationReport, StorageError> Migrator::run()
{
    MigrationReport report;
    report.m_ToVersion = m_Target;

    // A migration that was killed is dealt with first.
    if (std::filesystem::exists(m_Location.migration_marker()))
    {
        auto resumed = resume(m_Location, m_Clock);
        if (!resumed)
        {
            return Failure(resumed.error());
        }
    }
    if (!std::filesystem::exists(m_Location.database_path()))
    {
        report.m_FromVersion = m_Target;
        return report; // nothing to migrate: the first start makes a database at the current version
    }

    // 1. The versions.
    std::int32_t from = 0;
    {
        auto sql = Sql::open(m_Location.database_path());
        if (!sql)
        {
            return Failure(sql.error());
        }
        const auto application = (*sql)->number("PRAGMA application_id");
        const auto version = (*sql)->number("PRAGMA user_version");
        if (!application || !version)
        {
            return Failure(!application ? application.error() : version.error());
        }
        if (*version == 0 && *application == 0)
        {
            report.m_FromVersion = m_Target;
            return report; // an empty database: the repository sets it up at the current version
        }
        if (*application != SQLITE_APPLICATION_ID)
        {
            return Failure(
                make_error(StorageErrorKind::NOT_OUR_DATABASE, "this is not the database of this application"));
        }
        from = static_cast<std::int32_t>(*version);
    }
    report.m_FromVersion = from;
    if (from == m_Target)
    {
        return report;
    }
    if (from > m_Target)
    {
        return Failure(make_error(StorageErrorKind::NEWER_SCHEMA,
                                  "the database was written by a newer version and has been left alone"));
    }
    std::vector<const MigrationStep*> chain;
    for (std::int32_t version = from; version < m_Target; ++version)
    {
        const auto step = std::find_if(m_Steps.begin(), m_Steps.end(), [version](const MigrationStep& candidate)
                                       { return candidate.m_From == version; });
        if (step == m_Steps.end() || !step->m_Apply)
        {
            return Failure(make_error(StorageErrorKind::INTERNAL,
                                      "there is no migration step from version " + std::to_string(version)));
        }
        chain.push_back(&*step);
    }

    // 2. The copy and the marker, both on disk before the first step.
    const std::filesystem::path database = m_Location.database_path();
    std::error_code code;
    {
        auto sql = Sql::open(database);
        if (!sql)
        {
            return Failure(sql.error());
        }
        if (auto folded = (*sql)->exec("PRAGMA wal_checkpoint(TRUNCATE)"); !folded)
        {
            return Failure(folded.error());
        }
    }
    std::filesystem::create_directories(m_Location.backup_dir(), code);
    if (code)
    {
        return Failure(io_error("the backup folder could not be made", code));
    }
    protect_path(m_Location.backup_dir());
    std::filesystem::path backup;
    for (unsigned counter = 1;; ++counter)
    {
        backup = m_Location.backup_dir() / ("ej-v" + std::to_string(from) + "-" + time_stamp(m_Clock.now()) + "-" +
                                            std::to_string(counter) + ".db");
        if (!std::filesystem::exists(backup, code))
        {
            break;
        }
    }
    if (auto copied = copy_durably(database, backup); !copied)
    {
        return Failure(copied.error());
    }
    if (m_Options.m_Observer)
    {
        m_Options.m_Observer("backup_synced");
    }
    Marker marker;
    marker.m_From = from;
    marker.m_To = m_Target;
    marker.m_Backup = relative_text(m_Location, backup);
    report.m_Backup = marker.m_Backup;
    if (auto written = write_durably(m_Location.migration_marker(), marker_text(marker)); !written)
    {
        return Failure(written.error());
    }
    if (m_Options.m_Observer)
    {
        m_Options.m_Observer("marker_synced");
    }

    // 3. The steps, in one transaction. Anything that goes wrong up to and including the commit puts the copy back.
    Run run(m_Location, m_Clock);
    const auto fail_and_restore = [&](StorageError error) -> Result<MigrationReport, StorageError>
    {
        MigrationReport restored;
        if (auto done = run.restore(marker, restored); !done)
        {
            error.m_Message += "; the backup copy could not be put back (" + done.error().m_Message +
                               "), and the marker was left for the next start";
        }
        else
        {
            error.m_Message += "; the database was restored from the backup copy";
        }
        return Failure(std::move(error));
    };
    const auto hook = [&]() -> Result<void, StorageError>
    { return m_Options.m_Hook ? m_Options.m_Hook(CrashPoint::MID_MIGRATION) : Result<void, StorageError>{}; };

    if (auto hooked = hook(); !hooked)
    {
        return fail_and_restore(hooked.error());
    }
    {
        auto sql = Sql::open(database);
        if (!sql)
        {
            return fail_and_restore(sql.error());
        }
        if (auto begun = (*sql)->exec("BEGIN IMMEDIATE"); !begun)
        {
            return fail_and_restore(begun.error());
        }
        const auto abandon = [&](StorageError error)
        {
            (void)(*sql)->exec("ROLLBACK");
            (*sql).reset(); // closed before the files are touched
            return fail_and_restore(std::move(error));
        };
        for (const MigrationStep* step : chain)
        {
            if (auto applied = step->m_Apply(**sql); !applied)
            {
                return abandon(applied.error());
            }
            report.m_Steps.push_back(step->m_Name);
            if (auto hooked = hook(); !hooked)
            {
                return abandon(hooked.error());
            }
        }
        if (auto checked = post_check(**sql); !checked)
        {
            return abandon(checked.error());
        }
        if (m_Options.m_ExtraCheck)
        {
            if (auto checked = m_Options.m_ExtraCheck(**sql); !checked)
            {
                return abandon(checked.error());
            }
        }
        if (auto versioned = (*sql)->exec("PRAGMA user_version = " + std::to_string(m_Target)); !versioned)
        {
            return abandon(versioned.error());
        }
        if (auto hooked = hook(); !hooked)
        {
            return abandon(hooked.error());
        }
        if (auto committed = (*sql)->exec("COMMIT"); !committed)
        {
            return abandon(committed.error());
        }
    }
    (void)hook(); // committed: a kill from here leaves the new version and the marker, which `resume` completes

    // 4. Done.
    if (auto removed = run.remove_marker(); !removed)
    {
        return Failure(removed.error());
    }
    report.m_Outcome = MigrationOutcome::MIGRATED;
    return report;
}

} // namespace ej
