// SPDX-License-Identifier: GPL-3.0-or-later
//
// ej_kill_worker: the child process of the kill harness (section 4.1 of docs/plan/stage-2.md). It opens a store
// directory with the production code and either runs the workload until it is killed or finishes (mode `workload`), or
// runs recovery over a damaged store (mode `recover`). The driver reads its standard output, kills it, and checks what
// the store holds.
//
// Output, one line per write, flushed at once:
//   START <n>   before operation n is issued
//   ACK <n>     after operation n returned success, and never before
//   DONE        the workload ended by itself
//   TYPED <n>   the note field changed for the nth time (mode `draft`)
//   MIGRATING   a migration is about to run (mode `migrate`: the test-only steps, from the current version to two on)
//   MIGRATED    it finished
//   RECOVERING  recovery is about to run
//   RECOVERED <outcome>
//   AT <point>  at the crash point, waiting to be killed (mode `wait`)
//   ERROR <text>
//
// The driver may also name a crash point. In mode `exit` the worker ends itself with `std::_Exit(137)` the instant it
// reaches it. In mode `wait` it prints `AT <point>` and stops there, and the driver kills it from outside with the same
// kill as the random ones (SIGKILL, TerminateProcess). Either way the kill lands in exactly the same place every time,
// where the driver's random kill depends on scheduling.
#include "error_journaling/clock.hpp"
#include "error_journaling/crash_point.hpp"
#include "error_journaling/draft_store.hpp"
#include "error_journaling/file_journal.hpp"
#include "error_journaling/migrator.hpp"
#include "error_journaling/people_store.hpp"
#include "error_journaling/recovery.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "error_journaling/storage_location.hpp"
#include "kill_workload.hpp"
#include "migration_steps.hpp"

#include <QCoreApplication>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

using namespace ej;
using namespace ej::test::kill;
using ej::test::TEST_TARGET_VERSION;
using ej::test::test_steps;

namespace
{

/// One write per line, after the thing it reports. The stream is block-buffered when it is a pipe, so each line is
/// flushed on its own, which is one write call.
void say(const std::string& text)
{
    std::fputs((text + "\n").c_str(), stdout);
    std::fflush(stdout);
}

struct Arguments
{
    std::string m_Mode = "workload";
    std::filesystem::path m_Directory;
    std::uint64_t m_Seed = 1;
    std::uint64_t m_MaxOperations = 600;
    std::uint64_t m_PaceUs = 2000;
    std::uint64_t m_ForcedArchiveAt = 0;
    std::optional<CrashPoint> m_CrashPoint;
    std::uint64_t m_CrashOperation = 0; // the operation the crash point counts in; 0 for "anywhere" (recovery)
    std::uint64_t m_CrashHit = 1;       // the nth time the point is reached
    bool m_CrashAfterAck = false;       // K5: after_ack_before_next, a worker-level point
    bool m_CrashWait = false;           // wait to be killed at the point, instead of ending oneself
};

std::optional<Arguments> parse(int argc, char** argv)
{
    Arguments arguments;
    for (int i = 1; i < argc; ++i)
    {
        const std::string name = argv[i];
        if (i + 1 >= argc)
        {
            return std::nullopt;
        }
        const std::string value = argv[++i];
        if (name == "--mode")
        {
            arguments.m_Mode = value;
        }
        else if (name == "--dir")
        {
            arguments.m_Directory = value;
        }
        else if (name == "--seed")
        {
            arguments.m_Seed = std::stoull(value, nullptr, 0);
        }
        else if (name == "--max-ops")
        {
            arguments.m_MaxOperations = std::stoull(value);
        }
        else if (name == "--pace-us")
        {
            arguments.m_PaceUs = std::stoull(value);
        }
        else if (name == "--archive-at")
        {
            arguments.m_ForcedArchiveAt = std::stoull(value);
        }
        else if (name == "--crash-at")
        {
            if (value == "after_ack_before_next")
            {
                arguments.m_CrashAfterAck = true;
            }
            else
            {
                arguments.m_CrashPoint = crash_point_from_name(value);
                if (!arguments.m_CrashPoint)
                {
                    return std::nullopt;
                }
            }
        }
        else if (name == "--crash-op")
        {
            arguments.m_CrashOperation = std::stoull(value);
        }
        else if (name == "--crash-hit")
        {
            arguments.m_CrashHit = std::stoull(value);
        }
        else if (name == "--crash-mode")
        {
            if (value != "exit" && value != "wait")
            {
                return std::nullopt;
            }
            arguments.m_CrashWait = value == "wait";
        }
        else
        {
            return std::nullopt;
        }
    }
    return arguments.m_Directory.empty() ? std::nullopt : std::optional<Arguments>(arguments);
}

/// What happens when the process is at the point it was told to die at: it ends itself (no destructor, no flush, no
/// `atexit`, as a kill would), or it says where it is and waits for the driver's kill. A worker that is not killed in
/// time ends with an error of its own, so a driver that never came cannot leave it running.
[[noreturn]] void die_here(const Arguments& arguments, const char* where)
{
    if (arguments.m_CrashWait)
    {
        say(std::string("AT ") + where);
        std::this_thread::sleep_for(std::chrono::seconds(120));
        std::_Exit(3);
    }
    std::_Exit(137);
}

class Crasher
{
public:
    explicit Crasher(const Arguments& arguments) : m_Arguments(arguments) {}

    void begin_operation(std::uint64_t number)
    {
        m_Operation = number;
        m_Hits = 0;
    }

    [[nodiscard]] CrashHook hook()
    {
        return [this](CrashPoint point) -> Result<void, StorageError>
        {
            if (m_Arguments.m_CrashPoint && *m_Arguments.m_CrashPoint == point &&
                (m_Arguments.m_CrashOperation == 0 || m_Arguments.m_CrashOperation == m_Operation) &&
                ++m_Hits == m_Arguments.m_CrashHit)
            {
                die_here(m_Arguments, std::string(crash_point_name(point)).c_str());
            }
            return {};
        };
    }

private:
    const Arguments& m_Arguments;
    std::uint64_t m_Operation = 0;
    std::uint64_t m_Hits = 0;
};

int run_workload(const Arguments& arguments)
{
    const StorageLocation location(arguments.m_Directory);
    if (auto made = location.create_directories(); !made)
    {
        say("ERROR " + describe(made.error()));
        return 2;
    }
    ScriptedClock clock;
    Crasher crasher(arguments);

    // What the app does at launch: recovery first, then the repository over the journal.
    if (auto recovered = Recovery::run(location, clock); !recovered)
    {
        say("ERROR recovery: " + describe(recovered.error()));
        return 2;
    }
    FileJournal::Options journal_options;
    journal_options.m_Hook = crasher.hook();
    FileJournal journal(location, std::move(journal_options));
    SqliteLogRepository::Options options;
    options.m_Hook = crasher.hook();
    auto repository = SqliteLogRepository::open(location.database_path(), clock, journal, std::move(options));
    if (!repository)
    {
        say("ERROR open: " + describe(repository.error()));
        return 2;
    }

    // The list of people, with the same crash points as a log's journal (after the append, after the sync).
    PeopleStore::Options people_options;
    people_options.m_Hook = crasher.hook();
    people_options.m_Cache = repository->get();
    auto people = PeopleStore::open(location, clock, std::move(people_options));
    if (!people)
    {
        say("ERROR people: " + describe(people.error()));
        return 2;
    }

    Workload workload(arguments.m_Seed, arguments.m_ForcedArchiveAt);
    Pacer pacer(arguments.m_Seed, arguments.m_PaceUs);
    for (std::uint64_t n = 1; n <= arguments.m_MaxOperations; ++n)
    {
        const Operation operation = workload.next();
        say("START " + std::to_string(n));
        crasher.begin_operation(n);
        if (auto done = apply_operation(**repository, people->get(), clock, operation); !done)
        {
            say(std::string("ERROR operation ") + std::to_string(n) + " (" + operation_name(operation.m_Kind) +
                "): " + describe(done.error()));
            return 2;
        }
        say("ACK " + std::to_string(n));
        if (arguments.m_CrashAfterAck && arguments.m_CrashOperation == n)
        {
            die_here(arguments, "after_ack_before_next");
        }
        if (const std::uint64_t delay = pacer.next_delay_us(); delay > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(delay));
        }
    }
    say("DONE");
    return 0;
}

/// A `DraftTimer` on the monotonic clock that the worker's loop polls.
class PollingTimer final : public DraftTimer
{
public:
    void start(std::chrono::milliseconds delay, Callback on_expired) override
    {
        m_Deadline = std::chrono::steady_clock::now() + delay;
        m_Callback = std::move(on_expired);
    }

    void stop() override
    {
        m_Callback = nullptr;
    }

    void poll()
    {
        if (m_Callback && std::chrono::steady_clock::now() >= m_Deadline)
        {
            const Callback callback = std::move(m_Callback);
            m_Callback = nullptr;
            callback();
        }
    }

private:
    std::chrono::steady_clock::time_point m_Deadline;
    Callback m_Callback;
};

/// Types into the note field: a change every 10 to 90 ms, written by the draft store's timer, until killed.
int run_draft(const Arguments& arguments)
{
    const StorageLocation location(arguments.m_Directory);
    if (auto made = location.create_directories(); !made)
    {
        say("ERROR " + describe(made.error()));
        return 2;
    }
    ScriptedClock recovery_clock;
    if (auto recovered = Recovery::run(location, recovery_clock); !recovered)
    {
        say("ERROR recovery: " + describe(recovered.error()));
        return 2;
    }
    const SystemClock clock;
    FileJournal journal(location);
    auto repository = SqliteLogRepository::open(location.database_path(), clock, journal);
    if (!repository)
    {
        say("ERROR open: " + describe(repository.error()));
        return 2;
    }
    PollingTimer timer;
    DraftStore store(**repository, clock, timer);
    SplitMix64 rng(arguments.m_Seed ^ 0x54595045ULL);
    say("TYPING");
    for (std::uint64_t step = 1; step <= arguments.m_MaxOperations; ++step)
    {
        if (auto kept = store.set(draft_text(arguments.m_Seed, step)); !kept)
        {
            say("ERROR set: " + describe(kept.error()));
            return 2;
        }
        say("TYPED " + std::to_string(step));
        for (std::uint64_t waited = 0, pause = rng.between(10, 90); waited < pause; waited += 5)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            timer.poll();
        }
    }
    say("DONE");
    return 0;
}

int run_migration(const Arguments& arguments)
{
    const StorageLocation location(arguments.m_Directory);
    ScriptedClock clock(UtcMillis{std::chrono::milliseconds(arguments.m_Seed)});
    Crasher crasher(arguments);
    MigrationOptions options;
    options.m_Hook = crasher.hook();
    say("MIGRATING");
    auto report = Migrator(location, clock, test_steps(), TEST_TARGET_VERSION, std::move(options)).run();
    if (!report)
    {
        say("ERROR migration: " + describe(report.error()));
        return 2;
    }
    say(std::string("MIGRATED ") + std::string(migration_outcome_name(report->m_Outcome)));
    return 0;
}

int run_recovery(const Arguments& arguments)
{
    const StorageLocation location(arguments.m_Directory);
    ScriptedClock clock(UtcMillis{std::chrono::milliseconds(arguments.m_Seed)});
    Crasher crasher(arguments);
    RecoveryOptions options;
    options.m_Hook = crasher.hook();
    say("RECOVERING");
    auto report = Recovery::run(location, clock, std::move(options));
    if (!report)
    {
        say("ERROR recovery: " + describe(report.error()));
        return 2;
    }
    say(std::string("RECOVERED ") + std::string(recovery_outcome_name(report->m_Outcome)));
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    // Qt SQL loads its drivers through the application object.
    const QCoreApplication application(argc, argv);
    const auto arguments = parse(argc, argv);
    if (!arguments)
    {
        say("ERROR bad arguments");
        return 64;
    }
    if (arguments->m_Mode == "workload")
    {
        return run_workload(*arguments);
    }
    if (arguments->m_Mode == "recover")
    {
        return run_recovery(*arguments);
    }
    if (arguments->m_Mode == "draft")
    {
        return run_draft(*arguments);
    }
    if (arguments->m_Mode == "migrate")
    {
        return run_migration(*arguments);
    }
    say("ERROR unknown mode");
    return 64;
}
