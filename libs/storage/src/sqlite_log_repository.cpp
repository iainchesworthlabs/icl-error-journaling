// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/sqlite_log_repository.hpp"

#include "error_journaling/header_history.hpp"
#include "error_journaling/iso_date.hpp"
#include "error_journaling/utf8.hpp"
#include "protect.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QMetaType>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <atomic>
#include <filesystem>
#include <functional>
#include <system_error>
#include <utility>

namespace ej
{

namespace
{

using Failure = std::unexpected<StorageError>;

std::atomic<unsigned> g_ConnectionCounter{0};

// ---------------------------------------------------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------------------------------------------------

QString to_qt(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string to_std(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QVariant null_text()
{
    return QVariant(QMetaType::fromType<QString>());
}

QVariant null_integer()
{
    return QVariant(QMetaType::fromType<qlonglong>());
}

QVariant text_or_null(const std::optional<std::string>& text)
{
    return text ? QVariant(to_qt(*text)) : null_text();
}

QVariant millis_value(UtcMillis time)
{
    return QVariant(static_cast<qlonglong>(time.time_since_epoch().count()));
}

UtcMillis millis_of(const QVariant& value)
{
    return UtcMillis{std::chrono::milliseconds{value.toLongLong()}};
}

std::optional<std::string> optional_text(const QVariant& value)
{
    if (value.isNull())
    {
        return std::nullopt;
    }
    return to_std(value.toString());
}

// ---------------------------------------------------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------------------------------------------------

// SQLite's primary result codes that matter here (named apart from the macros of sqlite3.h).
constexpr int RC_PERM = 3;
constexpr int RC_BUSY = 5;
constexpr int RC_LOCKED = 6;
constexpr int RC_READONLY = 8;
constexpr int RC_IOERR = 10;
constexpr int RC_CORRUPT = 11;
constexpr int RC_FULL = 13;
constexpr int RC_CANTOPEN = 14;
constexpr int RC_AUTH = 23;
constexpr int RC_NOTADB = 26;

StorageErrorKind kind_of_sqlite_code(int primary)
{
    switch (primary)
    {
    case RC_BUSY:
    case RC_LOCKED:
        return StorageErrorKind::LOCKED;
    case RC_PERM:
    case RC_READONLY:
    case RC_CANTOPEN:
    case RC_AUTH:
        return StorageErrorKind::PERMISSION;
    case RC_FULL:
        return StorageErrorKind::DISK_FULL;
    case RC_CORRUPT:
    case RC_NOTADB:
        return StorageErrorKind::CORRUPT;
    case RC_IOERR:
        return StorageErrorKind::IO;
    default:
        return StorageErrorKind::DATABASE;
    }
}

/// The message names the statement (SQL text with placeholders, never values) and SQLite's own words, so it holds no
/// entry text.
StorageError from_sql(const QSqlError& error, const QString& statement)
{
    const int code = error.nativeErrorCode().toInt() & 0xFF;
    std::string message = "database statement failed: " + to_std(statement.left(60));
    const std::string detail = to_std(error.databaseText().left(200));
    if (!detail.empty())
    {
        message += " (" + detail + ")";
    }
    return make_error(kind_of_sqlite_code(code), std::move(message), code);
}

StorageError corrupt(std::string message)
{
    return make_error(StorageErrorKind::CORRUPT, std::move(message));
}

Failure not_found(std::string message)
{
    return Failure(make_error(StorageErrorKind::NOT_FOUND, std::move(message)));
}

// ---------------------------------------------------------------------------------------------------------------------
// Who owns a file
// ---------------------------------------------------------------------------------------------------------------------

struct FileIdentity
{
    std::int64_t m_ApplicationId = 0;
    std::int64_t m_UserVersion = 0;
    std::int64_t m_Tables = 0;

    [[nodiscard]] bool blank() const { return m_ApplicationId == 0 && m_UserVersion == 0 && m_Tables == 0; }
};

/// Reads who a database file belongs to on a read-only connection of its own, so that a file that is not ours, is newer
/// or is not a database comes out exactly as it went in: a read-write connection would switch it to WAL, and closing
/// the last connection to a WAL database checkpoints it.
Result<FileIdentity, StorageError> probe_file(const std::filesystem::path& path, int busy_timeout_ms)
{
    std::error_code ignored;
    if (!std::filesystem::exists(path, ignored) || std::filesystem::file_size(path, ignored) == 0)
    {
        return FileIdentity{};  // a new store
    }
    const std::u8string text = path.generic_u8string();
    const QString name = QStringLiteral("ej-probe-%1").arg(++g_ConnectionCounter);
    Result<FileIdentity, StorageError> result =
        Failure(make_error(StorageErrorKind::INTERNAL, "the probe did not run"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size())));
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=%1").arg(busy_timeout_ms));
        if (!db.isValid())
        {
            result = Failure(make_error(StorageErrorKind::INTERNAL, "the SQLite driver of Qt SQL is not available"));
        }
        else if (!db.open())
        {
            result = Failure(from_sql(db.lastError(), QStringLiteral("open")));
        }
        else
        {
            const auto first = [&db](const QString& sql) -> Result<std::int64_t, StorageError>
            {
                QSqlQuery query(db);
                if (!query.exec(sql))
                {
                    return Failure(from_sql(query.lastError(), sql));
                }
                return query.next() ? query.value(0).toLongLong() : 0;
            };
            const auto application_id = first(QStringLiteral("PRAGMA application_id"));
            const auto user_version = first(QStringLiteral("PRAGMA user_version"));
            const auto tables = first(QStringLiteral("SELECT count(*) FROM sqlite_master"));
            if (!application_id || !user_version || !tables)
            {
                result = Failure(!application_id ? application_id.error()
                                 : !user_version ? user_version.error()
                                                 : tables.error());
            }
            else
            {
                result = FileIdentity{*application_id, *user_version, *tables};
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The schema (section 2.3): version 1, and version 2 which adds `day_log.changed_since_share`. A new database is made
// at the current version in one go; a version 1 database gets the one column from the migration step in `migrator.cpp`.
// ---------------------------------------------------------------------------------------------------------------------

constexpr const char* SCHEMA[] = {
    "CREATE TABLE day_log ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " local_date TEXT NOT NULL,"
    " facilitator TEXT NOT NULL,"
    " unit_number TEXT,"
    " location_id TEXT,"
    " location_text TEXT,"
    " software_version TEXT,"
    " created_utc INTEGER NOT NULL,"
    " last_shared_utc INTEGER,"
    " archived INTEGER NOT NULL DEFAULT 0 CHECK (archived IN (0, 1)),"
    " previous_log_id TEXT,"
    " applied_seq INTEGER NOT NULL DEFAULT 0,"
    // Shared, and changed since (`LogMeta::m_ChangedSinceShare`). Last, as the migration adds it. A row from before the
    // column existed holds 0: a log that was shared reads as shared.
    " changed_since_share INTEGER NOT NULL DEFAULT 0 CHECK (changed_since_share IN (0, 1)))",
    "CREATE INDEX day_log_archived ON day_log(archived)",
    // At most one log is open, enforced by the database and not only by the code (DS-05).
    "CREATE UNIQUE INDEX day_log_one_open ON day_log(archived) WHERE archived = 0",
    "CREATE TABLE entry ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " day_log_id TEXT NOT NULL REFERENCES day_log(id) ON DELETE RESTRICT,"
    " seq INTEGER NOT NULL,"
    " kind TEXT NOT NULL CHECK (kind IN ('error', 'note', 'header_change')),"
    " captured_utc INTEGER NOT NULL,"
    " captured_tz TEXT NOT NULL,"
    " edited_utc INTEGER,"
    " deleted INTEGER NOT NULL DEFAULT 0 CHECK (deleted IN (0, 1)),"
    " UNIQUE (day_log_id, seq))",
    "CREATE INDEX entry_by_time ON entry(day_log_id, captured_utc, seq)",
    "CREATE TABLE error_entry ("
    " entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " error_type_id TEXT NOT NULL,"
    " error_type_label TEXT NOT NULL,"
    " attached_note TEXT)",
    "CREATE TABLE note_entry ("
    " entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " template_id TEXT,"
    " value_text TEXT NOT NULL)",
    // old_value is what the value was when the change was written. The log derives the old side from its history, so a
    // correction can leave this behind; nothing reads it back.
    "CREATE TABLE header_change_entry ("
    " entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " field TEXT NOT NULL,"
    " old_value TEXT,"
    " new_value TEXT NOT NULL)",
    "CREATE TABLE draft ("
    " id INTEGER PRIMARY KEY CHECK (id = 1),"
    " text TEXT NOT NULL,"
    " updated_utc INTEGER NOT NULL)",
    "CREATE TABLE store_state ("
    " id INTEGER PRIMARY KEY CHECK (id = 1),"
    " applied_seq INTEGER NOT NULL,"
    " journal_generation INTEGER NOT NULL)",
    "INSERT INTO store_state (id, applied_seq, journal_generation) VALUES (1, 0, 0)",
};

constexpr const char* HEADER_COLUMNS =
    "local_date, facilitator, unit_number, location_id, location_text, software_version";

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------------------------------------------------

struct SqliteLogRepository::Impl
{
    /// A log read from the database with the sequence number the database has applied to it.
    struct Loaded
    {
        StoredLog m_Stored;
        std::uint64_t m_AppliedSeq = 0;
    };

    Impl(const Clock& clock, Journal& journal, CrashHook hook)
        : m_Clock(clock), m_Journal(journal), m_Hook(std::move(hook))
    {
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl()
    {
        if (m_Db.isValid())
        {
            m_Db.close();
        }
        m_Db = QSqlDatabase();
        if (!m_Name.isEmpty())
        {
            QSqlDatabase::removeDatabase(m_Name);
        }
    }

    // --- statements ---

    Result<QSqlQuery, StorageError> run(const QString& sql, const QVariantList& args = {}) const
    {
        QSqlQuery query(m_Db);
        query.setForwardOnly(true);
        if (!query.prepare(sql) || !bind_and_exec(query, args))
        {
            return Failure(from_sql(query.lastError(), sql));
        }
        return query;
    }

    static bool bind_and_exec(QSqlQuery& query, const QVariantList& args)
    {
        for (const QVariant& arg : args)
        {
            query.addBindValue(arg);
        }
        return query.exec();
    }

    Result<void, StorageError> exec(const QString& sql, const QVariantList& args = {}) const
    {
        auto query = run(sql, args);
        if (!query)
        {
            return Failure(query.error());
        }
        return {};
    }

    /// The first column of the first row, or an invalid variant when there is no row.
    Result<QVariant, StorageError> scalar(const QString& sql, const QVariantList& args = {}) const
    {
        auto query = run(sql, args);
        if (!query)
        {
            return Failure(query.error());
        }
        return query->next() ? query->value(0) : QVariant();
    }

    Result<void, StorageError> at(CrashPoint point) const
    {
        return m_Hook ? m_Hook(point) : Result<void, StorageError>{};
    }

    // --- opening ---

    Result<void, StorageError> open_database(const std::filesystem::path& path, int busy_timeout_ms);
    Result<void, StorageError> apply_settings(int busy_timeout_ms) const;
    Result<void, StorageError> check_setting(const char* name, const QString& set, const QString& read,
                                             const QVariant& expected) const;
    Result<void, StorageError> initialise_schema() const;

    // --- reading ---

    Result<Loaded, StorageError> load(LogId id) const;
    Result<std::optional<LogId>, StorageError> open_log_id() const;

    // --- writing ---

    using Build = std::function<Result<std::vector<JournalRecord>, StorageError>()>;

    /// One atomic unit (section 2.6): begin; `build` checks and applies the change and returns the journal records it
    /// needs; the records are appended and made durable; then the commit. Returns an error when any step fails, with
    /// nothing changed in the database and the journal records cancelled.
    Result<void, StorageError> write(const Build& build) const;
    void rollback() const;
    void cancel_all(std::vector<JournalMark>& marks, StorageError& error) const;

    Result<void, StorageError> mutate(LogId id, JournalBody body, bool clear_draft) const;
    Result<void, StorageError> persist(const JournalRecord& record, const Loaded& before, const StoredLog& after) const;
    Result<void, StorageError> insert_log(const StoredLog& log, std::uint64_t applied_seq) const;
    Result<void, StorageError> insert_entry(const std::string& log_id, const Entry& entry, std::uint64_t seq,
                                            const HeaderHistory& history) const;
    Result<void, StorageError> insert_kind(const Entry& entry, const HeaderHistory& history) const;
    Result<void, StorageError> update_header(LogId id, const HeaderValues& header) const;
    Result<void, StorageError> delete_log_rows(const QString& key) const;
    Result<CheckResult, StorageError> check(const char* pragma) const;
    Result<void, StorageError> update_applied(LogId id, std::uint64_t seq, bool is_open) const;
    void checkpoint() const;

    QString m_Name;
    std::filesystem::path m_Path;
    mutable bool m_CompanionsProtected = false;
    QSqlDatabase m_Db;
    const Clock& m_Clock;
    Journal& m_Journal;
    CrashHook m_Hook;
};

// --- opening ---------------------------------------------------------------------------------------------------------

Result<void, StorageError> SqliteLogRepository::Impl::open_database(const std::filesystem::path& path,
                                                                    int busy_timeout_ms)
{
    if (QCoreApplication::instance() == nullptr)
    {
        // Qt SQL loads its drivers through the application object and refuses to work without one.
        return Failure(make_error(StorageErrorKind::INTERNAL, "Qt SQL needs a QCoreApplication to exist"));
    }

    // Who owns this file? Decided before anything is written to it (C14, C16).
    const auto identity = probe_file(path, busy_timeout_ms);
    if (!identity)
    {
        return Failure(identity.error());
    }
    if (!identity->blank())
    {
        if (identity->m_ApplicationId != SQLITE_APPLICATION_ID)
        {
            return Failure(
                make_error(StorageErrorKind::NOT_OUR_DATABASE, "the file is a database of another application"));
        }
        if (identity->m_UserVersion > SQLITE_SCHEMA_VERSION)
        {
            return Failure(make_error(StorageErrorKind::NEWER_SCHEMA, "the database was written by a newer version"));
        }
        if (identity->m_UserVersion != SQLITE_SCHEMA_VERSION)
        {
            return Failure(corrupt("the database has no known schema version"));
        }
    }

    m_Path = path;
    const std::u8string name = path.generic_u8string();
    m_Name = QStringLiteral("ej-store-%1").arg(++g_ConnectionCounter);
    m_Db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_Name);
    if (!m_Db.isValid())
    {
        return Failure(make_error(StorageErrorKind::INTERNAL, "the SQLite driver of Qt SQL is not available"));
    }
    m_Db.setDatabaseName(
        QString::fromUtf8(reinterpret_cast<const char*>(name.data()), static_cast<qsizetype>(name.size())));
    m_Db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=%1").arg(busy_timeout_ms));
    if (!m_Db.open())
    {
        return Failure(from_sql(m_Db.lastError(), QStringLiteral("open")));
    }
    if (auto settings = apply_settings(busy_timeout_ms); !settings)
    {
        return settings;
    }
    protect_database_files(path);  // the database, and the -wal and -shm files SQLite has made by now
    return identity->blank() ? initialise_schema() : Result<void, StorageError>{};
}

Result<void, StorageError> SqliteLogRepository::Impl::check_setting(const char* name, const QString& set,
                                                                    const QString& read, const QVariant& expected) const
{
    auto asked = scalar(set);
    if (!asked)
    {
        return Failure(asked.error());
    }
    auto reported = scalar(read);
    if (!reported)
    {
        return Failure(reported.error());
    }
    if (reported->toString().compare(expected.toString(), Qt::CaseInsensitive) != 0)
    {
        return Failure(make_error(StorageErrorKind::DATABASE,
                                  std::string("the database does not report the setting ") + name + " as asked"));
    }
    return {};
}

Result<void, StorageError> SqliteLogRepository::Impl::apply_settings(int busy_timeout_ms) const
{
    // Qt SQL ignores a pragma the driver cannot honour, so each one is read back and the open fails on a difference.
    // journal_mode answers with the mode now in force; the others answer with their value.
    const auto timeout = QString::number(busy_timeout_ms);
    const struct
    {
        const char* m_Name;
        QString m_Set;
        QString m_Read;
        QVariant m_Expected;
    } settings[] = {
        {"journal_mode", QStringLiteral("PRAGMA journal_mode=WAL"), QStringLiteral("PRAGMA journal_mode"), "wal"},
        {"synchronous", QStringLiteral("PRAGMA synchronous=FULL"), QStringLiteral("PRAGMA synchronous"), 2},
        {"foreign_keys", QStringLiteral("PRAGMA foreign_keys=ON"), QStringLiteral("PRAGMA foreign_keys"), 1},
        {"busy_timeout", QStringLiteral("PRAGMA busy_timeout=") + timeout, QStringLiteral("PRAGMA busy_timeout"),
         busy_timeout_ms},
        // On Apple platforms fsync does not flush the drive cache; without these a synchronous=FULL commit is weaker
        // than it claims (section 2.4).
        {"fullfsync", QStringLiteral("PRAGMA fullfsync=ON"), QStringLiteral("PRAGMA fullfsync"), 1},
        {"checkpoint_fullfsync", QStringLiteral("PRAGMA checkpoint_fullfsync=ON"),
         QStringLiteral("PRAGMA checkpoint_fullfsync"), 1},
    };
    for (const auto& setting : settings)
    {
        if (auto checked = check_setting(setting.m_Name, setting.m_Set, setting.m_Read, setting.m_Expected); !checked)
        {
            return checked;
        }
    }
    return {};
}

Result<void, StorageError> SqliteLogRepository::Impl::initialise_schema() const
{
    if (auto begun = exec(QStringLiteral("BEGIN IMMEDIATE")); !begun)
    {
        return begun;
    }
    Result<void, StorageError> done;
    for (const char* statement : SCHEMA)
    {
        done = exec(QString::fromLatin1(statement));
        if (!done)
        {
            break;
        }
    }
    if (done)
    {
        done = exec(QStringLiteral("PRAGMA application_id=%1").arg(SQLITE_APPLICATION_ID));
    }
    if (done)
    {
        done = exec(QStringLiteral("PRAGMA user_version=%1").arg(SQLITE_SCHEMA_VERSION));
    }
    if (done)
    {
        done = exec(QStringLiteral("COMMIT"));
    }
    if (!done)
    {
        rollback();
    }
    return done;
}

// --- reading ---------------------------------------------------------------------------------------------------------

namespace
{

/// Reads the six header columns starting at `first`.
Result<HeaderValues, StorageError> read_header(const QSqlQuery& query, int first)
{
    HeaderValues header;
    const auto date = parse_iso_date(to_std(query.value(first).toString()));
    if (!date)
    {
        return Failure(corrupt("a stored log date is not valid"));
    }
    header.m_LocalDate = *date;
    header.m_Facilitator = to_std(query.value(first + 1).toString());
    if (const auto unit = optional_text(query.value(first + 2)))
    {
        auto parsed = UnitNumber::parse_stored(*unit);
        if (!parsed)
        {
            return Failure(corrupt("a stored unit number is not valid"));
        }
        header.m_UnitNumber = std::move(*parsed);
    }
    const auto location_id = optional_text(query.value(first + 3));
    const auto location_text = optional_text(query.value(first + 4));
    if (location_id || location_text)
    {
        LocationValue location;
        if (location_id)
        {
            const auto parsed = Uuid::parse(*location_id);
            if (!parsed)
            {
                return Failure(corrupt("a stored location id is not valid"));
            }
            location.m_Id = LocationId{*parsed};
        }
        location.m_Text = location_text.value_or(std::string());
        header.m_Location = std::move(location);
    }
    if (const auto version = optional_text(query.value(first + 5)))
    {
        const auto parsed = SoftwareVersion::parse(*version);
        if (!parsed)
        {
            return Failure(corrupt("a stored software version is not valid"));
        }
        header.m_SoftwareVersion = *parsed;
    }
    return header;
}

QVariantList header_values(const HeaderValues& header)
{
    QVariantList values;
    values << to_qt(format_iso_date(header.m_LocalDate).value_or(std::string())) << to_qt(header.m_Facilitator);
    values << (header.m_UnitNumber ? QVariant(to_qt(header.m_UnitNumber->stored())) : null_text());
    values << (header.m_Location && header.m_Location->m_Id ? QVariant(to_qt(header.m_Location->m_Id->format()))
                                                            : null_text());
    values << (header.m_Location ? QVariant(to_qt(header.m_Location->m_Text)) : null_text());
    values << (header.m_SoftwareVersion ? QVariant(to_qt(header.m_SoftwareVersion->format())) : null_text());
    return values;
}

std::string_view kind_name(const EntryBody& body)
{
    if (std::holds_alternative<ErrorEntry>(body))
    {
        return "error";
    }
    return std::holds_alternative<NoteEntry>(body) ? "note" : "header_change";
}

}  // namespace

Result<SqliteLogRepository::Impl::Loaded, StorageError> SqliteLogRepository::Impl::load(LogId id) const
{
    const QString key = to_qt(id.format());
    auto row = run(QStringLiteral("SELECT created_utc, last_shared_utc, archived, previous_log_id, applied_seq, %1,"
                                  " changed_since_share FROM day_log WHERE id = ?")
                       .arg(QLatin1String(HEADER_COLUMNS)),
                   {key});
    if (!row)
    {
        return Failure(row.error());
    }
    if (!row->next())
    {
        return not_found("no such log");
    }
    auto header = read_header(*row, 5);
    if (!header)
    {
        return Failure(header.error());
    }
    LogMeta meta;
    meta.m_Id = id;
    meta.m_CreatedUtc = millis_of(row->value(0));
    if (!row->value(1).isNull())
    {
        meta.m_LastSharedUtc = millis_of(row->value(1));
    }
    meta.m_Archived = row->value(2).toInt() != 0;
    meta.m_ChangedSinceShare = row->value(11).toInt() != 0;
    if (const auto previous = optional_text(row->value(3)))
    {
        const auto parsed = Uuid::parse(*previous);
        if (!parsed)
        {
            return Failure(corrupt("a stored previous log id is not valid"));
        }
        meta.m_Previous = LogId{*parsed};
    }
    Loaded loaded{StoredLog{meta, Log(std::move(*header))}, row->value(4).toULongLong()};

    auto entries = run(QStringLiteral("SELECT e.id, e.kind, e.captured_utc, e.captured_tz, e.edited_utc, e.deleted,"
                                      " x.error_type_id, x.error_type_label, x.attached_note,"
                                      " n.template_id, n.value_text, h.field, h.new_value "
                                      "FROM entry e"
                                      " LEFT JOIN error_entry x ON x.entry_id = e.id"
                                      " LEFT JOIN note_entry n ON n.entry_id = e.id"
                                      " LEFT JOIN header_change_entry h ON h.entry_id = e.id "
                                      "WHERE e.day_log_id = ? ORDER BY e.seq"),
                       {key});
    if (!entries)
    {
        return Failure(entries.error());
    }
    while (entries->next())
    {
        Entry entry;
        const auto entry_id = Uuid::parse(to_std(entries->value(0).toString()));
        if (!entry_id)
        {
            return Failure(corrupt("a stored entry id is not valid"));
        }
        entry.m_Id = EntryId{*entry_id};
        entry.m_Captured = Timestamp{millis_of(entries->value(2)), to_std(entries->value(3).toString())};
        if (!entries->value(4).isNull())
        {
            entry.m_EditedUtc = millis_of(entries->value(4));
        }
        entry.m_Deleted = entries->value(5).toInt() != 0;

        const std::string kind = to_std(entries->value(1).toString());
        if (kind == "error")
        {
            const auto type = Uuid::parse(to_std(entries->value(6).toString()));
            if (!type || entries->value(7).isNull())
            {
                return Failure(corrupt("a stored error entry is not valid"));
            }
            entry.m_Body =
                ErrorEntry{ErrorTypeId{*type}, optional_text(entries->value(8)), to_std(entries->value(7).toString())};
        }
        else if (kind == "note")
        {
            NoteEntry note;
            if (const auto template_id = optional_text(entries->value(9)))
            {
                const auto parsed = Uuid::parse(*template_id);
                if (!parsed)
                {
                    return Failure(corrupt("a stored note template id is not valid"));
                }
                note.m_Template = NoteTemplateId{*parsed};
            }
            note.m_Value = to_std(entries->value(10).toString());
            entry.m_Body = std::move(note);
        }
        else
        {
            const auto field = header_field_from_key(to_std(entries->value(11).toString()));
            if (!field)
            {
                return Failure(corrupt("a stored header change is not valid"));
            }
            entry.m_Body = HeaderChangeEntry{*field, to_std(entries->value(12).toString())};
        }
        if (!loaded.m_Stored.m_Log.add(std::move(entry)))
        {
            return Failure(corrupt("a stored entry id is used twice"));
        }
    }
    return loaded;
}

Result<std::optional<LogId>, StorageError> SqliteLogRepository::Impl::open_log_id() const
{
    auto rows = run(QStringLiteral("SELECT id FROM day_log WHERE archived = 0"));
    if (!rows)
    {
        return Failure(rows.error());
    }
    std::optional<LogId> found;
    while (rows->next())
    {
        const auto parsed = Uuid::parse(to_std(rows->value(0).toString()));
        if (!parsed || found)
        {
            return Failure(corrupt(parsed ? "more than one log is open" : "a stored log id is not valid"));
        }
        found = LogId{*parsed};
    }
    return found;
}

// --- writing ---------------------------------------------------------------------------------------------------------

void SqliteLogRepository::Impl::rollback() const
{
    // Fails harmlessly when the transaction has already ended.
    (void)exec(QStringLiteral("ROLLBACK"));
}

void SqliteLogRepository::Impl::cancel_all(std::vector<JournalMark>& marks, StorageError& error) const
{
    for (auto mark = marks.rbegin(); mark != marks.rend(); ++mark)
    {
        if (!m_Journal.cancel(*mark))
        {
            error.m_Message += "; a journal record could not be cancelled";
        }
    }
    marks.clear();
}

Result<void, StorageError> SqliteLogRepository::Impl::write(const Build& build) const
{
    if (auto begun = exec(QStringLiteral("BEGIN IMMEDIATE")); !begun)
    {
        return begun;
    }
    auto records = build();
    if (!records)
    {
        rollback();
        return Failure(records.error());
    }

    // The journal first, and durable, while the transaction is open and uncommitted: a crash from here to the commit
    // leaves a record the next launch replays, never an entry the journal does not hold.
    std::vector<JournalMark> marks;
    for (std::size_t i = 0; i < records->size(); ++i)
    {
        auto mark = m_Journal.append((*records)[i]);
        if (!mark)
        {
            StorageError error = mark.error();
            cancel_all(marks, error);
            rollback();
            return Failure(std::move(error));
        }
        marks.push_back(*mark);
        if (i + 1 < records->size())
        {
            if (auto hooked = at(CrashPoint::MID_ARCHIVE_PAIR); !hooked)
            {
                StorageError error = hooked.error();
                cancel_all(marks, error);
                rollback();
                return Failure(std::move(error));
            }
        }
    }

    auto committed = at(CrashPoint::BEFORE_DB_COMMIT);
    if (committed)
    {
        committed = exec(QStringLiteral("COMMIT"));
    }
    if (!committed)
    {
        // The user is told this write failed, so it must not come back at the next launch.
        StorageError error = committed.error();
        cancel_all(marks, error);
        rollback();
        return Failure(std::move(error));
    }
    if (!m_CompanionsProtected)
    {
        // SQLite makes the -wal and -shm files on the first write, after the open, so they are protected here.
        m_CompanionsProtected = true;
        protect_database_files(m_Path);
    }
    (void)at(CrashPoint::AFTER_DB_COMMIT);
    return {};
}

Result<void, StorageError> SqliteLogRepository::Impl::update_applied(LogId id, std::uint64_t seq, bool is_open) const
{
    if (auto updated = exec(QStringLiteral("UPDATE day_log SET applied_seq = ? WHERE id = ?"),
                            {static_cast<qulonglong>(seq), to_qt(id.format())});
        !updated)
    {
        return updated;
    }
    if (is_open)
    {
        return exec(QStringLiteral("UPDATE store_state SET applied_seq = ? WHERE id = 1"),
                    {static_cast<qulonglong>(seq)});
    }
    return {};
}

Result<void, StorageError> SqliteLogRepository::Impl::delete_log_rows(const QString& key) const
{
    const char* const statements[] = {
        "DELETE FROM error_entry WHERE entry_id IN (SELECT id FROM entry WHERE day_log_id = ?1)",
        "DELETE FROM note_entry WHERE entry_id IN (SELECT id FROM entry WHERE day_log_id = ?1)",
        "DELETE FROM header_change_entry WHERE entry_id IN (SELECT id FROM entry WHERE day_log_id = ?1)",
        "DELETE FROM entry WHERE day_log_id = ?1",
        "DELETE FROM day_log WHERE id = ?1",
    };
    for (const char* statement : statements)
    {
        if (auto removed = exec(QString::fromLatin1(statement), {key}); !removed)
        {
            return removed;
        }
    }
    return {};
}

Result<CheckResult, StorageError> SqliteLogRepository::Impl::check(const char* pragma) const
{
    auto rows = run(QString::fromLatin1(pragma));
    if (!rows)
    {
        return Failure(rows.error());
    }
    CheckResult result;
    std::vector<std::string> lines;
    while (rows->next() && lines.size() < 5)
    {
        lines.push_back(to_std(rows->value(0).toString()));
    }
    result.m_Ok = lines.size() == 1 && lines.front() == "ok";
    for (const std::string& line : lines)
    {
        result.m_Message += (result.m_Message.empty() ? "" : "; ") + line;
    }
    return result;
}

Result<void, StorageError> SqliteLogRepository::Impl::update_header(LogId id, const HeaderValues& header) const
{
    QVariantList values = header_values(header);
    values << to_qt(id.format());
    return exec(QStringLiteral("UPDATE day_log SET local_date = ?, facilitator = ?, unit_number = ?, location_id = ?,"
                               " location_text = ?, software_version = ? WHERE id = ?"),
                values);
}

Result<void, StorageError> SqliteLogRepository::Impl::insert_kind(const Entry& entry,
                                                                  const HeaderHistory& history) const
{
    const QString id = to_qt(entry.m_Id.format());
    if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
    {
        return exec(QStringLiteral("INSERT INTO error_entry (entry_id, error_type_id, error_type_label, attached_note) "
                                   "VALUES (?, ?, ?, ?)"),
                    {id, to_qt(error->m_ErrorType.format()), to_qt(error->m_ErrorTypeLabel),
                     text_or_null(error->m_AttachedNote)});
    }
    if (const auto* note = std::get_if<NoteEntry>(&entry.m_Body))
    {
        return exec(
            QStringLiteral("INSERT INTO note_entry (entry_id, template_id, value_text) VALUES (?, ?, ?)"),
            {id, note->m_Template ? QVariant(to_qt(note->m_Template->format())) : null_text(), to_qt(note->m_Value)});
    }
    const auto& change = std::get<HeaderChangeEntry>(entry.m_Body);
    return exec(QStringLiteral("INSERT INTO header_change_entry (entry_id, field, old_value, new_value) "
                               "VALUES (?, ?, ?, ?)"),
                {id, to_qt(header_field_key(change.m_Field)), text_or_null(history.previous_value(entry)),
                 to_qt(change.m_NewValue)});
}

Result<void, StorageError> SqliteLogRepository::Impl::insert_entry(const std::string& log_id, const Entry& entry,
                                                                   std::uint64_t seq,
                                                                   const HeaderHistory& history) const
{
    if (auto inserted =
            exec(QStringLiteral("INSERT INTO entry (id, day_log_id, seq, kind, captured_utc, captured_tz,"
                                " edited_utc, deleted) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
                 {to_qt(entry.m_Id.format()), to_qt(log_id), static_cast<qulonglong>(seq),
                  to_qt(kind_name(entry.m_Body)), millis_value(entry.m_Captured.m_Utc), to_qt(entry.m_Captured.m_Zone),
                  entry.m_EditedUtc ? millis_value(*entry.m_EditedUtc) : null_integer(), entry.m_Deleted ? 1 : 0});
        !inserted)
    {
        return inserted;
    }
    return insert_kind(entry, history);
}

Result<void, StorageError> SqliteLogRepository::Impl::insert_log(const StoredLog& log, std::uint64_t applied_seq) const
{
    const LogMeta& meta = log.m_Meta;
    QVariantList values{to_qt(meta.m_Id.format())};
    values << header_values(log.m_Log.initial_header());
    values << millis_value(meta.m_CreatedUtc)
           << (meta.m_LastSharedUtc ? millis_value(*meta.m_LastSharedUtc) : null_integer()) << (meta.m_Archived ? 1 : 0)
           << (meta.m_Previous ? QVariant(to_qt(meta.m_Previous->format())) : null_text())
           << static_cast<qulonglong>(applied_seq) << (meta.m_ChangedSinceShare ? 1 : 0);
    return exec(QStringLiteral("INSERT INTO day_log (id, %1, created_utc, last_shared_utc, archived, previous_log_id,"
                               " applied_seq, changed_since_share) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
                    .arg(QLatin1String(HEADER_COLUMNS)),
                values);
}

Result<void, StorageError> SqliteLogRepository::Impl::persist(const JournalRecord& record, const Loaded& before,
                                                              const StoredLog& after) const
{
    const LogId id = record.m_Log;
    const HeaderHistory history(after.m_Log);
    struct Visitor
    {
        const Impl& m_Impl;
        const JournalRecord& m_Record;
        const Loaded& m_Before;
        const StoredLog& m_After;
        const HeaderHistory& m_History;

        Result<void, StorageError> operator()(const CreateLogBody&) const { return {}; }
        Result<void, StorageError> operator()(const AddEntryBody& body) const
        {
            return m_Impl.insert_entry(m_Record.m_Log.format(), *m_After.m_Log.find(body.m_Entry.m_Id), m_Record.m_Seq,
                                       m_History);
        }
        Result<void, StorageError> operator()(const EditEntryBody& body) const
        {
            const Entry& entry = *m_After.m_Log.find(body.m_Entry.m_Id);
            const QString id = to_qt(entry.m_Id.format());
            if (auto updated =
                    m_Impl.exec(QStringLiteral("UPDATE entry SET kind = ?, captured_utc = ?, captured_tz = ?,"
                                               " edited_utc = ? WHERE id = ?"),
                                {to_qt(kind_name(entry.m_Body)), millis_value(entry.m_Captured.m_Utc),
                                 to_qt(entry.m_Captured.m_Zone),
                                 entry.m_EditedUtc ? millis_value(*entry.m_EditedUtc) : null_integer(), id});
                !updated)
            {
                return updated;
            }
            for (const char* table : {"error_entry", "note_entry", "header_change_entry"})
            {
                if (auto removed = m_Impl.exec(
                        QStringLiteral("DELETE FROM %1 WHERE entry_id = ?").arg(QLatin1String(table)), {id});
                    !removed)
                {
                    return removed;
                }
            }
            return m_Impl.insert_kind(entry, m_History);
        }
        Result<void, StorageError> operator()(const SetDeletedBody& body) const
        {
            return m_Impl.exec(QStringLiteral("UPDATE entry SET deleted = ? WHERE id = ?"),
                               {body.m_Deleted ? 1 : 0, to_qt(body.m_Entry.format())});
        }
        Result<void, StorageError> operator()(const CorrectHeaderBody&) const
        {
            if (auto updated = m_Impl.update_header(m_Record.m_Log, m_After.m_Log.initial_header()); !updated)
            {
                return updated;
            }
            // A correction may have fixed a header change, and fixes the old side of every later one.
            for (const Entry& entry : m_After.m_Log.entries())
            {
                const auto* change = std::get_if<HeaderChangeEntry>(&entry.m_Body);
                if (change == nullptr)
                {
                    continue;
                }
                if (auto updated = m_Impl.exec(
                        QStringLiteral(
                            "UPDATE header_change_entry SET old_value = ?, new_value = ? WHERE entry_id = ?"),
                        {text_or_null(m_History.previous_value(entry)), to_qt(change->m_NewValue),
                         to_qt(entry.m_Id.format())});
                    !updated)
                {
                    return updated;
                }
            }
            return {};
        }
        Result<void, StorageError> operator()(const MarkSharedBody& body) const
        {
            return m_Impl.exec(QStringLiteral("UPDATE day_log SET last_shared_utc = ? WHERE id = ?"),
                               {millis_value(body.m_Utc), to_qt(m_Record.m_Log.format())});
        }
        Result<void, StorageError> operator()(const ArchiveBody&) const
        {
            return m_Impl.exec(QStringLiteral("UPDATE day_log SET archived = 1 WHERE id = ?"),
                               {to_qt(m_Record.m_Log.format())});
        }
        Result<void, StorageError> operator()(const VoidBody&) const { return {}; }
    };
    if (auto done = std::visit(Visitor{*this, record, before, after, history}, record.m_Body); !done)
    {
        return done;
    }
    // A change after a share, or the share that ends it, moves the flag. Written whatever the record was, so that every
    // way a record reaches the database (a write, a replay) keeps it the same.
    if (after.m_Meta.m_ChangedSinceShare != before.m_Stored.m_Meta.m_ChangedSinceShare)
    {
        if (auto flagged = exec(QStringLiteral("UPDATE day_log SET changed_since_share = ? WHERE id = ?"),
                                {after.m_Meta.m_ChangedSinceShare ? 1 : 0, to_qt(id.format())});
            !flagged)
        {
            return flagged;
        }
    }

    const bool is_archive = std::holds_alternative<ArchiveBody>(record.m_Body);
    if (is_archive)
    {
        if (auto reset = exec(QStringLiteral("UPDATE store_state SET applied_seq = 0 WHERE id = 1")); !reset)
        {
            return reset;
        }
    }
    return update_applied(id, record.m_Seq, !after.m_Meta.m_Archived);
}

Result<void, StorageError> SqliteLogRepository::Impl::mutate(LogId id, JournalBody body, bool clear_draft) const
{
    return write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            auto before = load(id);
            if (!before)
            {
                return Failure(before.error());
            }
            const auto seq = m_Journal.next_seq(id, before->m_AppliedSeq);
            if (!seq)
            {
                return Failure(seq.error());
            }
            JournalRecord record;
            record.m_Seq = *seq;
            record.m_Log = id;
            record.m_Body = std::move(body);

            StoredLog after = before->m_Stored;
            if (auto applied = apply_record(after, record); !applied)
            {
                return Failure(applied.error());
            }
            if (auto persisted = persist(record, *before, after); !persisted)
            {
                return Failure(persisted.error());
            }
            if (clear_draft)
            {
                if (auto cleared = exec(QStringLiteral("DELETE FROM draft")); !cleared)
                {
                    return Failure(cleared.error());
                }
            }
            return std::vector<JournalRecord>{std::move(record)};
        });
}

void SqliteLogRepository::Impl::checkpoint() const
{
    // Keeps the WAL short after an archive. Best effort: the data is safe either way.
    if (at(CrashPoint::MID_CHECKPOINT))
    {
        (void)exec(QStringLiteral("PRAGMA wal_checkpoint(PASSIVE)"));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// SqliteLogRepository
// ---------------------------------------------------------------------------------------------------------------------

SqliteLogRepository::SqliteLogRepository(std::unique_ptr<Impl> impl) : m_Impl(std::move(impl)) {}

SqliteLogRepository::~SqliteLogRepository() = default;

Result<std::unique_ptr<SqliteLogRepository>, StorageError>
SqliteLogRepository::open(const std::filesystem::path& database, const Clock& clock, Journal& journal, Options options)
{
    auto impl = std::make_unique<Impl>(clock, journal, std::move(options.m_Hook));
    if (auto opened = impl->open_database(database, options.m_BusyTimeoutMs); !opened)
    {
        return Failure(opened.error());
    }
    return std::unique_ptr<SqliteLogRepository>(new SqliteLogRepository(std::move(impl)));
}

Result<void, StorageError> SqliteLogRepository::create_log(LogId id, const HeaderValues& header)
{
    return m_Impl->write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            auto existing =
                m_Impl->scalar(QStringLiteral("SELECT archived FROM day_log WHERE id = ?"), {to_qt(id.format())});
            if (!existing)
            {
                return Failure(existing.error());
            }
            if (existing->isValid())
            {
                return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a log with this id exists"));
            }
            auto open = m_Impl->open_log_id();
            if (!open)
            {
                return Failure(open.error());
            }
            if (*open)
            {
                return Failure(make_error(StorageErrorKind::OPEN_LOG_EXISTS, "a log is open"));
            }
            const auto seq = m_Impl->m_Journal.next_seq(id, 0);
            if (!seq)
            {
                return Failure(seq.error());
            }
            JournalRecord record;
            record.m_Seq = *seq;
            record.m_Log = id;
            record.m_Body = CreateLogBody{header, m_Impl->m_Clock.now(), std::nullopt};
            auto stored = make_stored_log(id, std::get<CreateLogBody>(record.m_Body));
            if (!stored)
            {
                return Failure(stored.error());
            }
            if (auto inserted = m_Impl->insert_log(*stored, *seq); !inserted)
            {
                return Failure(inserted.error());
            }
            if (auto state = m_Impl->exec(QStringLiteral("UPDATE store_state SET applied_seq = ?,"
                                                         " journal_generation = journal_generation + 1 WHERE id = 1"),
                                          {static_cast<qulonglong>(*seq)});
                !state)
            {
                return Failure(state.error());
            }
            return std::vector<JournalRecord>{std::move(record)};
        });
}

Result<std::optional<StoredLog>, StorageError> SqliteLogRepository::open_log()
{
    auto id = m_Impl->open_log_id();
    if (!id)
    {
        return Failure(id.error());
    }
    if (!*id)
    {
        return std::optional<StoredLog>{};
    }
    auto loaded = m_Impl->load(**id);
    if (!loaded)
    {
        return Failure(loaded.error());
    }
    return std::optional<StoredLog>{std::move(loaded->m_Stored)};
}

Result<void, StorageError> SqliteLogRepository::archive_log(LogId id)
{
    if (auto archived = m_Impl->mutate(id, ArchiveBody{}, false); !archived)
    {
        return archived;
    }
    // The commit is done. Moving the journal and trimming the WAL are housekeeping: recovery finds a journal in the
    // wrong folder, so a failure here is not a failed archive.
    (void)m_Impl->m_Journal.archive(id);
    m_Impl->checkpoint();
    return {};
}

Result<void, StorageError> SqliteLogRepository::start_new_log(LogId old_log, LogId new_log, const HeaderValues& header)
{
    auto started = m_Impl->write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            auto old = m_Impl->load(old_log);
            if (!old)
            {
                const bool unknown = old.error().m_Kind == StorageErrorKind::NOT_FOUND;
                return Failure(
                    unknown ? make_error(StorageErrorKind::NOT_OPEN_LOG, "the log to archive is not the open log")
                            : old.error());
            }
            if (old->m_Stored.m_Meta.m_Archived)
            {
                return Failure(make_error(StorageErrorKind::NOT_OPEN_LOG, "the log to archive is not the open log"));
            }
            auto existing =
                m_Impl->scalar(QStringLiteral("SELECT 1 FROM day_log WHERE id = ?"), {to_qt(new_log.format())});
            if (!existing)
            {
                return Failure(existing.error());
            }
            if (existing->isValid())
            {
                return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a log with this id exists"));
            }

            // Two journals change: the new log's first record is written before the old log's archive record, so that
            // an archive record in the old journal always means the new log's journal exists too (section 2.9).
            JournalRecord create;
            create.m_Log = new_log;
            create.m_Body = CreateLogBody{header, m_Impl->m_Clock.now(), old_log};
            auto stored = make_stored_log(new_log, std::get<CreateLogBody>(create.m_Body));
            if (!stored)
            {
                return Failure(stored.error());
            }
            const auto create_seq = m_Impl->m_Journal.next_seq(new_log, 0);
            const auto archive_seq = m_Impl->m_Journal.next_seq(old_log, old->m_AppliedSeq);
            if (!create_seq || !archive_seq)
            {
                return Failure(create_seq ? archive_seq.error() : create_seq.error());
            }
            create.m_Seq = *create_seq;
            JournalRecord archive;
            archive.m_Seq = *archive_seq;
            archive.m_Log = old_log;
            archive.m_Body = ArchiveBody{};

            StoredLog archived = old->m_Stored;
            if (auto applied = apply_record(archived, archive); !applied)
            {
                return Failure(applied.error());
            }
            // The old log first: only one row may have archived = 0 at any moment.
            if (auto persisted = m_Impl->persist(archive, *old, archived); !persisted)
            {
                return Failure(persisted.error());
            }
            if (auto inserted = m_Impl->insert_log(*stored, *create_seq); !inserted)
            {
                return Failure(inserted.error());
            }
            if (auto state = m_Impl->exec(QStringLiteral("UPDATE store_state SET applied_seq = ?,"
                                                         " journal_generation = journal_generation + 1 WHERE id = 1"),
                                          {static_cast<qulonglong>(*create_seq)});
                !state)
            {
                return Failure(state.error());
            }
            return std::vector<JournalRecord>{std::move(create), std::move(archive)};
        });
    if (!started)
    {
        return started;
    }
    (void)m_Impl->m_Journal.archive(old_log);
    m_Impl->checkpoint();
    return {};
}

Result<std::vector<LogSummary>, StorageError> SqliteLogRepository::list_logs()
{
    auto rows = m_Impl->run(
        QStringLiteral("SELECT d.id, d.created_utc, d.last_shared_utc, d.archived,"
                       " (SELECT count(*) FROM entry e WHERE e.day_log_id = d.id),"
                       " (SELECT count(*) FROM entry e WHERE e.day_log_id = d.id AND e.deleted = 0), %1,"
                       " d.changed_since_share "
                       "FROM day_log d ORDER BY d.local_date DESC, d.created_utc DESC, d.id ASC")
            .arg(QStringLiteral("d.local_date, d.facilitator, d.unit_number, d.location_id, d.location_text, "
                                "d.software_version")));
    if (!rows)
    {
        return Failure(rows.error());
    }
    std::vector<LogSummary> result;
    while (rows->next())
    {
        const auto id = Uuid::parse(to_std(rows->value(0).toString()));
        auto header = read_header(*rows, 6);
        if (!id || !header)
        {
            return Failure(id ? header.error() : corrupt("a stored log id is not valid"));
        }
        LogSummary summary;
        summary.m_Id = LogId{*id};
        summary.m_Header = std::move(*header);
        summary.m_CreatedUtc = millis_of(rows->value(1));
        if (!rows->value(2).isNull())
        {
            summary.m_LastSharedUtc = millis_of(rows->value(2));
        }
        summary.m_Archived = rows->value(3).toInt() != 0;
        summary.m_ChangedSinceShare = rows->value(12).toInt() != 0;
        summary.m_TotalEntryCount = rows->value(4).toULongLong();
        summary.m_LiveEntryCount = rows->value(5).toULongLong();
        result.push_back(std::move(summary));
    }
    return result;
}

Result<StoredLog, StorageError> SqliteLogRepository::load_log(LogId id)
{
    auto loaded = m_Impl->load(id);
    if (!loaded)
    {
        return Failure(loaded.error());
    }
    return std::move(loaded->m_Stored);
}

Result<void, StorageError> SqliteLogRepository::add_entry(LogId log, const Entry& entry)
{
    // Adding a note clears the draft in the same transaction (NOTE-01, W5).
    return m_Impl->mutate(log, AddEntryBody{entry}, std::holds_alternative<NoteEntry>(entry.m_Body));
}

Result<void, StorageError> SqliteLogRepository::edit_entry(LogId log, const Entry& entry, UtcMillis edited_at)
{
    return m_Impl->mutate(log, EditEntryBody{entry, edited_at}, false);
}

Result<void, StorageError> SqliteLogRepository::set_entry_deleted(LogId log, EntryId entry, bool deleted)
{
    return m_Impl->mutate(log, SetDeletedBody{entry, deleted}, false);
}

Result<void, StorageError> SqliteLogRepository::correct_header(LogId log, HeaderField field, std::string value)
{
    return m_Impl->mutate(log, CorrectHeaderBody{field, std::move(value)}, false);
}

Result<void, StorageError> SqliteLogRepository::mark_shared(LogId log, UtcMillis when)
{
    return m_Impl->mutate(log, MarkSharedBody{when}, false);
}

Result<void, StorageError> SqliteLogRepository::hard_delete_log(LogId id, const DeleteConfirmation& confirmation)
{
    if (confirmation.log() != id)
    {
        return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, "the confirmation is for another log"));
    }
    return m_Impl->write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            auto archived =
                m_Impl->scalar(QStringLiteral("SELECT archived FROM day_log WHERE id = ?"), {to_qt(id.format())});
            if (!archived)
            {
                return Failure(archived.error());
            }
            if (!archived->isValid())
            {
                return not_found("no such log");
            }
            if (archived->toInt() == 0)
            {
                return Failure(make_error(StorageErrorKind::LOG_NOT_ARCHIVED, "only an archived log can be deleted"));
            }
            const QString key = to_qt(id.format());
            if (auto removed = m_Impl->delete_log_rows(key); !removed)
            {
                return Failure(removed.error());
            }
            // The journal goes before the commit: if the commit then fails, the log is still here and the next launch
            // puts its journal back from the database; the other order could bring a deleted log back from a journal.
            if (auto removed = m_Impl->m_Journal.remove(id); !removed)
            {
                return Failure(removed.error());
            }
            return std::vector<JournalRecord>{};
        });
}

Result<void, StorageError> SqliteLogRepository::set_draft(std::string_view text, UtcMillis when)
{
    if (auto valid = validate_draft(text); !valid)
    {
        return valid;
    }
    if (text.empty())
    {
        return m_Impl->exec(QStringLiteral("DELETE FROM draft"));
    }
    return m_Impl->exec(QStringLiteral("INSERT INTO draft (id, text, updated_utc) VALUES (1, ?, ?)"
                                       " ON CONFLICT (id) DO UPDATE SET text = excluded.text,"
                                       " updated_utc = excluded.updated_utc"),
                        {to_qt(text), millis_value(when)});
}

Result<std::optional<Draft>, StorageError> SqliteLogRepository::get_draft()
{
    auto row = m_Impl->run(QStringLiteral("SELECT text, updated_utc FROM draft WHERE id = 1"));
    if (!row)
    {
        return Failure(row.error());
    }
    if (!row->next())
    {
        return std::optional<Draft>{};
    }
    return std::optional<Draft>{Draft{to_std(row->value(0).toString()), millis_of(row->value(1))}};
}

Result<std::uint64_t, StorageError> SqliteLogRepository::applied_seq(LogId log)
{
    auto value = m_Impl->scalar(QStringLiteral("SELECT applied_seq FROM day_log WHERE id = ?"), {to_qt(log.format())});
    if (!value)
    {
        return Failure(value.error());
    }
    if (!value->isValid())
    {
        return not_found("no such log");
    }
    return value->toULongLong();
}

Result<std::uint64_t, StorageError> SqliteLogRepository::store_applied_seq()
{
    auto value = m_Impl->scalar(QStringLiteral("SELECT applied_seq FROM store_state WHERE id = 1"));
    if (!value)
    {
        return Failure(value.error());
    }
    return value->toULongLong();
}

Result<std::uint64_t, StorageError> SqliteLogRepository::journal_generation()
{
    auto value = m_Impl->scalar(QStringLiteral("SELECT journal_generation FROM store_state WHERE id = 1"));
    if (!value)
    {
        return Failure(value.error());
    }
    return value->toULongLong();
}

Result<CheckResult, StorageError> SqliteLogRepository::quick_check()
{
    return m_Impl->check("PRAGMA quick_check");
}

Result<CheckResult, StorageError> SqliteLogRepository::integrity_check()
{
    return m_Impl->check("PRAGMA integrity_check");
}

Result<std::map<LogId, std::uint64_t>, StorageError> SqliteLogRepository::applied_seqs()
{
    auto rows = m_Impl->run(QStringLiteral("SELECT id, applied_seq FROM day_log"));
    if (!rows)
    {
        return Failure(rows.error());
    }
    std::map<LogId, std::uint64_t> result;
    while (rows->next())
    {
        const auto id = Uuid::parse(to_std(rows->value(0).toString()));
        if (!id)
        {
            return Failure(corrupt("a stored log id is not valid"));
        }
        result.emplace(LogId{*id}, rows->value(1).toULongLong());
    }
    return result;
}

Result<ReplayCounts, StorageError> SqliteLogRepository::replay(const std::vector<JournalRecord>& records)
{
    ReplayCounts counts;
    auto done = m_Impl->write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            counts = ReplayCounts{};
            std::map<LogId, Impl::Loaded> cache;
            for (const JournalRecord& record : records)
            {
                if (const auto* create = std::get_if<CreateLogBody>(&record.m_Body))
                {
                    auto existing = m_Impl->scalar(QStringLiteral("SELECT 1 FROM day_log WHERE id = ?"),
                                                   {to_qt(record.m_Log.format())});
                    if (!existing)
                    {
                        return Failure(existing.error());
                    }
                    if (existing->isValid() || cache.contains(record.m_Log))
                    {
                        ++counts.m_Skipped;
                        continue;
                    }
                    auto stored = make_stored_log(record.m_Log, *create);
                    if (!stored)
                    {
                        return Failure(stored.error());
                    }
                    if (auto inserted = m_Impl->insert_log(*stored, record.m_Seq); !inserted)
                    {
                        return Failure(inserted.error());
                    }
                    if (auto state = m_Impl->exec(QStringLiteral("UPDATE store_state SET journal_generation ="
                                                                 " journal_generation + 1 WHERE id = 1"));
                        !state)
                    {
                        return Failure(state.error());
                    }
                    if (auto applied = m_Impl->update_applied(record.m_Log, record.m_Seq, !stored->m_Meta.m_Archived);
                        !applied)
                    {
                        return Failure(applied.error());
                    }
                    cache.emplace(record.m_Log, Impl::Loaded{std::move(*stored), record.m_Seq});
                    ++counts.m_Applied;
                    continue;
                }

                auto found = cache.find(record.m_Log);
                if (found == cache.end())
                {
                    auto loaded = m_Impl->load(record.m_Log);
                    if (!loaded)
                    {
                        return Failure(loaded.error());
                    }
                    found = cache.emplace(record.m_Log, std::move(*loaded)).first;
                }
                Impl::Loaded& loaded = found->second;
                if (record.m_Seq <= loaded.m_AppliedSeq)
                {
                    ++counts.m_Skipped;
                    continue;
                }
                const auto* add = std::get_if<AddEntryBody>(&record.m_Body);
                const bool already_there =
                    (add != nullptr && loaded.m_Stored.m_Log.find(add->m_Entry.m_Id) != nullptr) ||
                    (std::holds_alternative<ArchiveBody>(record.m_Body) && loaded.m_Stored.m_Meta.m_Archived);
                if (already_there)
                {
                    // The database has this already: only its sequence number moves on.
                    if (auto moved =
                            m_Impl->update_applied(record.m_Log, record.m_Seq, !loaded.m_Stored.m_Meta.m_Archived);
                        !moved)
                    {
                        return Failure(moved.error());
                    }
                    loaded.m_AppliedSeq = record.m_Seq;
                    ++counts.m_Skipped;
                    continue;
                }
                StoredLog after = loaded.m_Stored;
                if (auto applied = apply_record(after, record, ApplyMode::REPLAY); !applied)
                {
                    return Failure(applied.error());
                }
                if (auto persisted = m_Impl->persist(record, loaded, after); !persisted)
                {
                    return Failure(persisted.error());
                }
                if (add != nullptr && std::holds_alternative<NoteEntry>(add->m_Entry.m_Body))
                {
                    // The commit of this note never reached the database, and the same transaction would have taken out
                    // the draft it was made from (NOTE-01, W5): a kill between the journal and the commit must not leave
                    // both the note and its draft. A draft written after the note was captured is the next note's, and
                    // stays. The draft is not journalled, so the times are what tell them apart.
                    if (auto cleared = m_Impl->exec(QStringLiteral("DELETE FROM draft WHERE updated_utc <= ?"),
                                                    {millis_value(add->m_Entry.m_Captured.m_Utc)});
                        !cleared)
                    {
                        return Failure(cleared.error());
                    }
                }
                loaded.m_Stored = std::move(after);
                loaded.m_AppliedSeq = record.m_Seq;
                ++counts.m_Applied;
            }
            return std::vector<JournalRecord>{};
        });
    if (!done)
    {
        return Failure(done.error());
    }
    return counts;
}

Result<void, StorageError> SqliteLogRepository::replace_log(const StoredLog& log, std::uint64_t applied_seq)
{
    return m_Impl->write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            if (auto removed = m_Impl->delete_log_rows(to_qt(log.m_Meta.m_Id.format())); !removed)
            {
                return Failure(removed.error());
            }
            if (auto inserted = m_Impl->insert_log(log, applied_seq); !inserted)
            {
                return Failure(inserted.error());
            }
            const HeaderHistory history(log.m_Log);
            std::uint64_t seq = 2;
            for (const Entry& entry : log.m_Log.entries())
            {
                if (auto added = m_Impl->insert_entry(log.m_Meta.m_Id.format(), entry, seq++, history); !added)
                {
                    return Failure(added.error());
                }
            }
            if (!log.m_Meta.m_Archived)
            {
                if (auto state = m_Impl->exec(QStringLiteral("UPDATE store_state SET applied_seq = ? WHERE id = 1"),
                                              {static_cast<qulonglong>(applied_seq)});
                    !state)
                {
                    return Failure(state.error());
                }
            }
            return std::vector<JournalRecord>{};
        });
}

Result<void, StorageError> SqliteLogRepository::set_applied_seq(LogId log, std::uint64_t seq)
{
    return m_Impl->write(
        [&]() -> Result<std::vector<JournalRecord>, StorageError>
        {
            auto loaded = m_Impl->load(log);
            if (!loaded)
            {
                return Failure(loaded.error());
            }
            if (auto moved = m_Impl->update_applied(log, seq, !loaded->m_Stored.m_Meta.m_Archived); !moved)
            {
                return Failure(moved.error());
            }
            return std::vector<JournalRecord>{};
        });
}

Result<void, StorageError> SqliteLogRepository::checkpoint()
{
    return m_Impl->exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
}

std::optional<Draft> SqliteLogRepository::salvage_draft(const std::filesystem::path& database)
{
    std::error_code ignored;
    if (!std::filesystem::exists(database, ignored) || QCoreApplication::instance() == nullptr)
    {
        return std::nullopt;
    }
    const std::u8string text = database.generic_u8string();
    const QString name = QStringLiteral("ej-salvage-%1").arg(++g_ConnectionCounter);
    std::optional<Draft> draft;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(
            QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size())));
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.isValid() && db.open())
        {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT text, updated_utc FROM draft WHERE id = 1")) && query.next() &&
                !query.value(0).toString().isEmpty())
            {
                const std::string content = to_std(query.value(0).toString());
                if (validate_draft(content))
                {
                    draft = Draft{content, millis_of(query.value(1))};
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return draft;
}

}  // namespace ej
