// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/log_document.hpp"

#include "error_journaling/header_history.hpp"
#include "error_journaling/journal_record.hpp"
#include "error_journaling/log_repository.hpp"

#include <utility>
#include <variant>

namespace ej::viewmodel
{

namespace
{

using Failure = std::unexpected<DocumentError>;

Failure rejected(std::string message)
{
    return Failure(DocumentError{DocumentError::Code::REJECTED, std::nullopt, std::nullopt, std::move(message)});
}

Failure rejected_by_log(Log::Error error, std::string message)
{
    return Failure(DocumentError{DocumentError::Code::REJECTED, error, std::nullopt, std::move(message)});
}

std::string header_message(const HeaderError& error)
{
    switch (error.m_Kind)
    {
    case HeaderError::Kind::INVALID_UNIT_NUMBER:
        return "That is not a valid unit number.";
    case HeaderError::Kind::INVALID_SOFTWARE_VERSION:
        return "That is not a valid software version.";
    case HeaderError::Kind::INVALID_EXTRA_VERSIONS:
        return "That is not a valid list of versions.";
    case HeaderError::Kind::EMPTY_LOCATION:
        return "The location cannot be empty.";
    case HeaderError::Kind::UNCHANGED:
        return "That is the value already in force.";
    case HeaderError::Kind::UNKNOWN_ENTRY:
        return "That entry is not in this log.";
    case HeaderError::Kind::NOT_A_HEADER_CHANGE:
        return "That entry is not a header change.";
    case HeaderError::Kind::DUPLICATE_ID:
        return "An entry with that id exists already.";
    }
    return "The header value is refused.";
}

Failure rejected_by_header(const HeaderError& error)
{
    return Failure(DocumentError{DocumentError::Code::REJECTED, std::nullopt, error, header_message(error)});
}

/// What `done` receives for a command that was refused after it had been accepted.
StorageError as_storage_error(const DocumentError& error)
{
    return make_error(StorageErrorKind::INVALID_ARGUMENT, error.m_Message);
}

/// Runs a `Done`, if the caller gave one.
void report(LogDocument::Done& done, const Result<void, StorageError>& result)
{
    if (done)
    {
        done(result);
    }
}

/// Sends a body to the repository call that makes it. This is the one place that maps a command to a store operation;
/// the mirror applies the same body with `apply_record`.
class Writer
{
public:
    Writer(LogRepository& repository, LogId log) : m_Repository(repository), m_Log(log) {}

    Result<void, StorageError> operator()(const AddEntryBody& body) const
    {
        return m_Repository.add_entry(m_Log, body.m_Entry);
    }
    Result<void, StorageError> operator()(const EditEntryBody& body) const
    {
        return m_Repository.edit_entry(m_Log, body.m_Entry, body.m_EditedUtc);
    }
    Result<void, StorageError> operator()(const SetDeletedBody& body) const
    {
        return m_Repository.set_entry_deleted(m_Log, body.m_Entry, body.m_Deleted);
    }
    Result<void, StorageError> operator()(const CorrectHeaderBody& body) const
    {
        return m_Repository.correct_header(m_Log, body.m_Field, body.m_Value);
    }
    Result<void, StorageError> operator()(const MarkSharedBody& body) const
    {
        return m_Repository.mark_shared(m_Log, body.m_Utc);
    }
    Result<void, StorageError> operator()(const CreateLogBody&) const
    {
        return unsupported();
    }
    Result<void, StorageError> operator()(const ArchiveBody&) const
    {
        return unsupported();
    }
    Result<void, StorageError> operator()(const VoidBody&) const
    {
        return unsupported();
    }

private:
    static Result<void, StorageError> unsupported()
    {
        return fail(make_error(StorageErrorKind::INTERNAL, "a log document does not make this operation"));
    }

    LogRepository& m_Repository;
    LogId m_Log;
};

Result<void, StorageError> write(LogRepository& repository, LogId log, const JournalBody& body)
{
    return std::visit(Writer(repository, log), body);
}

StoredLog empty_stored_log(LogId id)
{
    return StoredLog{LogMeta{id, UtcMillis{}, std::nullopt, false, std::nullopt}, Log(HeaderValues{})};
}

} // namespace

LogDocument::LogDocument(const Environment& environment, StorageGateway& gateway, StoredLog loaded)
    : m_Environment(environment), m_Gateway(gateway), m_Stored(std::move(loaded)), m_Ready(true)
{
}

LogDocument::LogDocument(const Environment& environment, StorageGateway& gateway, LogId id)
    : m_Environment(environment), m_Gateway(gateway), m_Stored(empty_stored_log(id))
{
}

LogDocument::~LogDocument() = default;

LogId LogDocument::id() const
{
    return m_Stored.m_Meta.m_Id;
}

bool LogDocument::ready() const
{
    return m_Ready;
}

const Log& LogDocument::log() const
{
    return m_Stored.m_Log;
}

const LogMeta& LogDocument::meta() const
{
    return m_Stored.m_Meta;
}

std::optional<UtcMillis> LogDocument::last_shared() const
{
    return m_Stored.m_Meta.m_LastSharedUtc;
}

bool LogDocument::archived() const
{
    return m_Stored.m_Meta.m_Archived;
}

std::size_t LogDocument::pending() const
{
    return m_Waiting.size() + (m_InFlight ? 1 : 0);
}

std::size_t LogDocument::waiting() const
{
    return m_Waiting.size();
}

void LogDocument::add_observer(LogObserver& observer)
{
    m_Observers.add(observer);
}

void LogDocument::remove_observer(LogObserver& observer)
{
    m_Observers.remove(observer);
}

void LogDocument::set_mirror(StoredLog stored)
{
    m_Stored = std::move(stored);
    m_Ready = true;
}

void LogDocument::load(StoredLog stored)
{
    set_mirror(std::move(stored));
    const LogChange change{LogChange::Kind::LOADED, Origin::USER, std::nullopt};
    m_Observers.notify([this, &change](LogObserver& observer) { observer.on_log_changed(*this, change); });
}

void LogDocument::replace(StoredLog stored)
{
    const std::weak_ptr<void> alive = m_Alive;
    std::deque<Command> dropped = std::move(m_Waiting);
    m_Waiting.clear();
    ++m_Generation;
    set_mirror(std::move(stored));

    const LogChange change{LogChange::Kind::REPLACED, Origin::USER, std::nullopt};
    m_Observers.notify([this, &change](LogObserver& observer) { observer.on_log_changed(*this, change); });

    // The commands that were waiting were for the old log. None was written.
    const Result<void, StorageError> failure =
        fail(make_error(StorageErrorKind::NOT_OPEN_LOG, "the log was replaced before the command could be written"));
    for (Command& command : dropped)
    {
        if (alive.expired())
        {
            return;
        }
        report(command.m_Done, failure);
    }
}

void LogDocument::set_archived()
{
    m_Stored.m_Meta.m_Archived = true;
}

Result<void, DocumentError> LogDocument::add_entry(Entry entry, Origin origin, Done done)
{
    if (std::holds_alternative<HeaderChangeEntry>(entry.m_Body))
    {
        return rejected("A header change is made with change_header.");
    }
    Command command;
    command.m_Op = Command::Op::ADD_ENTRY;
    command.m_Origin = origin;
    command.m_Entry = std::move(entry);
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::edit_entry(EntryId id, Entry entry, Done done)
{
    entry.m_Id = id;
    Command command;
    command.m_Op = Command::Op::EDIT_ENTRY;
    command.m_Entry = std::move(entry);
    command.m_Time = m_Environment.m_Clock.now();
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::set_deleted(EntryId id, bool deleted, Origin origin, Done done)
{
    Command command;
    command.m_Op = Command::Op::SET_DELETED;
    command.m_Origin = origin;
    command.m_Target = id;
    command.m_Deleted = deleted;
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::correct_header(HeaderField field, std::string value, Done done)
{
    Command command;
    command.m_Op = Command::Op::CORRECT_HEADER;
    command.m_Field = field;
    command.m_Text = std::move(value);
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::change_header(HeaderField field, std::string value, Timestamp when,
                                                       EntryId entry, Done done)
{
    Command command;
    command.m_Op = Command::Op::CHANGE_HEADER;
    command.m_Entry.m_Id = entry;
    command.m_Entry.m_Captured = std::move(when);
    command.m_Entry.m_Body = HeaderChangeEntry{field, std::move(value)};
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::correct_header_change(EntryId change, std::string value, Done done)
{
    Command command;
    command.m_Op = Command::Op::CORRECT_CHANGE;
    command.m_Target = change;
    command.m_Text = std::move(value);
    command.m_Time = m_Environment.m_Clock.now();
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::mark_shared(UtcMillis when, Done done)
{
    Command command;
    command.m_Op = Command::Op::MARK_SHARED;
    command.m_Time = when;
    command.m_Done = std::move(done);
    return submit(std::move(command));
}

Result<void, DocumentError> LogDocument::submit(Command command)
{
    if (!m_Ready)
    {
        return Failure(
            DocumentError{DocumentError::Code::NOT_READY, std::nullopt, std::nullopt, "The log is not loaded yet."});
    }
    if (m_Waiting.size() >= MAX_WAITING)
    {
        return Failure(DocumentError{DocumentError::Code::QUEUE_FULL, std::nullopt, std::nullopt,
                                     "Too many changes are waiting to be saved. Try again in a moment."});
    }
    // The first check: the immediate answer. The body is made again when the command reaches the front.
    if (auto body = make_body(command); !body)
    {
        return Failure(std::move(body.error()));
    }
    m_Waiting.push_back(std::move(command));
    pump();
    return {};
}

Result<void, DocumentError> LogDocument::trial(const JournalBody& body) const
{
    JournalRecord record;
    record.m_Log = m_Stored.m_Meta.m_Id;
    record.m_Body = body;
    StoredLog copy = m_Stored;
    if (auto applied = apply_record(copy, record); !applied)
    {
        return rejected(describe(applied.error()));
    }
    return {};
}

Result<JournalBody, DocumentError> LogDocument::make_body(const Command& command) const
{
    const Log& log = m_Stored.m_Log;
    switch (command.m_Op)
    {
    case Command::Op::ADD_ENTRY:
    case Command::Op::CHANGE_HEADER:
    {
        const Entry& entry = command.m_Entry;
        if (m_Stored.m_Meta.m_Archived)
        {
            return rejected("This log is archived and takes no new entries.");
        }
        if (log.find(entry.m_Id) != nullptr)
        {
            return rejected_by_log(Log::Error::DUPLICATE_ID, "An entry with that id exists already.");
        }
        if (auto valid = validate_entry(entry); !valid)
        {
            return rejected(describe(valid.error()));
        }
        if (log.entries().size() >= MAX_ENTRIES_PER_LOG)
        {
            return rejected("This log holds the most entries it may.");
        }
        if (const auto* change = std::get_if<HeaderChangeEntry>(&entry.m_Body))
        {
            Log scratch = log;
            if (auto changed =
                    ej::change_header(scratch, change->m_Field, change->m_NewValue, entry.m_Captured, entry.m_Id);
                !changed)
            {
                return rejected_by_header(changed.error());
            }
        }
        return JournalBody{AddEntryBody{entry}};
    }
    case Command::Op::EDIT_ENTRY:
    {
        if (log.find(command.m_Entry.m_Id) == nullptr)
        {
            return rejected_by_log(Log::Error::UNKNOWN_ENTRY, "That entry is not in this log.");
        }
        if (auto valid = validate_entry(command.m_Entry); !valid)
        {
            return rejected(describe(valid.error()));
        }
        JournalBody body = EditEntryBody{command.m_Entry, command.m_Time};
        if (auto tried = trial(body); !tried)
        {
            return Failure(std::move(tried.error()));
        }
        return body;
    }
    case Command::Op::SET_DELETED:
    {
        if (log.find(command.m_Target) == nullptr)
        {
            return rejected_by_log(Log::Error::UNKNOWN_ENTRY, "That entry is not in this log.");
        }
        return JournalBody{SetDeletedBody{command.m_Target, command.m_Deleted}};
    }
    case Command::Op::CORRECT_HEADER:
    {
        Log scratch = log;
        if (auto corrected = ej::correct_header(scratch, command.m_Field, command.m_Text); !corrected)
        {
            return rejected_by_header(corrected.error());
        }
        JournalBody body = CorrectHeaderBody{command.m_Field, command.m_Text};
        if (auto tried = trial(body); !tried)
        {
            return Failure(std::move(tried.error()));
        }
        return body;
    }
    case Command::Op::CORRECT_CHANGE:
    {
        Log scratch = log;
        if (auto corrected = ej::correct_header_change(scratch, command.m_Target, command.m_Text); !corrected)
        {
            return rejected_by_header(corrected.error());
        }
        // The corrected entry, with the canonical text of the new value, is what the store is asked to hold.
        JournalBody body = EditEntryBody{*scratch.find(command.m_Target), command.m_Time};
        if (auto tried = trial(body); !tried)
        {
            return Failure(std::move(tried.error()));
        }
        return body;
    }
    case Command::Op::MARK_SHARED:
        return JournalBody{MarkSharedBody{command.m_Time}};
    }
    return rejected("The command is not known.");
}

LogChange LogDocument::change_for(const Command& command)
{
    LogChange change;
    change.m_Origin = command.m_Origin;
    switch (command.m_Op)
    {
    case Command::Op::ADD_ENTRY:
        change.m_Kind = LogChange::Kind::ENTRY_ADDED;
        change.m_Entry = command.m_Entry.m_Id;
        break;
    case Command::Op::EDIT_ENTRY:
        change.m_Kind = LogChange::Kind::ENTRY_EDITED;
        change.m_Entry = command.m_Entry.m_Id;
        break;
    case Command::Op::SET_DELETED:
        change.m_Kind = command.m_Deleted ? LogChange::Kind::ENTRY_DELETED : LogChange::Kind::ENTRY_RESTORED;
        change.m_Entry = command.m_Target;
        break;
    case Command::Op::CORRECT_HEADER:
        change.m_Kind = LogChange::Kind::HEADER_CORRECTED;
        break;
    case Command::Op::CHANGE_HEADER:
        change.m_Kind = LogChange::Kind::HEADER_CHANGED;
        change.m_Entry = command.m_Entry.m_Id;
        break;
    case Command::Op::CORRECT_CHANGE:
        change.m_Kind = LogChange::Kind::HEADER_CORRECTED;
        change.m_Entry = command.m_Target;
        break;
    case Command::Op::MARK_SHARED:
        change.m_Kind = LogChange::Kind::SHARED;
        break;
    }
    return change;
}

void LogDocument::pump()
{
    const std::weak_ptr<void> alive = m_Alive;
    while (!m_InFlight && !m_Waiting.empty())
    {
        Command command = std::move(m_Waiting.front());
        m_Waiting.pop_front();

        // The second check, against the mirror as it is now: commands before this one have committed.
        auto body = make_body(command);
        if (!body)
        {
            const Result<void, StorageError> failure = fail(as_storage_error(body.error()));
            report(command.m_Done, failure);
            if (alive.expired())
            {
                return;
            }
            continue;
        }
        start(std::move(command), std::move(*body));
    }
}

void LogDocument::start(Command command, JournalBody body)
{
    m_InFlight = true;
    const LogId log = m_Stored.m_Meta.m_Id;
    const LogChange change = change_for(command);
    // Two lambdas in one call: the order their captures are evaluated in is unspecified, so neither may move from what
    // the other copies.
    auto work = [log, job = body](StorageContext& context) { return write(context.m_Logs, log, job); };
    auto completion = [this, alive = std::weak_ptr<void>(m_Alive), generation = m_Generation, change,
                       applied = std::move(body),
                       done = std::move(command.m_Done)](Result<void, StorageError> result) mutable
    {
        if (alive.expired())
        {
            return;
        }
        finish(alive, generation, change, applied, done, std::move(result));
    };
    m_Gateway.run(std::move(work), std::move(completion));
}

void LogDocument::finish(const std::weak_ptr<void>& alive, std::uint64_t generation, const LogChange& change,
                         const JournalBody& body, Done& done, Result<void, StorageError> result)
{
    // A result for a log that `replace` has since swapped out says nothing about the mirror.
    if (generation == m_Generation && result)
    {
        JournalRecord record;
        record.m_Log = m_Stored.m_Meta.m_Id;
        record.m_Body = body;
        if (auto applied = apply_record(m_Stored, record); !applied)
        {
            // Validated twice and written, yet the rules disagree: report it, tell nobody the log changed.
            result = fail(make_error(StorageErrorKind::INTERNAL,
                                     "the mirror and the store disagree: " + describe(applied.error())));
        }
        else
        {
            m_Observers.notify([this, &change](LogObserver& observer) { observer.on_log_changed(*this, change); });
            if (alive.expired())
            {
                return;
            }
        }
    }

    report(done, result);
    if (alive.expired())
    {
        return;
    }
    m_InFlight = false;
    pump();
}

} // namespace ej::viewmodel
