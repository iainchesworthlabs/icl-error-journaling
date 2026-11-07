// SPDX-License-Identifier: GPL-3.0-or-later
#include "../process_containment.hpp"

#include <windows.h>

#include <QProcess>

namespace ej::test::kill
{

namespace
{

/// One job for the whole driver; when the driver goes, the job closes and every worker in it is terminated.
HANDLE job()
{
    static const HANDLE handle = []
    {
        HANDLE created = ::CreateJobObjectW(nullptr, nullptr);
        if (created != nullptr)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ::SetInformationJobObject(created, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
        return created;
    }();
    return handle;
}

} // namespace

void contain(QProcess& process)
{
    if (job() == nullptr)
    {
        return;
    }
    const HANDLE worker =
        ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(process.processId()));
    if (worker != nullptr)
    {
        ::AssignProcessToJobObject(job(), worker);
        ::CloseHandle(worker);
    }
}

} // namespace ej::test::kill
