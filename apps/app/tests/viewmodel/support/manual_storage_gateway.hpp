// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/storage_gateway.hpp"

#include <cstddef>
#include <deque>

namespace ej::test
{

/// The gateway of the unit tests. Jobs wait in a queue until the test runs them, so a test can look at the state
/// between "submitted" and "committed": no entry, no count, no toast. A job and its completion run in the same call, on
/// the calling thread, so the tests need no event loop.
///
///     gateway.post(...);          // queued; the repository has not been touched
///     CHECK(gateway.pending() == 1);
///     gateway.run_next();         // the job, then its completion
class ManualStorageGateway final : public viewmodel::StorageGateway
{
public:
    explicit ManualStorageGateway(viewmodel::StorageContext& context) : m_Context(context) {}

    void post(Work work, Completion completion) override;

    /// Runs the job at the front of the queue and then its completion. False when the queue is empty. A job or a
    /// completion may post more jobs; they go to the back.
    bool run_next();

    /// Runs jobs until the queue is empty; the number run.
    std::size_t run_all();

    /// `run_all`, because a manual gateway has nothing to wait for.
    void drain() override
    {
        run_all();
    }

    /// Jobs waiting.
    [[nodiscard]] std::size_t pending() const
    {
        return m_Queue.size();
    }

    /// Jobs posted since the gateway was made, and jobs run.
    [[nodiscard]] std::size_t posted() const
    {
        return m_Posted;
    }
    [[nodiscard]] std::size_t completed() const
    {
        return m_Completed;
    }

private:
    struct Job
    {
        Work m_Work;
        Completion m_Completion;
    };

    viewmodel::StorageContext& m_Context;
    std::deque<Job> m_Queue;
    std::size_t m_Posted = 0;
    std::size_t m_Completed = 0;
};

} // namespace ej::test
