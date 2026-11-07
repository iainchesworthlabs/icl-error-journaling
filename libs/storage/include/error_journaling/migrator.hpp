// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/crash_point.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/storage_location.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ej
{

/// What a migration step sees of the database: statements to run and one value to read. A step never sees Qt SQL, so a
/// step is the same text on every platform and a test can write one without a database library.
class MigrationConnection
{
public:
    MigrationConnection() = default;
    MigrationConnection(const MigrationConnection&) = delete;
    MigrationConnection& operator=(const MigrationConnection&) = delete;
    virtual ~MigrationConnection() = default;

    /// Runs one statement.
    [[nodiscard]] virtual Result<void, StorageError> exec(std::string_view sql) = 0;

    /// The first column of the first row as text; nothing when the statement returns no row.
    [[nodiscard]] virtual Result<std::optional<std::string>, StorageError> scalar(std::string_view sql) = 0;
};

/// One step of the ordered list (section 2.9): takes the schema from `m_From` to `m_From + 1`. It runs inside the
/// migration's transaction, so a step that fails leaves nothing behind.
struct MigrationStep
{
    std::int32_t m_From = 0;
    std::string m_Name; // for the report; never entry text
    std::function<Result<void, StorageError>(MigrationConnection&)> m_Apply;
};

/// The steps of the product. Version 1 is the only schema that ships in Stage 2, so the list is empty and a migration
/// to the current version is always "up to date". A later schema adds its step here and raises `SQLITE_SCHEMA_VERSION`.
[[nodiscard]] std::vector<MigrationStep> product_migration_steps();

struct MigrationOptions
{
    /// The kill harness's hook (`mid_migration`); null in production. Reached once after the backup and the marker are
    /// durable, once after each step, once before the commit and once after it (before the marker is removed). An error
    /// it returns before the commit fails the migration, which is restored; after the commit it is ignored.
    CrashHook m_Hook;

    /// Called with a short name after each durable step ("backup_synced", "marker_synced"), so a test can see the order
    /// in which they happened.
    std::function<void(std::string_view)> m_Observer;

    /// Checked together with `foreign_key_check` and `integrity_check`, before the version is set. A failure restores.
    std::function<Result<void, StorageError>(MigrationConnection&)> m_ExtraCheck;
};

enum class MigrationOutcome
{
    UP_TO_DATE,    // the database is at the version asked for
    MIGRATED,      // the steps ran and committed
    NO_MARKER,     // `resume`: there was no migration in progress
    NOT_COMMITTED, // `resume`: the migration never committed; the database is as it was and the marker is gone
    COMPLETED,     // `resume`: the migration had committed and its checks pass; the marker is gone
    RESTORED       // `resume`: the database was put back from the backup copy
};

[[nodiscard]] std::string_view migration_outcome_name(MigrationOutcome outcome);

/// What a migration did. Names, numbers and paths only.
struct MigrationReport
{
    MigrationOutcome m_Outcome = MigrationOutcome::UP_TO_DATE;
    std::int32_t m_FromVersion = 0;
    std::int32_t m_ToVersion = 0;
    std::vector<std::string> m_Steps;            // the steps that ran
    std::string m_Backup;                        // the backup copy, relative to the store; empty when none was made
    std::vector<std::string> m_QuarantinedPaths; // what a restore set aside, relative to the store
};

/// Schema migrations (DS-06, section 2.9 of docs/plan/stage-2.md). The procedure:
///
///  1. Compare `user_version` with the version asked for. Equal: nothing happens. Higher: `NEWER_SCHEMA`, and nothing
///     is touched (a newer app wrote this; it is not downgraded or "recovered"). Lower: every step from the one to the
///     other must exist, or `INTERNAL` is returned before anything is touched.
///  2. Checkpoint the WAL, copy the database to `backup/ej-v<from>-<UTC time>.db`, sync the copy and its folder, and
///  write
///     `migration.inprogress`, which names the copy, durably.
///  3. `BEGIN IMMEDIATE`, run the steps, run `foreign_key_check` and `integrity_check`, set `user_version`, `COMMIT`.
///  4. Remove the marker.
///
/// A failure at step 3 rolls back, closes, puts the copy back (the files it replaces go to `quarantine/`, never
/// deleted), removes the marker and returns the error, leaving the old database openable by the old schema code.
///
/// A kill between 2 and 4 is covered by the marker. `resume` is called at launch before anything opens the database
/// (`Recovery::run` does it): it finds the old version (the transaction never committed; the marker is removed), or the
/// new one with passing checks (the marker is removed), or anything else, and restores the copy.
///
/// Use from one thread, with nothing else holding the database open.
class Migrator
{
public:
    Migrator(StorageLocation location, const Clock& clock, std::vector<MigrationStep> steps,
             std::int32_t target_version, MigrationOptions options = {});

    /// Migrates the database to the target version. A marker left by a killed migration is dealt with first.
    [[nodiscard]] Result<MigrationReport, StorageError> run();

    /// Deals with a marker left by a killed migration (see above). `NO_MARKER` when there is none.
    [[nodiscard]] static Result<MigrationReport, StorageError> resume(const StorageLocation& location,
                                                                      const Clock& clock);

private:
    StorageLocation m_Location;
    const Clock& m_Clock;
    std::vector<MigrationStep> m_Steps;
    std::int32_t m_Target;
    MigrationOptions m_Options;
};

} // namespace ej
