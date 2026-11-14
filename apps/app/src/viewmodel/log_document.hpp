// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/header_field.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/stored_log.hpp"
#include "error_journaling/timestamp.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/move_only_function.hpp"
#include "viewmodel/observer_list.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ej::viewmodel
{

/// Why `LogDocument` refused a command before it was queued (or, for a command that waited, when it reached the front
/// of the queue).
struct DocumentError
{
    enum class Code
    {
        NOT_READY,  // the document has no log yet (it is waiting for `load`)
        QUEUE_FULL, // 64 commands are waiting already; nothing was dropped, the caller shows a message
        REJECTED    // the rules of the log refuse the command; `m_Log`, `m_Header` or `m_Message` say why
    };

    Code m_Code = Code::REJECTED;
    std::optional<Log::Error> m_Log;     // set when the log refused it: an unknown or a duplicate entry id
    std::optional<HeaderError> m_Header; // set when a header rule refused it: an invalid or an unchanged value
    std::string m_Message;               // plain English for the screen to show inline; never holds entry text
};

/// The owner of one log in the view-model layer (docs/plan/stage-3.md, "LogDocument and the registry"). It holds the
/// mirror of the log (a `StoredLog` by value) that the models read on the GUI thread, and it is the only code that
/// changes that mirror or writes that log. The open log and a past log opened for review behave the same, so History,
/// Review and Share work on one document per log and cannot overwrite each other's edits.
///
/// A command is validated against the mirror when it is submitted, which gives the caller its immediate answer. It
/// then waits in a FIFO of at most `MAX_WAITING` commands, and one command at a time is in flight: when it reaches the
/// front it is validated again against the mirror as it then is, and only then does its job go to the gateway. A
/// command that fails the second check reports through its `Done` and the queue carries on. The mirror changes, and
/// the observers are told, only when a commit has succeeded, so the screen never shows what the store does not have.
///
/// The mirror is changed with `apply_record`, the function the repository applies to its own copy, so the two cannot
/// disagree about what a command does.
///
/// GUI thread only. Not copyable and not movable: observers and queued jobs refer to it. Make one with
/// `std::make_shared` (the `DocumentRegistry` does).
class LogDocument
{
public:
    /// Commands that may wait behind the one in flight. The 65th is refused with `QUEUE_FULL`.
    static constexpr std::size_t MAX_WAITING = 64;

    using Origin = LogChange::Origin;

    /// Runs once on the GUI thread, after the commit or the failure. Observers are told before it, and only after a
    /// commit. It does not run if the document has been destroyed by then.
    using Done = MoveOnlyFunction<void(const Result<void, StorageError>&)>;

    /// A document for a log that is loaded: `ready()` at once.
    LogDocument(const Environment& environment, StorageGateway& gateway, StoredLog loaded);

    /// A document for the log `id` that has not been loaded yet. Every command is refused with `NOT_READY` until
    /// `load` has been called.
    LogDocument(const Environment& environment, StorageGateway& gateway, LogId id);

    ~LogDocument();

    LogDocument(const LogDocument&) = delete;
    LogDocument& operator=(const LogDocument&) = delete;

    // What the models read.

    [[nodiscard]] LogId id() const;
    [[nodiscard]] bool ready() const;

    /// The mirror. Empty (a log with no header values) while the document is not ready.
    [[nodiscard]] const Log& log() const;
    [[nodiscard]] const LogMeta& meta() const;
    [[nodiscard]] std::optional<UtcMillis> last_shared() const;
    [[nodiscard]] bool archived() const;

    /// Commands in flight or waiting. The services' `pendingWrites` is built from it.
    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] std::size_t waiting() const;

    /// Observers are told in the order they were added, after the mirror has changed. A document does not hold on to
    /// an observer: remove it before it is destroyed.
    void add_observer(LogObserver& observer);
    void remove_observer(LogObserver& observer);

    // Taking a log from the store. These change the mirror without a write.

    /// Puts `stored` in the document and tells the observers once: `LOADED`, however many entries the log has. For a
    /// document made without a log.
    void load(StoredLog stored);

    /// Puts another log in the document (a new day took the place of the open log) and tells the observers once:
    /// `REPLACED`. The id of the document is the id of the new log from now on. Commands that were waiting belonged to
    /// the old log: each is reported through its `Done` as failed and none is written. The command in flight, if any,
    /// finishes against the old log and the mirror ignores its result.
    void replace(StoredLog stored);

    /// Records that the log has been archived, which the owner of the new-day start does when that write has
    /// committed. An archived log takes edits, deletes and shares (they are journalled, Stage 3 decision 11) but no
    /// new entry and no header change. Nobody is told: it changes no entry and no header.
    void set_archived();

    // Commands. Each validates against the mirror, queues one job and returns. The result of the call is the
    // immediate answer; `done` reports the commit.

    /// Adds an error or a note entry. An error entry needs its type's name snapshot (`m_ErrorTypeLabel`). A header
    /// change goes through `change_header`. `origin` is passed on to the observers.
    [[nodiscard]] Result<void, DocumentError> add_entry(Entry entry, Origin origin, Done done);

    /// A review edit: the entry `id` takes the content of `entry` (time, kind and body), keeps its id and its deleted
    /// flag, and its edit time becomes the time of this call. `entry.m_Id` is ignored.
    [[nodiscard]] Result<void, DocumentError> edit_entry(EntryId id, Entry entry, Done done);

    /// An edit that is worked out when the command reaches the front of the queue, from the entry as it is then: commands
    /// before it have committed, so an edit that was submitted earlier and is not through yet is not overwritten (a note
    /// held onto an entry and the people put on it a moment later both stay). `mutate` changes the copy it is given and
    /// returns nothing, or a plain sentence that refuses the edit. It is called on the GUI thread, once when the command
    /// is submitted (on the entry as it is) and once at the front of the queue, so it must give the same answer on the
    /// same entry. Time, id and deleted flag follow `edit_entry`.
    using Mutator = std::function<std::optional<std::string>(Entry&)>;
    [[nodiscard]] Result<void, DocumentError> edit_entry(EntryId id, Mutator mutate, Done done);

    /// A soft delete (`deleted`) or a restore. Nothing is ever removed (Stage 3 decision 6). `origin` is `UNDO` for
    /// the undo of the last action.
    [[nodiscard]] Result<void, DocumentError> set_deleted(EntryId id, bool deleted, Origin origin, Done done);

    /// Correct (HDR-07): fixes the value of the field in force and writes no entry. An empty text clears a field that
    /// has no change after it.
    [[nodiscard]] Result<void, DocumentError> correct_header(HeaderField field, std::string value, Done done);

    /// Change (HDR-05): a header change entry `entry` captured at `when`. The value must be valid for the field and
    /// differ from the one in force at `when`.
    [[nodiscard]] Result<void, DocumentError> change_header(HeaderField field, std::string value, Timestamp when,
                                                            EntryId entry, Done done);

    /// Fixes the new value of one header change entry (an edit in the review screen). The store has no operation for
    /// a correction that is not the latest of its field, so it is written as an edit and the entry's edit time is set.
    [[nodiscard]] Result<void, DocumentError> correct_header_change(EntryId change, std::string value, Done done);

    /// Records that the log was shared at `when`. Allowed on an archived log.
    [[nodiscard]] Result<void, DocumentError> mark_shared(UtcMillis when, Done done);

private:
    struct Command
    {
        enum class Op
        {
            ADD_ENTRY,
            EDIT_ENTRY,
            SET_DELETED,
            CORRECT_HEADER,
            CHANGE_HEADER,
            CORRECT_CHANGE,
            MARK_SHARED
        };

        Op m_Op = Op::ADD_ENTRY;
        Origin m_Origin = Origin::USER;
        Entry m_Entry;                                  // ADD_ENTRY, EDIT_ENTRY, CHANGE_HEADER
        Mutator m_Mutate;                               // EDIT_ENTRY made with a mutator: the entry is worked out in make_body
        EntryId m_Target;                               // SET_DELETED, CORRECT_CHANGE
        bool m_Deleted = false;                         // SET_DELETED
        HeaderField m_Field = HeaderField::UNIT_NUMBER; // CORRECT_HEADER
        std::string m_Text;                             // CORRECT_HEADER, CORRECT_CHANGE
        UtcMillis m_Time;                               // the edit time or the share time, taken at submission
        Done m_Done;
    };

    [[nodiscard]] Result<void, DocumentError> submit(Command command);
    [[nodiscard]] Result<JournalBody, DocumentError> make_body(const Command& command) const;
    [[nodiscard]] Result<void, DocumentError> trial(const JournalBody& body) const;
    [[nodiscard]] static LogChange change_for(const Command& command);

    void pump();
    void start(Command command, JournalBody body);
    void finish(const std::weak_ptr<void>& alive, std::uint64_t generation, const LogChange& change,
                const JournalBody& body, Done& done, Result<void, StorageError> result);
    void set_mirror(StoredLog stored);

    Environment m_Environment;
    StorageGateway& m_Gateway;
    StoredLog m_Stored;
    bool m_Ready = false;

    ObserverList<LogObserver> m_Observers;
    std::deque<Command> m_Waiting;
    bool m_InFlight = false;
    std::uint64_t m_Generation = 0; // changes when `replace` swaps the log, so a late result is not applied to it

    // Completions hold a weak reference to this; a document that has gone is never called back.
    std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
};

} // namespace ej::viewmodel
