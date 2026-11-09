// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/header_field.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/recovery_report.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/timestamp.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/feedback_sink.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/move_only_function.hpp"
#include "viewmodel/observer_list.hpp"
#include "viewmodel/open_log_source.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ej::viewmodel
{

/// What start-up hands to the session: the composition root has opened the store on the storage thread and recovery
/// has run already (Stage 2, 2.4). The session reads the report once, for `recoveryNotice`.
struct StartupResult
{
    RecoveryReport m_Recovery;
};

/// Owns the open log (docs/plan/stage-3.md, "LogSession"). A QObject facade over a `LogDocument`, which does the work,
/// so that a past log opened from History is edited by the same code. QML sees the header in force now, the note
/// draft, the number of writes waiting, and the invokables that record an error, add a note, correct or change the
/// header and start a new log.
///
/// Every command is validated on the GUI thread against the mirror, given its time and its ids there, queued through
/// the document and answered at once with a `bool`. What follows the commit is the document's: the observers are told,
/// then this class gives the confirmation (`FeedbackSink`, `entryRecorded`) and nowhere else, so a failed write gives
/// none of it and emits `writeFailed` instead. The impact haptic is not here: it fires on touch-down, in QML.
///
/// Taps and notes are accepted whatever the header holds (HDR-06), and refused while the session is not `OPEN`.
///
/// A new log (DAY-01 and DAY-02) takes the place of the open one in the same document: `LogDocument::replace`, so the
/// observers of the document are told `REPLACED` once and `open_log()` keeps returning the same pointer. The archived
/// log is a different document, found through the registry by its id.
///
/// GUI thread only.
class LogSession : public QObject, public OpenLogSource
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // The header in force now (HeaderHistory::at(now)), for the header card. The export's header block is the start
    // values.
    Q_PROPERTY(QString facilitator READ facilitator NOTIFY headerChanged)
    Q_PROPERTY(QString unitText READ unit_text NOTIFY headerChanged)         // "UNIT-27" or "UNIT-27-prod", or empty
    Q_PROPERTY(QString unitDigits READ unit_digits NOTIFY headerChanged)     // "27", or empty: the number alone
    // "prod", or empty when the unit has no environment (and when it has no number): the environment alone (PRF-11).
    Q_PROPERTY(QString unitEnvironment READ unit_environment NOTIFY headerChanged)
    Q_PROPERTY(QString locationText READ location_text NOTIFY headerChanged) // the name, resolved from the id
    Q_PROPERTY(QString versionText READ version_text NOTIFY headerChanged)
    Q_PROPERTY(QString dateText READ date_text NOTIFY headerChanged) // "Sat 7 Nov"
    Q_PROPERTY(
        QStringList missingFields READ missing_fields NOTIFY headerChanged) // HDR-06 banner: keys of empty fields
    // What a new log would archive: the live (not deleted) entries of the open log, every kind. It follows commits and
    // undo, so it is notified with the header: any change of the log is a `headerChanged`, except a share.
    Q_PROPERTY(int liveEntryCount READ live_entry_count NOTIFY headerChanged)
    // True when the open log has at least one live entry and either has never been shared or has changed since its last
    // share (an entry added, edited, deleted or restored, or a header corrected): starting a new log would archive work
    // that has not left the phone ("...and hasn't been shared", with "Share this log first"). Sharing can happen
    // several times a day, so a share is not the end of the log. Only a share clears it: copying the text does not. The
    // rule of `DayWatcher::unshared` and History (`is_unshared`). Not true for an empty log, which has nothing to
    // share. Follows commits, undo, a share and a new log.
    Q_PROPERTY(bool unshared READ unshared NOTIFY unsharedChanged)
    // True when `unshared` and the log was shared earlier: it has changed since, which the texts say apart from "never
    // shared".
    Q_PROPERTY(bool changedSinceShare READ changed_since_share NOTIFY unsharedChanged)
    // The clock in the device zone as seconds since local midnight, for the "now" default of the version sheet.
    Q_PROPERTY(int nowLocalSeconds READ now_local_seconds)
    Q_PROPERTY(QString draft READ draft WRITE set_draft NOTIFY draftChanged) // NOTE-03
    Q_PROPERTY(int pendingWrites READ pending_writes NOTIFY pendingWritesChanged)
    Q_PROPERTY(QString recoveryNotice READ recovery_notice CONSTANT) // DS-04, empty when nothing was recovered

public:
    enum class State
    {
        LOADING,
        NEEDS_SETUP,
        OPEN,
        FAILED
    };
    Q_ENUM(State)

    /// The note is stored after this many quiet milliseconds (no `DraftStore` exists to debounce it, Stage 2, 2.6).
    static constexpr std::chrono::milliseconds DRAFT_DEBOUNCE{300};

    /// How often a new log that waits for earlier writes looks again.
    static constexpr std::chrono::milliseconds NEW_LOG_POLL{20};

    /// Nothing is owned. All must outlive the session. Call `load()` once the objects around it are connected.
    LogSession(const Environment& environment, StorageGateway& gateway, DocumentRegistry& registry,
               ConfigurationSource& configuration, FeedbackSink& feedback, StartupResult startup,
               QObject* parent = nullptr);
    ~LogSession() override;

    LogSession(const LogSession&) = delete;
    LogSession& operator=(const LogSession&) = delete;

    /// Reads the open log and the draft through the gateway; the state is `LOADING` until that has completed. With no
    /// open log and a valid configuration it starts today's log (HDR-01: the facilitator of the preferences, today's
    /// local date, three empty fields); with no configuration the state becomes `NEEDS_SETUP` and the first log is
    /// started when the configuration comes. Not QML-facing: the composition root calls it.
    void load();

    // Properties.

    [[nodiscard]] State state() const;
    [[nodiscard]] QString facilitator() const;
    [[nodiscard]] QString unit_text() const;
    [[nodiscard]] QString unit_digits() const;
    [[nodiscard]] QString unit_environment() const;
    [[nodiscard]] bool unshared() const
    {
        return m_Unshared;
    }
    [[nodiscard]] bool changed_since_share() const
    {
        return m_ChangedSinceShare;
    }
    [[nodiscard]] QString location_text() const;
    [[nodiscard]] QString version_text() const;
    [[nodiscard]] QString date_text() const;
    [[nodiscard]] QStringList missing_fields() const;
    [[nodiscard]] int live_entry_count() const;
    [[nodiscard]] int now_local_seconds() const;
    [[nodiscard]] QString draft() const;
    void set_draft(const QString& text);
    [[nodiscard]] int pending_writes() const;
    [[nodiscard]] QString recovery_notice() const;

    // Invokables. Each returns whether the command was accepted. A command accepted is committed later and reported
    // through `entryRecorded` or `writeFailed`.

    /// BRD-02. False for a type that is unknown or retired, and when the state is not `OPEN`.
    Q_INVOKABLE bool recordError(const QString& errorTypeId);

    /// NOTE-01. The text is trimmed; empty text and text over 10,000 characters are refused.
    Q_INVOKABLE bool addNote(const QString& text);

    /// NOTE-04. The template id and the value are stored; only the renderer makes the sentence.
    Q_INVOKABLE bool addTemplatedNote(const QString& templateId, const QString& value);

    /// The inline check of HDR-04 and HDR-02, "" when the value is valid for the field. `fieldKey` is the core's
    /// `header_field_key`: `unit_number`, `location` or `software_version`.
    Q_INVOKABLE QString validateHeaderValue(const QString& fieldKey, const QString& text) const;

    /// The inline check of the unit as the person builds it (HDR-02, PRF-11): `digits` as typed and `environment` as
    /// picked, "" for none. "" when the pair is valid. The environment must be one of the configured choices or the one
    /// in force already (a setup file that changes the list does not make a log's own environment invalid).
    Q_INVOKABLE QString validateUnit(const QString& digits, const QString& environment) const;

    /// A Correct (HDR-07) of the unit from its two parts: the same as `correctHeader("unit_number", ...)` with the
    /// stored text "27-prod" or "27" put together here. False for a pair `validateUnit` refuses.
    Q_INVOKABLE bool correctUnit(const QString& digits, const QString& environment);

    /// HDR-07 Correct: fixes the value in force and writes no entry. A first fill of an empty field is also a Correct.
    Q_INVOKABLE bool correctHeader(const QString& fieldKey, const QString& text);

    /// HDR-07 Change: an entry at `localSeconds` (seconds since local midnight of the log's date, 0 to 86399),
    /// resolved in the device zone with `TimeZoneProvider::to_utc`, so an instant in the missing or the repeated hour
    /// behaves as Stage 1 defined. Refused for an invalid value, a value equal to the one in force at that instant, or
    /// a `localSeconds` out of range. The entry takes the device zone.
    Q_INVOKABLE bool changeHeader(const QString& fieldKey, const QString& text, int localSeconds);

    /// The same change at a UTC instant; 0 means now. For tests and for callers that have an instant already.
    Q_INVOKABLE bool changeHeaderAtUtc(const QString& fieldKey, const QString& text, qint64 atUtcMs = 0);

    /// What `changeHeader` would record, as the exact log line (`render_entry_line` of the entry on a scratch copy of
    /// the log), or the plain refusal message of `validateHeaderValue`, or for a value equal to the one in force. It
    /// writes nothing and makes no id. The line shows the old value, the new value and the time of the change.
    Q_INVOKABLE QString previewHeaderChange(const QString& fieldKey, const QString& text, int localSeconds) const;

    /// DAY-01 and DAY-02: archives the open log and starts one with the facilitator and today's local date and no
    /// other value, in one repository call (`start_new_log`), after the writes that are waiting. It is the entry point
    /// for a log for another unit or site as well as the answer to the new-day prompt. While it is in progress every
    /// other command is refused (`writeFailed`), because a tap would otherwise land in the log being archived. The
    /// draft is not touched. False when the state is not `OPEN` or a new log is on its way.
    Q_INVOKABLE bool startNewLog();

    /// Stores the draft now if the debounce has not yet (Stage 5 calls it on going inactive).
    Q_INVOKABLE void flushDraft();

    // OpenLogSource: the models follow the open log through it.

    [[nodiscard]] LogDocument* open_log() const override;
    void add_observer(OpenLogObserver& observer) override;
    void remove_observer(OpenLogObserver& observer) override;

signals:
    void stateChanged();
    void headerChanged();
    void draftChanged();
    void pendingWritesChanged();
    void entryRecorded(const QString& entryId); // after the commit; BRD-07 opens the note sheet from it
    void unsharedChanged();
    /// A tap on an error type could not be saved: the commit failed or too many writes were waiting. Emitted just after
    /// the `writeFailed("recordError", ...)` of the same failure, which is unchanged, so a screen that wants the name
    /// of the type listens to this one. `title` is "Mute Delay wasn't saved" and `message` is "Nothing was added to the
    /// log. Tap Mute Delay again to log it.", with the typographic apostrophe, both through `tr`.
    void recordFailed(const QString& errorTypeId, const QString& errorTypeName, const QString& title,
                      const QString& message);
    void newLogStarted(); // Stage 4 opens Today's details
    void writeFailed(const QString& operation, const QString& message);

private:
    class Listener;
    struct Loaded;

    /// Told, once the commit has been dealt with, whether it succeeded.
    using Finished = MoveOnlyFunction<void(bool succeeded)>;

    // Loading.
    void start_load();
    void finish_load(Result<Loaded, StorageError> result);
    void set_state(State state);

    // Commands.
    [[nodiscard]] bool can_write(const char* operation);
    [[nodiscard]] LogDocument::Done make_done(const char* operation, Finished finished);
    [[nodiscard]] bool settle(const char* operation, const Result<void, DocumentError>& submitted);
    void command_finished(const char* operation, const Result<void, StorageError>& result, Finished& finished);
    void fail_write(const char* operation, const QString& message);
    [[nodiscard]] bool add_note_entry(const char* operation, NoteEntry body, const QString& submitted, bool free_text);

    // Header.
    [[nodiscard]] HeaderValues header_in_force() const;
    [[nodiscard]] QString header_message(const HeaderError& error) const;
    [[nodiscard]] std::optional<Timestamp> timestamp_for_local(int local_seconds) const;
    [[nodiscard]] bool change_header_at(const QString& field_key, const QString& text, Timestamp when);

    // The unshared flag and the failure of a tap.
    void update_unshared();
    void announce_record_failed(const QString& type_id, const QString& type_name);

    // Draft.
    void arm_draft_timer();
    void write_draft_now();
    void note_finished(bool succeeded, bool free_text, const QString& submitted, std::uint64_t revision);

    // The new log.
    void attempt_new_log(LogId new_id, HeaderValues header);

    [[nodiscard]] Timestamp now_timestamp() const;
    [[nodiscard]] std::chrono::year_month_day today() const;
    [[nodiscard]] std::string facilitator_for_new_log() const;

    Environment m_Environment;
    StorageGateway& m_Gateway;
    DocumentRegistry& m_Registry;
    ConfigurationSource& m_Configuration;
    FeedbackSink& m_Feedback;
    QString m_RecoveryNotice;

    State m_State = State::LOADING;
    bool m_Loading = false; // a load job is in flight
    std::shared_ptr<LogDocument> m_Document;
    ObserverList<OpenLogObserver> m_OpenLogObservers;
    std::unique_ptr<Listener> m_Listener;

    bool m_Unshared = false;
    bool m_ChangedSinceShare = false;
    int m_OwnPending = 0; // commands this session submitted and has not heard back from
    bool m_StartingNewLog = false;
    TimerId m_NewLogTimer = NO_TIMER;

    QString m_Draft;
    bool m_DraftDirty = false;         // m_Draft has not been handed to the store yet
    std::uint64_t m_DraftRevision = 0; // changes with every edit of the draft
    TimerId m_DraftTimer = NO_TIMER;

    // Completions hold a weak reference to this; a session that has gone is never called back.
    std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
};

} // namespace ej::viewmodel
