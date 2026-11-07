// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/log_repository.hpp"
#include "viewmodel/config_store.hpp"
#include "viewmodel/move_only_function.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

class QObject;

namespace ej::viewmodel
{

/// What a storage job may touch. A context is used only on the thread that runs the jobs: a `QSqlDatabase`
/// connection belongs to one thread, and the threads then share no mutable state.
struct StorageContext
{
    LogRepository& m_Logs;
    ConfigStore& m_Config;
};

/// The only way for the view-model layer to reach the store (standard 7.6: nothing that blocks runs on the GUI
/// thread). A caller submits a job and a completion. The job runs on the storage thread, in submission order. The
/// completion runs afterwards on the GUI thread and receives whatever the job produced.
///
/// Services keep a mirror of what they show and change it in the completion of a successful job, so the screen never
/// shows what the store does not have (docs/plan/stage-3.md, "Threading and the order of feedback").
class StorageGateway
{
public:
    using Work = MoveOnlyFunction<void(StorageContext&)>;
    using Completion = MoveOnlyFunction<void()>;

    StorageGateway() = default;
    StorageGateway(const StorageGateway&) = delete;
    StorageGateway& operator=(const StorageGateway&) = delete;
    virtual ~StorageGateway() = default;

    /// Queues `work` and its `completion`. Call it on the GUI thread.
    virtual void post(Work work, Completion completion) = 0;

    /// Blocks until every queued job has run and its completion has been delivered. For quit only (once, from
    /// `aboutToQuit`), never while the person is using the app, and never from a completion.
    virtual void drain() = 0;

    /// The typed form: `work` returns an `R`, which `done` receives on the GUI thread. A job that returns nothing has a
    /// `done` that takes nothing.
    ///
    ///     gateway.run([entry](StorageContext& c) { return c.m_Logs.add_entry(log, entry); },
    ///                 [this](Result<void, StorageError> result) { ... });
    template <typename WorkFunction, typename DoneFunction>
    void run(WorkFunction work, DoneFunction done)
    {
        using Product = std::invoke_result_t<WorkFunction&, StorageContext&>;
        if constexpr (std::is_void_v<Product>)
        {
            post(Work(std::move(work)), Completion(std::move(done)));
        }
        else
        {
            // The slot is written by the job on the storage thread and read by the completion on the GUI thread; the
            // queued call between them orders the two.
            auto slot = std::make_shared<std::optional<Product>>();
            post([task = std::move(work), slot](StorageContext& context) mutable { slot->emplace(task(context)); },
                 [finish = std::move(done), slot]() mutable { finish(std::move(**slot)); });
        }
    }
};

/// The storage a `ThreadedStorageGateway` runs its jobs against. It is created on the storage thread, used only there
/// and destroyed there.
class StorageResources
{
public:
    StorageResources() = default;
    StorageResources(const StorageResources&) = delete;
    StorageResources& operator=(const StorageResources&) = delete;
    virtual ~StorageResources() = default;

    [[nodiscard]] virtual StorageContext& context() = 0;
};

/// Makes the resources on the storage thread. It must not return null: a store that cannot be opened is resources
/// whose repository answers every call with its error.
using StorageFactory = MoveOnlyFunction<std::unique_ptr<StorageResources>()>;

/// The production gateway: one thread, a mutex-guarded queue, and results returned to the thread that made the gateway
/// through a queued call (`QMetaObject::invokeMethod` with `Qt::QueuedConnection`, standard 7.6).
///
/// Destroying the gateway finishes the jobs that are queued, destroys the resources on the storage thread, and then
/// drops every completion that was not yet delivered, without calling it: a service that has gone is never called
/// back. Use `drain()` before that when the completions matter.
class ThreadedStorageGateway final : public StorageGateway
{
public:
    /// Starts the thread, which calls `factory` first. Construct it on the GUI thread, with an event loop.
    explicit ThreadedStorageGateway(StorageFactory factory);

    /// Runs the jobs against a context the caller owns, which must outlive the gateway and which nothing else may use
    /// while the gateway lives. For tests; the product creates its storage on the storage thread.
    explicit ThreadedStorageGateway(StorageContext& borrowed);

    ~ThreadedStorageGateway() override;

    void post(Work work, Completion completion) override;
    void drain() override;

    /// Jobs queued, running, or finished with their completion not yet delivered.
    [[nodiscard]] std::size_t outstanding() const;

    /// The thread the jobs run on; it is the default id until that thread has started.
    [[nodiscard]] std::thread::id storage_thread_id() const;

private:
    struct Job
    {
        Work m_Work;
        Completion m_Completion;
    };

    void run_loop();
    void deliver(Completion completion);
    void deliver_finished();

    StorageFactory m_Factory;
    // The completions are queued to this object, which lives on the thread that made the gateway. Destroying it
    // removes the completions still posted to it.
    std::unique_ptr<QObject> m_Receiver;

    mutable std::mutex m_Mutex;
    std::condition_variable m_WorkAvailable;
    std::condition_variable m_Idle;
    std::deque<Job> m_Queue;
    std::deque<Completion> m_Finished; // jobs done, completions not yet called on the GUI thread
    bool m_Busy = false;
    bool m_Stopping = false;
    std::size_t m_Outstanding = 0;
    std::thread::id m_StorageThreadId;

    // std::thread and a join in the destructor, not std::jthread: Apple's libc++ keeps jthread behind
    // -fexperimental-library (standard 5.1). Declared last so that everything above exists when the thread starts.
    std::thread m_Thread;
};

} // namespace ej::viewmodel
