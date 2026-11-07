// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/manual_storage_gateway.hpp"

#include <utility>

namespace ej::test
{

void ManualStorageGateway::post(Work work, Completion completion)
{
    m_Queue.push_back(Job{std::move(work), std::move(completion)});
    ++m_Posted;
}

bool ManualStorageGateway::run_next()
{
    if (m_Queue.empty())
    {
        return false;
    }
    Job job = std::move(m_Queue.front());
    m_Queue.pop_front();
    if (job.m_Work)
    {
        job.m_Work(m_Context);
    }
    if (job.m_Completion)
    {
        job.m_Completion();
    }
    ++m_Completed;
    return true;
}

std::size_t ManualStorageGateway::run_all()
{
    std::size_t count = 0;
    while (run_next())
    {
        ++count;
    }
    return count;
}

} // namespace ej::test
