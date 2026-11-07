// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/stored_log.hpp"

#include "error_journaling/header_history.hpp"
#include "error_journaling/utf8.hpp"

#include <cstdlib>

namespace ej
{

namespace
{

constexpr std::int64_t MAX_MILLIS = 9'007'199'254'740'991;  // the largest integer a JSON number carries exactly

using Failure = std::unexpected<StorageError>;

Failure invalid(std::string message)
{
    return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, std::move(message)));
}

Failure limit(std::string message)
{
    return Failure(make_error(StorageErrorKind::LIMIT_EXCEEDED, std::move(message)));
}

/// OK when `text` is well-formed UTF-8 of at most `max` code points.
Result<void, StorageError> check_text(std::string_view text, std::size_t max, std::string_view what)
{
    const auto count = utf8_code_points(text);
    if (!count)
    {
        return invalid(std::string(what) + " is not well-formed UTF-8");
    }
    if (*count > max)
    {
        return limit(std::string(what) + " is longer than " + std::to_string(max) + " characters");
    }
    return {};
}

Result<void, StorageError> check_time(UtcMillis time, std::string_view what)
{
    if (std::llabs(time.time_since_epoch().count()) > MAX_MILLIS)
    {
        return invalid(std::string(what) + " is out of range");
    }
    return {};
}

StorageError from_header_error(const HeaderError& error)
{
    switch (error.m_Kind)
    {
    case HeaderError::Kind::INVALID_UNIT_NUMBER:
        return make_error(StorageErrorKind::INVALID_ARGUMENT, "invalid unit number");
    case HeaderError::Kind::INVALID_SOFTWARE_VERSION:
        return make_error(StorageErrorKind::INVALID_ARGUMENT, "invalid software version");
    case HeaderError::Kind::EMPTY_LOCATION:
        return make_error(StorageErrorKind::INVALID_ARGUMENT, "empty location");
    case HeaderError::Kind::UNCHANGED:
        return make_error(StorageErrorKind::INVALID_ARGUMENT, "the value does not differ from the one in force");
    default:
        // The entry cannot be unknown, duplicated or the wrong kind here: the callers checked before they asked.
        return make_error(StorageErrorKind::INVALID_ARGUMENT, "invalid header value");
    }
}

/// The stored text of a header value, or the reason it is not valid for the field.
Result<std::string, StorageError> canonical_header_text(HeaderField field, std::string_view text)
{
    HeaderValues scratch;
    if (auto set = set_header_field(scratch, field, text); !set)
    {
        return Failure(from_header_error(set.error()));
    }
    // set_header_field succeeded, so the field holds a value.
    return header_field_text(scratch, field).value_or(std::string());
}

}  // namespace

Result<void, StorageError> validate_header(const HeaderValues& header)
{
    if (auto ok = check_text(header.m_Facilitator, MAX_HEADER_TEXT_CODE_POINTS, "the facilitator"); !ok)
    {
        return ok;
    }
    const int year = static_cast<int>(header.m_LocalDate.year());
    if (!header.m_LocalDate.ok() || year < 0 || year > 9999)
    {
        return invalid("the log date is out of range");
    }
    if (header.m_Location)
    {
        if (auto ok = check_text(header.m_Location->m_Text, MAX_HEADER_TEXT_CODE_POINTS, "the location"); !ok)
        {
            return ok;
        }
        if (!header.m_Location->m_Id && header.m_Location->m_Text.empty())
        {
            return invalid("the location is empty");
        }
    }
    return {};
}

Result<void, StorageError> validate_entry(const Entry& entry)
{
    if (entry.m_Captured.m_Zone.size() > MAX_ZONE_BYTES || !utf8_code_points(entry.m_Captured.m_Zone))
    {
        return invalid("the time zone name is not valid");
    }
    if (auto ok = check_time(entry.m_Captured.m_Utc, "the captured time"); !ok)
    {
        return ok;
    }
    if (entry.m_EditedUtc)
    {
        if (auto ok = check_time(*entry.m_EditedUtc, "the edit time"); !ok)
        {
            return ok;
        }
    }

    if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
    {
        if (error->m_ErrorTypeLabel.empty())
        {
            return invalid("an error entry needs the label of its error type");
        }
        if (auto ok = check_text(error->m_ErrorTypeLabel, MAX_LABEL_CODE_POINTS, "the error type label"); !ok)
        {
            return ok;
        }
        if (error->m_AttachedNote)
        {
            return check_text(*error->m_AttachedNote, MAX_NOTE_CODE_POINTS, "the attached note");
        }
        return {};
    }
    if (const auto* note = std::get_if<NoteEntry>(&entry.m_Body))
    {
        return check_text(note->m_Value, MAX_NOTE_CODE_POINTS, "the note");
    }
    const auto& change = std::get<HeaderChangeEntry>(entry.m_Body);
    return check_text(change.m_NewValue, MAX_HEADER_TEXT_CODE_POINTS, "the header value");
}

Result<StoredLog, StorageError> make_stored_log(LogId id, const CreateLogBody& body)
{
    if (auto ok = validate_header(body.m_Header); !ok)
    {
        return Failure(ok.error());
    }
    if (auto ok = check_time(body.m_CreatedUtc, "the creation time"); !ok)
    {
        return Failure(ok.error());
    }
    StoredLog log{LogMeta{id, body.m_CreatedUtc, std::nullopt, false, body.m_Previous}, Log(body.m_Header)};
    return log;
}

namespace
{

Result<void, StorageError> apply_add(StoredLog& stored, const AddEntryBody& body)
{
    if (auto ok = validate_entry(body.m_Entry); !ok)
    {
        return ok;
    }
    if (stored.m_Log.entries().size() >= MAX_ENTRIES_PER_LOG)
    {
        return limit("the log holds the most entries it may");
    }
    const Entry& entry = body.m_Entry;
    if (stored.m_Log.find(entry.m_Id) != nullptr)
    {
        return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "an entry with this id exists"));
    }
    if (const auto* change = std::get_if<HeaderChangeEntry>(&entry.m_Body))
    {
        if (entry.m_Deleted || entry.m_EditedUtc)
        {
            return invalid("a new header change cannot arrive deleted or edited");
        }
        // Validates the value, requires it to differ from the one in force, and stores its canonical text.
        if (auto changed =
                change_header(stored.m_Log, change->m_Field, change->m_NewValue, entry.m_Captured, entry.m_Id);
            !changed)
        {
            return Failure(from_header_error(changed.error()));
        }
        return {};
    }
    return stored.m_Log.add(entry).transform_error(
        [](Log::Error) { return make_error(StorageErrorKind::ALREADY_EXISTS, "an entry with this id exists"); });
}

Result<void, StorageError> apply_edit(StoredLog& stored, const EditEntryBody& body)
{
    if (auto ok = validate_entry(body.m_Entry); !ok)
    {
        return ok;
    }
    if (auto ok = check_time(body.m_EditedUtc, "the edit time"); !ok)
    {
        return ok;
    }
    Entry entry = body.m_Entry;
    if (stored.m_Log.find(entry.m_Id) == nullptr)
    {
        return Failure(make_error(StorageErrorKind::NOT_FOUND, "no such entry"));
    }
    if (auto* change = std::get_if<HeaderChangeEntry>(&entry.m_Body))
    {
        auto canonical = canonical_header_text(change->m_Field, change->m_NewValue);
        if (!canonical)
        {
            return Failure(canonical.error());
        }
        change->m_NewValue = std::move(*canonical);
    }
    const EntryId id = entry.m_Id;
    return stored.m_Log.replace(id, std::move(entry), body.m_EditedUtc)
        .transform_error([](Log::Error) { return make_error(StorageErrorKind::NOT_FOUND, "no such entry"); });
}

}  // namespace

Result<void, StorageError> apply_record(StoredLog& log, const JournalRecord& record)
{
    if (record.m_Log != log.m_Meta.m_Id)
    {
        return invalid("the record belongs to another log");
    }
    struct Visitor
    {
        StoredLog& m_Log;

        Result<void, StorageError> operator()(const CreateLogBody&) const { return invalid("a log is created once"); }
        Result<void, StorageError> operator()(const AddEntryBody& b) const { return apply_add(m_Log, b); }
        Result<void, StorageError> operator()(const EditEntryBody& b) const { return apply_edit(m_Log, b); }
        Result<void, StorageError> operator()(const SetDeletedBody& b) const
        {
            if (auto done = m_Log.m_Log.set_deleted(b.m_Entry, b.m_Deleted); !done)
            {
                return Failure(make_error(StorageErrorKind::NOT_FOUND, "no such entry"));
            }
            return {};
        }
        Result<void, StorageError> operator()(const CorrectHeaderBody& b) const
        {
            if (auto ok = check_text(b.m_Value, MAX_HEADER_TEXT_CODE_POINTS, "the header value"); !ok)
            {
                return ok;
            }
            if (auto done = correct_header(m_Log.m_Log, b.m_Field, b.m_Value); !done)
            {
                return Failure(from_header_error(done.error()));
            }
            return {};
        }
        Result<void, StorageError> operator()(const MarkSharedBody& b) const
        {
            if (auto ok = check_time(b.m_Utc, "the share time"); !ok)
            {
                return ok;
            }
            m_Log.m_Meta.m_LastSharedUtc = b.m_Utc;
            return {};
        }
        Result<void, StorageError> operator()(const ArchiveBody&) const
        {
            if (m_Log.m_Meta.m_Archived)
            {
                return Failure(make_error(StorageErrorKind::NOT_OPEN_LOG, "the log is archived already"));
            }
            m_Log.m_Meta.m_Archived = true;
            return {};
        }
        Result<void, StorageError> operator()(const VoidBody&) const { return {}; }
    };
    return std::visit(Visitor{log}, record.m_Body);
}

}  // namespace ej
