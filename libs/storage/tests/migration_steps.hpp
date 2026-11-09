// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/migrator.hpp"
#include "error_journaling/sqlite_log_repository.hpp"

#include <vector>

/// Steps for testing the migration framework. They are not part of the product build. They start where the product's
/// schema is now and take a database two versions on, with changes that are easy to look for: a column that every log
/// row gets a value in, and a table that is filled from the entries.
namespace ej::test
{

/// The version of a store made by this build, the one after it, and the one the test steps lead to.
inline constexpr std::int32_t TEST_FROM_VERSION = SQLITE_SCHEMA_VERSION;
inline constexpr std::int32_t TEST_MIDDLE_VERSION = TEST_FROM_VERSION + 1;
inline constexpr std::int32_t TEST_TARGET_VERSION = TEST_FROM_VERSION + 2;

/// From the current version: a new column on `day_log`, filled in.
inline MigrationStep add_column_step()
{
    return MigrationStep{TEST_FROM_VERSION, "add a column", [](MigrationConnection& sql) -> Result<void, StorageError>
                         {
                             if (auto added = sql.exec("ALTER TABLE day_log ADD COLUMN test_note TEXT"); !added)
                             {
                                 return added;
                             }
                             return sql.exec("UPDATE day_log SET test_note = 'v2'");
                         }};
}

/// The next one: a new table, filled from the entries.
inline MigrationStep add_table_step()
{
    return MigrationStep{TEST_MIDDLE_VERSION, "add a table", [](MigrationConnection& sql) -> Result<void, StorageError>
                         {
                             if (auto created = sql.exec("CREATE TABLE test_audit (entry_id TEXT PRIMARY KEY NOT NULL,"
                                                         " seen INTEGER NOT NULL)");
                                 !created)
                             {
                                 return created;
                             }
                             return sql.exec("INSERT INTO test_audit (entry_id, seen) SELECT id, 1 FROM entry");
                         }};
}

[[nodiscard]] inline std::vector<MigrationStep> test_steps()
{
    return {add_column_step(), add_table_step()};
}

/// A step from `from` that changes something and then fails.
inline MigrationStep failing_step(std::int32_t from = TEST_MIDDLE_VERSION)
{
    return MigrationStep{from, "a step that fails", [](MigrationConnection& sql) -> Result<void, StorageError>
                         {
                             if (auto changed = sql.exec("UPDATE day_log SET facilitator = 'half done'"); !changed)
                             {
                                 return changed;
                             }
                             return fail(make_error(StorageErrorKind::DATABASE, "the step could not finish"));
                         }};
}

/// The second step, which succeeds but leaves a row without its parent, which the post-check finds.
inline MigrationStep orphan_step()
{
    return MigrationStep{TEST_MIDDLE_VERSION, "a step that leaves an orphan",
                         [](MigrationConnection& sql) -> Result<void, StorageError>
                         {
                             return sql.exec("INSERT INTO error_entry (entry_id, error_type_id, error_type_label)"
                                             " VALUES ('no-such-entry', 'no-such-type', 'Wrong Response')");
                         }};
}

} // namespace ej::test
