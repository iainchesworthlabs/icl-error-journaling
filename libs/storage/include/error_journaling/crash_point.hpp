// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace ej
{

/// The places in the write path where the kill harness ends the process (K1 to K9 in docs/plan/stage-2.md, section 7),
/// and where a test can make a step fail. The names are the `--crash-at` values of `ej_kill_worker`.
enum class CrashPoint
{
    AFTER_JOURNAL_APPEND,  // the record is written, not yet synced
    AFTER_JOURNAL_SYNC,    // the record is durable, the database has not committed
    BEFORE_DB_COMMIT,      // the journal is durable and the transaction is open
    AFTER_DB_COMMIT,       // committed, the caller has not been told
    MID_CHECKPOINT,        // in the middle of the WAL checkpoint after an archive
    MID_ARCHIVE_PAIR,      // between the two journal records of "start a new log"
    MID_RECOVERY,
    MID_MIGRATION
};

[[nodiscard]] std::string_view crash_point_name(CrashPoint point);
[[nodiscard]] std::optional<CrashPoint> crash_point_from_name(std::string_view name);

/// Called at each crash point, and null in production, so there is no `#if` (section 4.2). The harness's hook ends the
/// process there; a test's hook returns an error, which the step treats as a failure of its own.
using CrashHook = std::function<Result<void, StorageError>(CrashPoint)>;

}  // namespace ej
