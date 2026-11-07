// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/move_only_function.hpp"

#include <QObject>
#include <QString>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ej::viewmodel
{

/// The undo toast (BRD-04, REV-03; docs/plan/stage-3.md, "UndoController"). One toast slot for the whole app, a window
/// of five seconds on the monotonic scheduler, and undo of the most recent action only.
///
/// It watches the documents it is given through `observe`. A committed action of the user opens or refreshes the toast:
///  - an error entry: "<name> logged at <time>", or "<name> logged x2 at <time>" for a repeat of the same type in the
///    same document while the toast was still showing (the window slides with every action);
///  - a note: "Note added at <time>"; its undo also hands the text back to the composer;
///  - a delete from the review screen: "Entry deleted"; its undo restores the entry.
/// The toast opens only after the commit, which is when the observers are told, so the window starts when the
/// confirmation is shown and not when the finger landed. A change with origin `UNDO` never opens a toast, and nothing
/// here fires a haptic. A new action replaces the toast and the earlier action stays committed but can no longer be
/// undone. When the window ends, on `dismiss`, or when the document is replaced by a new log, the toast closes.
///
/// `undo` soft-deletes the entry of the most recent tap or note (or restores the deleted one) with origin `UNDO`.
/// The toast closes when that commit succeeds, whatever the repeat count was. If the commit fails, `writeFailed` is
/// emitted, the toast stays until its window ends and the press can be repeated. A press while one is being committed
/// is ignored.
///
/// GUI thread only. A document passed to `observe` must be passed to `stop_observing` before it is destroyed, and the
/// controller must not outlive the documents it still observes.
class UndoController : public QObject, private LogObserver
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY toastChanged)
    Q_PROPERTY(QString text READ text NOTIFY toastChanged)
    Q_PROPERTY(int repeatCount READ repeat_count NOTIFY toastChanged)
    Q_PROPERTY(bool undoing READ undoing NOTIFY toastChanged)
    Q_PROPERTY(int generation READ generation NOTIFY toastChanged)
    Q_PROPERTY(int windowMs READ window_ms CONSTANT)

public:
    /// The window, from the moment a commit finishes.
    static constexpr std::chrono::milliseconds WINDOW{5000};

    /// `environment` gives the scheduler and the zones; `configuration` gives the time format in force when a line is
    /// made. Neither is owned.
    UndoController(const Environment& environment, const ConfigurationSource& configuration, QObject* parent = nullptr);
    ~UndoController() override;

    [[nodiscard]] bool active() const
    {
        return m_Active;
    }
    /// The toast's line; empty when there is no toast.
    [[nodiscard]] QString text() const
    {
        return m_Text;
    }
    /// 1 when the toast is not a repeat, and when there is no toast.
    [[nodiscard]] int repeat_count() const
    {
        return m_Active ? m_Repeat : 1;
    }
    /// True while the undo of the toast is being committed.
    [[nodiscard]] bool undoing() const
    {
        return m_Undoing;
    }
    /// Goes up by one whenever the window restarts, so that the bar in QML restarts.
    [[nodiscard]] int generation() const
    {
        return m_Generation;
    }
    [[nodiscard]] int window_ms() const
    {
        return static_cast<int>(WINDOW.count());
    }

    /// Undoes the action of the toast. False when there is no toast, the window has ended, an undo is being committed
    /// already, or the document refused the command; true when the undo has been queued.
    Q_INVOKABLE bool undo();

    /// Closes the toast. The action stays committed and can no longer be undone.
    Q_INVOKABLE void dismiss();

    void observe(LogDocument& document);
    void stop_observing(LogDocument& document);

    /// Where the text of an undone note goes: the composer. Called once, after the undo has committed.
    void set_note_undo_target(MoveOnlyFunction<void(const std::string&)> target);

signals:
    /// Any change of the toast: it opened, was refreshed, started or ended an undo, or closed.
    void toastChanged();

    /// An undo could not be saved. `operation` is "undo"; `message` is plain English and holds no entry text.
    void writeFailed(const QString& operation, const QString& message);

private:
    enum class Kind
    {
        TAP,
        NOTE,
        DELETED
    };

    void on_log_changed(const LogDocument& document, const LogChange& change) override;

    void open_for_added(LogDocument& document, const Entry& entry);
    void open_toast(Kind kind, LogDocument& document, EntryId entry, QString text);
    void close_toast();
    void expire_if_due();
    void finish_undo(std::uint64_t serial, bool is_note, const std::string& note_text,
                     const Result<void, StorageError>& result);

    [[nodiscard]] LogDocument* observed(const LogDocument& document) const;
    [[nodiscard]] std::string time_text(const Timestamp& when) const;
    [[nodiscard]] std::string type_name(const ErrorEntry& entry) const;

    Environment m_Environment;
    const ConfigurationSource& m_Configuration;
    std::vector<LogDocument*> m_Documents;
    MoveOnlyFunction<void(const std::string&)> m_NoteTarget;

    bool m_Active = false;
    bool m_Undoing = false;
    Kind m_Kind = Kind::TAP;
    LogDocument* m_Document = nullptr;
    EntryId m_Entry;
    ErrorTypeId m_ErrorType;
    int m_Repeat = 1;
    QString m_Text;
    std::string m_NoteText;
    int m_Generation = 0;
    std::uint64_t m_Serial = 0; // names the toast, so that a late result or timer is not applied to the next one
    std::chrono::milliseconds m_Deadline{0};
    TimerId m_Timer = NO_TIMER;

    // Completions hold a weak reference to this; a controller that has gone is never called back.
    std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
};

} // namespace ej::viewmodel
