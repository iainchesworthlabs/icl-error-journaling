// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/header_values.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/timestamp.hpp"
#include "viewmodel/clipboard.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/open_log_source.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/share_service.hpp"
#include "viewmodel/storage_gateway.hpp"
#include "viewmodel/timeline_model.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <Qt>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

/// The archived logs, newest first, grouped by date, with their shared state (DAY-04, DAY-05; docs/plan/stage-3.md,
/// "HistoryModel"). Opening one binds a second `TimelineModel`, `reviewModel`, to the log's document, so the review
/// screen, edit, delete, copy and share work on a past log without a second implementation.
///
/// Rows. One row per archived log, from `list_logs()` summaries only: History never loads the entries of a year. The
/// open log is not listed (it is the Log and Review tabs). `section` is the local date, "7 Nov 2026", so two logs of
/// one date sit under one heading, newest first. `unitText` and `locationText` are the values the log started with,
/// `shared` is whether it has a shared time, and `sharedAtText` is that time in the user's time format, in the device's
/// zone, formatted by `SettingsModel::formatTime`, the one function the screens use for a time of day. The strings
/// follow the configuration: a change of the time format, the unit prefix or a location name re-renders every row,
/// including logs written before the change (PRF-07).
///
/// A row follows the document of the log that is open for review: a share, a delete or restore of an entry and a header
/// correction made there change the row in place, with one `dataChanged`. A share of a log that is not open (the row's
/// Share button calls `ShareService::shareLog`) is picked up from `ShareService::shareRecorded`.
///
/// A new day. When the open log is replaced by the next day's (`LogChange::Kind::REPLACED`: the open document keeps its
/// pointer, so the model listens to the document and not to a change of pointer) the list is read again, so the log
/// that has just been archived appears. A read already under way is followed by one more.
///
/// Deleting a log (DAY-05). `requestDelete` returns a single-use token for a listed, archived log, and an empty string
/// for anything else, the open log included. `confirmDelete(token)` is the only code of this layer that makes a
/// `DeleteConfirmation` and calls `hard_delete_log`. A token expires when `closeLog` or `leave` is called, that is when
/// History is left.
///
/// For the composition root (3.11): `reviewOpened` and `reviewClosing` bracket the life of the document bound to
/// `reviewModel`, so that `UndoController::observe` and `stop_observing` can be called for it. `reviewClosing` is
/// emitted before the document is let go, and by the destructor if a log is still open.
///
/// GUI thread only. The gateway completions hold a guarded pointer, so a model that has gone is never called back.
/// Not copyable. The model must be destroyed before the documents it holds are, and the references it is given must
/// outlive it.
class HistoryModel : public QAbstractListModel,
                     private LogObserver,
                     private ConfigurationObserver,
                     private OpenLogObserver
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(TimelineModel* reviewModel READ review_model NOTIFY reviewModelChanged)

public:
    /// QML role names: logId, dateText, section, unitText, locationText, entryCount, shared, sharedAtText, entriesText,
    /// versionText, detailText, sharedDateText, longDateText. `Qt::DisplayRole` is `dateText`.
    ///
    /// Beyond the first eight, for the designs: `entriesText` is "12 entries" ("1 entry"), `versionText` is the
    /// software version the log started with, "v1.4.2" (empty when it had none), `detailText` is the two joined as the
    /// row reads, "4 entries, middle dot, v1.4.2" (just the entries when there is no version), `sharedDateText` is the
    /// day of the last share, in the device zone, "5 Nov" (empty when the log has not been shared), and `longDateText`
    /// is the date of the log with its year, "Thu 5 Nov 2026", for the delete alert. `sharedAtText` is the time of that
    /// share, and "Shared 5 Nov at 16:58" is made of the two.
    enum Role
    {
        LOG_ID = Qt::UserRole + 1,
        DATE_TEXT,
        SECTION,
        UNIT_TEXT,
        LOCATION_TEXT,
        ENTRY_COUNT,
        SHARED,
        SHARED_AT_TEXT,
        ENTRIES_TEXT,
        VERSION_TEXT,
        DETAIL_TEXT,
        SHARED_DATE_TEXT,
        LONG_DATE_TEXT
    };
    Q_ENUM(Role)

    /// None of the arguments is owned; all must outlive the model. The model listens to `settings` (the configuration
    /// in force) and to `share` for its whole life. `open_log` is watched for the new day. `clipboard` is for the copy
    /// buttons of `reviewModel`.
    HistoryModel(const Environment& environment, StorageGateway& gateway, DocumentRegistry& registry,
                 SettingsModel& settings, OpenLogSource& open_log, Clipboard& clipboard, ShareService& share,
                 QObject* parent = nullptr);
    ~HistoryModel() override;

    [[nodiscard]] bool loading() const noexcept
    {
        return m_Loading;
    }

    /// The past log that is open for review, or null.
    [[nodiscard]] TimelineModel* review_model()
    {
        return m_Opened ? &m_Review : nullptr;
    }

    /// The document `reviewModel` shows, or null.
    [[nodiscard]] LogDocument* review_document() const
    {
        return m_Opened.get();
    }

    // QAbstractListModel. Qt's names, so these overrides keep them.

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Reads the list of logs on the storage thread and replaces the rows when it arrives: one `modelReset`. A call
    /// while a read is under way does nothing. A read that fails leaves the rows as they were and emits
    /// `refreshFailed`.
    Q_INVOKABLE void refresh();

    /// Loads the log through the registry and binds `reviewModel` to it; `reviewModel` is null until it has loaded.
    /// A log that is already open is closed first. An id that is not a log, a log the store does not have, a read that
    /// fails, or the open log, which is not reviewed here, emits `logOpenFailed`.
    Q_INVOKABLE void openLog(const QString& logId);

    /// Unbinds `reviewModel` and lets the document go, and expires every token.
    Q_INVOKABLE void closeLog();

    /// History is left: `closeLog`, and the same for a request that has not been confirmed.
    Q_INVOKABLE void leave();

    /// A token for deleting the archived log `logId`, or an empty string when the log is not a listed archived log
    /// (the open log, an unknown id) or is being deleted already. Nothing is written.
    Q_INVOKABLE QString requestDelete(const QString& logId);

    /// Deletes the log the token was given for, with `hard_delete_log` on the storage thread. The token is used up
    /// whatever the answer. False when the token is not valid (used, expired, unknown): nothing happens. True when the
    /// delete has been queued; `deleteFinished` reports it, and the row leaves the model only when the store has
    /// deleted the log. A log that is open for review is closed first.
    Q_INVOKABLE bool confirmDelete(const QString& token);

signals:
    void loadingChanged();
    void reviewModelChanged();

    /// The list could not be read; the rows are those of the last read. `message` is plain and holds no entry text.
    void refreshFailed(const QString& message);

    /// `openLog` could not open the log. `message` is plain.
    void logOpenFailed(const QString& message);

    /// A confirmed delete ended. On `ok` the row is gone; otherwise the log and its row are as they were.
    void deleteFinished(const QString& logId, bool ok);

    /// `document` is now the log of `reviewModel`.
    void reviewOpened(ej::viewmodel::LogDocument* document);

    /// `document` is about to be let go. Called before `reviewModel` is unbound.
    void reviewClosing(ej::viewmodel::LogDocument* document);

private:
    struct Row
    {
        LogId m_Id;
        QString m_IdText;
        HeaderValues m_Header; // the values the log started with
        UtcMillis m_Created;
        std::optional<UtcMillis> m_Shared;
        int m_Count = 0; // live entries

        // Rendered, so that data() formats nothing.
        QString m_Date;
        QString m_Section;
        QString m_Unit;
        QString m_Location;
        QString m_SharedAt;
        QString m_Entries;
        QString m_Version;
        QString m_Detail;
        QString m_SharedDate;
        QString m_LongDate;
    };

    struct Token
    {
        QString m_Text;
        LogId m_Log;
    };

    // LogObserver, ConfigurationObserver
    void on_log_changed(const LogDocument& document, const LogChange& change) override;
    void on_configuration_changed(const ConfigurationSource& source) override;
    void on_open_log_changed(LogDocument* document) override;

    void render(Row& row) const;
    [[nodiscard]] static QList<int> changed_roles(const Row& before, const Row& after);
    [[nodiscard]] std::optional<std::size_t> find_row(LogId id) const;
    void show_document(const LogDocument& document);
    void list_arrived(const Result<std::vector<LogSummary>, StorageError>& result);
    void opened(std::uint64_t serial, Result<std::shared_ptr<LogDocument>, StorageError> result);
    void close_review();
    void deleted(LogId id, const Result<void, StorageError>& result);
    void share_recorded(const QString& logId);
    void set_loading(bool loading);
    void watch_open_log(LogDocument* document);

    Environment m_Environment;
    StorageGateway& m_Gateway;
    DocumentRegistry& m_Registry;
    SettingsModel& m_Settings;
    OpenLogSource& m_OpenLog;

    std::vector<Row> m_Rows;
    bool m_Loading = false;
    bool m_RefreshAgain = false;      // the open log rolled over while a read was under way
    LogDocument* m_Watched = nullptr; // the open log, told to us when it is replaced by a new day

    std::uint64_t m_OpenSerial = 0; // an open that is in flight is dropped when this has moved on
    std::vector<Token> m_Tokens;
    std::vector<LogId> m_Deleting;

    // Declared in this order so that the timeline goes first: it listens to the document and the configuration.
    std::shared_ptr<LogDocument> m_Opened;
    TimelineModel m_Review;
};

} // namespace ej::viewmodel
