// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/board_model.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/share_service.hpp"

#include <QObject>
#include <QString>

namespace ej::viewmodel
{

/// The new-day prompt (DAY-01; docs/plan/stage-3.md, "DayWatcher"). When the app opens, or returns to the foreground,
/// on a later calendar day than the open log's, it offers to start a new log, naming the new date, and says whether the
/// open log has been shared.
///
/// The day logic is the core's: `day_prompt(open_log_date, now, zones)`, with `now` in the zone the device is in now,
/// so a flight west keeps the log until local midnight and a 23- or a 25-hour day rolls over at local midnight.
///
/// There is no timer. The prompt is raised by `check()` and by nothing else, and the shell calls `check()` on launch
/// and on `Qt::ApplicationActive` (Stage 5). A session that is still open at midnight keeps logging into the open log
/// until the next foreground, and a prompt that was kept open (`keepOpen`) is raised again by the next `check()`.
///
/// `check()` before the log has loaded does nothing now and runs once the session becomes `OPEN`. A second `check()`
/// that finds the same prompt changes nothing and emits nothing. A zone the zone provider does not know hides the
/// prompt and is reported once (`checkFailed`), until a check succeeds.
///
/// While the prompt is up, the entry count and `unshared` follow the open log, so a share that is recorded turns
/// `unshared` off at once, an entry added past midnight is counted, and a new log that takes the place of the open
/// one (from here or from anywhere) takes the prompt down. The texts and the count keep their last values while the
/// prompt is hidden, so that a screen that fades out does not go blank; they are made again when the prompt is raised.
///
/// `unshared` is true when the open log has at least one live entry and either has no share time or has changed since
/// its last share (`is_unshared`): sharing can happen several times a day, so a share is not the end of the log, and
/// an entry added, edited or deleted afterwards brings the offer back. Copying the text does not count as a share. An
/// empty log has nothing to share, so it is never called unshared. `changedSinceShare` is `unshared` for a log that was
/// shared earlier, so the text can say "changed after it was last shared" and not "hasn't been shared".
///
/// The prompt is offered on the first check on a later date than the open log's, whatever the gap: a facilitator who
/// works on Mondays and Fridays is asked on Friday about Monday's log.
///
/// GUI thread only. No I/O of its own: a new log is `LogSession::startNewLog` and sharing is
/// `ShareService::shareOpenLog`. Nothing is owned; the session and the service must outlive the watcher.
class DayWatcher : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool promptVisible READ prompt_visible NOTIFY promptChanged)
    Q_PROPERTY(QString newDateText READ new_date_text NOTIFY promptChanged)           // "Sunday 8 November"
    Q_PROPERTY(QString openLogDayText READ open_log_day_text NOTIFY promptChanged)    // "Saturday"
    Q_PROPERTY(int openLogEntryCount READ open_log_entry_count NOTIFY promptChanged)  // live entries, every kind
    Q_PROPERTY(bool unshared READ unshared NOTIFY promptChanged)
    Q_PROPERTY(bool changedSinceShare READ changed_since_share NOTIFY promptChanged)

public:
    DayWatcher(const Environment& environment, LogSession& session, ShareService& share, QObject* parent = nullptr);
    ~DayWatcher() override;

    DayWatcher(const DayWatcher&) = delete;
    DayWatcher& operator=(const DayWatcher&) = delete;

    [[nodiscard]] bool prompt_visible() const { return m_Prompt.m_Visible; }
    /// The date of the day the prompt offers, from the English (Australia) locale: "Sunday 8 November".
    [[nodiscard]] QString new_date_text() const { return m_Prompt.m_NewDateText; }
    /// The weekday of the open log, in the same locale: "Saturday".
    [[nodiscard]] QString open_log_day_text() const { return m_Prompt.m_OpenLogDayText; }
    /// The live (not deleted) entries the new log would archive, every kind.
    [[nodiscard]] int open_log_entry_count() const { return m_Prompt.m_EntryCount; }
    [[nodiscard]] bool unshared() const { return m_Prompt.m_Unshared; }
    [[nodiscard]] bool changed_since_share() const
    {
        return m_Prompt.m_ChangedSinceShare;
    }

    /// "Start a new log": `LogSession::startNewLog`. False, with the prompt kept, when the prompt is not up or the
    /// session refuses (it is not `OPEN`, or a new log is on its way). The prompt comes down when the new log has taken
    /// the place of the open one, which is after the commit; a write that fails leaves it up.
    Q_INVOKABLE bool acceptNewLog();

    /// "Keep the open log": hides the prompt and changes nothing. The next `check()` raises it again.
    Q_INVOKABLE void keepOpen();

    /// "Share the open log first": `ShareService::shareOpenLog`. The prompt stays up whatever the outcome; `unshared`
    /// turns off when the share has been recorded. False when a share is under way already.
    Q_INVOKABLE bool shareFirst();

public slots:
    /// Looks at the clock and the zone and raises, updates or lowers the prompt. Stage 5 connects it to the launch and
    /// to the app becoming active. Idempotent.
    void check();

signals:
    /// Any change of the prompt: it appeared, went, or one of its texts, the count or `unshared` changed.
    void promptChanged();

    /// The date could not be worked out (the device zone is not one the zone provider knows). The prompt is hidden.
    /// Emitted once until a check works again; `message` is plain English.
    void checkFailed(const QString& message);

private:
    struct Prompt
    {
        bool m_Visible = false;
        QString m_NewDateText;
        QString m_OpenLogDayText;
        int m_EntryCount = 0;
        bool m_Unshared = false;
        bool m_ChangedSinceShare = false;

        friend bool operator==(const Prompt&, const Prompt&) = default;
    };

    void run_check();
    void on_session_state_changed();
    void on_open_log_change(const LogChange& change);
    void refresh_content();
    void hide();
    void show(Prompt prompt);
    void report_failure();

    /// The open log when the session is `OPEN` and has one.
    [[nodiscard]] const LogDocument* ready_document() const;

    Environment m_Environment;
    LogSession& m_Session;
    ShareService& m_Share;

    Prompt m_Prompt;
    bool m_CheckWaiting = false;     // `check()` was called before the log had loaded
    bool m_FailureReported = false;  // `checkFailed` was emitted and no check has worked since

    OpenLogFollower m_Log;
};

}  // namespace ej::viewmodel
