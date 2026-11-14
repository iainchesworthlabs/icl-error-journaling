// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/rig.hpp"

#include "golden.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QString>
#include <algorithm>
#include <filesystem>
#include <string>
#include <thread>

namespace ej::test
{

namespace vm = ej::viewmodel;
using namespace std::chrono_literals;

namespace
{

constexpr auto SETTLE_TIMEOUT = 20s;

/// The local time in Melbourne on 7 and 8 November 2026 is the UTC time plus eleven hours.
constexpr auto MELBOURNE_OFFSET = 11h;

} // namespace

Rig::Rig(RigStore store)
    : m_Kind(store), m_StoreClock(m_Clock.now()), m_FakeZones(make_fixed_zones()), m_Platform(&m_Events),
      m_Clipboard(&m_Events)
{
    if (m_Kind == RigStore::SQLITE)
    {
        REQUIRE(m_Directory.isValid());
        m_QtZones = make_qt_time_zone_provider();
    }
    build();
}

Rig::~Rig()
{
    // A commit that is still held would never let the storage thread end.
    {
        const std::scoped_lock lock(m_HoldMutex);
        m_Holding = false;
        m_StartGateClosed = false;
    }
    m_HoldReleased.notify_all();
    tear_down();
}

vm::Environment Rig::environment()
{
    const TimeZoneProvider& zones = m_QtZones ? static_cast<const TimeZoneProvider&>(*m_QtZones)
                                              : static_cast<const TimeZoneProvider&>(m_FakeZones);
    vm::Environment environment{m_Clock, m_Clock, m_Ids, m_DeviceZone, zones};
    environment.m_People = &m_PersonNames;
    environment.m_ExportPreferences = &m_ExportPreferences;
    return environment;
}

void Rig::build()
{
    app::StartedStorage started;
    if (m_Kind == RigStore::MEMORY)
    {
        if (!m_Memory)
        {
            m_Memory = std::make_unique<MemoryStorage>(m_Clock, &m_Events);
        }
        auto gateway = std::make_unique<ManualStorageGateway>(m_Memory->context());
        m_Manual = gateway.get();
        started.m_Gateway = std::move(gateway);
    }
    else
    {
        app::StoreOptions options;
        options.m_Root = std::filesystem::path(m_Directory.path().toStdU16String()) / "store";
        options.m_Clock = &m_StoreClock;
        // The wrapper is made on the storage thread, when the store is opened; start_sqlite_storage returns after that.
        options.m_Wrap = [this](LogRepository& inner) -> std::unique_ptr<LogRepository>
        {
            auto recording = std::make_unique<RecordingRepository>(inner, &m_Events);
            m_Recording.store(recording.get());
            return recording;
        };
        started = app::start_sqlite_storage(options);
        m_Threaded = static_cast<vm::ThreadedStorageGateway*>(started.m_Gateway.get());
        repository().set_before_commit(
            [this](RepositoryOp)
            {
                std::unique_lock lock(m_HoldMutex);
                m_HoldReleased.wait(lock, [this] { return !m_Holding; });
            });
    }

    if (m_HoldNextStart)
    {
        m_HoldNextStart = false;
        {
            const std::scoped_lock lock(m_HoldMutex);
            m_StartGateClosed = true;
        }
        started.m_Gateway->post(
            [this](vm::StorageContext&)
            {
                std::unique_lock lock(m_HoldMutex);
                m_HoldReleased.wait(lock, [this] { return !m_StartGateClosed; });
            },
            [] {});
    }

    if (m_PendingRecovery)
    {
        started.m_Startup.m_Recovery = std::move(*m_PendingRecovery);
        m_PendingRecovery.reset();
    }

    const app::AppPlatform platform{m_Platform, m_Platform, m_Clipboard, true};
    m_Root = std::make_unique<app::AppRoot>(environment(), platform, std::move(started));
    m_PersonNames.set(m_Root->people());
    watch_events();
}

void Rig::tear_down()
{
    m_Watching = false;
    harvest_records();
    m_PersonNames.set(nullptr);
    m_Root.reset();
    m_Threaded = nullptr;
    m_Manual = nullptr;
    m_Recording.store(nullptr);
}

void Rig::harvest_records()
{
    if (m_Kind != RigStore::SQLITE || m_Recording.load() == nullptr)
    {
        return;
    }
    RecordingRepository& recording = *m_Recording.load();
    m_PastMutatingCalls += recording.mutating_call_count();
    const auto threads = recording.threads();
    m_PastThreads.insert(threads.begin(), threads.end());
}

void Rig::watch_events()
{
    app::AppRoot* const root = m_Root.get();
    m_Watching = true;
    QObject::connect(root->undo(), &vm::UndoController::toastChanged, root,
                     [this]
                     {
                         if (m_Watching)
                         {
                             m_Events.record(EventKind::TOAST,
                                             undo().active() ? undo().text().toStdString() : std::string("closed"));
                         }
                     });
    QObject::connect(root->board(), &vm::BoardModel::countsChanged, root,
                     [this]
                     {
                         if (m_Watching)
                         {
                             m_Events.record(EventKind::MARK, "count " + std::to_string(board().total_count()));
                         }
                     });
    QObject::connect(root->timeline(), &vm::TimelineModel::summaryChanged, root,
                     [this]
                     {
                         if (m_Watching)
                         {
                             m_Events.record(EventKind::MARK, "last " + timeline().last_line().toStdString());
                         }
                     });
    QObject::connect(root->share(), &vm::ShareService::shareFinished, root,
                     [this](const QString&, const QString& outcome)
                     {
                         if (m_Watching)
                         {
                             m_Events.record(EventKind::MARK, "share " + outcome.toStdString());
                         }
                     });
    QObject::connect(root, &app::AppRoot::writeFailed, root,
                     [this](const QString& operation, const QString&)
                     {
                         ++m_WriteFailures;
                         m_Events.record(EventKind::WRITE_FAILED, operation.toStdString());
                     });
}

RecordingRepository& Rig::repository()
{
    if (m_Kind == RigStore::MEMORY)
    {
        return m_Memory->repository();
    }
    RecordingRepository* const recording = m_Recording.load();
    REQUIRE(recording != nullptr);
    return *recording;
}

MemoryStorage& Rig::memory()
{
    REQUIRE(m_Memory != nullptr);
    return *m_Memory;
}

// ------------------------------------------------------------------------------------------------------- time

void Rig::go_to(int day, int hour, int minute, int second, int millisecond)
{
    const UtcMillis target = utc(2026, 11, static_cast<unsigned>(day), hour, minute, second, millisecond) -
                             std::chrono::duration_cast<std::chrono::milliseconds>(MELBOURNE_OFFSET);
    const auto delta = target - m_Clock.now();
    if (delta > std::chrono::milliseconds::zero())
    {
        advance(delta);
    }
    else
    {
        m_Clock.set_wall(target);
        m_StoreClock.set(target);
    }
}

void Rig::advance(std::chrono::milliseconds by)
{
    m_Clock.advance(by);
    m_StoreClock.set(m_Clock.now());
}

std::string Rig::local_time_text() const
{
    const auto local = std::chrono::floor<std::chrono::seconds>(m_Clock.now() + MELBOURNE_OFFSET);
    const auto days = std::chrono::floor<std::chrono::days>(local);
    const std::chrono::hh_mm_ss time{local - days};
    auto two = [](long long n) { return (n < 10 ? "0" : "") + std::to_string(n); };
    return two(time.hours().count()) + ":" + two(time.minutes().count()) + ":" + two(time.seconds().count());
}

// ---------------------------------------------------------------------------------------------------- driving

void Rig::settle()
{
    if (m_Kind == RigStore::MEMORY)
    {
        while (m_Manual->pending() > 0)
        {
            m_Manual->run_all();
        }
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + SETTLE_TIMEOUT;
    int idle = 0;
    while (idle < 3)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (m_Threaded->outstanding() == 0)
        {
            ++idle;
        }
        else
        {
            idle = 0;
            std::this_thread::sleep_for(1ms);
        }
        if (std::chrono::steady_clock::now() > deadline)
        {
            FAIL("the store did not settle within " << SETTLE_TIMEOUT.count() << " s (a commit still held?)");
        }
    }
}

void Rig::pump()
{
    if (m_Kind == RigStore::MEMORY)
    {
        return;
    }
    const auto until = std::chrono::steady_clock::now() + 30ms;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(1ms);
    }
}

void Rig::wait_until(const std::function<bool()>& done)
{
    if (m_Kind == RigStore::MEMORY)
    {
        settle();
        REQUIRE(done());
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!done())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(1ms);
        if (std::chrono::steady_clock::now() > deadline)
        {
            FAIL("timed out waiting for the store");
        }
    }
}

bool Rig::tap(const QString& type_id)
{
    // The touch-down: QML fires the impact haptic from onPressed, before it calls into the services.
    m_Platform.fire(ej::bridge::HapticKind::IMPACT);
    const bool accepted = session().recordError(type_id);
    m_Events.record(EventKind::MARK, accepted ? "tap accepted" : "tap refused");
    return accepted;
}

bool Rig::add_note(const QString& text)
{
    const bool accepted = session().addNote(text);
    m_Events.record(EventKind::MARK, accepted ? "note accepted" : "note refused");
    return accepted;
}

void Rig::hold_commits()
{
    const std::scoped_lock lock(m_HoldMutex);
    m_Holding = true;
}

void Rig::release_commits()
{
    {
        const std::scoped_lock lock(m_HoldMutex);
        m_Holding = false;
    }
    m_HoldReleased.notify_all();
    settle();
}

void Rig::hold_startup()
{
    REQUIRE(m_Kind == RigStore::SQLITE);
    m_HoldNextStart = true;
}

void Rig::release_startup()
{
    {
        const std::scoped_lock lock(m_HoldMutex);
        m_StartGateClosed = false;
    }
    m_HoldReleased.notify_all();
    settle();
}

void Rig::import_sample_configuration()
{
    QString path;
    if (m_Kind == RigStore::MEMORY)
    {
        const auto text = read_bytes(std::filesystem::path(EJ_SAMPLE_CONFIG));
        REQUIRE(text.has_value());
        memory().config().add_file("sample-config.json", *text);
        path = QStringLiteral("sample-config.json");
    }
    else
    {
        path = QString::fromUtf8(EJ_SAMPLE_CONFIG);
    }
    QSignalSpy prepared(&settings(), &vm::SettingsModel::importPrepared);
    QSignalSpy failed(&settings(), &vm::SettingsModel::importFailed);
    settings().loadImport(path);
    wait_until([&] { return prepared.count() + failed.count() > 0; });
    REQUIRE(failed.count() == 0);
    settings().applyImport();
    wait_until([&] { return settings().configured(); });
    settle();
}

void Rig::restart()
{
    settle();
    tear_down();
    build();
    if (!m_StartGateClosed)
    {
        settle();
    }
}

// ------------------------------------------------------------------------------------------------- the store

std::vector<LogSummary> Rig::stored_summaries()
{
    auto result = in_store([](vm::StorageContext& context) { return context.m_Logs.list_logs(); });
    REQUIRE(result.has_value());
    return std::move(*result);
}

StoredLog Rig::stored_log(LogId id)
{
    auto result = in_store([id](vm::StorageContext& context) { return context.m_Logs.load_log(id); });
    REQUIRE(result.has_value());
    return std::move(*result);
}

std::optional<StoredLog> Rig::stored_open_log()
{
    auto result = in_store([](vm::StorageContext& context) { return context.m_Logs.open_log(); });
    REQUIRE(result.has_value());
    return std::move(*result);
}

std::optional<Draft> Rig::stored_draft()
{
    auto result = in_store([](vm::StorageContext& context) { return context.m_Logs.get_draft(); });
    REQUIRE(result.has_value());
    return std::move(*result);
}

bool Rig::idle() const
{
    if (m_Kind == RigStore::MEMORY)
    {
        return m_Manual->pending() == 0;
    }
    return m_Threaded->outstanding() == 0;
}

std::size_t Rig::mutating_calls()
{
    return m_PastMutatingCalls + repository().mutating_call_count();
}

std::set<std::thread::id> Rig::repository_threads()
{
    std::set<std::thread::id> threads = m_PastThreads;
    const auto current = repository().threads();
    threads.insert(current.begin(), current.end());
    return threads;
}

} // namespace ej::test
