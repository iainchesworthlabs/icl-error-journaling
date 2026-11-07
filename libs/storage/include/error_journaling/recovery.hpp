// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/crash_point.hpp"
#include "error_journaling/recovery_report.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/storage_location.hpp"

#include <cstddef>

namespace ej
{

struct RecoveryOptions
{
    /// Run the full `integrity_check`, and compare every log with its journal, not only the open one. The quick check
    /// runs on every launch and the full one only after a failed quick check or on the first launch after an upgrade
    /// (DS-04, decision 6), so the app sets this when its version changed.
    bool m_FullCheck = false;

    /// How many quarantine folders to keep (section 2.7 step 6).
    std::size_t m_KeepQuarantine = 5;

    /// The kill harness's hook (`mid_recovery`); null in production.
    CrashHook m_Hook;
};

/// What happens at launch before the board appears (DS-03, DS-04, section 2.7). It makes the database and the journals
/// agree and leaves both ready for the app to open.
///
/// A database that opens and passes its checks is brought up to date from the journals: every record beyond what it holds
/// is replayed in one transaction, and the open log is compared with the log its journal describes (SQLite has no page
/// checksums, so a check can pass on wrong data).
///
/// A database that is damaged, truncated, not a database or missing is rebuilt from the journals into a new file, which
/// replaces it by an atomic rename. The damaged files go to `quarantine/` and are never deleted by the app.
///
/// A file that cannot be read, is locked, belongs to another application or is newer than this build is a different error.
/// It is returned and nothing is touched (F7, C13, C14, C16).
///
/// A torn journal tail is cut off. A journal that is missing, behind the database, or damaged in the middle is rewritten
/// from the log it describes, after a copy of the old file goes to quarantine.
///
/// It is safe to kill at any point and to run again: it works on copies and renames at the end.
class Recovery
{
public:
    /// The report holds counts and paths only, never entry text.
    [[nodiscard]] static Result<RecoveryReport, StorageError> run(const StorageLocation& location, const Clock& clock,
                                                                  RecoveryOptions options = {});
};

}  // namespace ej
