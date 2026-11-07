// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using ej::viewmodel::StorageContext;

TEST_CASE("A job waits in the manual gateway until the test runs it", "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());
    const LogId log = sample_id<LogId>(1);
    bool done = false;

    gateway.run([&](StorageContext& context) { return context.m_Logs.create_log(log, sample_header()); },
                [&](Result<void, StorageError> result)
                {
                    CHECK(result.has_value());
                    done = true;
                });

    CHECK(gateway.pending() == 1);
    CHECK_FALSE(done);
    CHECK(storage.repository().calls().empty());

    CHECK(gateway.run_next());
    CHECK(done);
    CHECK(gateway.pending() == 0);
    CHECK(storage.memory().open_log()->has_value());
}

TEST_CASE("The manual gateway runs jobs in submission order, each with its completion", "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());
    std::vector<std::string> order;

    for (const char* name : {"a", "b", "c"})
    {
        gateway.post([&order, name](StorageContext&) { order.push_back(std::string("work ") + name); },
                     [&order, name] { order.push_back(std::string("done ") + name); });
    }
    CHECK(order.empty());

    CHECK(gateway.run_all() == 3);

    CHECK(order == std::vector<std::string>{"work a", "done a", "work b", "done b", "work c", "done c"});
    CHECK(gateway.posted() == 3);
    CHECK(gateway.completed() == 3);
}

TEST_CASE("The manual gateway has nothing to run when its queue is empty", "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());

    CHECK_FALSE(gateway.run_next());
    CHECK(gateway.run_all() == 0);
    gateway.drain();
}

TEST_CASE("A completion that posts another job leaves it for the next run", "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());
    int second_ran = 0;

    gateway.post([](StorageContext&) {}, [&] { gateway.post([&](StorageContext&) { ++second_ran; }, [] {}); });

    CHECK(gateway.run_next());
    CHECK(second_ran == 0);
    CHECK(gateway.pending() == 1);
    CHECK(gateway.run_all() == 1);
    CHECK(second_ran == 1);
}

TEST_CASE("The typed run hands the result of the job to the completion, a failure included",
          "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());
    const LogId log = sample_id<LogId>(1);
    REQUIRE(storage.repository().create_log(log, sample_header()));
    storage.repository().fail_next(RepositoryOp::ADD_ENTRY);
    std::vector<bool> outcomes;

    for (const std::uint64_t id : {7U, 8U})
    {
        gateway.run([&, id](StorageContext& context) { return context.m_Logs.add_entry(log, sample_error(id, 10)); },
                    [&](Result<void, StorageError> result) { outcomes.push_back(result.has_value()); });
    }
    gateway.run_all();

    CHECK(outcomes == std::vector<bool>{false, true});
}

TEST_CASE("The typed run accepts a job that returns a value other than a result", "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());
    std::string received;

    gateway.run([](StorageContext&) { return std::string("loaded"); }, [&](std::string text) { received = text; });
    gateway.run_all();

    CHECK(received == "loaded");
}

TEST_CASE("The job reaches both the repository and the configuration store", "[viewmodel][support][gateway]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    ManualStorageGateway gateway(storage.context());

    gateway.post([](StorageContext& context) { static_cast<void>(context.m_Config.save("{}")); }, [] {});
    gateway.run_all();

    CHECK(storage.config().saves() == std::vector<std::string>{"{}"});
}
