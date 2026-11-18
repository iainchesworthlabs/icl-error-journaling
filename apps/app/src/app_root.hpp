// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/haptics.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/share.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/clipboard.hpp"
#include "viewmodel/day_watcher.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/error_picker_model.hpp"
#include "viewmodel/feedback_sink.hpp"
#include "viewmodel/history_model.hpp"
#include "viewmodel/interaction_board_model.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/open_log_source.hpp"
#include "viewmodel/people_admin_model.hpp"
#include "viewmodel/people_model.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/share_service.hpp"
#include "viewmodel/storage_gateway.hpp"
#include "viewmodel/timeline_model.hpp"
#include "viewmodel/undo_controller.hpp"

#include <QObject>
#include <QString>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

class QTimer;

namespace ej::app
{

// ------------------------------------------------------------------------------------------------------ storage

/// Where the product keeps its store and how it is opened (Stage 3, 3.11).
struct StoreOptions
{
    std::filesystem::path m_Root; // the data directory: ej.db, the journals, quarantine and the configuration file
    /// The configuration file. Empty means `<root>/configuration.json`.
    std::filesystem::path m_ConfigFile;
    /// Stamps the logs the repository makes. Read on the storage thread, so it must be safe to read from there; it
    /// must outlive the gateway.
    const Clock* m_Clock = nullptr;
    /// Run the full integrity check as well (the first launch after an upgrade, Stage 2 decision 6).
    bool m_FullCheck = false;
    /// Lets a test put a wrapper (a recording repository) around the real one. Called on the storage thread; the
    /// wrapper must not outlive the repository it is given.
    std::function<std::unique_ptr<LogRepository>(LogRepository& inner)> m_Wrap;
};

/// A gateway on a store that is open, and what recovery did on the way.
struct StartedStorage
{
    std::unique_ptr<viewmodel::StorageGateway> m_Gateway;
    viewmodel::StartupResult m_Startup;
};

/// Starts the storage thread, which runs `Recovery::run` and opens the SQLite repository and the configuration file
/// (Stage 2, 2.7), and returns when that has finished. This is the one place the GUI thread waits for the store: the
/// board needs the outcome of recovery, which Stage 2 puts before it appears. Every later use of the store goes
/// through the gateway and does not wait. A store that cannot be opened (a newer schema, a file that is not ours) is
/// not an error here: the repository answers every call with the reason, and the session reports it.
[[nodiscard]] StartedStorage start_sqlite_storage(const StoreOptions& options);

// ------------------------------------------------------------------------------------------------- environment

/// Monotonic one-shot timers on the Qt event loop (the undo window and the draft debounce).
class QtScheduler final : public viewmodel::Scheduler
{
public:
    QtScheduler();
    ~QtScheduler() override;

    viewmodel::TimerId after(std::chrono::milliseconds delay, Callback callback) override;
    void cancel(viewmodel::TimerId timer) override;
    [[nodiscard]] std::chrono::milliseconds monotonic_now() const override;

private:
    struct Pending;
    void fire(std::uint64_t id);

    std::uint64_t m_Last = 0;
    std::map<std::uint64_t, std::unique_ptr<Pending>> m_Timers;
};

/// Two words of `QRandomGenerator::system()`.
class RandomIds final : public viewmodel::IdGenerator
{
public:
    [[nodiscard]] Uuid next() override;
};

/// `QTimeZone::systemTimeZoneId()`.
class SystemDeviceZone final : public viewmodel::DeviceZone
{
public:
    [[nodiscard]] std::string current() const override;
};

/// The product's time, ids and zones, and the `Environment` that refers to them.
class ProductionEnvironment
{
public:
    ProductionEnvironment();

    ProductionEnvironment(const ProductionEnvironment&) = delete;
    ProductionEnvironment& operator=(const ProductionEnvironment&) = delete;

    [[nodiscard]] viewmodel::Environment environment();
    [[nodiscard]] const Clock& clock() const
    {
        return m_Clock;
    }

private:
    SystemClock m_Clock;
    QtScheduler m_Scheduler;
    RandomIds m_Ids;
    SystemDeviceZone m_Zone;
    std::unique_ptr<TimeZoneProvider> m_Zones;
};

// ---------------------------------------------------------------------------------------------------- AppRoot

/// What the platform gives the app, behind interfaces. Nothing is owned; all must outlive the root.
struct AppPlatform
{
    ej::bridge::Share& m_Share;
    ej::bridge::Haptics& m_Haptics;
    viewmodel::Clipboard& m_Clipboard;
    /// Whether the platform has a share sheet at all (false on the desktop stub, so QML hides the control).
    bool m_ShareAvailable = false;
};

/// The composition root (Stage 3, 3.11): builds the services in the order they depend on each other, connects them,
/// and destroys them in the reverse order. `main.cpp` and the headless tests (`Rig`) build it the same way, so the
/// full-day test runs the wiring the phone runs.
///
/// Order of construction (destruction is the reverse):
///   gateway, SettingsModel (load), DocumentRegistry, PlatformFeedbackSink, LogSession (load), UndoController,
///   BoardModel, ErrorPickerModel, TimelineModel (follows the open log), ShareService, DayWatcher, HistoryModel.
///
/// Lifetime contract. A model dies before the documents it observes: the open document is held by `LogSession`, which
/// is built before every model, and a past log is held by `HistoryModel`, which is built after `UndoController` and so
/// lets go of it first. `LogSession` reports a null document to its followers before it releases one. Every
/// `writeFailed` of the services arrives at the root's single `writeFailed`.
///
/// The models are properties of the root so that QML takes them as `required property` of the window (Stage 4,
/// decision 3). GUI thread only.
class AppRoot final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ej::viewmodel::LogSession* session READ session CONSTANT)
    Q_PROPERTY(ej::viewmodel::UndoController* undo READ undo CONSTANT)
    Q_PROPERTY(ej::viewmodel::BoardModel* board READ board CONSTANT)
    Q_PROPERTY(ej::viewmodel::InteractionBoardModel* interactionBoard READ interaction_board CONSTANT)
    Q_PROPERTY(ej::viewmodel::PeopleModel* people READ people CONSTANT)
    Q_PROPERTY(ej::viewmodel::PeopleAdminModel* peopleAdmin READ people_admin CONSTANT)
    Q_PROPERTY(ej::viewmodel::ErrorPickerModel* picker READ picker CONSTANT)
    Q_PROPERTY(ej::viewmodel::TimelineModel* timeline READ timeline CONSTANT)
    Q_PROPERTY(ej::viewmodel::HistoryModel* history READ history CONSTANT)
    Q_PROPERTY(ej::viewmodel::DayWatcher* day READ day CONSTANT)
    Q_PROPERTY(ej::viewmodel::SettingsModel* settings READ settings CONSTANT)
    Q_PROPERTY(ej::viewmodel::ShareService* share READ share CONSTANT)

public:
    AppRoot(const viewmodel::Environment& environment, const AppPlatform& platform, StartedStorage storage,
            QObject* parent = nullptr);
    ~AppRoot() override;

    AppRoot(const AppRoot&) = delete;
    AppRoot& operator=(const AppRoot&) = delete;

    [[nodiscard]] viewmodel::LogSession* session()
    {
        return &m_Session;
    }
    [[nodiscard]] viewmodel::UndoController* undo()
    {
        return &m_Undo;
    }
    [[nodiscard]] viewmodel::BoardModel* board()
    {
        return &m_Board;
    }
    [[nodiscard]] viewmodel::InteractionBoardModel* interaction_board()
    {
        return &m_InteractionBoard;
    }
    [[nodiscard]] viewmodel::PeopleModel* people()
    {
        return &m_People;
    }
    [[nodiscard]] viewmodel::PeopleAdminModel* people_admin()
    {
        return &m_PeopleAdmin;
    }
    [[nodiscard]] viewmodel::ErrorPickerModel* picker()
    {
        return &m_Picker;
    }
    [[nodiscard]] viewmodel::TimelineModel* timeline()
    {
        return &m_Timeline;
    }
    [[nodiscard]] viewmodel::HistoryModel* history()
    {
        return &m_History;
    }
    [[nodiscard]] viewmodel::DayWatcher* day()
    {
        return &m_Day;
    }
    [[nodiscard]] viewmodel::SettingsModel* settings()
    {
        return &m_Settings;
    }
    [[nodiscard]] viewmodel::ShareService* share()
    {
        return &m_Share;
    }

    [[nodiscard]] viewmodel::StorageGateway& gateway()
    {
        return *m_Gateway;
    }

    /// Finishes every queued write and delivers its completion. Once, from `aboutToQuit`, on the GUI thread; never
    /// while the person is using the app.
    void drain();

signals:
    /// A write of any service failed. `operation` names it ("recordError", "undo", "saveEntry", ...); `message` is
    /// plain English and holds no entry text. The one place Stage 4's banner listens.
    void writeFailed(const QString& operation, const QString& message);

    /// A tap on an error type could not be saved (`LogSession::recordFailed`), with the name of the type for the
    /// banner: `title` "Mute Delay wasn't saved", `message` "Nothing was added to the log. Tap Mute Delay again to log
    /// it.". It follows the `writeFailed("recordError", ...)` of the same failure, which is still emitted.
    void recordFailed(const QString& errorTypeId, const QString& errorTypeName, const QString& title,
                      const QString& message);

private:
    class UndoFollower;

    void connect_write_failures();
    void review_opened(viewmodel::LogDocument* document);

    std::unique_ptr<viewmodel::StorageGateway> m_Gateway;
    viewmodel::SettingsModel m_Settings;
    viewmodel::DocumentRegistry m_Registry;
    viewmodel::PlatformFeedbackSink m_Feedback;
    viewmodel::LogSession m_Session;
    viewmodel::UndoController m_Undo;
    // Between the undo controller and the models: it keeps the controller on the open document, and goes first.
    std::unique_ptr<UndoFollower> m_UndoFollower;
    viewmodel::BoardModel m_Board;
    viewmodel::InteractionBoardModel m_InteractionBoard;
    viewmodel::PeopleModel m_People;
    viewmodel::PeopleAdminModel m_PeopleAdmin;
    viewmodel::ErrorPickerModel m_Picker;
    viewmodel::TimelineModel m_Timeline;
    viewmodel::ShareService m_Share;
    viewmodel::DayWatcher m_Day;
    viewmodel::HistoryModel m_History;
    bool m_ReviewConnected = false;
};

} // namespace ej::app
