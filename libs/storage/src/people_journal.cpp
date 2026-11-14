// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/people_journal.hpp"

#include "error_journaling/journal_record.hpp"
#include "error_journaling/uuid.hpp"
#include "platform/durable_sync.hpp"
#include "protect.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <algorithm>
#include <cmath>
#include <system_error>

namespace ej
{

namespace
{

using Failure = std::unexpected<StorageError>;

constexpr std::string_view CRC_KEY = "crc";
constexpr double MAX_SAFE = 9007199254740991.0; // 2^53 - 1, what a JSON number holds exactly

Failure invalid(std::string message)
{
    return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, std::move(message)));
}

std::string hex8(std::uint32_t value)
{
    static constexpr std::string_view HEX = "0123456789abcdef";
    std::string text(8, '0');
    for (std::size_t i = 0; i < 8; ++i)
    {
        text[7 - i] = HEX[(value >> (4U * i)) & 0x0FU];
    }
    return text;
}

QString to_qt(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string to_std(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QJsonValue millis_json(UtcMillis time)
{
    return QJsonValue(static_cast<double>(time.time_since_epoch().count()));
}

std::string compact(const QJsonObject& object)
{
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

struct BodyWriter
{
    QJsonObject operator()(const AddPersonBody& b) const
    {
        QJsonObject body;
        body.insert(QStringLiteral("id"), to_qt(b.m_Person.m_Id.format()));
        body.insert(QStringLiteral("name"), to_qt(b.m_Person.m_DisplayName));
        body.insert(QStringLiteral("location"),
                    b.m_Person.m_Location ? QJsonValue(to_qt(b.m_Person.m_Location->format())) : QJsonValue());
        body.insert(QStringLiteral("created_utc"), millis_json(b.m_Person.m_CreatedUtc));
        return body;
    }
    QJsonObject operator()(const RenamePersonBody& b) const
    {
        QJsonObject body;
        body.insert(QStringLiteral("id"), to_qt(b.m_Id.format()));
        body.insert(QStringLiteral("name"), to_qt(b.m_Name));
        return body;
    }
    QJsonObject operator()(const RetirePersonBody& b) const
    {
        QJsonObject body;
        body.insert(QStringLiteral("id"), to_qt(b.m_Id.format()));
        body.insert(QStringLiteral("retired"), b.m_Retired);
        return body;
    }
    QJsonObject operator()(const TouchPersonBody& b) const
    {
        QJsonObject body;
        body.insert(QStringLiteral("id"), to_qt(b.m_Id.format()));
        body.insert(QStringLiteral("utc"), millis_json(b.m_Utc));
        return body;
    }
    QJsonObject operator()(const ForgetPersonBody& b) const
    {
        QJsonObject body;
        body.insert(QStringLiteral("id"), to_qt(b.m_Id.format()));
        return body;
    }
    QJsonObject operator()(const SetPersonLocationBody& b) const
    {
        QJsonObject body;
        body.insert(QStringLiteral("id"), to_qt(b.m_Id.format()));
        body.insert(QStringLiteral("location"),
                    b.m_Location ? QJsonValue(to_qt(b.m_Location->format())) : QJsonValue());
        return body;
    }
};

QJsonObject record_object(const PeopleRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("v"), static_cast<int>(people_record_version(record.m_Body)));
    object.insert(QStringLiteral("seq"), static_cast<double>(record.m_Seq));
    object.insert(QStringLiteral("op"), to_qt(people_op_name(people_op(record.m_Body))));
    object.insert(QStringLiteral("body"), std::visit(BodyWriter{}, record.m_Body));
    return object;
}

// --- decoding
// ---------------------------------------------------------------------------------------------------------

struct Fields
{
    const QJsonObject& m_Object;
    bool& m_Bad;

    [[nodiscard]] QString text(const char* key) const
    {
        const QJsonValue value = m_Object.value(QLatin1String(key));
        if (!value.isString())
        {
            m_Bad = true;
            return {};
        }
        return value.toString();
    }
    [[nodiscard]] std::optional<std::string> optional_text(const char* key) const
    {
        const QJsonValue value = m_Object.value(QLatin1String(key));
        if (value.isNull() || value.isUndefined())
        {
            return std::nullopt;
        }
        if (!value.isString())
        {
            m_Bad = true;
            return std::nullopt;
        }
        return to_std(value.toString());
    }
    [[nodiscard]] std::int64_t integer(const char* key) const
    {
        const QJsonValue value = m_Object.value(QLatin1String(key));
        const double number = value.toDouble(-1);
        if (!value.isDouble() || number < 0 || number > MAX_SAFE || number != std::floor(number))
        {
            m_Bad = true;
            return 0;
        }
        return static_cast<std::int64_t>(number);
    }
    [[nodiscard]] bool boolean(const char* key) const
    {
        const QJsonValue value = m_Object.value(QLatin1String(key));
        if (!value.isBool())
        {
            m_Bad = true;
            return false;
        }
        return value.toBool();
    }
    [[nodiscard]] Uuid uuid(const char* key) const
    {
        const auto parsed = Uuid::parse(to_std(text(key)));
        if (!parsed)
        {
            m_Bad = true;
            return Uuid{};
        }
        return *parsed;
    }
    [[nodiscard]] QJsonObject object(const char* key) const
    {
        const QJsonValue value = m_Object.value(QLatin1String(key));
        if (!value.isObject())
        {
            m_Bad = true;
            return {};
        }
        return value.toObject();
    }
};

std::optional<PeopleOp> op_from_name(std::string_view name)
{
    for (const PeopleOp op : {PeopleOp::ADD_PERSON, PeopleOp::RENAME_PERSON, PeopleOp::RETIRE_PERSON,
                              PeopleOp::TOUCH_PERSON, PeopleOp::FORGET_PERSON, PeopleOp::SET_PERSON_LOCATION})
    {
        if (people_op_name(op) == name)
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

} // namespace

PeopleOp people_op(const PeopleBody& body)
{
    return static_cast<PeopleOp>(body.index());
}

std::string_view people_op_name(PeopleOp op)
{
    switch (op)
    {
    case PeopleOp::ADD_PERSON:
        return "add_person";
    case PeopleOp::RENAME_PERSON:
        return "rename_person";
    case PeopleOp::RETIRE_PERSON:
        return "retire_person";
    case PeopleOp::TOUCH_PERSON:
        return "touch_person";
    case PeopleOp::FORGET_PERSON:
        return "forget_person";
    case PeopleOp::SET_PERSON_LOCATION:
        return "set_person_location";
    }
    return {};
}

std::uint32_t people_record_version(const PeopleBody& body)
{
    return std::holds_alternative<SetPersonLocationBody>(body) ? 2U : 1U;
}

Result<std::string, StorageError> encode_people_record(const PeopleRecord& record)
{
    if (record.m_Seq == 0 || static_cast<double>(record.m_Seq) >= MAX_SAFE)
    {
        return invalid("a people journal sequence number is out of range");
    }
    QJsonObject object = record_object(record);
    const std::uint32_t checksum = crc32(compact(object));
    object.insert(QString::fromLatin1(CRC_KEY.data(), static_cast<qsizetype>(CRC_KEY.size())), to_qt(hex8(checksum)));
    std::string line = compact(object);
    if (line.size() > MAX_RECORD_BYTES)
    {
        return Failure(make_error(StorageErrorKind::LIMIT_EXCEEDED, "a people journal record is too long"));
    }
    return line;
}

Result<PeopleRecord, PeopleDecodeError> decode_people_record(std::string_view line, std::uint64_t* seq_out)
{
    if (line.size() > MAX_RECORD_BYTES || line.empty() || line.front() != '{')
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromRawData(line.data(), static_cast<qsizetype>(line.size())), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    QJsonObject object = document.object();
    const QString crc_key = QString::fromLatin1(CRC_KEY.data(), static_cast<qsizetype>(CRC_KEY.size()));
    const QJsonValue crc_value = object.value(crc_key);
    if (!crc_value.isString())
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    const std::string digits = to_std(crc_value.toString());
    if (digits.size() != 8 || !is_lower_hex(digits))
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    object.remove(crc_key);
    if (hex8(crc32(compact(object))) != digits)
    {
        return std::unexpected(PeopleDecodeError::BAD_CRC);
    }

    bool bad = false;
    const Fields root{object, bad};
    const std::int64_t version = root.integer("v");
    if (bad)
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    if (version > static_cast<std::int64_t>(PEOPLE_JOURNAL_VERSION))
    {
        return std::unexpected(PeopleDecodeError::UNSUPPORTED_VERSION);
    }
    const std::int64_t seq = root.integer("seq");
    const auto op = op_from_name(to_std(root.text("op")));
    const QJsonObject body_object = root.object("body");
    if (bad || version < 1 || seq <= 0)
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    if (!op)
    {
        if (seq_out != nullptr)
        {
            *seq_out = static_cast<std::uint64_t>(seq);
        }
        return std::unexpected(PeopleDecodeError::UNKNOWN_OP);
    }
    const Fields body{body_object, bad};
    PeopleRecord record;
    record.m_Version = static_cast<std::uint32_t>(version);
    record.m_Seq = static_cast<std::uint64_t>(seq);
    switch (*op)
    {
    case PeopleOp::ADD_PERSON:
    {
        AddPersonBody add;
        add.m_Person.m_Id = PersonId{body.uuid("id")};
        add.m_Person.m_DisplayName = to_std(body.text("name"));
        if (const auto location = body.optional_text("location"))
        {
            const auto parsed = Uuid::parse(*location);
            if (!parsed)
            {
                bad = true;
            }
            else
            {
                add.m_Person.m_Location = LocationId{*parsed};
            }
        }
        add.m_Person.m_CreatedUtc = UtcMillis{std::chrono::milliseconds{body.integer("created_utc")}};
        record.m_Body = std::move(add);
        break;
    }
    case PeopleOp::RENAME_PERSON:
        record.m_Body = RenamePersonBody{PersonId{body.uuid("id")}, to_std(body.text("name"))};
        break;
    case PeopleOp::RETIRE_PERSON:
        record.m_Body = RetirePersonBody{PersonId{body.uuid("id")}, body.boolean("retired")};
        break;
    case PeopleOp::TOUCH_PERSON:
        record.m_Body =
            TouchPersonBody{PersonId{body.uuid("id")}, UtcMillis{std::chrono::milliseconds{body.integer("utc")}}};
        break;
    case PeopleOp::FORGET_PERSON:
        record.m_Body = ForgetPersonBody{PersonId{body.uuid("id")}};
        break;
    case PeopleOp::SET_PERSON_LOCATION:
    {
        SetPersonLocationBody set;
        set.m_Id = PersonId{body.uuid("id")};
        if (const auto location = body.optional_text("location"))
        {
            const auto parsed = Uuid::parse(*location);
            if (!parsed)
            {
                bad = true;
            }
            else
            {
                set.m_Location = LocationId{*parsed};
            }
        }
        record.m_Body = std::move(set);
        break;
    }
    }
    if (bad)
    {
        return std::unexpected(PeopleDecodeError::BAD_FIELD);
    }
    // Strict, as the log journal is: the line is exactly what the encoder writes for this record.
    const auto canonical = encode_people_record(record);
    if (!canonical || *canonical != line)
    {
        return std::unexpected(PeopleDecodeError::MALFORMED);
    }
    return record;
}

// ---------------------------------------------------------------------------------------------------------------------
// The state
// ---------------------------------------------------------------------------------------------------------------------

const Person* PeopleState::find(const PersonId& id) const
{
    const auto found = m_People.find(id);
    return found == m_People.end() ? nullptr : &found->second;
}

const Person* PeopleState::find_active_by_name(std::string_view name) const
{
    for (const auto& [id, person] : m_People)
    {
        if (!person.m_Retired && person_names_match(person.m_DisplayName, name))
        {
            return &person;
        }
    }
    return nullptr;
}

std::vector<Person> PeopleState::people() const
{
    std::vector<Person> result;
    result.reserve(m_People.size());
    for (const auto& [id, person] : m_People)
    {
        result.push_back(person);
    }
    return result;
}

Result<void, StorageError> PeopleState::apply(const PeopleBody& body)
{
    struct Visitor
    {
        PeopleState& m_State;

        Result<void, StorageError> operator()(const AddPersonBody& b) const
        {
            const Person& person = b.m_Person;
            if (m_State.knows(person.m_Id))
            {
                return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a person with this id exists"));
            }
            const auto name = normalise_person_name(person.m_DisplayName);
            if (!name || *name != person.m_DisplayName)
            {
                return invalid("a person's name is empty, too long or not written as the rules keep it");
            }
            if (m_State.find_active_by_name(person.m_DisplayName) != nullptr)
            {
                return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a person with this name exists"));
            }
            Person added = person;
            added.m_Retired = false;
            added.m_LastUsedUtc.reset();
            m_State.m_People.emplace(added.m_Id, std::move(added));
            return {};
        }
        Result<void, StorageError> operator()(const RenamePersonBody& b) const
        {
            const auto found = m_State.m_People.find(b.m_Id);
            if (found == m_State.m_People.end())
            {
                return Failure(make_error(StorageErrorKind::NOT_FOUND, "no such person"));
            }
            const auto name = normalise_person_name(b.m_Name);
            if (!name || *name != b.m_Name)
            {
                return invalid("a person's name is empty, too long or not written as the rules keep it");
            }
            const Person* other = m_State.find_active_by_name(b.m_Name);
            if (!found->second.m_Retired && other != nullptr && other->m_Id != b.m_Id)
            {
                return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a person with this name exists"));
            }
            found->second.m_DisplayName = b.m_Name;
            return {};
        }
        Result<void, StorageError> operator()(const RetirePersonBody& b) const
        {
            const auto found = m_State.m_People.find(b.m_Id);
            if (found == m_State.m_People.end())
            {
                return Failure(make_error(StorageErrorKind::NOT_FOUND, "no such person"));
            }
            if (!b.m_Retired && found->second.m_Retired)
            {
                const Person* other = m_State.find_active_by_name(found->second.m_DisplayName);
                if (other != nullptr)
                {
                    return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a person with this name exists"));
                }
            }
            found->second.m_Retired = b.m_Retired;
            return {};
        }
        Result<void, StorageError> operator()(const TouchPersonBody& b) const
        {
            const auto found = m_State.m_People.find(b.m_Id);
            if (found == m_State.m_People.end())
            {
                return Failure(make_error(StorageErrorKind::NOT_FOUND, "no such person"));
            }
            found->second.m_LastUsedUtc = b.m_Utc;
            return {};
        }
        Result<void, StorageError> operator()(const SetPersonLocationBody& b) const
        {
            const auto found = m_State.m_People.find(b.m_Id);
            if (found == m_State.m_People.end())
            {
                return Failure(make_error(StorageErrorKind::NOT_FOUND, "no such person"));
            }
            found->second.m_Location = b.m_Location;
            return {};
        }
        Result<void, StorageError> operator()(const ForgetPersonBody& b) const
        {
            if (m_State.m_Forgotten.contains(b.m_Id))
            {
                return {}; // forgetting twice is the same as once
            }
            // The id of a person this journal never added (their records are gone) is remembered as forgotten all the
            // same, so that an entry that holds the id reads "someone" and is not counted as lost.
            m_State.m_People.erase(b.m_Id);
            m_State.m_Forgotten.insert(b.m_Id);
            return {};
        }
    };
    return std::visit(Visitor{*this}, body);
}

std::vector<PeopleRecord> PeopleState::seed_records() const
{
    std::vector<const Person*> order;
    order.reserve(m_People.size());
    for (const auto& [id, person] : m_People)
    {
        order.push_back(&person);
    }
    // Retired people first, each retired at once, so that a retired and an active person with one name never meet as
    // two active ones; then the others. Within a group, in the order they were created.
    std::stable_sort(order.begin(), order.end(),
                     [](const Person* a, const Person* b)
                     {
                         if (a->m_Retired != b->m_Retired)
                         {
                             return a->m_Retired;
                         }
                         if (a->m_CreatedUtc != b->m_CreatedUtc)
                         {
                             return a->m_CreatedUtc < b->m_CreatedUtc;
                         }
                         return a->m_Id < b->m_Id;
                     });
    std::vector<PeopleRecord> records;
    const auto push = [&records](PeopleBody body)
    {
        PeopleRecord record;
        record.m_Seq = records.size() + 1;
        record.m_Body = std::move(body);
        records.push_back(std::move(record));
    };
    for (const Person* person : order)
    {
        Person added = *person;
        added.m_Retired = false;
        added.m_LastUsedUtc.reset();
        push(AddPersonBody{std::move(added)});
        if (person->m_Retired)
        {
            push(RetirePersonBody{person->m_Id, true});
        }
        if (person->m_LastUsedUtc)
        {
            push(TouchPersonBody{person->m_Id, *person->m_LastUsedUtc});
        }
    }
    for (const PersonId& id : m_Forgotten)
    {
        push(ForgetPersonBody{id});
    }
    return records;
}

// ---------------------------------------------------------------------------------------------------------------------
// Scanning and writing the file
// ---------------------------------------------------------------------------------------------------------------------

PeopleScan scan_people_journal(std::string_view bytes)
{
    PeopleScan scan;
    scan.m_FileBytes = bytes.size();
    const std::size_t last_newline = bytes.rfind('\n');
    scan.m_CompleteBytes = last_newline == std::string_view::npos ? 0 : last_newline + 1;
    scan.m_TornTailBytes = bytes.size() - scan.m_CompleteBytes;

    std::size_t at = 0;
    while (at < scan.m_CompleteBytes)
    {
        const std::size_t end = bytes.find('\n', at);
        const std::string_view line = bytes.substr(at, end - at);
        at = end + 1;
        if (scan.m_HasProblem)
        {
            ++scan.m_BadRecords; // after the first problem nothing is taken: the journal is not carried on past a hole
            continue;
        }
        std::uint64_t unknown_seq = 0;
        const auto record = decode_people_record(line, &unknown_seq);
        if (!record && record.error() == PeopleDecodeError::UNKNOWN_OP && unknown_seq == scan.m_LastSeq + 1)
        {
            ++scan.m_UnknownRecords; // a record type of a later build: skipped, and the sequence goes on
            scan.m_LastSeq = unknown_seq;
            continue;
        }
        if (!record || record->m_Seq != scan.m_LastSeq + 1)
        {
            ++scan.m_BadRecords;
            scan.m_HasProblem = true;
            continue;
        }
        PeopleState trial = scan.m_State;
        if (!trial.apply(record->m_Body))
        {
            ++scan.m_BadRecords;
            scan.m_HasProblem = true;
            continue;
        }
        scan.m_State = std::move(trial);
        scan.m_Records.push_back(*record);
        scan.m_LastSeq = record->m_Seq;
    }
    return scan;
}

Result<void, StorageError> write_people_file(const std::filesystem::path& path,
                                             const std::vector<PeopleRecord>& records)
{
    std::string text;
    for (const PeopleRecord& record : records)
    {
        auto line = encode_people_record(record);
        if (!line)
        {
            return Failure(line.error());
        }
        text += *line;
        text.push_back('\n');
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::remove(temporary, code); // a leftover of a rewrite that was killed: ours, and incomplete
    {
        auto file = platform::open_for_append(temporary);
        if (!file)
        {
            return Failure(file.error());
        }
        protect_path(temporary);
        if (std::fwrite(text.data(), 1, text.size(), file->get()) != text.size())
        {
            return Failure(make_error(StorageErrorKind::IO, "a new people journal could not be written"));
        }
        if (auto synced = platform::durable_sync(file->get()); !synced)
        {
            return synced;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code)
    {
        return Failure(
            make_error(StorageErrorKind::IO, "a new people journal could not be moved into place", code.value()));
    }
    return platform::sync_directory(path.parent_path());
}

} // namespace ej
