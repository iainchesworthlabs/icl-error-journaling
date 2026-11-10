// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/share.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/timestamp.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/open_log_source.hpp"

#include <QObject>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

namespace ej::viewmodel
{

/// Sends the text of a log to the platform's share sheet and records that the log was shared (REV-05; DAY-01 share
/// first; DAY-04 re-share). docs/plan/stage-3.md, "ShareService".
///
/// The text is `export_text` of the document's mirror with a context from `ExportContextBuilder`, the same function
/// Copy uses, so Share and Copy cannot differ. The mirror holds committed changes only, so an edit that is still
/// waiting in the document's queue is not in the text.
///
/// What counts as shared (Stage 3 decision 10): the platform reported `SHARED`, which on a phone includes the sheet's
/// own Copy action. A dismissed sheet (`cancelled`), a platform without a sheet (`unavailable`) and a failure record
/// nothing. The in-app Copy buttons never reach this class. The share time is the time the sheet completed, read when
/// the platform calls back and never earlier, and is committed through `LogDocument::mark_shared`; an archived log
/// takes it too. An edit made later does not clear it, and nothing here ever clears it.
///
/// One share at a time. While one is under way (from the call until the sheet has ended) `sharing` is true and a
/// second `shareOpenLog` or `shareLog` returns `false` and does nothing: it emits no signal, and the share under way
/// is not touched. It is not queued; the facilitator taps again once the first has ended.
///
/// Sharing fires no haptic: the only platform dependency is the bridge's `Share` (Stage 5, 5.1).
///
/// The completion of the sheet may run before `Share::present` returns (the desktop stub) or after (a phone); both
/// are handled. A completion that arrives after the service is gone is ignored. GUI thread only.
class ShareService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool sharing READ sharing NOTIFY sharingChanged)

public:
    /// None of the references is owned; all must outlive the service. `available` is whether the platform has a share
    /// sheet at all, which the composition root knows (false on the desktop stub, so that QML hides the control); the
    /// service does not stop a share on it, it passes the platform's `unavailable` on.
    ShareService(const Environment& environment, OpenLogSource& open_log, DocumentRegistry& registry,
                 const ConfigurationSource& configuration, ej::bridge::Share& share, bool available,
                 QObject* parent = nullptr);
    ~ShareService() override;

    [[nodiscard]] bool available() const noexcept
    {
        return m_Available;
    }
    [[nodiscard]] bool sharing() const noexcept
    {
        return m_Sharing;
    }

    /// The Share log button: shares the open log. False when a share is under way (nothing happens), or when there is
    /// no open log (`shareFinished` with an empty log id and "failed").
    Q_INVOKABLE bool shareOpenLog();

    /// History: shares a past log, loading it if it is not in memory. False when a share is under way (nothing
    /// happens) or `logId` is not an id (`shareFinished` "failed"). An id the store does not have is accepted and ends
    /// with `shareFinished` "failed" and no sheet, as does text that cannot be rendered.
    Q_INVOKABLE bool shareLog(const QString& logId);

    /// Says where the next share's popover points (an iPad shows the share sheet as a popover): the control that is
    /// about to ask for the share, as a rectangle in points in the window. It applies to the next `shareOpenLog` or
    /// `shareLog` only, whatever its outcome, so a share that does not set it never points at an old control.
    Q_INVOKABLE void setAnchor(qreal x, qreal y, qreal width, qreal height);

signals:
    void sharingChanged();

    /// The share ended. `outcome` is "shared", "cancelled", "unavailable" or "failed". Emitted once per share, when the
    /// sheet has ended and before the time is committed.
    void shareFinished(const QString& logId, const QString& outcome);

    /// The share time of the log is committed (the document, and through it the History row, shows it).
    void shareRecorded(const QString& logId);

    /// Recording the time failed after the text was shared; the log stays unshared, which errs on the side of one extra
    /// prompt. Sharing again records it.
    void writeFailed(const QString& operation, const QString& message);

private:
    using Loaded = Result<std::shared_ptr<LogDocument>, StorageError>;

    bool begin(LogId id, std::optional<ej::bridge::ShareAnchor> anchor);
    void acquired(LogId id, Loaded loaded);
    void sheet_ended(LogId id, const ej::bridge::ShareResult& result);
    void end(LogId id, const QString& outcome);
    void record(const std::shared_ptr<LogDocument>& document, LogId id, UtcMillis when);
    void recorded(const LogDocument* document, LogId id, const Result<void, StorageError>& result);
    void forget(const LogDocument* document);
    void recording_failed();

    Environment m_Environment;
    OpenLogSource& m_OpenLog;
    DocumentRegistry& m_Registry;
    const ConfigurationSource& m_Configuration;
    ej::bridge::Share& m_Share;
    bool m_Available;
    bool m_Sharing = false;
    /// Set by `setAnchor`, taken by the next share.
    std::optional<ej::bridge::ShareAnchor> m_Anchor;
    /// The anchor of the share under way, until its sheet is presented.
    std::optional<ej::bridge::ShareAnchor> m_RequestAnchor;

    /// The document whose text is on the sheet, from the render to the end of the sheet.
    std::shared_ptr<LogDocument> m_Shown;
    /// Documents with a `mark_shared` in flight, so that a past log is not dropped before its commit reports.
    std::vector<std::shared_ptr<LogDocument>> m_Recording;
};

} // namespace ej::viewmodel
