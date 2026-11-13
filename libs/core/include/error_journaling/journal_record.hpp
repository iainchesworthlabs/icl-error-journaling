// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/header_field.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/timestamp.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace ej
{

/// The journal format version this build writes. The reader understands every version ever shipped (DS-02), so a
/// journal written by an older build still rebuilds after a schema migration.
inline constexpr std::uint32_t JOURNAL_VERSION = 1;

/// The version of a record whose entry is an interaction (feedback page, 3.2). A record of any other kind stays at
/// `JOURNAL_VERSION`, so a journal that never saw an interaction is byte for byte what an older build wrote.
inline constexpr std::uint32_t JOURNAL_VERSION_INTERACTIONS = 2;

/// The newest version this build reads. A line with a higher one is `UNSUPPORTED_VERSION`.
inline constexpr std::uint32_t JOURNAL_VERSION_NEWEST = JOURNAL_VERSION_INTERACTIONS;

/// A journal line, including its CRC, is never longer than this. Notes are capped at 10,000 characters (decision 7), so
/// a torn tail can never look like a huge valid line, and the reader never buffers more than this for one record.
inline constexpr std::size_t MAX_RECORD_BYTES = 128 * 1024;

enum class JournalOp
{
    CREATE_LOG,
    ADD_ENTRY,
    EDIT_ENTRY,
    SET_DELETED,
    CORRECT_HEADER,
    MARK_SHARED,
    ARCHIVE,
    VOID_RECORD  // the operation `void`
};

/// The first record of a journal: the whole header, so a rebuild needs nothing from the database.
struct CreateLogBody
{
    HeaderValues m_Header;
    UtcMillis m_CreatedUtc;
    /// The log this one replaced when "start a new log" archived it (section 2.9). A journal whose predecessor has no
    /// `archive` record is a start that was interrupted before it was acknowledged.
    std::optional<LogId> m_Previous;
};

struct AddEntryBody
{
    Entry m_Entry;
};

/// A review-screen edit: the entry takes the new content and `m_EditedUtc` becomes its edit time.
struct EditEntryBody
{
    Entry m_Entry;
    UtcMillis m_EditedUtc;
};

struct SetDeletedBody
{
    EntryId m_Entry;
    bool m_Deleted = false;
};

/// The Correct action (HDR-07): fixes a header value, writes no entry.
struct CorrectHeaderBody
{
    HeaderField m_Field = HeaderField::UNIT_NUMBER;
    std::string m_Value;  // empty clears the field
};

struct MarkSharedBody
{
    UtcMillis m_Utc;
};

struct ArchiveBody
{
};

/// Cancels the named earlier record: the write it described failed after the journal append, and the user was told so.
struct VoidBody
{
    std::uint64_t m_Target = 0;
};

using JournalBody = std::variant<CreateLogBody, AddEntryBody, EditEntryBody, SetDeletedBody, CorrectHeaderBody,
                                 MarkSharedBody, ArchiveBody, VoidBody>;

struct JournalRecord
{
    std::uint32_t m_Version = JOURNAL_VERSION;
    std::uint64_t m_Seq = 0;  // gapless, starting at 1 for each log
    LogId m_Log;
    JournalBody m_Body;
};

[[nodiscard]] JournalOp journal_op(const JournalBody& body);

/// The version a record with this body is written at: `JOURNAL_VERSION_INTERACTIONS` for an `add_entry` or
/// `edit_entry` of an interaction, else `JOURNAL_VERSION`. `encode_record` refuses any other version.
[[nodiscard]] std::uint32_t journal_version_for(const JournalBody& body);
[[nodiscard]] std::string_view journal_op_name(JournalOp op);

/// CRC-32 (IEEE 802.3, the zlib polynomial) of `data`, continuing from `previous` (0 starts a new checksum). So
/// `crc32(b, crc32(a)) == crc32(a + b)`, and `crc32("123456789") == 0xCBF43926`.
[[nodiscard]] std::uint32_t crc32(std::string_view data, std::uint32_t previous = 0) noexcept;

/// The line of a record without its terminating newline:
/// `{"v":1,"seq":42,"op":"add_entry","log":"...","body":{...},"crc":"1a2b3c4d"}`. The keys are in a fixed order and the
/// text has no spaces, so the encoding of a record is unique. The CRC is that of the same object without the `crc` key.
/// Fails when the record is too long, or holds text that is not well-formed UTF-8.
[[nodiscard]] Result<std::string, StorageError> encode_record(const JournalRecord& record);

/// Why a line is not a record.
enum class DecodeError
{
    TOO_LONG,             // longer than MAX_RECORD_BYTES
    MALFORMED,            // not JSON, not UTF-8, or not the shape of a record
    BAD_CRC,              // the checksum does not match
    UNSUPPORTED_VERSION,  // the checksum matches but the version is newer than this build knows
    BAD_FIELD             // well formed, but a value is not valid (a bad UUID, a unit number with letters)
};

[[nodiscard]] std::string_view decode_error_name(DecodeError error);

/// Reads one line (without its newline). Strict: the line must be exactly what `encode_record` writes for the record it
/// holds, so a flipped bit anywhere is an error, never a different record.
[[nodiscard]] Result<JournalRecord, DecodeError> decode_record(std::string_view line);

}  // namespace ej
