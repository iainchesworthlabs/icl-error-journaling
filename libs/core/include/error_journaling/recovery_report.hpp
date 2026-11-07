// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ej
{

enum class RecoveryOutcome
{
    CLEAN,        // nothing to do
    REPLAYED,     // a healthy database was behind the journal and was brought up to date
    REBUILT,      // the database was replaced by one built from the journals
    PARTIAL_LOSS, // recovered, but records were skipped for a bad CRC or a gap
    UNRECOVERABLE // the open log could not be restored; a new empty store was created
};

/// What `Recovery::run` did (section 2.7). The UI turns it into the message DS-04 requires. It holds counts and paths
/// only, never entry text, so it is safe in CI output.
struct RecoveryReport
{
    RecoveryOutcome m_Outcome = RecoveryOutcome::CLEAN;
    std::size_t m_OpenLogEntries = 0;     // entries in the open log after recovery
    std::size_t m_ReplayedRecords = 0;    // journal records applied over a healthy database
    std::size_t m_RebuiltRecords = 0;     // records used to rebuild
    std::size_t m_DiscardedTailBytes = 0; // torn tail removed
    std::size_t m_DiscardedRecords = 0;   // records skipped for a bad CRC or a gap
    std::size_t m_VoidedRecords = 0;      // writes that were rolled back by a void
    std::size_t m_ArchivedLogsLost = 0;   // archived logs that could not be recovered
    bool m_DraftRecovered = false;
    std::vector<std::string> m_QuarantinedPaths; // paths only, no content
    std::string m_IntegrityMessage;              // SQLite's own text, never user data
    std::vector<std::string> m_Notes;            // plain remarks such as "the journal was seeded from the database"

    friend bool operator==(const RecoveryReport&, const RecoveryReport&) = default;
};

[[nodiscard]] std::string_view recovery_outcome_name(RecoveryOutcome outcome);

/// One plain sentence for logs: "rebuilt: 312 records, 0 discarded, 1 file quarantined".
[[nodiscard]] std::string describe(const RecoveryReport& report);

} // namespace ej
