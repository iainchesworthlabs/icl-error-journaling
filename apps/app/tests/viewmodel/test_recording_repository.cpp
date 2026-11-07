// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/memory_storage.hpp"
#include "support/test_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace ej;
using namespace ej::test;

namespace
{

LogId log_id(std::uint64_t number)
{
    return sample_id<LogId>(number);
}

} // namespace

TEST_CASE("The recording repository passes calls through and records each one", "[viewmodel][support][repository]")
{
    TestClock clock;
    EventLog events;
    MemoryStorage storage(clock, &events);
    auto& repository = storage.repository();

    REQUIRE(repository.create_log(log_id(1), sample_header()));
    REQUIRE(repository.add_entry(log_id(1), sample_error(7, 10)));
    const auto open = repository.open_log();
    REQUIRE(open.has_value());
    REQUIRE(open->has_value());

    CHECK((*open)->m_Log.entries().size() == 1);
    CHECK(repository.call_count(RepositoryOp::CREATE_LOG) == 1);
    CHECK(repository.call_count(RepositoryOp::ADD_ENTRY) == 1);
    CHECK(repository.call_count(RepositoryOp::OPEN_LOG) == 1);
    CHECK(repository.mutating_call_count() == 2);
    CHECK(repository.calls().size() == 3);
    CHECK(repository.threads() == std::set<std::thread::id>{std::this_thread::get_id()});
}

TEST_CASE("Only a mutating call that succeeded is a commit in the event log", "[viewmodel][support][repository]")
{
    TestClock clock;
    EventLog events;
    MemoryStorage storage(clock, &events);
    auto& repository = storage.repository();
    REQUIRE(repository.create_log(log_id(1), sample_header()));

    repository.fail_next(RepositoryOp::ADD_ENTRY);
    CHECK_FALSE(repository.add_entry(log_id(1), sample_error(7, 10)));
    REQUIRE(repository.add_entry(log_id(1), sample_error(8, 11)));
    static_cast<void>(repository.open_log());

    CHECK(events.count(EventKind::COMMIT) == 2);
    CHECK(events.count(EventKind::COMMIT, "add_entry " + sample_id<EntryId>(8).format()) == 1);
    CHECK(events.count(EventKind::COMMIT, "add_entry " + sample_id<EntryId>(7).format()) == 0);
}

TEST_CASE("An injected failure changes nothing in the wrapped repository and clears after its count",
          "[viewmodel][support][repository]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    auto& repository = storage.repository();
    REQUIRE(repository.create_log(log_id(1), sample_header()));

    const StorageError error = make_error(StorageErrorKind::DISK_FULL, "full");
    repository.fail_next(RepositoryOp::ADD_ENTRY, 2, error);

    const auto first = repository.add_entry(log_id(1), sample_error(7, 10));
    REQUIRE_FALSE(first.has_value());
    CHECK(first.error() == error);
    CHECK_FALSE(repository.add_entry(log_id(1), sample_error(7, 10)));
    CHECK(storage.memory().open_log()->value().m_Log.entries().empty());
    CHECK(repository.add_entry(log_id(1), sample_error(7, 10)).has_value());

    const auto failed = repository.calls();
    CHECK(failed[1].m_Failed);
    CHECK(failed[2].m_Failed);
    CHECK_FALSE(failed[3].m_Failed);
}

TEST_CASE("An injected failure can last until it is stopped", "[viewmodel][support][repository]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    auto& repository = storage.repository();
    REQUIRE(repository.create_log(log_id(1), sample_header()));

    repository.fail_next(RepositoryOp::SET_DRAFT, RecordingRepository::ALWAYS);
    for (int i = 0; i < 5; ++i)
    {
        CHECK_FALSE(repository.set_draft("text", clock.now()));
    }
    repository.stop_failing(RepositoryOp::SET_DRAFT);
    CHECK(repository.set_draft("text", clock.now()).has_value());
}

TEST_CASE("The before-commit hook runs for mutating calls only, ahead of the commit",
          "[viewmodel][support][repository]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    auto& repository = storage.repository();
    std::vector<RepositoryOp> seen;
    bool entry_present_in_hook = true;
    repository.set_before_commit(
        [&](RepositoryOp op)
        {
            seen.push_back(op);
            if (op == RepositoryOp::ADD_ENTRY)
            {
                entry_present_in_hook = !storage.memory().open_log()->value().m_Log.entries().empty();
            }
        });

    REQUIRE(repository.create_log(log_id(1), sample_header()));
    REQUIRE(repository.add_entry(log_id(1), sample_error(7, 10)));
    static_cast<void>(repository.open_log());

    CHECK(seen == std::vector<RepositoryOp>{RepositoryOp::CREATE_LOG, RepositoryOp::ADD_ENTRY});
    CHECK_FALSE(entry_present_in_hook);
}

TEST_CASE("A commit delay slows mutating calls only", "[viewmodel][support][repository]")
{
    using namespace std::chrono_literals;
    TestClock clock;
    MemoryStorage storage(clock);
    auto& repository = storage.repository();
    repository.set_commit_delay(30ms);

    const auto start = std::chrono::steady_clock::now();
    REQUIRE(repository.create_log(log_id(1), sample_header()));
    const auto slow = std::chrono::steady_clock::now() - start;
    CHECK(slow >= 30ms);

    const auto read_start = std::chrono::steady_clock::now();
    static_cast<void>(repository.open_log());
    CHECK(std::chrono::steady_clock::now() - read_start < 30ms);
}

TEST_CASE("Starting a new log never passes through a moment with no open log or two",
          "[viewmodel][support][repository]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    auto& repository = storage.repository();
    REQUIRE(repository.create_log(log_id(1), sample_header()));
    REQUIRE(repository.start_new_log(log_id(1), log_id(2), sample_header()));

    CHECK(repository.open_log_counts() == std::vector<std::size_t>{1, 1});
}

TEST_CASE("Archiving and creating as two calls shows the moment with no open log", "[viewmodel][support][repository]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    auto& repository = storage.repository();
    REQUIRE(repository.create_log(log_id(1), sample_header()));
    REQUIRE(repository.archive_log(log_id(1)));
    REQUIRE(repository.create_log(log_id(2), sample_header()));

    CHECK(repository.open_log_counts() == std::vector<std::size_t>{1, 0, 1});
}

TEST_CASE("The records can be cleared", "[viewmodel][support][repository]")
{
    TestClock clock;
    MemoryStorage storage(clock);
    REQUIRE(storage.repository().create_log(log_id(1), sample_header()));

    storage.repository().clear_records();

    CHECK(storage.repository().calls().empty());
    CHECK(storage.repository().open_log_counts().empty());
}

TEST_CASE("The operation names are the repository's method names", "[viewmodel][support][repository]")
{
    CHECK(repository_op_name(RepositoryOp::ADD_ENTRY) == "add_entry");
    CHECK(repository_op_name(RepositoryOp::START_NEW_LOG) == "start_new_log");
    CHECK(is_mutating(RepositoryOp::SET_DRAFT));
    CHECK_FALSE(is_mutating(RepositoryOp::GET_DRAFT));
}
