// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/configuration.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <array>
#include <cmath>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

namespace ej
{
namespace
{

using Code = ConfigError::Code;

constexpr int MAX_SHOWN = 40;
constexpr int MAX_NAME = 200;
constexpr int MAX_PREFIX = 20;
constexpr int MAX_PATTERN = 500;
constexpr int MAX_SORT_ORDER = 1'000'000;

std::string pointer_escape(const QString& key)
{
    std::string out;
    for (const char c : key.toStdString())
    {
        if (c == '~')
        {
            out += "~0";
        }
        else if (c == '/')
        {
            out += "~1";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string child(const std::string& path, const char* key)
{
    return path + "/" + key;
}

std::string child(const std::string& path, std::size_t index)
{
    return path + "/" + std::to_string(index);
}

/// A value for a message, quoted. Long values are cut so a message never echoes a whole long value.
std::string shown(const QString& value)
{
    if (value.size() > MAX_SHOWN)
    {
        return "\"" + value.left(MAX_SHOWN - 3).toStdString() + "...\"";
    }
    return "\"" + value.toStdString() + "\"";
}

class Reader
{
public:
    std::vector<ConfigError> m_Errors;

    void add(Code code, const std::string& path, const std::string& message)
    {
        m_Errors.push_back({code, path, path.empty() ? message : path + ": " + message});
    }

    /// Reports every key of `object` that is not in `known`.
    void reject_unknown(const QJsonObject& object, const std::string& path, std::initializer_list<const char*> known)
    {
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            bool is_known = false;
            for (const char* name : known)
            {
                if (it.key() == QLatin1StringView(name))
                {
                    is_known = true;
                    break;
                }
            }
            if (!is_known)
            {
                add(Code::UNKNOWN_FIELD, path + "/" + pointer_escape(it.key()),
                    "this app does not use " + shown(it.key()) + ". Remove it, or check the spelling.");
            }
        }
    }

    /// The value at `key`; reports MISSING_FIELD when `required` and absent. Null counts as absent.
    std::optional<QJsonValue> field(const QJsonObject& object, const char* key, const std::string& path, bool required)
    {
        const auto value = object.value(QLatin1StringView(key));
        if (value.isUndefined() || value.isNull())
        {
            if (required)
            {
                add(Code::MISSING_FIELD, child(path, key), std::string("this is required. Add \"") + key + "\".");
            }
            return std::nullopt;
        }
        return value;
    }

    /// A string field. Blank text is reported as OUT_OF_RANGE.
    std::optional<QString> text(const QJsonObject& object, const char* key, const std::string& path, bool required,
                                int max_length)
    {
        const auto value = field(object, key, path, required);
        if (!value)
        {
            return std::nullopt;
        }
        const auto here = child(path, key);
        if (!value->isString())
        {
            add(Code::WRONG_TYPE, here, "this must be text.");
            return std::nullopt;
        }
        const auto string = value->toString();
        if (string.trimmed().isEmpty())
        {
            add(Code::OUT_OF_RANGE, here, "this is empty. Enter some text.");
            return std::nullopt;
        }
        if (string.size() > max_length)
        {
            add(Code::OUT_OF_RANGE, here,
                "this is longer than " + std::to_string(max_length) + " characters. Shorten it.");
            return std::nullopt;
        }
        return string;
    }

    std::optional<bool> boolean(const QJsonObject& object, const char* key, const std::string& path)
    {
        const auto value = field(object, key, path, false);
        if (!value)
        {
            return std::nullopt;
        }
        if (!value->isBool())
        {
            add(Code::WRONG_TYPE, child(path, key), "this must be true or false.");
            return std::nullopt;
        }
        return value->toBool();
    }

    /// A whole number in [low, high]; anything else is reported with `range_text`.
    std::optional<int> integer(const QJsonObject& object, const char* key, const std::string& path, int low, int high,
                               const std::string& range_text)
    {
        const auto value = field(object, key, path, false);
        if (!value)
        {
            return std::nullopt;
        }
        const auto here = child(path, key);
        if (!value->isDouble() || std::floor(value->toDouble()) != value->toDouble())
        {
            add(Code::WRONG_TYPE, here, "this must be a whole number.");
            return std::nullopt;
        }
        const double number = value->toDouble();
        if (number < low || number > high)
        {
            add(Code::OUT_OF_RANGE, here, "this must be " + range_text + ".");
            return std::nullopt;
        }
        return static_cast<int>(number);
    }

    /// An id field: a UUID that has not been seen before in `seen`.
    template <typename Id>
    std::optional<Id> id(const QJsonObject& object, const std::string& path, std::unordered_set<Id>& seen)
    {
        const auto value = field(object, "id", path, true);
        if (!value)
        {
            return std::nullopt;
        }
        const auto here = child(path, "id");
        if (!value->isString())
        {
            add(Code::WRONG_TYPE, here, "this must be text in the form 8-4-4-4-12 hexadecimal digits.");
            return std::nullopt;
        }
        const auto parsed = Uuid::parse(value->toString().toStdString());
        if (!parsed || parsed->is_nil())
        {
            add(Code::BAD_ID, here,
                shown(value->toString()) +
                    " is not an id. Use 8-4-4-4-12 hexadecimal digits, for example "
                    "0f8fad5b-d9cb-469f-a165-70867728950e, and not all zeros.");
            return std::nullopt;
        }
        const Id result{*parsed};
        if (!seen.insert(result).second)
        {
            add(Code::DUPLICATE, here, "this id is already used above. Give every entry its own id.");
            return std::nullopt;
        }
        return result;
    }

    /// Names must differ, ignoring case, among live (not retired) entries.
    void check_name_unique(std::unordered_map<std::string, std::size_t>& live_names, const QString& name, bool retired,
                           const std::string& path, std::size_t index, const char* what)
    {
        if (retired)
        {
            return;
        }
        const auto [it, inserted] = live_names.emplace(name.toCaseFolded().toStdString(), index);
        if (!inserted)
        {
            add(Code::DUPLICATE, child(path, "name"),
                std::string("another ") + what + " (number " + std::to_string(it->second + 1) +
                    " in the list) is already called " + shown(name) +
                    ". Rename one of them, or mark one as retired.");
        }
    }

    /// The array at `key`, or nullopt after reporting why it cannot be used.
    std::optional<QJsonArray> list(const QJsonObject& object, const char* key)
    {
        const auto value = field(object, key, "", true);
        if (!value)
        {
            return std::nullopt;
        }
        const auto here = child("", key);
        if (!value->isArray())
        {
            add(Code::WRONG_TYPE, here, "this must be a list.");
            return std::nullopt;
        }
        auto array = value->toArray();
        if (static_cast<std::size_t>(array.size()) > CONFIG_MAX_ENTRIES_PER_LIST)
        {
            add(Code::OUT_OF_RANGE, here,
                "this list has " + std::to_string(array.size()) + " entries; the most allowed is " +
                    std::to_string(CONFIG_MAX_ENTRIES_PER_LIST) + ". Remove the ones you do not use.");
            return std::nullopt;
        }
        return array;
    }

    std::optional<QJsonObject> item(const QJsonArray& array, int index, const std::string& path)
    {
        const auto value = array.at(index);
        if (!value.isObject())
        {
            add(Code::WRONG_TYPE, path, "this must be an object with the fields of one entry.");
            return std::nullopt;
        }
        return value.toObject();
    }
};

void read_time_format(Reader& r, const QJsonObject& preferences, Configuration& out)
{
    const auto value = r.field(preferences, "time_format", "/preferences", false);
    if (!value)
    {
        return;
    }
    const std::string path = "/preferences/time_format";
    if (!value->isObject())
    {
        r.add(Code::WRONG_TYPE, path, "this must be an object with \"style\", \"seconds\" and \"meridiem\".");
        return;
    }
    const auto object = value->toObject();

    if (const auto style = r.text(object, "style", path, false, 10))
    {
        if (*style == QLatin1StringView("24h"))
        {
            out.m_TimeFormat.m_Style = TimeStyle::H24;
        }
        else if (*style == QLatin1StringView("12h"))
        {
            out.m_TimeFormat.m_Style = TimeStyle::H12;
        }
        else
        {
            r.add(Code::OUT_OF_RANGE, child(path, "style"),
                  shown(*style) + " is not a clock style. Use \"24h\" or \"12h\".");
        }
    }
    if (const auto seconds = r.boolean(object, "seconds", path))
    {
        out.m_TimeFormat.m_Seconds = *seconds;
    }
    if (const auto meridiem = r.text(object, "meridiem", path, false, 10))
    {
        if (*meridiem == QLatin1StringView("upper"))
        {
            out.m_TimeFormat.m_Meridiem = MeridiemCase::UPPER;
        }
        else if (*meridiem == QLatin1StringView("lower"))
        {
            out.m_TimeFormat.m_Meridiem = MeridiemCase::LOWER;
        }
        else
        {
            r.add(Code::OUT_OF_RANGE, child(path, "meridiem"),
                  shown(*meridiem) + " is not an AM/PM style. Use \"upper\" (AM, PM) or \"lower\" (am, pm).");
        }
    }
    r.reject_unknown(object, path, {"style", "seconds", "meridiem"});
}

void read_preferences(Reader& r, const QJsonObject& root, Configuration& out)
{
    const auto value = r.field(root, "preferences", "", true);
    if (!value)
    {
        return;
    }
    if (!value->isObject())
    {
        r.add(Code::WRONG_TYPE, "/preferences", "this must be an object.");
        return;
    }
    const auto object = value->toObject();
    const std::string path = "/preferences";

    if (const auto facilitator = r.text(object, "facilitator", path, true, MAX_NAME))
    {
        out.m_Facilitator = facilitator->toStdString();
    }
    if (const auto prefix = r.text(object, "unit_prefix", path, true, MAX_PREFIX))
    {
        if (prefix->contains(QLatin1Char('-')) || prefix->contains(QLatin1Char('#')))
        {
            r.add(Code::OUT_OF_RANGE, child(path, "unit_prefix"),
                  shown(*prefix) +
                      " contains '-' or '#'. The app adds those itself, so use only the letters, for example "
                      "\"UNIT\".");
        }
        else
        {
            out.m_UnitPrefix = prefix->toStdString();
        }
    }
    read_time_format(r, object, out);
    if (const auto totals = r.boolean(object, "include_totals", path))
    {
        out.m_IncludeTotals = *totals;
    }
    r.reject_unknown(object, path, {"facilitator", "unit_prefix", "time_format", "include_totals"});
}

void read_error_types(Reader& r, const QJsonObject& root, Configuration& out)
{
    const auto array = r.list(root, "error_types");
    if (!array)
    {
        return;
    }
    std::unordered_set<ErrorTypeId> ids;
    std::unordered_map<std::string, std::size_t> names;
    std::unordered_map<int, std::size_t> positions;
    for (int i = 0; i < array->size(); ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto path = child("/error_types", index);
        const auto object = r.item(*array, i, path);
        if (!object)
        {
            continue;
        }
        ErrorType entry;
        bool usable = true;
        if (const auto id = r.id<ErrorTypeId>(*object, path, ids))
        {
            entry.m_Id = *id;
        }
        else
        {
            usable = false;
        }
        const auto name = r.text(*object, "name", path, true, MAX_NAME);
        if (name)
        {
            entry.m_Name = name->toStdString();
        }
        else
        {
            usable = false;
        }
        if (const auto retired = r.boolean(*object, "retired", path))
        {
            entry.m_Retired = *retired;
        }
        const auto position = r.integer(*object, "board_position", path, 1, CONFIG_BOARD_POSITIONS, "from 1 to 11");
        if (position)
        {
            entry.m_BoardPosition = *position;
        }
        if (name)
        {
            r.check_name_unique(names, *name, entry.m_Retired, path, index, "error type");
        }
        if (position && !entry.m_Retired)
        {
            const auto [it, inserted] = positions.emplace(*position, index);
            if (!inserted)
            {
                r.add(Code::DUPLICATE, child(path, "board_position"),
                      "board position " + std::to_string(*position) + " is already used by error type number " +
                          std::to_string(it->second + 1) + ". Give each type on the board its own position.");
            }
        }
        r.reject_unknown(*object, path, {"id", "name", "retired", "board_position"});
        if (usable)
        {
            out.m_ErrorTypes.push_back(std::move(entry));
        }
    }
}

void read_locations(Reader& r, const QJsonObject& root, Configuration& out)
{
    const auto array = r.list(root, "locations");
    if (!array)
    {
        return;
    }
    std::unordered_set<LocationId> ids;
    std::unordered_map<std::string, std::size_t> names;
    for (int i = 0; i < array->size(); ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto path = child("/locations", index);
        const auto object = r.item(*array, i, path);
        if (!object)
        {
            continue;
        }
        Location entry;
        bool usable = true;
        if (const auto id = r.id<LocationId>(*object, path, ids))
        {
            entry.m_Id = *id;
        }
        else
        {
            usable = false;
        }
        const auto name = r.text(*object, "name", path, true, MAX_NAME);
        if (name)
        {
            entry.m_Name = name->toStdString();
        }
        else
        {
            usable = false;
        }
        if (const auto order = r.integer(*object, "sort_order", path, -MAX_SORT_ORDER, MAX_SORT_ORDER,
                                         "between -1000000 and 1000000"))
        {
            entry.m_SortOrder = *order;
        }
        if (const auto retired = r.boolean(*object, "retired", path))
        {
            entry.m_Retired = *retired;
        }
        if (name)
        {
            r.check_name_unique(names, *name, entry.m_Retired, path, index, "location");
        }
        r.reject_unknown(*object, path, {"id", "name", "sort_order", "retired"});
        if (usable)
        {
            out.m_Locations.push_back(std::move(entry));
        }
    }
}

void report_pattern(Reader& r, const std::string& path, const QString& pattern, PatternError error)
{
    switch (error)
    {
        case PatternError::MISSING_PLACEHOLDER:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) +
                      " has no {value}. Put {value} where the typed text should go, for example \"Played {value}\".");
            break;
        case PatternError::UNBALANCED_BRACE:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) + " has a stray brace. Write {{ or }} for a literal brace, and close every {value}.");
            break;
        case PatternError::UNKNOWN_PLACEHOLDER:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) + " uses a placeholder the app does not know. Only {value} is allowed.");
            break;
    }
}

void read_templates(Reader& r, const QJsonObject& root, Configuration& out)
{
    const auto array = r.list(root, "note_templates");
    if (!array)
    {
        return;
    }
    std::unordered_set<NoteTemplateId> ids;
    std::unordered_map<std::string, std::size_t> names;
    for (int i = 0; i < array->size(); ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto path = child("/note_templates", index);
        const auto object = r.item(*array, i, path);
        if (!object)
        {
            continue;
        }
        NoteTemplate entry;
        bool usable = true;
        if (const auto id = r.id<NoteTemplateId>(*object, path, ids))
        {
            entry.m_Id = *id;
        }
        else
        {
            usable = false;
        }
        if (const auto name = r.text(*object, "name", path, true, MAX_NAME))
        {
            entry.m_Name = name->toStdString();
            r.check_name_unique(names, *name, false, path, index, "note template");
        }
        else
        {
            usable = false;
        }
        if (const auto prompt = r.text(*object, "value_prompt", path, true, MAX_NAME))
        {
            entry.m_ValuePrompt = prompt->toStdString();
        }
        else
        {
            usable = false;
        }
        if (const auto pattern = r.text(*object, "output_pattern", path, true, MAX_PATTERN))
        {
            const auto checked = validate_pattern(pattern->toStdString());
            if (checked)
            {
                entry.m_OutputPattern = pattern->toStdString();
            }
            else
            {
                usable = false;
                report_pattern(r, child(path, "output_pattern"), *pattern, checked.error());
            }
        }
        else
        {
            usable = false;
        }
        if (const auto order = r.integer(*object, "sort_order", path, -MAX_SORT_ORDER, MAX_SORT_ORDER,
                                         "between -1000000 and 1000000"))
        {
            entry.m_SortOrder = *order;
        }
        r.reject_unknown(*object, path, {"id", "name", "value_prompt", "output_pattern", "sort_order"});
        if (usable)
        {
            out.m_Templates.push_back(std::move(entry));
        }
    }
}

/// Returns false when nothing else in the file can be trusted (the version is not one this app reads).
bool read_version(Reader& r, const QJsonObject& root)
{
    const auto value = root.value(QLatin1StringView("schema_version"));
    if (value.isUndefined() || value.isNull())
    {
        r.add(Code::MISSING_FIELD, "/schema_version",
              "this is required. Add \"schema_version\": " + std::to_string(CONFIG_SCHEMA_VERSION) +
                  " at the top of the file.");
        return false;
    }
    if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble())
    {
        r.add(Code::WRONG_TYPE, "/schema_version",
              "this must be a whole number such as " + std::to_string(CONFIG_SCHEMA_VERSION) + ".");
        return false;
    }
    const double number = value.toDouble();
    if (number > CONFIG_SCHEMA_VERSION)
    {
        const std::string found = number < 1e9 ? " " + std::to_string(static_cast<long long>(number)) : "";
        r.add(Code::UNSUPPORTED_VERSION, "/schema_version",
              "this file was written by a newer version of the app, with schema version" + found +
                  "; this app reads up to " + std::to_string(CONFIG_SCHEMA_VERSION) +
                  ". Update the app, then import the file again.");
        return false;
    }
    if (number < 1)
    {
        r.add(Code::OUT_OF_RANGE, "/schema_version",
              "this must be " + std::to_string(CONFIG_SCHEMA_VERSION) + " or more. The first version is 1.");
        return false;
    }
    return true;
}

void write_string(std::string& out, std::string_view text)
{
    constexpr std::string_view hex = "0123456789abcdef";
    out += '"';
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        switch (c)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20)
                {
                    out += "\\u00";
                    out += hex[byte >> 4];
                    out += hex[byte & 0xF];
                }
                else
                {
                    out += c;
                }
        }
    }
    out += '"';
}

const char* boolean_text(bool value)
{
    return value ? "true" : "false";
}

}  // namespace

Result<Configuration, std::vector<ConfigError>> import_configuration(std::string_view json)
{
    Reader r;
    if (json.size() > CONFIG_MAX_BYTES)
    {
        r.add(Code::OUT_OF_RANGE, "",
              "The file is " + std::to_string(json.size() / 1024) + " KiB; the largest configuration file allowed is " +
                  std::to_string(CONFIG_MAX_BYTES / 1024) + " KiB. Check that it is the right file.");
        return fail(std::move(r.m_Errors));
    }
    if (json.starts_with("\xEF\xBB\xBF"))
    {
        json.remove_prefix(3);
    }

    QJsonParseError parse_error;
    const auto document =
        QJsonDocument::fromJson(QByteArray(json.data(), static_cast<qsizetype>(json.size())), &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        r.add(Code::MALFORMED_JSON, "",
              "The file is not valid JSON (" + parse_error.errorString().toStdString() + " near byte " +
                  std::to_string(parse_error.offset) + "). Open it in a text editor and fix that spot.");
        return fail(std::move(r.m_Errors));
    }
    if (!document.isObject())
    {
        r.add(Code::WRONG_TYPE, "", "The top level of the file must be an object { ... }, not a list.");
        return fail(std::move(r.m_Errors));
    }

    const auto root = document.object();
    if (!read_version(r, root))
    {
        return fail(std::move(r.m_Errors));
    }

    Configuration configuration;
    read_preferences(r, root, configuration);
    read_error_types(r, root, configuration);
    read_locations(r, root, configuration);
    read_templates(r, root, configuration);
    r.reject_unknown(root, "", {"schema_version", "preferences", "error_types", "locations", "note_templates"});

    if (!r.m_Errors.empty())
    {
        return fail(std::move(r.m_Errors));
    }
    return configuration;
}

std::string export_configuration(const Configuration& configuration)
{

    std::string out;
    out += "{\n  \"schema_version\": " + std::to_string(CONFIG_SCHEMA_VERSION) + ",\n";
    out += "  \"preferences\": {\n    \"facilitator\": ";
    write_string(out, configuration.m_Facilitator);
    out += ",\n    \"unit_prefix\": ";
    write_string(out, configuration.m_UnitPrefix);
    out += ",\n    \"time_format\": {\n";
    out += std::string("      \"style\": ") +
           (configuration.m_TimeFormat.m_Style == TimeStyle::H12 ? "\"12h\"" : "\"24h\"") + ",\n";
    out += std::string("      \"seconds\": ") + boolean_text(configuration.m_TimeFormat.m_Seconds) + ",\n";
    out += std::string("      \"meridiem\": ") +
           (configuration.m_TimeFormat.m_Meridiem == MeridiemCase::LOWER ? "\"lower\"" : "\"upper\"") + "\n    },\n";
    out += std::string("    \"include_totals\": ") + boolean_text(configuration.m_IncludeTotals) + "\n  },\n";

    out += "  \"error_types\": [";
    for (std::size_t i = 0; i < configuration.m_ErrorTypes.size(); ++i)
    {
        const auto& e = configuration.m_ErrorTypes[i];
        out += i == 0 ? "\n    {\n" : ",\n    {\n";
        out += "      \"id\": ";
        write_string(out, e.m_Id.format());
        out += ",\n      \"name\": ";
        write_string(out, e.m_Name);
        out += std::string(",\n      \"retired\": ") + boolean_text(e.m_Retired);
        if (e.m_BoardPosition)
        {
            out += ",\n      \"board_position\": " + std::to_string(*e.m_BoardPosition);
        }
        out += "\n    }";
    }
    out += configuration.m_ErrorTypes.empty() ? "],\n" : "\n  ],\n";

    out += "  \"locations\": [";
    for (std::size_t i = 0; i < configuration.m_Locations.size(); ++i)
    {
        const auto& l = configuration.m_Locations[i];
        out += i == 0 ? "\n    {\n" : ",\n    {\n";
        out += "      \"id\": ";
        write_string(out, l.m_Id.format());
        out += ",\n      \"name\": ";
        write_string(out, l.m_Name);
        out += ",\n      \"sort_order\": " + std::to_string(l.m_SortOrder);
        out += std::string(",\n      \"retired\": ") + boolean_text(l.m_Retired) + "\n    }";
    }
    out += configuration.m_Locations.empty() ? "],\n" : "\n  ],\n";

    out += "  \"note_templates\": [";
    for (std::size_t i = 0; i < configuration.m_Templates.size(); ++i)
    {
        const auto& t = configuration.m_Templates[i];
        out += i == 0 ? "\n    {\n" : ",\n    {\n";
        out += "      \"id\": ";
        write_string(out, t.m_Id.format());
        out += ",\n      \"name\": ";
        write_string(out, t.m_Name);
        out += ",\n      \"value_prompt\": ";
        write_string(out, t.m_ValuePrompt);
        out += ",\n      \"output_pattern\": ";
        write_string(out, t.m_OutputPattern);
        out += ",\n      \"sort_order\": " + std::to_string(t.m_SortOrder) + "\n    }";
    }
    out += configuration.m_Templates.empty() ? "]\n" : "\n  ]\n";
    out += "}\n";
    return out;
}

}  // namespace ej
