// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/migrator.hpp"

#include <vector>

/// Steps for testing the migration framework. They are not part of the product build: the product ships schema version
/// 1 and an empty list of steps. They take a version 1 database to version 3 with changes that are easy to look for: a
/// column that every log row gets a value in, and a table that is filled from the entries.
namespace ej::test
{

inline constexpr std::int32_t TEST_TARGET_VERSION = 3;

/// 1 to 2: a new column on `day_log`, filled in.
inline MigrationStep add_column_step()
{
    return MigrationStep{1, "add a column", [](MigrationConnection& sql) -> Result<void, StorageError>
                         {
                             if (auto added = sql.exec("ALTER TABLE day_log ADD COLUMN test_note TEXT"); !added)
                             {
                                 return added;
                             }
                             return sql.exec("UPDATE day_log SET test_note = 'v2'");
                         }};
}

/// 2 to 3: a new table, filled from the entries.
inline MigrationStep add_table_step()
{
    return MigrationStep{2, "add a table", [](MigrationConnection& sql) -> Result<void, StorageError>
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
inline MigrationStep failing_step(std::int32_t from = 2)
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

/// 2 to 3 that succeeds but leaves a row without its parent, which the post-check finds.
inline MigrationStep orphan_step()
{
    return MigrationStep{2, "a step that leaves an orphan", [](MigrationConnection& sql) -> Result<void, StorageError>
                         {
                             return sql.exec("INSERT INTO error_entry (entry_id, error_type_id, error_type_label)"
                                             " VALUES ('no-such-entry', 'no-such-type', 'Wrong Response')");
                         }};
}

} // namespace ej::test
