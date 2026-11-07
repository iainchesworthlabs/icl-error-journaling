// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/journal_record.hpp"

#include "error_journaling/software_version.hpp"
#include "error_journaling/unit_number.hpp"
#include "error_journaling/utf8.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace ej
{

// ---------------------------------------------------------------------------------------------------------------------
// CRC-32
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr std::array<std::uint32_t, 256> make_crc_table()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit)
        {
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
        }
        table[i] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> CRC_TABLE = make_crc_table();

}  // namespace

std::uint32_t crc32(std::string_view data, std::uint32_t previous) noexcept
{
    std::uint32_t value = ~previous;
    for (const char c : data)
    {
        value = CRC_TABLE[(value ^ static_cast<std::uint8_t>(c)) & 0xFFU] ^ (value >> 8U);
    }
    return ~value;
}

// ---------------------------------------------------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------------------------------------------------

JournalOp journal_op(const JournalBody& body)
{
    return static_cast<JournalOp>(body.index());
}

std::string_view journal_op_name(JournalOp op)
{
    switch (op)
    {
    case JournalOp::CREATE_LOG:
        return "create_log";
    case JournalOp::ADD_ENTRY:
        return "add_entry";
    case JournalOp::EDIT_ENTRY:
        return "edit_entry";
    case JournalOp::SET_DELETED:
        return "set_deleted";
    case JournalOp::CORRECT_HEADER:
        return "correct_header";
    case JournalOp::MARK_SHARED:
        return "mark_shared";
    case JournalOp::ARCHIVE:
        return "archive";
    case JournalOp::VOID_RECORD:
        return "void";
    }
    return {};
}

std::string_view decode_error_name(DecodeError error)
{
    switch (error)
    {
    case DecodeError::TOO_LONG:
        return "too long";
    case DecodeError::MALFORMED:
        return "malformed";
    case DecodeError::BAD_CRC:
        return "bad crc";
    case DecodeError::UNSUPPORTED_VERSION:
        return "unsupported version";
    case DecodeError::BAD_FIELD:
        return "bad field";
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------
// Encoding. The text is written by hand in a fixed key order with no spaces, so the encoding of a record is unique.
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr std::string_view CRC_SUFFIX_PREFIX = R"(,"crc":")";
constexpr std::size_t CRC_HEX_DIGITS = 8;
// ,"crc":"1a2b3c4d"}
constexpr std::size_t CRC_SUFFIX_SIZE = 8 + CRC_HEX_DIGITS + 2;

constexpr std::int64_t MAX_SAFE_INTEGER = 9'007'199'254'740'992;  // 2^53: every integer below it is exact as a double

struct EncodeFailure
{
    std::string m_Message;
};

class Writer
{
public:
    [[nodiscard]] bool failed() const { return !m_Failure.m_Message.empty(); }
    [[nodiscard]] const EncodeFailure& failure() const { return m_Failure; }
    [[nodiscard]] std::string& text() { return m_Text; }

    void raw(std::string_view text) { m_Text.append(text); }

    void key(std::string_view name)
    {
        string(name);
        m_Text.push_back(':');
    }

    void null() { raw("null"); }
    void boolean(bool value) { raw(value ? "true" : "false"); }

    void integer(std::int64_t value)
    {
        if (value >= MAX_SAFE_INTEGER || value <= -MAX_SAFE_INTEGER)
        {
            fail("a number is out of range");
            return;
        }
        m_Text.append(std::to_string(value));
    }

    void string(std::string_view value)
    {
        if (!utf8_code_points(value))
        {
            fail("text is not well-formed UTF-8");
            return;
        }
        static constexpr std::string_view HEX = "0123456789abcdef";
        m_Text.push_back('"');
        for (const char c : value)
        {
            const auto byte = static_cast<std::uint8_t>(c);
            switch (c)
            {
            case '"':
                m_Text.append("\\\"");
                break;
            case '\\':
                m_Text.append("\\\\");
                break;
            case '\b':
                m_Text.append("\\b");
                break;
            case '\f':
                m_Text.append("\\f");
                break;
            case '\n':
                m_Text.append("\\n");
                break;
            case '\r':
                m_Text.append("\\r");
                break;
            case '\t':
                m_Text.append("\\t");
                break;
            default:
                if (byte < 0x20U)
                {
                    m_Text.append("\\u00");
                    m_Text.push_back(HEX[byte >> 4U]);
                    m_Text.push_back(HEX[byte & 0x0FU]);
                }
                else
                {
                    m_Text.push_back(c);
                }
            }
        }
        m_Text.push_back('"');
    }

    void optional_string(const std::optional<std::string>& value)
    {
        if (value)
        {
            string(*value);
        }
        else
        {
            null();
        }
    }

    void uuid(const Uuid& value) { string(value.format()); }

    void fail(std::string message)
    {
        if (m_Failure.m_Message.empty())
        {
            m_Failure.m_Message = std::move(message);
        }
    }

private:
    std::string m_Text;
    EncodeFailure m_Failure;
};

void write_millis(Writer& w, UtcMillis value)
{
    w.integer(value.time_since_epoch().count());
}

void write_date(Writer& w, std::chrono::year_month_day date)
{
    const int year = static_cast<int>(date.year());
    if (!date.ok() || year < 0 || year > 9999)
    {
        w.fail("a date is out of range");
        return;
    }
    char buffer[16];
    const int written = std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u", year,
                                      static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
    w.string(std::string_view(buffer, static_cast<std::size_t>(written)));
}

void write_header(Writer& w, const HeaderValues& header)
{
    w.raw("{");
    w.key("facilitator");
    w.string(header.m_Facilitator);
    w.raw(",");
    w.key("date");
    write_date(w, header.m_LocalDate);
    w.raw(",");
    w.key("unit");
    if (header.m_UnitNumber)
    {
        w.string(header.m_UnitNumber->digits());
    }
    else
    {
        w.null();
    }
    w.raw(",");
    w.key("location");
    if (header.m_Location)
    {
        w.raw("{");
        w.key("id");
        if (header.m_Location->m_Id)
        {
            w.uuid(header.m_Location->m_Id->uuid());
        }
        else
        {
            w.null();
        }
        w.raw(",");
        w.key("text");
        w.string(header.m_Location->m_Text);
        w.raw("}");
    }
    else
    {
        w.null();
    }
    w.raw(",");
    w.key("version");
    if (header.m_SoftwareVersion)
    {
        w.string(header.m_SoftwareVersion->format());
    }
    else
    {
        w.null();
    }
    w.raw("}");
}

void write_entry(Writer& w, const Entry& entry)
{
    w.raw("{");
    w.key("id");
    w.uuid(entry.m_Id.uuid());
    w.raw(",");
    w.key("utc");
    write_millis(w, entry.m_Captured.m_Utc);
    w.raw(",");
    w.key("zone");
    w.string(entry.m_Captured.m_Zone);
    w.raw(",");
    w.key("edited");
    if (entry.m_EditedUtc)
    {
        write_millis(w, *entry.m_EditedUtc);
    }
    else
    {
        w.null();
    }
    w.raw(",");
    w.key("deleted");
    w.boolean(entry.m_Deleted);
    w.raw(",");
    if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
    {
        w.key("kind");
        w.string("error");
        w.raw(",");
        w.key("type");
        w.uuid(error->m_ErrorType.uuid());
        w.raw(",");
        w.key("label");
        w.string(error->m_ErrorTypeLabel);
        w.raw(",");
        w.key("note");
        w.optional_string(error->m_AttachedNote);
    }
    else if (const auto* note = std::get_if<NoteEntry>(&entry.m_Body))
    {
        w.key("kind");
        w.string("note");
        w.raw(",");
        w.key("template");
        if (note->m_Template)
        {
            w.uuid(note->m_Template->uuid());
        }
        else
        {
            w.null();
        }
        w.raw(",");
        w.key("value");
        w.string(note->m_Value);
    }
    else
    {
        const auto& change = std::get<HeaderChangeEntry>(entry.m_Body);
        w.key("kind");
        w.string("header_change");
        w.raw(",");
        w.key("field");
        w.string(header_field_key(change.m_Field));
        w.raw(",");
        w.key("value");
        w.string(change.m_NewValue);
    }
    w.raw("}");
}

void write_body(Writer& w, const JournalBody& body)
{
    struct Visitor
    {
        Writer& m_W;

        void operator()(const CreateLogBody& b) const
        {
            m_W.raw("{");
            m_W.key("header");
            write_header(m_W, b.m_Header);
            m_W.raw(",");
            m_W.key("created");
            write_millis(m_W, b.m_CreatedUtc);
            m_W.raw(",");
            m_W.key("prev");
            if (b.m_Previous)
            {
                m_W.uuid(b.m_Previous->uuid());
            }
            else
            {
                m_W.null();
            }
            m_W.raw("}");
        }
        void operator()(const AddEntryBody& b) const
        {
            m_W.raw("{");
            m_W.key("entry");
            write_entry(m_W, b.m_Entry);
            m_W.raw("}");
        }
        void operator()(const EditEntryBody& b) const
        {
            m_W.raw("{");
            m_W.key("entry");
            write_entry(m_W, b.m_Entry);
            m_W.raw(",");
            m_W.key("at");
            write_millis(m_W, b.m_EditedUtc);
            m_W.raw("}");
        }
        void operator()(const SetDeletedBody& b) const
        {
            m_W.raw("{");
            m_W.key("entry");
            m_W.uuid(b.m_Entry.uuid());
            m_W.raw(",");
            m_W.key("deleted");
            m_W.boolean(b.m_Deleted);
            m_W.raw("}");
        }
        void operator()(const CorrectHeaderBody& b) const
        {
            m_W.raw("{");
            m_W.key("field");
            m_W.string(header_field_key(b.m_Field));
            m_W.raw(",");
            m_W.key("value");
            m_W.string(b.m_Value);
            m_W.raw("}");
        }
        void operator()(const MarkSharedBody& b) const
        {
            m_W.raw("{");
            m_W.key("at");
            write_millis(m_W, b.m_Utc);
            m_W.raw("}");
        }
        void operator()(const ArchiveBody&) const { m_W.raw("{}"); }
        void operator()(const VoidBody& b) const
        {
            m_W.raw("{");
            m_W.key("target");
            m_W.integer(static_cast<std::int64_t>(b.m_Target));
            m_W.raw("}");
        }
    };
    std::visit(Visitor{w}, body);
}

/// The object without its `crc` key: the text the checksum covers.
Result<std::string, StorageError> encode_without_crc(const JournalRecord& record)
{
    if (record.m_Seq == 0 || record.m_Seq >= static_cast<std::uint64_t>(MAX_SAFE_INTEGER))
    {
        return fail(make_error(StorageErrorKind::INVALID_ARGUMENT, "a journal sequence number is out of range"));
    }
    Writer w;
    w.raw("{");
    w.key("v");
    w.integer(record.m_Version);
    w.raw(",");
    w.key("seq");
    w.integer(static_cast<std::int64_t>(record.m_Seq));
    w.raw(",");
    w.key("op");
    w.string(journal_op_name(journal_op(record.m_Body)));
    w.raw(",");
    w.key("log");
    w.uuid(record.m_Log.uuid());
    w.raw(",");
    w.key("body");
    write_body(w, record.m_Body);
    w.raw("}");
    if (w.failed())
    {
        return fail(make_error(StorageErrorKind::INVALID_ARGUMENT, w.failure().m_Message));
    }
    return std::move(w.text());
}

std::string hex8(std::uint32_t value)
{
    static constexpr std::string_view HEX = "0123456789abcdef";
    std::string text(CRC_HEX_DIGITS, '0');
    for (std::size_t i = 0; i < CRC_HEX_DIGITS; ++i)
    {
        text[CRC_HEX_DIGITS - 1 - i] = HEX[(value >> (4U * i)) & 0x0FU];
    }
    return text;
}

}  // namespace

Result<std::string, StorageError> encode_record(const JournalRecord& record)
{
    if (record.m_Version != JOURNAL_VERSION)
    {
        return fail(make_error(StorageErrorKind::INVALID_ARGUMENT, "this build writes only journal version 1"));
    }
    auto text = encode_without_crc(record);
    if (!text)
    {
        return fail(text.error());
    }
    const std::uint32_t checksum = crc32(*text);
    text->pop_back();  // the closing brace comes back after the crc
    text->append(CRC_SUFFIX_PREFIX);
    text->append(hex8(checksum));
    text->append("\"}");
    if (text->size() > MAX_RECORD_BYTES)
    {
        return fail(make_error(StorageErrorKind::LIMIT_EXCEEDED, "a journal record is longer than the limit"));
    }
    return text;
}

// ---------------------------------------------------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

using Fail = std::unexpected<DecodeError>;

Fail malformed()
{
    return Fail(DecodeError::MALFORMED);
}

Fail bad_field()
{
    return Fail(DecodeError::BAD_FIELD);
}

/// The value under `name`; an undefined value when the key is missing.
QJsonValue member(const QJsonObject& object, const char* name)
{
    return object.value(QLatin1StringView(name));
}

Result<std::string, DecodeError> get_string(const QJsonObject& object, const char* name)
{
    const QJsonValue value = member(object, name);
    if (!value.isString())
    {
        return malformed();
    }
    const QByteArray utf8 = value.toString().toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

Result<std::optional<std::string>, DecodeError> get_optional_string(const QJsonObject& object, const char* name)
{
    const QJsonValue value = member(object, name);
    if (value.isUndefined())
    {
        return malformed();
    }
    if (value.isNull())
    {
        return std::optional<std::string>{};
    }
    auto text = get_string(object, name);
    if (!text)
    {
        return Fail(text.error());
    }
    return std::optional<std::string>{std::move(*text)};
}

Result<std::int64_t, DecodeError> integer_of(const QJsonValue& value)
{
    if (!value.isDouble())
    {
        return malformed();
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number != std::floor(number) ||
        std::fabs(number) >= static_cast<double>(MAX_SAFE_INTEGER))
    {
        return malformed();
    }
    return static_cast<std::int64_t>(number);
}

Result<std::int64_t, DecodeError> get_integer(const QJsonObject& object, const char* name)
{
    const QJsonValue value = member(object, name);
    if (value.isUndefined())
    {
        return malformed();
    }
    return integer_of(value);
}

Result<std::optional<std::int64_t>, DecodeError> get_optional_integer(const QJsonObject& object, const char* name)
{
    const QJsonValue value = member(object, name);
    if (value.isUndefined())
    {
        return malformed();
    }
    if (value.isNull())
    {
        return std::optional<std::int64_t>{};
    }
    auto number = integer_of(value);
    if (!number)
    {
        return Fail(number.error());
    }
    return std::optional<std::int64_t>{*number};
}

Result<bool, DecodeError> get_bool(const QJsonObject& object, const char* name)
{
    const QJsonValue value = member(object, name);
    if (!value.isBool())
    {
        return malformed();
    }
    return value.toBool();
}

Result<Uuid, DecodeError> parse_uuid(const std::string& text)
{
    auto uuid = Uuid::parse(text);
    if (!uuid)
    {
        return bad_field();
    }
    return *uuid;
}

Result<Uuid, DecodeError> get_uuid(const QJsonObject& object, const char* name)
{
    auto text = get_string(object, name);
    if (!text)
    {
        return Fail(text.error());
    }
    return parse_uuid(*text);
}

Result<std::optional<Uuid>, DecodeError> get_optional_uuid(const QJsonObject& object, const char* name)
{
    auto text = get_optional_string(object, name);
    if (!text)
    {
        return Fail(text.error());
    }
    if (!*text)
    {
        return std::optional<Uuid>{};
    }
    auto uuid = parse_uuid(**text);
    if (!uuid)
    {
        return Fail(uuid.error());
    }
    return std::optional<Uuid>{*uuid};
}

Result<QJsonObject, DecodeError> get_object(const QJsonObject& object, const char* name)
{
    const QJsonValue value = member(object, name);
    if (!value.isObject())
    {
        return malformed();
    }
    return value.toObject();
}

Result<UtcMillis, DecodeError> get_millis(const QJsonObject& object, const char* name)
{
    auto number = get_integer(object, name);
    if (!number)
    {
        return Fail(number.error());
    }
    return UtcMillis{std::chrono::milliseconds{*number}};
}

Result<std::chrono::year_month_day, DecodeError> parse_date(const std::string& text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
    {
        return bad_field();
    }
    const auto number = [&text](std::size_t from, std::size_t count) -> std::optional<int>
    {
        int value = 0;
        const auto [end, code] = std::from_chars(text.data() + from, text.data() + from + count, value);
        if (code != std::errc{} || end != text.data() + from + count)
        {
            return std::nullopt;
        }
        return value;
    };
    const auto year = number(0, 4);
    const auto month = number(5, 2);
    const auto day = number(8, 2);
    if (!year || !month || !day)
    {
        return bad_field();
    }
    const std::chrono::year_month_day date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok())
    {
        return bad_field();
    }
    return date;
}

Result<HeaderValues, DecodeError> read_header(const QJsonObject& object)
{
    HeaderValues header;
    auto facilitator = get_string(object, "facilitator");
    auto date = get_string(object, "date");
    auto unit = get_optional_string(object, "unit");
    auto version = get_optional_string(object, "version");
    if (!facilitator)
    {
        return Fail(facilitator.error());
    }
    if (!date)
    {
        return Fail(date.error());
    }
    if (!unit)
    {
        return Fail(unit.error());
    }
    if (!version)
    {
        return Fail(version.error());
    }
    header.m_Facilitator = std::move(*facilitator);
    auto parsed_date = parse_date(*date);
    if (!parsed_date)
    {
        return Fail(parsed_date.error());
    }
    header.m_LocalDate = *parsed_date;
    if (*unit)
    {
        auto parsed = UnitNumber::parse(**unit);
        if (!parsed)
        {
            return bad_field();
        }
        header.m_UnitNumber = std::move(*parsed);
    }
    if (*version)
    {
        auto parsed = SoftwareVersion::parse(**version);
        if (!parsed)
        {
            return bad_field();
        }
        header.m_SoftwareVersion = *parsed;
    }
    const QJsonValue location = member(object, "location");
    if (location.isUndefined())
    {
        return malformed();
    }
    if (!location.isNull())
    {
        if (!location.isObject())
        {
            return malformed();
        }
        const QJsonObject location_object = location.toObject();
        auto id = get_optional_uuid(location_object, "id");
        auto text = get_string(location_object, "text");
        if (!id)
        {
            return Fail(id.error());
        }
        if (!text)
        {
            return Fail(text.error());
        }
        LocationValue value;
        if (*id)
        {
            value.m_Id = LocationId{**id};
        }
        value.m_Text = std::move(*text);
        header.m_Location = std::move(value);
    }
    return header;
}

Result<Entry, DecodeError> read_entry(const QJsonObject& object)
{
    Entry entry;

    auto id = get_uuid(object, "id");
    auto utc = get_millis(object, "utc");
    auto zone = get_string(object, "zone");
    auto edited = get_optional_integer(object, "edited");
    auto deleted = get_bool(object, "deleted");
    auto kind = get_string(object, "kind");
    if (!id)
    {
        return Fail(id.error());
    }
    if (!utc)
    {
        return Fail(utc.error());
    }
    if (!zone)
    {
        return Fail(zone.error());
    }
    if (!edited)
    {
        return Fail(edited.error());
    }
    if (!deleted)
    {
        return Fail(deleted.error());
    }
    if (!kind)
    {
        return Fail(kind.error());
    }
    entry.m_Id = EntryId{*id};
    entry.m_Captured = Timestamp{*utc, std::move(*zone)};
    if (*edited)
    {
        entry.m_EditedUtc = UtcMillis{std::chrono::milliseconds{**edited}};
    }
    entry.m_Deleted = *deleted;

    if (*kind == "error")
    {
        auto type = get_uuid(object, "type");
        auto label = get_string(object, "label");
        auto note = get_optional_string(object, "note");
        if (!type)
        {
            return Fail(type.error());
        }
        if (!label)
        {
            return Fail(label.error());
        }
        if (!note)
        {
            return Fail(note.error());
        }
        entry.m_Body = ErrorEntry{ErrorTypeId{*type}, std::move(*note), std::move(*label)};
    }
    else if (*kind == "note")
    {
        auto note_template = get_optional_uuid(object, "template");
        auto value = get_string(object, "value");
        if (!note_template)
        {
            return Fail(note_template.error());
        }
        if (!value)
        {
            return Fail(value.error());
        }
        NoteEntry note;
        if (*note_template)
        {
            note.m_Template = NoteTemplateId{**note_template};
        }
        note.m_Value = std::move(*value);
        entry.m_Body = std::move(note);
    }
    else if (*kind == "header_change")
    {
        auto field = get_string(object, "field");
        auto value = get_string(object, "value");
        if (!field)
        {
            return Fail(field.error());
        }
        if (!value)
        {
            return Fail(value.error());
        }
        const auto parsed = header_field_from_key(*field);
        if (!parsed)
        {
            return bad_field();
        }
        entry.m_Body = HeaderChangeEntry{*parsed, std::move(*value)};
    }
    else
    {
        return bad_field();
    }
    return entry;
}

Result<JournalBody, DecodeError> read_body(JournalOp op, const QJsonObject& body)
{
    switch (op)
    {
    case JournalOp::CREATE_LOG:
    {
        auto header_object = get_object(body, "header");
        auto created = get_millis(body, "created");
        auto previous = get_optional_uuid(body, "prev");
        if (!header_object)
        {
            return Fail(header_object.error());
        }
        if (!created)
        {
            return Fail(created.error());
        }
        if (!previous)
        {
            return Fail(previous.error());
        }
        auto header = read_header(*header_object);
        if (!header)
        {
            return Fail(header.error());
        }
        CreateLogBody result{std::move(*header), *created, std::nullopt};
        if (*previous)
        {
            result.m_Previous = LogId{**previous};
        }
        return JournalBody{std::move(result)};
    }
    case JournalOp::ADD_ENTRY:
    {
        auto entry_object = get_object(body, "entry");
        if (!entry_object)
        {
            return Fail(entry_object.error());
        }
        auto entry = read_entry(*entry_object);
        if (!entry)
        {
            return Fail(entry.error());
        }
        return JournalBody{AddEntryBody{std::move(*entry)}};
    }
    case JournalOp::EDIT_ENTRY:
    {
        auto entry_object = get_object(body, "entry");
        auto at = get_millis(body, "at");
        if (!entry_object)
        {
            return Fail(entry_object.error());
        }
        if (!at)
        {
            return Fail(at.error());
        }
        auto entry = read_entry(*entry_object);
        if (!entry)
        {
            return Fail(entry.error());
        }
        return JournalBody{EditEntryBody{std::move(*entry), *at}};
    }
    case JournalOp::SET_DELETED:
    {
        auto id = get_uuid(body, "entry");
        auto deleted = get_bool(body, "deleted");
        if (!id)
        {
            return Fail(id.error());
        }
        if (!deleted)
        {
            return Fail(deleted.error());
        }
        return JournalBody{SetDeletedBody{EntryId{*id}, *deleted}};
    }
    case JournalOp::CORRECT_HEADER:
    {
        auto field = get_string(body, "field");
        auto value = get_string(body, "value");
        if (!field)
        {
            return Fail(field.error());
        }
        if (!value)
        {
            return Fail(value.error());
        }
        const auto parsed = header_field_from_key(*field);
        if (!parsed)
        {
            return bad_field();
        }
        return JournalBody{CorrectHeaderBody{*parsed, std::move(*value)}};
    }
    case JournalOp::MARK_SHARED:
    {
        auto at = get_millis(body, "at");
        if (!at)
        {
            return Fail(at.error());
        }
        return JournalBody{MarkSharedBody{*at}};
    }
    case JournalOp::ARCHIVE:
        return JournalBody{ArchiveBody{}};
    case JournalOp::VOID_RECORD:
    {
        auto target = get_integer(body, "target");
        if (!target)
        {
            return Fail(target.error());
        }
        if (*target <= 0)
        {
            return bad_field();
        }
        return JournalBody{VoidBody{static_cast<std::uint64_t>(*target)}};
    }
    }
    return malformed();
}

std::optional<JournalOp> op_from_name(std::string_view name)
{
    for (std::size_t i = 0; i <= static_cast<std::size_t>(JournalOp::VOID_RECORD); ++i)
    {
        const auto op = static_cast<JournalOp>(i);
        if (journal_op_name(op) == name)
        {
            return op;
        }
    }
    return std::nullopt;
}

bool is_lower_hex(std::string_view text)
{
    for (const char c : text)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

}  // namespace

Result<JournalRecord, DecodeError> decode_record(std::string_view line)
{
    if (line.size() > MAX_RECORD_BYTES)
    {
        return Fail(DecodeError::TOO_LONG);
    }
    // The line ends with ,"crc":"xxxxxxxx"} and the checksum covers the same object without that key.
    if (line.size() <= CRC_SUFFIX_SIZE || line.front() != '{')
    {
        return malformed();
    }
    const std::string_view suffix = line.substr(line.size() - CRC_SUFFIX_SIZE);
    const std::string_view digits = suffix.substr(CRC_SUFFIX_PREFIX.size(), CRC_HEX_DIGITS);
    if (suffix.substr(0, CRC_SUFFIX_PREFIX.size()) != CRC_SUFFIX_PREFIX || suffix.substr(suffix.size() - 2) != "\"}" ||
        !is_lower_hex(digits))
    {
        return malformed();
    }
    const std::string_view covered = line.substr(0, line.size() - CRC_SUFFIX_SIZE);
    const std::uint32_t computed = crc32("}", crc32(covered));
    if (hex8(computed) != digits)
    {
        return Fail(DecodeError::BAD_CRC);
    }

    // The checksum matched, so the text is what some writer produced. Parse it.
    std::string payload(covered);
    payload.push_back('}');
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromRawData(payload.data(), static_cast<qsizetype>(payload.size())), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return malformed();
    }
    const QJsonObject object = document.object();
    auto version = get_integer(object, "v");
    auto seq = get_integer(object, "seq");
    auto op_name = get_string(object, "op");
    auto log = get_uuid(object, "log");
    auto body_object = get_object(object, "body");
    if (!version || !seq || !op_name || !body_object)
    {
        return malformed();
    }
    if (*version != static_cast<std::int64_t>(JOURNAL_VERSION))
    {
        // The reader understands every version ever shipped; only version 1 exists so far, and anything else is from a
        // newer build or damaged in a way the checksum happened to miss.
        return Fail(*version > static_cast<std::int64_t>(JOURNAL_VERSION) ? DecodeError::UNSUPPORTED_VERSION
                                                                          : DecodeError::MALFORMED);
    }
    if (!log)
    {
        return Fail(log.error());
    }
    const auto op = op_from_name(*op_name);
    if (*seq <= 0 || !op)
    {
        return malformed();
    }
    auto body = read_body(*op, *body_object);
    if (!body)
    {
        return Fail(body.error());
    }

    JournalRecord record;
    record.m_Version = static_cast<std::uint32_t>(*version);
    record.m_Seq = static_cast<std::uint64_t>(*seq);
    record.m_Log = LogId{*log};
    record.m_Body = std::move(*body);

    // Strict: the line must be exactly what the encoder writes for this record. A character that is escaped in a
    // different but equivalent way, an upper-case hexadecimal digit or an extra key is an error, never a record.
    const auto canonical = encode_record(record);
    if (!canonical || *canonical != line)
    {
        return malformed();
    }
    return record;
}

}  // namespace ej
