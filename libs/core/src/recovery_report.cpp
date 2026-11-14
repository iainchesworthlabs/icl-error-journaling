// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/recovery_report.hpp"

namespace ej
{

std::string_view recovery_outcome_name(RecoveryOutcome outcome)
{
    switch (outcome)
    {
    case RecoveryOutcome::CLEAN:
        return "clean";
    case RecoveryOutcome::REPLAYED:
        return "replayed";
    case RecoveryOutcome::REBUILT:
        return "rebuilt";
    case RecoveryOutcome::PARTIAL_LOSS:
        return "partial loss";
    case RecoveryOutcome::UNRECOVERABLE:
        return "unrecoverable";
    }
    return "unknown";
}

namespace
{

std::string plural(std::size_t count, std::string_view singular, std::string_view plural_form)
{
    return std::to_string(count) + " " + std::string(count == 1 ? singular : plural_form);
}

} // namespace

std::string describe(const RecoveryReport& report)
{
    std::string text(recovery_outcome_name(report.m_Outcome));
    text += ": ";
    text += plural(report.m_OpenLogEntries, "entry", "entries") + " in the open log";
    if (report.m_ReplayedRecords != 0)
    {
        text += ", " + plural(report.m_ReplayedRecords, "record", "records") + " replayed";
    }
    if (report.m_RebuiltRecords != 0)
    {
        text += ", " + plural(report.m_RebuiltRecords, "record", "records") + " rebuilt";
    }
    if (report.m_DiscardedRecords != 0)
    {
        text += ", " + plural(report.m_DiscardedRecords, "record", "records") + " discarded";
    }
    if (report.m_DiscardedTailBytes != 0)
    {
        text += ", " + plural(report.m_DiscardedTailBytes, "byte", "bytes") + " of torn journal tail removed";
    }
    if (report.m_VoidedRecords != 0)
    {
        text += ", " + plural(report.m_VoidedRecords, "write", "writes") + " rolled back";
    }
    if (report.m_ArchivedLogsLost != 0)
    {
        text += ", " + plural(report.m_ArchivedLogsLost, "archived log", "archived logs") + " lost";
    }
    if (report.m_DraftRecovered)
    {
        text += ", note draft recovered";
    }
    if (report.m_PeopleRecovered != 0)
    {
        text += ", " + plural(report.m_PeopleRecovered, "person", "people") + " recovered";
    }
    if (report.m_PeopleLost != 0)
    {
        text += ", " + plural(report.m_PeopleLost, "person", "people") + " lost";
    }
    if (!report.m_QuarantinedPaths.empty())
    {
        text += ", " + plural(report.m_QuarantinedPaths.size(), "file", "files") + " quarantined";
    }
    return text;
}

} // namespace ej
