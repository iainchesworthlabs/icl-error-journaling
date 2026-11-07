// SPDX-License-Identifier: GPL-3.0-or-later
//
// ThreadedStorageGateway on a real thread: where jobs and completions run, the order, shutdown. The timing budgets of
// the threading section (no blocking of the GUI thread under a 40 ms commit) are work package 3.11's.
#include "storage_samples.hpp"
#include "support/memory_storage.hpp"
#include "support/test_clock.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::viewmodel;
using namespace std::chrono_literals;

namespace
{

/// Spins the event loop until `done` is true or five seconds have passed.
template <typename Done>
bool wait_for(Done&& done)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done() && std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(1ms);
    }
    return done();
}

} // namespace

TEST_CASE("A job runs off the GUI thread and its completion on it", "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    std::thread::id job_thread;
    std::thread::id completion_thread;
    bool done = false;

    gateway.post([&](StorageContext&) { job_thread = std::this_thread::get_id(); },
                 [&]
                 {
                     completion_thread = std::this_thread::get_id();
                     done = true;
                 });

    REQUIRE(wait_for([&] { return done; }));
    CHECK(job_thread != std::this_thread::get_id());
    CHECK(completion_thread == std::this_thread::get_id());
    CHECK(job_thread == gateway.storage_thread_id());
}

TEST_CASE("Every repository call is made on the one storage thread", "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    const LogId log = sample_id<LogId>(1);
    int completed = 0;

    gateway.run([log](StorageContext& context) { return context.m_Logs.create_log(log, sample_header()); },
                [&](Result<void, StorageError> result)
                {
                    CHECK(result.has_value());
                    ++completed;
                });
    for (std::uint64_t i = 1; i <= 20; ++i)
    {
        gateway.run([log, i](StorageContext& context) { return context.m_Logs.add_entry(log, sample_error(i, 10)); },
                    [&](Result<void, StorageError> result)
                    {
                        CHECK(result.has_value());
                        ++completed;
                    });
    }

    REQUIRE(wait_for([&] { return completed == 21; }));
    const auto threads = storage.repository().threads();
    REQUIRE(threads.size() == 1);
    CHECK(*threads.begin() == gateway.storage_thread_id());
    CHECK(*threads.begin() != std::this_thread::get_id());
}

TEST_CASE("A thousand jobs run in submission order and their completions arrive in order",
          "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    std::vector<int> ran;       // written on the storage thread only
    std::vector<int> completed; // written on the GUI thread only

    for (int i = 0; i < 1000; ++i)
    {
        gateway.post([&ran, i](StorageContext&) { ran.push_back(i); }, [&completed, i] { completed.push_back(i); });
    }

    gateway.drain();

    REQUIRE(completed.size() == 1000);
    for (int i = 0; i < 1000; ++i)
    {
        CHECK(ran[static_cast<std::size_t>(i)] == i);
        CHECK(completed[static_cast<std::size_t>(i)] == i);
    }
    CHECK(gateway.outstanding() == 0);
}

TEST_CASE("Drain returns only when the queue is empty and every completion has been delivered",
          "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    int completed = 0;
    for (int i = 0; i < 5; ++i)
    {
        gateway.post([](StorageContext&) { std::this_thread::sleep_for(5ms); }, [&completed] { ++completed; });
    }

    gateway.drain();

    CHECK(completed == 5);
    CHECK(gateway.outstanding() == 0);
}

TEST_CASE("Drain also finishes the jobs that a completion posts", "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    std::vector<std::string> order;

    gateway.post([&order](StorageContext&) { order.emplace_back("first job"); },
                 [&]
                 {
                     order.emplace_back("first done");
                     gateway.post([&order](StorageContext&) { order.emplace_back("second job"); },
                                  [&order] { order.emplace_back("second done"); });
                 });

    gateway.drain();

    CHECK(order == std::vector<std::string>{"first job", "first done", "second job", "second done"});
}

TEST_CASE("Drain on an idle gateway returns at once", "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());

    gateway.drain();
    gateway.drain();

    CHECK(gateway.outstanding() == 0);
}

TEST_CASE("Destroying the gateway finishes the queued jobs but never calls their completions",
          "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    std::atomic<int> jobs_run{0};
    int completions = 0;
    {
        ThreadedStorageGateway gateway(storage.context());
        // The first job holds the thread, so the rest are still queued when the gateway goes.
        std::mutex mutex;
        std::condition_variable released;
        bool release = false;
        gateway.post(
            [&](StorageContext&)
            {
                std::unique_lock lock(mutex);
                released.wait(lock, [&] { return release; });
                ++jobs_run;
            },
            [&] { ++completions; });
        for (int i = 0; i < 9; ++i)
        {
            gateway.post([&](StorageContext&) { ++jobs_run; }, [&] { ++completions; });
        }
        {
            const std::lock_guard lock(mutex);
            release = true;
        }
        released.notify_all();
    }

    CHECK(jobs_run == 10);
    QCoreApplication::processEvents();
    CHECK(completions == 0);
}

TEST_CASE("A completion for a service that has gone is dropped", "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    bool called = false;
    {
        ThreadedStorageGateway gateway(storage.context());
        gateway.post([](StorageContext&) { std::this_thread::sleep_for(20ms); }, [&called] { called = true; });
    }
    QCoreApplication::processEvents();
    CHECK_FALSE(called);
}

TEST_CASE("The factory runs on the storage thread and the resources die there", "[viewmodel][gateway][threaded]")
{
    struct Resources final : StorageResources
    {
        Resources(TestClock& clock, std::thread::id* made_on, std::thread::id* destroyed_on)
            : m_Storage(clock), m_DestroyedOn(destroyed_on)
        {
            *made_on = std::this_thread::get_id();
        }
        ~Resources() override
        {
            *m_DestroyedOn = std::this_thread::get_id();
        }
        StorageContext& context() override
        {
            return m_Storage.context();
        }

        MemoryStorage m_Storage;
        std::thread::id* m_DestroyedOn;
    };

    TestClock clock;
    std::thread::id made_on;
    std::thread::id destroyed_on;
    std::thread::id job_on;
    {
        ThreadedStorageGateway gateway(
            StorageFactory([&]() -> std::unique_ptr<StorageResources>
                           { return std::make_unique<Resources>(clock, &made_on, &destroyed_on); }));
        gateway.post([&](StorageContext&) { job_on = std::this_thread::get_id(); }, [] {});
        gateway.drain();
    }

    CHECK(made_on != std::this_thread::get_id());
    CHECK(made_on == job_on);
    CHECK(destroyed_on == job_on);
}

TEST_CASE("A completion runs from the event loop, so a timer on the GUI thread keeps ticking while a job runs",
          "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    int ticks = 0;
    QTimer timer;
    timer.setInterval(2);
    QObject::connect(&timer, &QTimer::timeout, [&ticks] { ++ticks; });
    timer.start();
    bool done = false;

    gateway.post([](StorageContext&) { std::this_thread::sleep_for(100ms); }, [&done] { done = true; });

    REQUIRE(wait_for([&] { return done; }));
    // The job slept for 100 ms on the other thread; this thread was free to tick throughout. A blocked thread would
    // have ticked once or not at all. The bound is loose on purpose: the timing budget is 3.11's.
    CHECK(ticks >= 5);
}

TEST_CASE("The typed run returns the result of a job that returns a value", "[viewmodel][gateway][threaded]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ThreadedStorageGateway gateway(storage.context());
    std::string received;

    gateway.run([](StorageContext&) { return std::string("from the storage thread"); },
                [&received](std::string text) { received = std::move(text); });
    gateway.drain();

    CHECK(received == "from the storage thread");
}
