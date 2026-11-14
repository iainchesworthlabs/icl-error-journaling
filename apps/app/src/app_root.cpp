// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_root.hpp"

#include "error_journaling/file_journal.hpp"
#include "error_journaling/recovery.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "error_journaling/storage_location.hpp"
#include "viewmodel/file_config_store.hpp"

#include <QRandomGenerator>
#include <QTimeZone>
#include <QTimer>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace ej::app
{

namespace vm = ej::viewmodel;

namespace
{

// ------------------------------------------------------------------------------------------------------ storage

/// What the storage thread tells the GUI thread once the store has been opened.
struct Handshake
{
    std::mutex m_Mutex;
    std::condition_variable m_Ready;
    bool m_Done = false;
    vm::StartupResult m_Result;

    void publish(vm::StartupResult result)
    {
        {
            const std::scoped_lock lock(m_Mutex);
            m_Result = std::move(result);
            m_Done = true;
        }
        m_Ready.notify_all();
    }

    vm::StartupResult wait()
    {
        std::unique_lock lock(m_Mutex);
        m_Ready.wait(lock, [this] { return m_Done; });
        return m_Result;
    }
};

/// The repository of a store that could not be opened: every call answers with the reason, so the session reports it
/// and nothing is written to a file this build does not understand.
class FailedRepository final : public LogRepository
{
public:
    explicit FailedRepository(StorageError error) : m_Error(std::move(error)) {}

    Result<void, StorageError> create_log(LogId, const HeaderValues&) override
    {
        return fail(m_Error);
    }
    Result<std::optional<StoredLog>, StorageError> open_log() override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> archive_log(LogId) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> start_new_log(LogId, LogId, const HeaderValues&) override
    {
        return fail(m_Error);
    }
    Result<std::vector<LogSummary>, StorageError> list_logs() override
    {
        return fail(m_Error);
    }
    Result<StoredLog, StorageError> load_log(LogId) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> add_entry(LogId, const Entry&) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> edit_entry(LogId, const Entry&, UtcMillis) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> set_entry_deleted(LogId, EntryId, bool) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> correct_header(LogId, HeaderField, std::string) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> mark_shared(LogId, UtcMillis) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> hard_delete_log(LogId, const DeleteConfirmation&) override
    {
        return fail(m_Error);
    }
    Result<void, StorageError> set_draft(std::string_view, UtcMillis) override
    {
        return fail(m_Error);
    }
    Result<std::optional<Draft>, StorageError> get_draft() override
    {
        return fail(m_Error);
    }

private:
    StorageError m_Error;
};

/// The product's storage: recovery, the file journal, the SQLite repository and the configuration file. Made on the
/// storage thread, used only there, destroyed there.
class SqliteResources final : public vm::StorageResources
{
public:
    SqliteResources(const StoreOptions& options, Handshake& handshake)
        : m_Location(options.m_Root),
          m_Config(options.m_ConfigFile.empty() ? options.m_Root / "configuration.json" : options.m_ConfigFile)
    {
        static const SystemClock system_clock;
        const Clock& clock = options.m_Clock != nullptr ? *options.m_Clock : system_clock;

        RecoveryReport report;
        std::optional<StorageError> failure;
        if (auto made = m_Location.create_directories(); !made)
        {
            failure = made.error();
        }
        else
        {
            RecoveryOptions recovery;
            recovery.m_FullCheck = options.m_FullCheck;
            if (auto recovered = Recovery::run(m_Location, clock, std::move(recovery)); recovered)
            {
                report = std::move(*recovered);
            }
            else
            {
                failure = recovered.error();
            }
        }
        if (!failure)
        {
            m_Journal = std::make_unique<FileJournal>(m_Location);
            if (auto opened = SqliteLogRepository::open(m_Location.database_path(), clock, *m_Journal); opened)
            {
                m_Sqlite = std::move(*opened);
            }
            else
            {
                failure = opened.error();
            }
        }

        LogRepository* repository = nullptr;
        if (m_Sqlite)
        {
            repository = m_Sqlite.get();
            if (options.m_Wrap)
            {
                m_Wrapped = options.m_Wrap(*m_Sqlite);
                repository = m_Wrapped.get();
            }
        }
        else
        {
            m_Failed = std::make_unique<FailedRepository>(*failure);
            repository = m_Failed.get();
        }
        if (!failure && m_Sqlite)
        {
            // The list of people (Stage 14): its own journal, cached in the database. A list that cannot be opened is
            // not fatal: the log works and the picker says people are not available.
            PeopleStoreOptions people_options;
            people_options.m_Cache = m_Sqlite.get();
            if (auto people = PeopleStore::open(m_Location, clock, std::move(people_options)); people)
            {
                m_People = std::move(*people);
            }
        }
        m_Context.emplace(vm::StorageContext{*repository, m_Config, m_People.get()});
        handshake.publish(vm::StartupResult{std::move(report)});
    }

    [[nodiscard]] vm::StorageContext& context() override
    {
        return *m_Context;
    }

private:
    // Destroyed in the reverse order: the wrapper, then the repository, then the journal it appends to.
    StorageLocation m_Location;
    vm::FileConfigStore m_Config;
    std::unique_ptr<FileJournal> m_Journal;
    std::unique_ptr<SqliteLogRepository> m_Sqlite;
    std::unique_ptr<PeopleStore> m_People; // after the repository it caches into: destroyed first
    std::unique_ptr<LogRepository> m_Wrapped;
    std::unique_ptr<FailedRepository> m_Failed;
    std::optional<vm::StorageContext> m_Context;
};

} // namespace

StartedStorage start_sqlite_storage(const StoreOptions& options)
{
    auto handshake = std::make_shared<Handshake>();
    StartedStorage started;
    started.m_Gateway = std::make_unique<vm::ThreadedStorageGateway>(
        [options, handshake]() -> std::unique_ptr<vm::StorageResources>
        { return std::make_unique<SqliteResources>(options, *handshake); });
    started.m_Startup = handshake->wait();
    return started;
}

// ------------------------------------------------------------------------------------------------- environment

struct QtScheduler::Pending
{
    QTimer* m_Timer = nullptr;
    Callback m_Callback;
};

QtScheduler::QtScheduler() = default;

QtScheduler::~QtScheduler()
{
    for (auto& [id, pending] : m_Timers)
    {
        delete pending->m_Timer;
    }
}

vm::TimerId QtScheduler::after(std::chrono::milliseconds delay, Callback callback)
{
    const std::uint64_t id = ++m_Last;
    auto pending = std::make_unique<Pending>();
    pending->m_Callback = std::move(callback);
    pending->m_Timer = new QTimer();
    pending->m_Timer->setSingleShot(true);
    pending->m_Timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(pending->m_Timer, &QTimer::timeout, pending->m_Timer, [this, id] { fire(id); });
    pending->m_Timer->start(static_cast<int>(delay.count()));
    m_Timers.emplace(id, std::move(pending));
    return vm::TimerId{id};
}

void QtScheduler::cancel(vm::TimerId timer)
{
    const auto it = m_Timers.find(timer.m_Value);
    if (it == m_Timers.end())
    {
        return;
    }
    it->second->m_Timer->stop();
    it->second->m_Timer->deleteLater();
    m_Timers.erase(it);
}

void QtScheduler::fire(std::uint64_t id)
{
    const auto it = m_Timers.find(id);
    if (it == m_Timers.end())
    {
        return;
    }
    // The timer is in the middle of its own signal, so it is deleted after the event, not here.
    Callback callback = std::move(it->second->m_Callback);
    it->second->m_Timer->deleteLater();
    m_Timers.erase(it);
    callback();
}

std::chrono::milliseconds QtScheduler::monotonic_now() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

Uuid RandomIds::next()
{
    auto* generator = QRandomGenerator::system();
    const std::uint64_t high = generator->generate64();
    const std::uint64_t low = generator->generate64();
    return Uuid::from_random(high, low);
}

std::string SystemDeviceZone::current() const
{
    return QTimeZone::systemTimeZoneId().toStdString();
}

ProductionEnvironment::ProductionEnvironment() : m_Zones(make_qt_time_zone_provider()) {}

vm::Environment ProductionEnvironment::environment()
{
    return vm::Environment{m_Clock, m_Scheduler, m_Ids, m_Zone, *m_Zones};
}

// ---------------------------------------------------------------------------------------------------- AppRoot

/// Keeps the undo controller on the open document. The session says "no document" before it lets one go, which is
/// when the controller stops observing it, so the controller never holds a document that has gone.
class AppRoot::UndoFollower final : private vm::OpenLogObserver
{
public:
    UndoFollower(vm::OpenLogSource& source, vm::UndoController& undo) : m_Source(source), m_Undo(undo)
    {
        m_Source.add_observer(*this);
        follow(m_Source.open_log());
    }

    ~UndoFollower() override
    {
        m_Source.remove_observer(*this);
        follow(nullptr);
    }

    UndoFollower(const UndoFollower&) = delete;
    UndoFollower& operator=(const UndoFollower&) = delete;

private:
    void on_open_log_changed(vm::LogDocument* document) override
    {
        follow(document);
    }

    void follow(vm::LogDocument* document)
    {
        if (m_Current == document)
        {
            return;
        }
        if (m_Current != nullptr)
        {
            m_Undo.stop_observing(*m_Current);
        }
        m_Current = document;
        if (m_Current != nullptr)
        {
            m_Undo.observe(*m_Current);
        }
    }

    vm::OpenLogSource& m_Source;
    vm::UndoController& m_Undo;
    vm::LogDocument* m_Current = nullptr;
};

AppRoot::AppRoot(const vm::Environment& environment, const AppPlatform& platform, StartedStorage storage,
                 QObject* parent)
    : QObject(parent), m_Gateway(std::move(storage.m_Gateway)), m_Settings(*m_Gateway, environment),
      m_Registry(environment, *m_Gateway), m_Feedback(platform.m_Haptics),
      m_Session(environment, *m_Gateway, m_Registry, m_Settings, m_Feedback, std::move(storage.m_Startup)),
      m_Undo(environment, m_Settings), m_UndoFollower(std::make_unique<UndoFollower>(m_Session, m_Undo)),
      m_Board(m_Settings, m_Session), m_InteractionBoard(m_Settings, m_Session), m_People(environment, *m_Gateway),
      m_Picker(m_Settings, m_Session, m_Settings), m_Timeline(environment, m_Settings, platform.m_Clipboard),
      m_Share(environment, m_Session, m_Registry, m_Settings, platform.m_Share, platform.m_ShareAvailable),
      m_Day(environment, m_Session, m_Share),
      m_History(environment, *m_Gateway, m_Registry, m_Settings, m_Session, platform.m_Clipboard, m_Share)
{
    m_Timeline.follow(m_Session);

    // An undone note goes back to the composer through the session's draft. Text the facilitator has typed since is
    // kept below it, so neither is lost.
    m_Undo.set_note_undo_target(
        [this](const std::string& text)
        {
            const QString restored = QString::fromStdString(text);
            const QString current = m_Session.draft();
            m_Session.set_draft(current.isEmpty() ? restored : restored + QLatin1Char('\n') + current);
        });

    // A draft that a held tile attached (BRD-07) comes back with the undo of that tile, as a note does.
    connect(&m_Session, &vm::LogSession::draftAttached, this,
            [this](const QString& entryId, const QString& text)
            {
                if (const auto uuid = Uuid::parse(entryId.toStdString()))
                {
                    m_Undo.note_attached(EntryId(*uuid), text.toStdString());
                }
            });

    // The log open for review is a document the undo controller must watch while it is open (REV-03) and let go of
    // before it is released.
    connect(&m_History, &vm::HistoryModel::reviewOpened, this,
            [this](vm::LogDocument* document) { review_opened(document); });
    connect(&m_History, &vm::HistoryModel::reviewClosing, this,
            [this](vm::LogDocument* document)
            {
                if (document != nullptr)
                {
                    m_Undo.stop_observing(*document);
                }
            });

    connect_write_failures();

    m_Settings.load();
    m_Session.load();
}

AppRoot::~AppRoot() = default;

void AppRoot::connect_write_failures()
{
    connect(&m_Session, &vm::LogSession::writeFailed, this, &AppRoot::writeFailed);
    connect(&m_Session, &vm::LogSession::recordFailed, this, &AppRoot::recordFailed);
    connect(&m_Undo, &vm::UndoController::writeFailed, this, &AppRoot::writeFailed);
    connect(&m_Timeline, &vm::TimelineModel::writeFailed, this, &AppRoot::writeFailed);
    connect(&m_Share, &vm::ShareService::writeFailed, this, &AppRoot::writeFailed);
    connect(&m_Settings, &vm::SettingsModel::writeFailed, this, &AppRoot::writeFailed);
    connect(&m_History, &vm::HistoryModel::deleteFinished, this,
            [this](const QString&, bool ok)
            {
                if (!ok)
                {
                    emit writeFailed(QStringLiteral("deleteLog"), tr("The log could not be deleted. It is unchanged."));
                }
            });
}

void AppRoot::review_opened(vm::LogDocument* document)
{
    if (document == nullptr)
    {
        return;
    }
    m_Undo.observe(*document);
    if (!m_ReviewConnected && m_History.review_model() != nullptr)
    {
        // The review timeline is a model of its own; it is the same object for the life of the history model.
        connect(m_History.review_model(), &vm::TimelineModel::writeFailed, this, &AppRoot::writeFailed);
        m_ReviewConnected = true;
    }
}

void AppRoot::drain()
{
    m_Gateway->drain();
}

} // namespace ej::app
