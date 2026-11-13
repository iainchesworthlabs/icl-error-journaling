// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/journal_record.hpp"

#include "error_journaling/iso_date.hpp"
#include "error_journaling/extra_versions.hpp"
#include "error_journaling/software_version.hpp"
#include "error_journaling/unit_number.hpp"
#include "error_journaling/utf8.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <algorithm>
#include <array>
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
    const auto text = format_iso_date(date);
    if (!text)
    {
        w.fail("a date is out of range");
        return;
    }
    w.string(*text);
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
        w.string(header.m_UnitNumber->stored());
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
        w.string(header.m_SoftwareVersion->stored());
    }
    else
    {
        w.null();
    }
    if (!header.m_ExtraVersions.empty())
    {
        // Only when there are some, so a log without extra versions is written exactly as before.
        w.raw(",");
        w.key("extra_versions");
        w.string(stored_extra_versions(header.m_ExtraVersions));
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
// Decoding. A `Decoder` collects the first error, and a field that cannot be read answers a default, so the readers
// below are straight lines and the caller checks once at the end (a record with an error is thrown away whole).
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

struct Decoder
{
    std::optional<DecodeError> m_Error;

    void fail(DecodeError error)
    {
        if (!m_Error)
        {
            m_Error = error;
        }
    }
};

class Fields
{
public:
    Fields(Decoder& decoder, QJsonObject object) : m_Decoder(decoder), m_Object(std::move(object)) {}

    [[nodiscard]] Fields object(const char* name) const
    {
        const QJsonValue value = m_Object.value(QLatin1StringView(name));
        if (!value.isObject())
        {
            m_Decoder.fail(DecodeError::MALFORMED);
        }
        return Fields(m_Decoder, value.toObject());
    }

    /// Whether the key is there at all (a key added later is absent from older records).
    [[nodiscard]] bool has(const char* name) const
    {
        return m_Object.contains(QLatin1StringView(name));
    }

    /// The sub-object, or nothing when the value is null.
    [[nodiscard]] std::optional<Fields> optional_object(const char* name) const
    {
        if (m_Object.value(QLatin1StringView(name)).isNull())
        {
            return std::nullopt;
        }
        return object(name);
    }

    [[nodiscard]] std::string string(const char* name) const
    {
        const QJsonValue value = m_Object.value(QLatin1StringView(name));
        if (!value.isString())
        {
            m_Decoder.fail(DecodeError::MALFORMED);
            return {};
        }
        const QByteArray utf8 = value.toString().toUtf8();
        return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    }

    [[nodiscard]] std::optional<std::string> optional_string(const char* name) const
    {
        if (m_Object.value(QLatin1StringView(name)).isNull())
        {
            return std::nullopt;
        }
        return string(name);
    }

    [[nodiscard]] std::int64_t integer(const char* name) const
    {
        const QJsonValue value = m_Object.value(QLatin1StringView(name));
        const double number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || number != std::floor(number) ||
            std::fabs(number) >= static_cast<double>(MAX_SAFE_INTEGER))
        {
            m_Decoder.fail(DecodeError::MALFORMED);
            return 0;
        }
        return static_cast<std::int64_t>(number);
    }

    [[nodiscard]] std::optional<std::int64_t> optional_integer(const char* name) const
    {
        if (m_Object.value(QLatin1StringView(name)).isNull())
        {
            return std::nullopt;
        }
        return integer(name);
    }

    [[nodiscard]] UtcMillis millis(const char* name) const
    {
        return UtcMillis{std::chrono::milliseconds{integer(name)}};
    }

    [[nodiscard]] bool boolean(const char* name) const
    {
        const QJsonValue value = m_Object.value(QLatin1StringView(name));
        if (!value.isBool())
        {
            m_Decoder.fail(DecodeError::MALFORMED);
        }
        return value.toBool();
    }

    [[nodiscard]] Uuid uuid(const char* name) const { return parse_uuid(string(name)); }

    [[nodiscard]] std::optional<Uuid> optional_uuid(const char* name) const
    {
        const auto text = optional_string(name);
        if (!text)
        {
            return std::nullopt;
        }
        return parse_uuid(*text);
    }

    /// A value that parsed as text but is not valid for its type.
    void bad_field() const { m_Decoder.fail(DecodeError::BAD_FIELD); }

private:
    [[nodiscard]] Uuid parse_uuid(const std::string& text) const
    {
        const auto parsed = Uuid::parse(text);
        if (!parsed)
        {
            bad_field();
            return Uuid{};
        }
        return *parsed;
    }

    Decoder& m_Decoder;
    QJsonObject m_Object;
};

std::chrono::year_month_day parse_date(const Fields& fields, const std::string& text)
{
    const auto date = parse_iso_date(text);
    if (!date)
    {
        fields.bad_field();
    }
    return date.value_or(std::chrono::year_month_day{});
}

HeaderValues read_header(const Fields& fields)
{
    HeaderValues header;
    header.m_Facilitator = fields.string("facilitator");
    header.m_LocalDate = parse_date(fields, fields.string("date"));
    if (const auto unit = fields.optional_string("unit"))
    {
        auto parsed = UnitNumber::parse_stored(*unit);
        if (parsed)
        {
            header.m_UnitNumber = std::move(*parsed);
        }
        else
        {
            fields.bad_field();
        }
    }
    if (const auto version = fields.optional_string("version"))
    {
        const auto parsed = SoftwareVersion::parse_stored(*version);
        if (parsed)
        {
            header.m_SoftwareVersion = *parsed;
        }
        else
        {
            fields.bad_field();
        }
    }
    if (fields.has("extra_versions"))
    {
        auto parsed = parse_extra_versions(fields.string("extra_versions"));
        if (parsed)
        {
            header.m_ExtraVersions = std::move(*parsed);
        }
        else
        {
            fields.bad_field();
        }
    }
    if (const auto location = fields.optional_object("location"))
    {
        LocationValue value;
        if (const auto id = location->optional_uuid("id"))
        {
            value.m_Id = LocationId{*id};
        }
        value.m_Text = location->string("text");
        header.m_Location = std::move(value);
    }
    return header;
}

Entry read_entry(const Fields& fields)
{
    Entry entry;
    entry.m_Id = EntryId{fields.uuid("id")};
    entry.m_Captured = Timestamp{fields.millis("utc"), fields.string("zone")};
    if (const auto edited = fields.optional_integer("edited"))
    {
        entry.m_EditedUtc = UtcMillis{std::chrono::milliseconds{*edited}};
    }
    entry.m_Deleted = fields.boolean("deleted");

    const std::string kind = fields.string("kind");
    if (kind == "error")
    {
        const ErrorTypeId type{fields.uuid("type")};
        std::string label = fields.string("label");
        entry.m_Body = ErrorEntry{type, fields.optional_string("note"), std::move(label)};
    }
    else if (kind == "note")
    {
        NoteEntry note;
        if (const auto note_template = fields.optional_uuid("template"))
        {
            note.m_Template = NoteTemplateId{*note_template};
        }
        note.m_Value = fields.string("value");
        entry.m_Body = std::move(note);
    }
    else if (kind == "header_change")
    {
        const auto field = header_field_from_key(fields.string("field"));
        if (!field)
        {
            fields.bad_field();
        }
        entry.m_Body = HeaderChangeEntry{field.value_or(HeaderField::UNIT_NUMBER), fields.string("value")};
    }
    else
    {
        fields.bad_field();
    }
    return entry;
}

JournalBody read_body(JournalOp op, const Fields& body)
{
    switch (op)
    {
    case JournalOp::CREATE_LOG:
    {
        CreateLogBody result{read_header(body.object("header")), body.millis("created"), std::nullopt};
        if (const auto previous = body.optional_uuid("prev"))
        {
            result.m_Previous = LogId{*previous};
        }
        return result;
    }
    case JournalOp::ADD_ENTRY:
        return AddEntryBody{read_entry(body.object("entry"))};
    case JournalOp::EDIT_ENTRY:
    {
        Entry entry = read_entry(body.object("entry"));
        return EditEntryBody{std::move(entry), body.millis("at")};
    }
    case JournalOp::SET_DELETED:
    {
        const EntryId entry{body.uuid("entry")};
        return SetDeletedBody{entry, body.boolean("deleted")};
    }
    case JournalOp::CORRECT_HEADER:
    {
        const auto field = header_field_from_key(body.string("field"));
        if (!field)
        {
            body.bad_field();
        }
        return CorrectHeaderBody{field.value_or(HeaderField::UNIT_NUMBER), body.string("value")};
    }
    case JournalOp::MARK_SHARED:
        return MarkSharedBody{body.millis("at")};
    case JournalOp::ARCHIVE:
        return ArchiveBody{};
    case JournalOp::VOID_RECORD:
    {
        const std::int64_t target = body.integer("target");
        if (target <= 0)
        {
            body.bad_field();
        }
        return VoidBody{static_cast<std::uint64_t>(target > 0 ? target : 0)};
    }
    }
    return ArchiveBody{};  // not reached: every operation is handled above
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
    return std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

}  // namespace

Result<JournalRecord, DecodeError> decode_record(std::string_view line)
{
    if (line.size() > MAX_RECORD_BYTES)
    {
        return std::unexpected(DecodeError::TOO_LONG);
    }
    // The line ends with ,"crc":"xxxxxxxx"} and the checksum covers the same object without that key.
    if (line.size() <= CRC_SUFFIX_SIZE || line.front() != '{')
    {
        return std::unexpected(DecodeError::MALFORMED);
    }
    const std::string_view suffix = line.substr(line.size() - CRC_SUFFIX_SIZE);
    const std::string_view digits = suffix.substr(CRC_SUFFIX_PREFIX.size(), CRC_HEX_DIGITS);
    if (!suffix.starts_with(CRC_SUFFIX_PREFIX) || !suffix.ends_with("\"}") || !is_lower_hex(digits))
    {
        return std::unexpected(DecodeError::MALFORMED);
    }
    const std::string_view covered = line.substr(0, line.size() - CRC_SUFFIX_SIZE);
    if (hex8(crc32("}", crc32(covered))) != digits)
    {
        return std::unexpected(DecodeError::BAD_CRC);
    }

    // The checksum matched, so the text is what some writer produced. Parse it.
    std::string payload(covered);
    payload.push_back('}');
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromRawData(payload.data(), static_cast<qsizetype>(payload.size())), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(DecodeError::MALFORMED);
    }

    Decoder decoder;
    const Fields root(decoder, document.object());
    const std::int64_t version = root.integer("v");
    if (version > static_cast<std::int64_t>(JOURNAL_VERSION))
    {
        // The reader understands every version ever shipped; only version 1 exists so far. A newer one comes from a
        // newer build and is not damage.
        return std::unexpected(DecodeError::UNSUPPORTED_VERSION);
    }
    const std::int64_t seq = root.integer("seq");
    const auto op = op_from_name(root.string("op"));
    JournalRecord record;
    record.m_Log = LogId{root.uuid("log")};
    const Fields body = root.object("body");
    if (decoder.m_Error || version != static_cast<std::int64_t>(JOURNAL_VERSION) || seq <= 0 || !op)
    {
        return std::unexpected(decoder.m_Error.value_or(DecodeError::MALFORMED));
    }
    record.m_Version = static_cast<std::uint32_t>(version);
    record.m_Seq = static_cast<std::uint64_t>(seq);
    record.m_Body = read_body(*op, body);
    if (decoder.m_Error)
    {
        return std::unexpected(*decoder.m_Error);
    }

    // Strict: the line must be exactly what the encoder writes for this record. A character that is escaped in a
    // different but equivalent way, an upper-case hexadecimal digit or an extra key is an error, never a record.
    const auto canonical = encode_record(record);
    if (!canonical || *canonical != line)
    {
        return std::unexpected(DecodeError::MALFORMED);
    }
    return record;
}

}  // namespace ej
