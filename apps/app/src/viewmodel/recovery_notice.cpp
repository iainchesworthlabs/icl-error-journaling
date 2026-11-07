// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/recovery_notice.hpp"

#include <QCoreApplication>
#include <cstddef>

namespace ej::viewmodel
{

namespace
{

constexpr const char* CONTEXT = "ej::viewmodel::RecoveryNotice";

QString text(const char* source)
{
    return QCoreApplication::translate(CONTEXT, source);
}

/// A sentence that counts something: the singular sentence for one, the plural one (with `%1`) for any other number.
QString counted(std::size_t count, const char* singular, const char* plural)
{
    if (count == 1)
    {
        return text(singular);
    }
    return text(plural).arg(static_cast<qulonglong>(count));
}

} // namespace

QString recovery_notice(const RecoveryReport& report)
{
    switch (report.m_Outcome)
    {
    case RecoveryOutcome::CLEAN:
        return {};
    case RecoveryOutcome::REPLAYED:
        return counted(report.m_ReplayedRecords,
                       "The app did not close properly last time, so 1 change was recovered and your log is up to "
                       "date.",
                       "The app did not close properly last time, so %1 changes were recovered and your log is up "
                       "to date.");
    case RecoveryOutcome::REBUILT:
        return counted(report.m_RebuiltRecords,
                       "The app did not close properly last time, so your log was rebuilt from 1 saved record.",
                       "The app did not close properly last time, so your log was rebuilt from %1 saved records.");
    case RecoveryOutcome::PARTIAL_LOSS:
        return counted(report.m_DiscardedRecords,
                       "Your log was recovered, but 1 saved record could not be read and was left out. Please check "
                       "today's entries.",
                       "Your log was recovered, but %1 saved records could not be read and were left out. Please "
                       "check today's entries.");
    case RecoveryOutcome::UNRECOVERABLE:
        return text("Today's log could not be recovered, so the app has started a new, empty one.");
    }
    return {};
}

} // namespace ej::viewmodel
