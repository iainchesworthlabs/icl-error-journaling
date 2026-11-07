// SPDX-License-Identifier: GPL-3.0-or-later
//
// ej_kill_driver: the parent of the kill harness (section 4 of docs/plan/stage-2.md). It starts ej_kill_worker on a
// fresh store, reads which operations the worker acknowledged, ends the worker with a kill, opens the store through the
// production `Recovery`, and checks that nothing acknowledged is lost and that what is there is whole.
//
// The parent never shares memory or a database handle with the child, so nothing from the child's page cache is
// "helping". What a process kill cannot show (a missing or weak fsync, loss of power) is the business of `LossyFile`
// and of the pragma read-back, not of this harness.
//
// Environment:
//   EJ_KILL_SEED                 the master seed (decimal or 0x hex); drawn at random and printed when unset
//   EJ_KILL_ITERATIONS           random-kill iterations (default 25)
//   EJ_KILL_FIRST, EJ_KILL_LAST  the range of iterations to run (default 0 .. iterations - 1), to shard a long run
//   EJ_KILL_RECOVERY_ITERATIONS  random kills of recovery (default 25)
//   EJ_KILL_ARTIFACT_DIR         where the store of a failed iteration is copied (default ./ej-kill-artifacts)
//   EJ_KILL_SUMMARY              a file the result line is appended to
// Command line: `--seed 0x... --iteration i` re-runs one iteration (up to 20 times) and says how often it failed.
#include "error_journaling/recovery.hpp"
#include "kill_workload.hpp"
#include "platform/process_containment.hpp"
#include "recovery_fixture.hpp"

#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::test::kill;

namespace
{

constexpr std::uint64_t DEFAULT_ITERATIONS = 25;
constexpr std::uint64_t MAX_ACK_TARGET = 400;
constexpr int WORKER_TIMEOUT_MS = 180'000;
constexpr std::uint64_t REPRO_RUNS = 20;

struct CommandLine
{
    std::optional<std::uint64_t> m_Seed;
    std::optional<std::uint64_t> m_Iteration;
} g_CommandLine;

std::optional<std::string> env_text(const char* name)
{
    const QByteArray value = qgetenv(name);
    if (value.isEmpty())
    {
        return std::nullopt;
    }
    return value.toStdString();
}

std::uint64_t parse_number(const std::string& text)
{
    return static_cast<std::uint64_t>(std::stoull(text, nullptr, 0));
}

std::uint64_t env_number(const char* name, std::uint64_t fallback)
{
    const auto text = env_text(name);
    return text ? parse_number(*text) : fallback;
}

std::string hex(std::uint64_t value)
{
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "0x%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string decimal(std::uint64_t value)
{
    return std::to_string(value);
}

void print(const std::string& line)
{
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

/// The master seed: `--seed`, else `EJ_KILL_SEED`, else drawn at random. Printed by the first test that uses it, before
/// its first iteration, so a failure can always be repeated.
std::uint64_t master_seed()
{
    static const std::uint64_t seed = []() -> std::uint64_t
    {
        if (g_CommandLine.m_Seed)
        {
            return *g_CommandLine.m_Seed;
        }
        if (const auto text = env_text("EJ_KILL_SEED"))
        {
            return parse_number(*text);
        }
        std::random_device device;
        return (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }();
    return seed;
}

std::filesystem::path artifact_directory()
{
    if (const auto text = env_text("EJ_KILL_ARTIFACT_DIR"))
    {
        return *text;
    }
    return std::filesystem::current_path() / "ej-kill-artifacts";
}

/// Keeps the store a failed iteration started from. The data is synthetic, so it is safe to upload.
std::filesystem::path save_artifact(const std::filesystem::path& store, const std::string& label)
{
    const std::filesystem::path target = artifact_directory() / label;
    std::error_code ignored;
    std::filesystem::remove_all(target, ignored);
    std::filesystem::create_directories(target.parent_path(), ignored);
    std::filesystem::copy(store, target, std::filesystem::copy_options::recursive, ignored);
    return target;
}

void append_summary(const std::string& line)
{
    if (const auto path = env_text("EJ_KILL_SUMMARY"))
    {
        std::ofstream(*path, std::ios::app | std::ios::binary) << line << "\n";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The worker process
// ---------------------------------------------------------------------------------------------------------------------

QString worker_path()
{
    const QString directory = QCoreApplication::applicationDirPath();
    const QString found = QStandardPaths::findExecutable(QStringLiteral("ej_kill_worker"), {directory});
    return found.isEmpty() ? directory + QStringLiteral("/ej_kill_worker") : found;
}

QString path_text(const std::filesystem::path& path)
{
    return QString::fromStdString(path.generic_string());
}

/// What the worker has said so far.
struct WorkerOutput
{
    std::uint64_t m_Acked = 0;   // the highest ACK
    std::uint64_t m_Started = 0; // the highest START
    bool m_Done = false;
    bool m_Recovering = false;
    bool m_Recovered = false;
    bool m_At = false; // the worker is at its crash point, waiting to be killed
    // Each change of the note field and when the driver read the line, which is never earlier than the change.
    std::vector<std::pair<std::uint64_t, std::chrono::steady_clock::time_point>> m_Typed;
    std::string m_Error;
};

class Worker
{
public:
    explicit Worker(const QStringList& arguments)
    {
        m_Process.setProcessChannelMode(QProcess::SeparateChannels);
        m_Process.setProgram(worker_path());
        m_Process.setArguments(arguments);
        m_Process.start();
        if (!m_Process.waitForStarted(30'000))
        {
            m_Output.m_Error = "the worker did not start";
            return;
        }
        contain(m_Process);
    }

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    ~Worker()
    {
        if (m_Process.state() != QProcess::NotRunning)
        {
            m_Process.kill();
            m_Process.waitForFinished(15'000);
        }
    }

    [[nodiscard]] const WorkerOutput& output() const
    {
        return m_Output;
    }
    [[nodiscard]] bool running() const
    {
        return m_Process.state() != QProcess::NotRunning;
    }
    [[nodiscard]] int exit_code() const
    {
        return m_Process.exitCode();
    }
    [[nodiscard]] bool crashed() const
    {
        return m_Process.exitStatus() == QProcess::CrashExit;
    }

    /// Reads until `done` holds, the worker ends, or the time is up. True when `done` holds.
    template <typename Done>
    bool wait_until(Done done, int timeout_ms = WORKER_TIMEOUT_MS)
    {
        QElapsedTimer timer;
        timer.start();
        consume();
        while (!done())
        {
            if (timer.elapsed() > timeout_ms)
            {
                return false;
            }
            if (m_Process.state() == QProcess::NotRunning)
            {
                m_Process.waitForFinished(1'000);
                consume();
                return done();
            }
            m_Process.waitForReadyRead(20);
            consume();
        }
        return true;
    }

    bool wait_for_ack(std::uint64_t number)
    {
        return wait_until([&] { return m_Output.m_Acked >= number; });
    }
    bool wait_for_end()
    {
        return wait_until([&] { return !running(); });
    }

    /// SIGKILL on POSIX, TerminateProcess on Windows: nothing in the worker can catch it or run after it. Whatever the
    /// worker had written before dying is still in the pipe and is read here.
    void kill_now()
    {
        m_Process.kill();
        m_Process.waitForFinished(15'000);
        consume();
    }

private:
    void consume()
    {
        m_Buffer += m_Process.readAllStandardOutput();
        m_Errors += m_Process.readAllStandardError();
        qsizetype end = 0;
        while ((end = m_Buffer.indexOf('\n')) >= 0)
        {
            const std::string line = m_Buffer.left(end).trimmed().toStdString();
            m_Buffer.remove(0, end + 1);
            parse(line);
        }
    }

    void parse(const std::string& line)
    {
        const auto number_after = [&line](std::size_t skip)
        { return static_cast<std::uint64_t>(std::stoull(line.substr(skip))); };
        if (line.starts_with("ACK "))
        {
            m_Output.m_Acked = std::max(m_Output.m_Acked, number_after(4));
        }
        else if (line.starts_with("START "))
        {
            m_Output.m_Started = std::max(m_Output.m_Started, number_after(6));
        }
        else if (line == "DONE")
        {
            m_Output.m_Done = true;
        }
        else if (line.starts_with("TYPED "))
        {
            m_Output.m_Typed.emplace_back(number_after(6), std::chrono::steady_clock::now());
        }
        else if (line.starts_with("AT "))
        {
            m_Output.m_At = true;
        }
        else if (line == "RECOVERING")
        {
            m_Output.m_Recovering = true;
        }
        else if (line.starts_with("RECOVERED"))
        {
            m_Output.m_Recovered = true;
        }
        else if (line.starts_with("ERROR"))
        {
            m_Output.m_Error = line;
        }
    }

    QProcess m_Process;
    QByteArray m_Buffer;
    QByteArray m_Errors;
    WorkerOutput m_Output;
};

QStringList workload_arguments(const std::filesystem::path& dir, std::uint64_t seed, std::uint64_t pace_us,
                               std::uint64_t forced_archive_at = 0)
{
    return {QStringLiteral("--mode"),       QStringLiteral("workload"),
            QStringLiteral("--dir"),        path_text(dir),
            QStringLiteral("--seed"),       QString::fromStdString(decimal(seed)),
            QStringLiteral("--pace-us"),    QString::fromStdString(decimal(pace_us)),
            QStringLiteral("--archive-at"), QString::fromStdString(decimal(forced_archive_at))};
}

// ---------------------------------------------------------------------------------------------------------------------
// The kill plan
// ---------------------------------------------------------------------------------------------------------------------

struct KillPlan
{
    std::uint64_t m_AckTarget = 1;        // wait for this acknowledgement
    std::chrono::microseconds m_Delay{0}; // then wait this long, then kill
};

/// N in [1, 400] and a delay of 0 to 5 ms with extra weight near 0 (section 4.2). The delay moves the kill through the
/// phases of an operation: the journal write, the journal sync, the database commit, the gap between operations.
KillPlan kill_plan(std::uint64_t seed)
{
    SplitMix64 rng(seed ^ 0x4B494C4CULL);
    KillPlan plan;
    plan.m_AckTarget = rng.between(1, MAX_ACK_TARGET);
    const std::uint64_t bucket = rng.below(10);
    std::uint64_t micros = 0;
    if (bucket < 4)
    {
        micros = 0;
    }
    else if (bucket < 7)
    {
        micros = rng.between(0, 500);
    }
    else if (bucket < 9)
    {
        micros = rng.between(0, 2'000);
    }
    else
    {
        micros = rng.between(2'000, 5'000);
    }
    plan.m_Delay = std::chrono::microseconds(static_cast<std::int64_t>(micros));
    return plan;
}

void spin_for(std::chrono::microseconds delay)
{
    const auto until = std::chrono::steady_clock::now() + delay;
    while (std::chrono::steady_clock::now() < until)
    {
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The checks after a kill
// ---------------------------------------------------------------------------------------------------------------------

struct Verdict
{
    bool m_Ok = true;
    std::string m_Message;
    std::uint64_t m_K = 0; // how many operations the recovered store holds
    RecoveryOutcome m_Outcome = RecoveryOutcome::CLEAN;
};

Verdict failed(std::string message)
{
    Verdict verdict;
    verdict.m_Ok = false;
    verdict.m_Message = std::move(message);
    return verdict;
}

ScriptedClock& verification_clock()
{
    static ScriptedClock clock(sample_time(5000));
    clock.advance(std::chrono::seconds(1));
    return clock;
}

/// Section 4.2, "Checks after the kill": recovery succeeds and reports `clean` or `replayed`; the store equals the
/// result of the first k operations for some k in `candidates`; the integrity check says ok; the next operation can be
/// applied and gives the state after k + 1; a second recovery changes nothing.
Verdict verify_store(const std::filesystem::path& dir, std::uint64_t seed, std::uint64_t forced_archive_at,
                     const std::vector<std::uint64_t>& candidates)
{
    const StorageLocation location(dir);
    RecoveryOptions options;
    options.m_FullCheck = true; // the full integrity check, and every log compared with its journal
    auto report = Recovery::run(location, verification_clock(), options);
    if (!report)
    {
        return failed("recovery failed: " + describe(report.error()));
    }
    Verdict verdict;
    verdict.m_Outcome = report->m_Outcome;
    if (report->m_Outcome != RecoveryOutcome::CLEAN && report->m_Outcome != RecoveryOutcome::REPLAYED)
    {
        std::string notes = describe(*report);
        for (const std::string& note : report->m_Notes)
        {
            notes += " [" + note + "]";
        }
        return failed("recovery after a kill reported " + notes);
    }

    {
        ScriptedClock clock(sample_time(6000));
        FileJournal journal(location);
        auto repository = SqliteLogRepository::open(location.database_path(), clock, journal);
        if (!repository)
        {
            return failed("the recovered store does not open: " + describe(repository.error()));
        }
        const auto integrity = (*repository)->integrity_check();
        if (!integrity || !integrity->m_Ok)
        {
            return failed("integrity_check after recovery: " +
                          (integrity ? integrity->m_Message : describe(integrity.error())));
        }
        auto actual = state_of(**repository);
        if (!actual)
        {
            return failed("the recovered store cannot be read: " + describe(actual.error()));
        }
        std::optional<std::uint64_t> matched;
        std::string differences;
        for (const std::uint64_t k : candidates)
        {
            const ModelState expected = model_after(seed, forced_archive_at, k);
            const std::string diff = difference(expected, *actual);
            if (diff.empty())
            {
                matched = k;
                break;
            }
            differences += (differences.empty() ? "" : " | ") + std::string("k=") + decimal(k) + ": " + diff;
        }
        if (!matched)
        {
            return failed("the store matches none of the allowed states (" + differences + ")");
        }
        verdict.m_K = *matched;

        // The store must go on working: the next operation applies and gives the next state.
        Workload workload(seed, forced_archive_at);
        Operation next;
        for (std::uint64_t i = 0; i <= *matched; ++i)
        {
            next = workload.next();
        }
        if (auto applied = apply_operation(**repository, clock, next); !applied)
        {
            return failed("the next operation after recovery failed: " + describe(applied.error()));
        }
        auto after = state_of(**repository);
        if (!after)
        {
            return failed("the store cannot be read after one more operation: " + describe(after.error()));
        }
        if (const std::string diff = difference(model_after(seed, forced_archive_at, *matched + 1), *after);
            !diff.empty())
        {
            return failed("after one more operation the store differs: " + diff);
        }
    }

    // A second recovery changes nothing.
    const auto before = snapshot_of(dir);
    auto again = Recovery::run(location, verification_clock(), options);
    if (!again)
    {
        return failed("the second recovery failed: " + describe(again.error()));
    }
    if (again->m_Outcome != RecoveryOutcome::CLEAN || again->m_ReplayedRecords != 0 ||
        !again->m_QuarantinedPaths.empty())
    {
        return failed("the second recovery was not clean: " + describe(*again));
    }
    if (snapshot_of(dir) != before)
    {
        return failed("the second recovery changed a file");
    }
    return verdict;
}

/// Copies the store as the kill left it, so that a failure can keep it (recovery changes the files).
class Snapshot
{
public:
    explicit Snapshot(const std::filesystem::path& store) : m_Path(m_Dir.path() / "store")
    {
        std::error_code ignored;
        std::filesystem::copy(store, m_Path, std::filesystem::copy_options::recursive, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const
    {
        return m_Path;
    }

private:
    TempDir m_Dir;
    std::filesystem::path m_Path;
};

std::vector<std::uint64_t> range(std::uint64_t first, std::uint64_t last)
{
    std::vector<std::uint64_t> numbers;
    for (std::uint64_t n = first; n <= last; ++n)
    {
        numbers.push_back(n);
    }
    return numbers;
}

// ---------------------------------------------------------------------------------------------------------------------
// One random-kill iteration
// ---------------------------------------------------------------------------------------------------------------------

struct IterationResult
{
    bool m_Ok = true;
    std::string m_Message;
    std::uint64_t m_Acked = 0;
    std::uint64_t m_Started = 0;
    std::uint64_t m_K = 0;
    RecoveryOutcome m_Outcome = RecoveryOutcome::CLEAN;
};

IterationResult run_iteration(std::uint64_t master, std::uint64_t iteration)
{
    const std::uint64_t seed = iteration_seed(master, iteration);
    const KillPlan plan = kill_plan(seed);
    TempDir dir;
    const std::filesystem::path store = dir.path() / "store";

    IterationResult result;
    Worker worker(workload_arguments(store, seed, 2'000));
    const bool reached = worker.wait_for_ack(plan.m_AckTarget);
    if (reached)
    {
        spin_for(plan.m_Delay);
        worker.kill_now();
    }
    result.m_Acked = worker.output().m_Acked;
    result.m_Started = worker.output().m_Started;
    if (!reached)
    {
        result.m_Ok = false;
        result.m_Message = "the worker did not reach acknowledgement " + decimal(plan.m_AckTarget) + " (" +
                           (worker.output().m_Error.empty() ? "no error reported" : worker.output().m_Error) + ")";
        return result;
    }
    if (!worker.output().m_Error.empty())
    {
        result.m_Ok = false;
        result.m_Message = "the worker reported: " + worker.output().m_Error;
        return result;
    }

    const Snapshot killed(store);
    const Verdict verdict =
        verify_store(store, seed, 0, range(result.m_Acked, std::max(result.m_Started, result.m_Acked)));
    result.m_K = verdict.m_K;
    result.m_Outcome = verdict.m_Outcome;
    if (!verdict.m_Ok)
    {
        result.m_Ok = false;
        const auto kept = save_artifact(killed.path(), "seed-" + hex(master) + "-iteration-" + decimal(iteration));
        result.m_Message = verdict.m_Message + " [store kept in " + kept.generic_string() + "]";
    }
    return result;
}

} // namespace

TEST_CASE("kill: random kills lose no acknowledged entry", "[kill]")
{
    const std::uint64_t master = master_seed();
    std::uint64_t first = env_number("EJ_KILL_FIRST", 0);
    const std::uint64_t count = env_number("EJ_KILL_ITERATIONS", DEFAULT_ITERATIONS);
    std::uint64_t last = env_number("EJ_KILL_LAST", first + count - 1);
    std::uint64_t repeats = 1;
    if (g_CommandLine.m_Iteration)
    {
        first = last = *g_CommandLine.m_Iteration;
        repeats = REPRO_RUNS;
    }
    print("ej-kill master_seed=" + hex(master) + " iterations=" + decimal(last - first + 1) +
          " first=" + decimal(first) + " last=" + decimal(last));

    std::uint64_t failures = 0;
    std::uint64_t operations = 0;
    std::uint64_t in_flight_present = 0;
    std::uint64_t replayed_runs = 0;
    std::uint64_t runs = 0;
    for (std::uint64_t iteration = first; iteration <= last; ++iteration)
    {
        std::uint64_t failed_runs = 0;
        for (std::uint64_t repeat = 0; repeat < repeats; ++repeat)
        {
            const IterationResult result = run_iteration(master, iteration);
            ++runs;
            operations += result.m_Acked;
            if (result.m_Ok)
            {
                in_flight_present += result.m_K > result.m_Acked ? 1 : 0;
                replayed_runs += result.m_Outcome == RecoveryOutcome::REPLAYED ? 1 : 0;
                continue;
            }
            ++failed_runs;
            ++failures;
            print("FAIL seed=" + hex(master) + " iteration=" + decimal(iteration) + " A=" + decimal(result.m_Acked) +
                  " k=" + decimal(result.m_K) + ": " + result.m_Message);
        }
        if (repeats > 1)
        {
            print("iteration " + decimal(iteration) + " failed " + decimal(failed_runs) + " of " + decimal(repeats) +
                  " runs");
        }
        else if ((iteration - first + 1) % 50 == 0)
        {
            print("ej-kill progress " + decimal(iteration - first + 1) + " of " + decimal(last - first + 1) +
                  ", failures " + decimal(failures));
        }
    }
    const std::string summary = "ej-kill result master_seed=" + hex(master) + " first=" + decimal(first) +
                                " last=" + decimal(last) + " runs=" + decimal(runs) + " failures=" + decimal(failures) +
                                " acknowledged_operations=" + decimal(operations) +
                                " in_flight_present=" + decimal(in_flight_present) +
                                " replayed=" + decimal(replayed_runs);
    print(summary);
    append_summary(summary);
    CHECK(failures == 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Crash points: the same checks, with a kill that lands in exactly one place every time
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

struct CrashCase
{
    const char* m_Id;
    const char* m_Point;
    std::uint64_t m_Operation; // the operation the process dies in
    std::uint64_t m_Hit;       // the nth time the point is reached during that operation
    bool m_MustBePresent;      // the write must come back (the journal record was durable); otherwise either way
};

constexpr std::uint64_t FORCED_ARCHIVE = 10; // operation 10 is always "start a new log": the pair of records

// K1 to K6 and K9 of section 7. K7 (recovery) and K8 (migration) have drivers of their own.
constexpr CrashCase CRASH_CASES[] = {
    {"K1 first write", "after_journal_append", 1, 1, false},
    {"K1 add", "after_journal_append", 2, 1, false},
    {"K1 add later", "after_journal_append", 30, 1, false},
    {"K1 archive pair, first record", "after_journal_append", FORCED_ARCHIVE, 1, false},
    {"K1 archive pair, second record", "after_journal_append", FORCED_ARCHIVE, 2, false},
    {"K2 first write", "after_journal_sync", 1, 1, true},
    {"K2 add", "after_journal_sync", 2, 1, true},
    {"K2 add later", "after_journal_sync", 30, 1, true},
    {"K2 archive pair, first record", "after_journal_sync", FORCED_ARCHIVE, 1, false},
    {"K2 archive pair, second record", "after_journal_sync", FORCED_ARCHIVE, 2, true},
    {"K3 first write", "before_db_commit", 1, 1, true},
    {"K3 add", "before_db_commit", 2, 1, true},
    {"K3 add later", "before_db_commit", 30, 1, true},
    {"K3 archive pair", "before_db_commit", FORCED_ARCHIVE, 1, true},
    {"K4 first write", "after_db_commit", 1, 1, true},
    {"K4 add", "after_db_commit", 2, 1, true},
    {"K4 add later", "after_db_commit", 30, 1, true},
    {"K4 archive pair", "after_db_commit", FORCED_ARCHIVE, 1, true},
    {"K5 first write", "after_ack_before_next", 1, 1, true},
    {"K5 add", "after_ack_before_next", 2, 1, true},
    {"K5 add later", "after_ack_before_next", 30, 1, true},
    {"K5 archive pair", "after_ack_before_next", FORCED_ARCHIVE, 1, true},
    {"K6 checkpoint after an archive", "mid_checkpoint", FORCED_ARCHIVE, 1, true},
    {"K9 between the archive pair's records", "mid_archive_pair", FORCED_ARCHIVE, 1, false},
};

/// A seed whose operation `item.m_Operation` is one that writes the journal and the database (not a draft, which does
/// neither), so that the crash point is reached. The archive operation is forced; every other one is a plain write.
std::uint64_t seed_for(std::uint64_t master, std::uint64_t index, const CrashCase& item)
{
    for (std::uint64_t attempt = 0;; ++attempt)
    {
        const std::uint64_t seed = iteration_seed(master, 1'000'000 + index * 1'000 + attempt);
        Workload workload(seed, FORCED_ARCHIVE);
        Operation operation;
        for (std::uint64_t n = 0; n < item.m_Operation; ++n)
        {
            operation = workload.next();
        }
        const bool journalled = operation.m_Kind != OperationKind::SET_DRAFT;
        const bool archive = operation.m_Kind == OperationKind::START_NEW_LOG;
        if (std::string_view(item.m_Point) == "after_ack_before_next" ||
            (journalled && archive == (item.m_Operation == FORCED_ARCHIVE)))
        {
            return seed;
        }
    }
}

} // namespace

TEST_CASE("kill: every crash point leaves a store that recovers whole", "[kill][crash-point]")
{
    const std::uint64_t master = master_seed();
    print("ej-kill master_seed=" + hex(master) + " crash points=" + decimal(std::size(CRASH_CASES)));
    std::uint64_t index = 0;
    for (const CrashCase& item : CRASH_CASES)
    {
        const std::uint64_t seed = seed_for(master, index++, item);
        // Twice: the worker ends itself at the point, and the driver kills a worker that waits there.
        for (const bool external : {false, true})
        {
            INFO(item.m_Id << " (" << item.m_Point << " in operation " << item.m_Operation << ", hit " << item.m_Hit
                           << (external ? ", killed from outside" : ", ends itself") << ")");
            TempDir dir;
            const std::filesystem::path store = dir.path() / "store";
            QStringList arguments = workload_arguments(store, seed, 0, FORCED_ARCHIVE);
            arguments << QStringLiteral("--crash-at") << QString::fromLatin1(item.m_Point)
                      << QStringLiteral("--crash-op") << QString::fromStdString(decimal(item.m_Operation))
                      << QStringLiteral("--crash-hit") << QString::fromStdString(decimal(item.m_Hit))
                      << QStringLiteral("--max-ops") << QString::fromStdString(decimal(item.m_Operation + 5))
                      << QStringLiteral("--crash-mode") << QString::fromLatin1(external ? "wait" : "exit");
            Worker worker(arguments);
            if (external)
            {
                REQUIRE(worker.wait_until([&] { return worker.output().m_At || !worker.running(); }));
                CHECK(worker.output().m_At);
                worker.kill_now();
            }
            else
            {
                REQUIRE(worker.wait_for_end());
                CHECK(worker.exit_code() == 137);
            }
            CHECK(worker.output().m_Error.empty());

            const std::uint64_t acked = worker.output().m_Acked;
            const std::uint64_t started = worker.output().m_Started;
            CHECK(started == item.m_Operation);
            // After the ack, the operation is acknowledged; before it, the one in flight is whole or absent.
            const std::vector<std::uint64_t> allowed = item.m_MustBePresent
                                                           ? std::vector<std::uint64_t>{item.m_Operation}
                                                           : range(item.m_Operation - 1, item.m_Operation);
            const Snapshot killed(store);
            const Verdict verdict = verify_store(store, seed, FORCED_ARCHIVE, allowed);
            if (!verdict.m_Ok)
            {
                const auto kept = save_artifact(killed.path(),
                                                "crash-point-" + std::to_string(index) + (external ? "-external" : ""));
                print("FAIL crash point " + std::string(item.m_Id) + " A=" + decimal(acked) + ": " + verdict.m_Message +
                      " [store kept in " + kept.generic_string() + "]");
            }
            INFO(verdict.m_Message);
            CHECK(verdict.m_Ok);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Recovery killed (K7, X3)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

/// A page of the table `entry`, found by reading the page map, with 16 bytes flipped in it (C4).
void damage_entry_page(const std::filesystem::path& database)
{
    std::uint64_t offset = 0;
    {
        RawDb raw(database);
        const std::uint64_t page_size = raw.scalar(QStringLiteral("PRAGMA page_size")).toULongLong();
        const std::uint64_t root =
            raw.scalar(QStringLiteral("SELECT rootpage FROM sqlite_master WHERE name = 'entry'")).toULongLong();
        offset = (root - 1) * page_size + 8;
    }
    std::string content = read_bytes(database);
    REQUIRE(offset + 16 <= content.size());
    for (std::size_t i = 0; i < 16; ++i)
    {
        content[static_cast<std::size_t>(offset) + i] = static_cast<char>(0xA5 ^ i);
    }
    write_bytes(database, content);
}

enum class Damage
{
    EMPTY_DATABASE, // C1
    ENTRY_PAGE,     // C4
    NO_DATABASE     // C6
};

void damage(const StoreCopy& store, Damage kind)
{
    switch (kind)
    {
    case Damage::EMPTY_DATABASE:
        write_bytes(store.database(), "");
        break;
    case Damage::ENTRY_PAGE:
        damage_entry_page(store.database());
        break;
    case Damage::NO_DATABASE:
        std::filesystem::remove(store.database());
        break;
    }
}

QStringList recovery_arguments(const std::filesystem::path& dir, std::uint64_t crash_hit,
                               bool wait_to_be_killed = false)
{
    QStringList arguments{QStringLiteral("--mode"), QStringLiteral("recover"), QStringLiteral("--dir"),
                          path_text(dir),           QStringLiteral("--seed"),  QStringLiteral("1790000000000")};
    if (crash_hit > 0)
    {
        arguments << QStringLiteral("--crash-at") << QStringLiteral("mid_recovery") << QStringLiteral("--crash-hit")
                  << QString::fromStdString(decimal(crash_hit)) << QStringLiteral("--crash-mode")
                  << QString::fromLatin1(wait_to_be_killed ? "wait" : "exit");
    }
    return arguments;
}

} // namespace

TEST_CASE("kill: recovery killed at each of its crash points recovers again to the same result", "[kill][recovery]")
{
    const std::map<LogId, StateDigest>& expected = pristine().m_Digests;
    for (const Damage kind : {Damage::EMPTY_DATABASE, Damage::ENTRY_PAGE, Damage::NO_DATABASE})
    {
        for (const bool external : {false, true})
        {
            bool finished = false;
            for (std::uint64_t hit = 1; hit <= 12 && !finished; ++hit)
            {
                INFO("damage " << static_cast<int>(kind) << ", killed at crash point " << hit
                               << (external ? " from outside" : ", ends itself"));
                StoreCopy store;
                damage(store, kind);
                Worker worker(recovery_arguments(store.root(), hit, external));
                REQUIRE(worker.wait_until([&] { return worker.output().m_At || !worker.running(); }));
                if (worker.output().m_At)
                {
                    worker.kill_now();
                }
                else
                {
                    REQUIRE(worker.wait_for_end());
                }
                CHECK(worker.output().m_Error.empty());
                if (!worker.output().m_At && worker.exit_code() == 0)
                {
                    finished = true; // recovery has fewer crash points than this: every one was tried
                    CHECK(worker.output().m_Recovered);
                }
                else if (!external)
                {
                    CHECK(worker.exit_code() == 137);
                }
                // The next launch.
                const auto report = recover(store.location());
                REQUIRE(report.has_value());
                CHECK((report->m_Outcome == RecoveryOutcome::REBUILT || report->m_Outcome == RecoveryOutcome::CLEAN));
                require_recovered(store, expected, pristine().m_OpenLog);
            }
            CHECK(finished);
        }
    }
}

TEST_CASE("kill: recovery killed at random moments recovers again to the same result", "[kill][recovery]")
{
    const std::uint64_t master = master_seed();
    const std::uint64_t count = env_number("EJ_KILL_RECOVERY_ITERATIONS", DEFAULT_ITERATIONS);
    print("ej-kill master_seed=" + hex(master) + " recovery iterations=" + decimal(count));
    const std::map<LogId, StateDigest>& expected = pristine().m_Digests;
    std::uint64_t killed_mid_run = 0;
    for (std::uint64_t iteration = 0; iteration < count; ++iteration)
    {
        const std::uint64_t seed = iteration_seed(master, 2'000'000 + iteration);
        SplitMix64 rng(seed);
        const std::array<Damage, 3> kinds{Damage::EMPTY_DATABASE, Damage::ENTRY_PAGE, Damage::NO_DATABASE};
        const Damage kind = kinds[static_cast<std::size_t>(rng.below(kinds.size()))];
        const std::chrono::microseconds delay(static_cast<std::int64_t>(rng.between(0, 150'000)));
        INFO("recovery iteration " << iteration << ", seed " << hex(seed) << ", kill after " << delay.count() << " us");

        StoreCopy store;
        damage(store, kind);
        Worker worker(recovery_arguments(store.root(), 0));
        REQUIRE(worker.wait_until([&] { return worker.output().m_Recovering || !worker.running(); }));
        if (worker.running())
        {
            spin_for(delay);
            worker.kill_now();
            if (!worker.output().m_Recovered)
            {
                ++killed_mid_run;
            }
        }
        CHECK(worker.output().m_Error.empty());
        // The next launch.
        const auto report = recover(store.location());
        REQUIRE(report.has_value());
        CHECK((report->m_Outcome == RecoveryOutcome::REBUILT || report->m_Outcome == RecoveryOutcome::CLEAN));
        require_recovered(store, expected, pristine().m_OpenLog);
    }
    print("ej-kill recovery result master_seed=" + hex(master) + " iterations=" + decimal(count) +
          " killed_before_the_end=" + decimal(killed_mid_run));
}

// ---------------------------------------------------------------------------------------------------------------------
// The typed draft (NOTE-03, decision 3): at most the debounce's worth of typing is lost
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("kill: a typed draft is lost for no more than the debounce", "[kill][draft]")
{
    // The draft store writes 300 ms after the first change it is holding. A change this old, or a later one, must be in
    // the store after a kill; the rest of the age is slack for the polling of the worker's timer, the write itself and
    // the delay before the driver reads a line (which only makes the age measured here smaller than the real one).
    constexpr std::chrono::milliseconds REQUIRED_AGE{1500};
    const std::uint64_t master = master_seed();
    const std::uint64_t count = env_number("EJ_KILL_DRAFT_ITERATIONS", 6);
    print("ej-kill master_seed=" + hex(master) + " draft iterations=" + decimal(count));
    std::uint64_t failures = 0;
    for (std::uint64_t iteration = 0; iteration < count; ++iteration)
    {
        const std::uint64_t seed = iteration_seed(master, 3'000'000 + iteration);
        SplitMix64 rng(seed);
        const std::uint64_t target = rng.between(30, 100);
        const std::chrono::milliseconds delay(static_cast<std::int64_t>(rng.between(0, 400)));
        INFO("draft iteration " << iteration << ", seed " << hex(seed) << ", " << target << " changes, then "
                                << delay.count() << " ms");
        TempDir dir;
        const std::filesystem::path store = dir.path() / "store";
        QStringList arguments = workload_arguments(store, seed, 0);
        arguments.replace(1, QStringLiteral("draft"));
        arguments << QStringLiteral("--max-ops") << QStringLiteral("2000");
        Worker worker(arguments);
        REQUIRE(worker.wait_until([&] { return worker.output().m_Typed.size() >= target; }));
        std::this_thread::sleep_for(delay);
        const auto killed_at = std::chrono::steady_clock::now();
        worker.kill_now();
        CHECK(worker.output().m_Error.empty());

        std::uint64_t required = 0; // the newest change that is old enough to have to be there
        for (const auto& [step, seen] : worker.output().m_Typed)
        {
            if (seen + REQUIRED_AGE <= killed_at)
            {
                required = std::max(required, step);
            }
        }
        const std::uint64_t newest = worker.output().m_Typed.back().first;

        const Snapshot killed(store);
        const StorageLocation location(store);
        RecoveryOptions options;
        options.m_FullCheck = true;
        const auto report = Recovery::run(location, verification_clock(), options);
        REQUIRE(report.has_value());
        CHECK((report->m_Outcome == RecoveryOutcome::CLEAN || report->m_Outcome == RecoveryOutcome::REPLAYED));
        std::optional<Draft> draft;
        {
            ScriptedClock clock;
            FileJournal journal(location);
            auto repository = SqliteLogRepository::open(location.database_path(), clock, journal);
            REQUIRE(repository.has_value());
            const auto integrity = (*repository)->integrity_check();
            REQUIRE(integrity.has_value());
            CHECK(integrity->m_Ok);
            draft = (*repository)->get_draft().value();
        }
        std::optional<std::uint64_t> found;
        if (draft)
        {
            for (std::uint64_t step = 1; step <= newest + 1 && !found; ++step)
            {
                if (draft_text(seed, step) == draft->m_Text)
                {
                    found = step;
                }
            }
        }
        bool ok = true;
        std::string why;
        if (draft && !found)
        {
            ok = false;
            why = "the stored draft is none of the texts that were typed";
        }
        else if (found && *found < required)
        {
            ok = false;
            why = "the draft holds change " + decimal(*found) + " but change " + decimal(required) + " was typed " +
                  decimal(static_cast<std::uint64_t>(REQUIRED_AGE.count())) + " ms before the kill";
        }
        else if (!draft && required > 0)
        {
            ok = false;
            why = "no draft, but change " + decimal(required) + " was typed " +
                  decimal(static_cast<std::uint64_t>(REQUIRED_AGE.count())) + " ms before the kill";
        }
        if (!ok)
        {
            ++failures;
            const auto kept =
                save_artifact(killed.path(), "draft-seed-" + hex(master) + "-iteration-" + decimal(iteration));
            print("FAIL draft seed=" + hex(master) + " iteration=" + decimal(iteration) + ": " + why +
                  " [store kept in " + kept.generic_string() + "]");
        }
        CHECK(ok);
    }
    const std::string summary = "ej-kill draft result master_seed=" + hex(master) + " iterations=" + decimal(count) +
                                " failures=" + decimal(failures);
    print(summary);
    append_summary(summary);
}

int main(int argc, char* argv[])
{
    const QCoreApplication application(argc, argv);
    // `--seed` and `--iteration` are ours; the rest belongs to Catch2.
    std::vector<char*> rest;
    for (int i = 0; i < argc; ++i)
    {
        const std::string name = argv[i];
        if ((name == "--seed" || name == "--iteration") && i + 1 < argc)
        {
            const std::uint64_t value = parse_number(argv[++i]);
            (name == "--seed" ? g_CommandLine.m_Seed : g_CommandLine.m_Iteration) = value;
        }
        else
        {
            rest.push_back(argv[i]);
        }
    }
    return Catch::Session().run(static_cast<int>(rest.size()), rest.data());
}
