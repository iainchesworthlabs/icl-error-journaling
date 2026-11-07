// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/storage_gateway.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <exception>

namespace ej::viewmodel
{

namespace
{

/// A context the caller keeps.
class BorrowedResources final : public StorageResources
{
public:
    explicit BorrowedResources(StorageContext& context) : m_Context(context) {}

    [[nodiscard]] StorageContext& context() override
    {
        return m_Context;
    }

private:
    StorageContext& m_Context;
};

} // namespace

ThreadedStorageGateway::ThreadedStorageGateway(StorageFactory factory)
    : m_Factory(std::move(factory)), m_Receiver(std::make_unique<QObject>())
{
    m_Thread = std::thread([this] { run_loop(); });
}

ThreadedStorageGateway::ThreadedStorageGateway(StorageContext& borrowed)
    : ThreadedStorageGateway(StorageFactory([&borrowed]() -> std::unique_ptr<StorageResources>
                                            { return std::make_unique<BorrowedResources>(borrowed); }))
{
}

ThreadedStorageGateway::~ThreadedStorageGateway()
{
    {
        const std::lock_guard lock(m_Mutex);
        m_Stopping = true;
    }
    m_WorkAvailable.notify_all();
    // The loop leaves only when the queue is empty, so every queued job runs.
    m_Thread.join();
    // Completions still posted are removed with the receiver and never called.
    m_Receiver.reset();
}

void ThreadedStorageGateway::post(Work work, Completion completion)
{
    {
        const std::lock_guard lock(m_Mutex);
        m_Queue.push_back(Job{std::move(work), std::move(completion)});
        ++m_Outstanding;
    }
    m_WorkAvailable.notify_one();
}

void ThreadedStorageGateway::drain()
{
    const bool on_receiver_thread = QThread::currentThread() == m_Receiver->thread();
    while (true)
    {
        {
            std::unique_lock lock(m_Mutex);
            m_Idle.wait(lock, [this] { return m_Queue.empty() && !m_Busy; });
            if (!on_receiver_thread)
            {
                return;
            }
        }
        // Deliver the completions of the jobs that have finished. They may post more jobs.
        QCoreApplication::sendPostedEvents(m_Receiver.get(), 0);
        const std::lock_guard lock(m_Mutex);
        if (m_Outstanding == 0)
        {
            return;
        }
    }
}

std::size_t ThreadedStorageGateway::outstanding() const
{
    const std::lock_guard lock(m_Mutex);
    return m_Outstanding;
}

std::thread::id ThreadedStorageGateway::storage_thread_id() const
{
    const std::lock_guard lock(m_Mutex);
    return m_StorageThreadId;
}

void ThreadedStorageGateway::run_loop()
{
    {
        const std::lock_guard lock(m_Mutex);
        m_StorageThreadId = std::this_thread::get_id();
    }
    const std::unique_ptr<StorageResources> resources = m_Factory();
    if (!resources) [[unlikely]]
    {
        // A broken invariant of the composition root, not a condition to recover from.
        std::terminate();
    }
    StorageContext& context = resources->context();

    while (true)
    {
        Job job;
        {
            std::unique_lock lock(m_Mutex);
            m_WorkAvailable.wait(lock, [this] { return m_Stopping || !m_Queue.empty(); });
            if (m_Queue.empty())
            {
                break;
            }
            job = std::move(m_Queue.front());
            m_Queue.pop_front();
            m_Busy = true;
        }

        if (job.m_Work)
        {
            job.m_Work(context);
        }
        deliver(std::move(job.m_Completion));

        {
            const std::lock_guard lock(m_Mutex);
            m_Busy = false;
        }
        m_Idle.notify_all();
    }
}

void ThreadedStorageGateway::deliver(Completion completion)
{
    // `this` is safe to capture: the receiver, and with it every call posted to it, is gone before the gateway is.
    QMetaObject::invokeMethod(
        m_Receiver.get(),
        [this, shared = std::make_shared<Completion>(std::move(completion))]
        {
            if (*shared)
            {
                (*shared)();
            }
            {
                const std::lock_guard lock(m_Mutex);
                --m_Outstanding;
            }
            m_Idle.notify_all();
        },
        Qt::QueuedConnection);
}

} // namespace ej::viewmodel
