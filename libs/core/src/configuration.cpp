// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/configuration.hpp"

#include "error_journaling/unit_number.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string_view>
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
constexpr int MAX_ICON = 32;

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

std::string pointer_unescape(std::string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '~' && i + 1 < text.size() && (text[i + 1] == '0' || text[i + 1] == '1'))
        {
            out += text[i + 1] == '0' ? '~' : '/';
            ++i;
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

/// What else is known about a problem besides its code, path and message.
struct Detail
{
    ConfigError::Problem m_Problem = ConfigError::Problem::OTHER;
    std::size_t m_Other = 0;
    std::string m_Value;
    int m_Limit = 0;
};

Detail empty_detail()
{
    Detail detail;
    detail.m_Problem = ConfigError::Problem::EMPTY;
    return detail;
}

Detail problem_detail(ConfigError::Problem problem)
{
    Detail detail;
    detail.m_Problem = problem;
    return detail;
}

/// Splits a JSON pointer such as "/error_types/2/name" into the section, the 1-based item and the rest.
void split_path(const std::string& path, ConfigError& error)
{
    std::vector<std::string> parts;
    std::size_t start = 1;
    while (!path.empty() && start <= path.size())
    {
        const std::size_t end = path.find('/', start);
        const std::size_t length = end == std::string::npos ? std::string::npos : end - start;
        parts.push_back(pointer_unescape(std::string_view(path).substr(start, length)));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    if (parts.empty())
    {
        return;
    }
    error.m_Section = parts[0];
    std::size_t next = 1;
    const bool is_list = error.m_Section == "error_types" || error.m_Section == "locations" ||
                         error.m_Section == "note_templates" || error.m_Section == "interaction_types";
    if (is_list && parts.size() > 1 && !parts[1].empty() &&
        parts[1].find_first_not_of("0123456789") == std::string::npos && parts[1].size() < 9)
    {
        error.m_Item = static_cast<std::size_t>(std::stoul(parts[1])) + 1;
        next = 2;
    }
    if (error.m_Section == "preferences" && parts.size() == 3 && parts[1] == "unit_environments" && !parts[2].empty() &&
        parts[2].find_first_not_of("0123456789") == std::string::npos && parts[2].size() < 9)
    {
        // One environment of the list: the item is its 1-based place, the field is the list's key.
        error.m_Item = static_cast<std::size_t>(std::stoul(parts[2])) + 1;
        error.m_Field = parts[1];
        return;
    }
    for (std::size_t i = next; i < parts.size(); ++i)
    {
        if (i != next)
        {
            error.m_Field += "/";
        }
        error.m_Field += parts[i];
    }
}

class Reader
{
public:
    std::vector<ConfigError> m_Errors;

    void add(Code code, const std::string& path, const std::string& message, Detail detail = {})
    {
        ConfigError error;
        error.m_Code = code;
        error.m_Path = path;
        error.m_Message = path.empty() ? message : path + ": " + message;
        error.m_Problem = detail.m_Problem;
        error.m_Other = detail.m_Other;
        error.m_Value = std::move(detail.m_Value);
        error.m_Limit = detail.m_Limit;
        split_path(path, error);
        m_Errors.push_back(std::move(error));
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
            if (!is_known && (it.key() == QLatin1StringView("people") || it.key() == QLatin1StringView("residents")))
            {
                // Never part of a setup file: a name list is kept on the device and not passed around (PRF-08).
                add(Code::UNKNOWN_FIELD, path + "/" + pointer_escape(it.key()),
                    "a setup file never holds people. Remove " + shown(it.key()) +
                        "; people are added in the app and stay on that device.",
                    problem_detail(ConfigError::Problem::PEOPLE_IN_FILE));
            }
            else if (!is_known)
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
                add(Code::MISSING_FIELD, child(path, key), std::string("this is required. Add \"") + key + "\".",
                    empty_detail());
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
            add(Code::OUT_OF_RANGE, here, "this is empty. Enter some text.", empty_detail());
            return std::nullopt;
        }
        if (string.size() > max_length)
        {
            Detail detail = problem_detail(ConfigError::Problem::TOO_LONG);
            detail.m_Limit = max_length;
            add(Code::OUT_OF_RANGE, here,
                "this is longer than " + std::to_string(max_length) + " characters. Shorten it.", std::move(detail));
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
            Detail detail = problem_detail(ConfigError::Problem::NAME_TAKEN);
            detail.m_Other = it->second + 1;
            detail.m_Value = name.toStdString();
            add(Code::DUPLICATE, child(path, "name"),
                std::string("another ") + what + " (number " + std::to_string(it->second + 1) +
                    " in the list) is already called " + shown(name) +
                    ". Rename one of them, or mark one as retired.",
                std::move(detail));
        }
    }

    /// The array at `key`, or nullopt after reporting why it cannot be used.
    std::optional<QJsonArray> list(const QJsonObject& object, const char* key, bool required = true)
    {
        const auto value = field(object, key, "", required);
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

/// The optional `unit_environments` list: the choices for the environment of a unit. Without the key the defaults stay.
void read_unit_environments(Reader& r, const QJsonObject& preferences, Configuration& out)
{
    const std::string path = "/preferences";
    const auto value = r.field(preferences, "unit_environments", path, false);
    if (!value)
    {
        return;
    }
    const auto here = child(path, "unit_environments");
    if (!value->isArray())
    {
        r.add(Code::WRONG_TYPE, here, "this must be a list of names, for example [\"prod\", \"dev\"].");
        return;
    }
    const auto array = value->toArray();
    if (array.isEmpty())
    {
        r.add(Code::OUT_OF_RANGE, here, "this list is empty. Name at least one environment, for example \"prod\".",
              empty_detail());
        return;
    }
    if (static_cast<std::size_t>(array.size()) > CONFIG_MAX_ENTRIES_PER_LIST)
    {
        r.add(Code::OUT_OF_RANGE, here,
              "this list has " + std::to_string(array.size()) + " entries; the most allowed is " +
                  std::to_string(CONFIG_MAX_ENTRIES_PER_LIST) + ". Remove the ones you do not use.");
        return;
    }
    std::vector<std::string> names;
    std::unordered_map<std::string, std::size_t> first_place;
    bool usable = true;
    for (int i = 0; i < array.size(); ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto item_path = child(here, index);
        const auto item = array.at(i);
        if (!item.isString())
        {
            usable = false;
            r.add(Code::WRONG_TYPE, item_path, "this must be text, for example \"prod\".");
            continue;
        }
        const auto text = item.toString();
        const std::string name = text.toStdString();
        if (text.isEmpty())
        {
            usable = false;
            r.add(Code::OUT_OF_RANGE, item_path, "this is empty. Enter a name, for example \"prod\".", empty_detail());
            continue;
        }
        if (static_cast<std::size_t>(text.size()) > UnitNumber::MAX_ENVIRONMENT_LENGTH)
        {
            usable = false;
            Detail detail = problem_detail(ConfigError::Problem::TOO_LONG);
            detail.m_Limit = static_cast<int>(UnitNumber::MAX_ENVIRONMENT_LENGTH);
            r.add(Code::OUT_OF_RANGE, item_path,
                  "this is longer than " + std::to_string(UnitNumber::MAX_ENVIRONMENT_LENGTH) +
                      " characters. Shorten it.",
                  std::move(detail));
            continue;
        }
        if (!UnitNumber::is_valid_environment(name))
        {
            usable = false;
            r.add(Code::OUT_OF_RANGE, item_path,
                  shown(text) + " is not an environment name. Use lower-case letters, digits and dashes, for example "
                                "\"prod\". It cannot start or end with a dash.");
            continue;
        }
        const auto [it, inserted] = first_place.emplace(name, index);
        if (!inserted)
        {
            usable = false;
            Detail detail = problem_detail(ConfigError::Problem::NAME_TAKEN);
            detail.m_Other = it->second + 1;
            detail.m_Value = name;
            r.add(Code::DUPLICATE, item_path,
                  "another environment (number " + std::to_string(it->second + 1) + " in the list) is already " +
                      shown(text) + ". List each environment once.",
                  std::move(detail));
            continue;
        }
        names.push_back(name);
    }
    if (usable)
    {
        out.m_UnitEnvironments = std::move(names);
    }
}

/// The optional `error_prefix` (PRF-13): text, which may be empty, of at most 12 characters on one line. Spaces at either
/// end are dropped. A file without the key keeps the default.
void read_error_prefix(Reader& r, const QJsonObject& object, Configuration& out)
{
    const std::string path = "/preferences";
    const auto value = r.field(object, "error_prefix", path, false);
    if (!value)
    {
        return;
    }
    const auto here = child(path, "error_prefix");
    if (!value->isString())
    {
        r.add(Code::WRONG_TYPE, here, "this must be text.");
        return;
    }
    const std::string text = trim_error_prefix(value->toString().toStdString());
    switch (error_prefix_problem(text))
    {
    case ErrorPrefixProblem::NONE:
        out.m_ErrorPrefix = text;
        break;
    case ErrorPrefixProblem::TOO_LONG:
    {
        Detail detail = problem_detail(ConfigError::Problem::TOO_LONG);
        detail.m_Limit = export_format::MAX_ERROR_PREFIX_CHARACTERS;
        r.add(Code::OUT_OF_RANGE, here,
              "this is longer than " + std::to_string(export_format::MAX_ERROR_PREFIX_CHARACTERS) +
                  " characters. Shorten it.",
              std::move(detail));
        break;
    }
    case ErrorPrefixProblem::CONTROL_CHARACTER:
        r.add(Code::OUT_OF_RANGE, here, "this has a line break or a tab in it. Write it on one line.");
        break;
    }
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

    if (const auto tester = r.text(object, "facilitator", path, true, MAX_NAME)) // legacy name: the setup file key stays "facilitator"
    {
        out.m_Tester = tester->toStdString();
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
    read_unit_environments(r, object, out);
    read_time_format(r, object, out);
    if (const auto totals = r.boolean(object, "include_totals", path))
    {
        out.m_IncludeTotals = *totals;
    }
    read_error_prefix(r, object, out);
    if (const auto enabled = r.boolean(object, "interactions_board_enabled", path))
    {
        out.m_InteractionsBoardEnabled = *enabled;
    }
    r.reject_unknown(object, path,
                     {"facilitator", "unit_prefix", "unit_environments", "time_format", "include_totals", "error_prefix",
                      "interactions_board_enabled"});
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
                Detail detail = problem_detail(ConfigError::Problem::POSITION_TAKEN);
                detail.m_Other = it->second + 1;
                detail.m_Value = std::to_string(*position);
                r.add(Code::DUPLICATE, child(path, "board_position"),
                      "board position " + std::to_string(*position) + " is already used by error type number " +
                          std::to_string(it->second + 1) + ". Give each type on the board its own position.",
                      std::move(detail));
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
                      " has no {value}. Put {value} where the typed text should go, for example \"Played {value}\".",
                  problem_detail(ConfigError::Problem::NO_PLACEHOLDER));
            break;
        case PatternError::UNBALANCED_BRACE:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) + " has a stray brace. Write {{ or }} for a literal brace, and close every {value}.",
                  problem_detail(ConfigError::Problem::STRAY_BRACE));
            break;
        case PatternError::UNKNOWN_PLACEHOLDER:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) + " uses a placeholder the app does not know. Only {value} is allowed.",
                  problem_detail(ConfigError::Problem::UNKNOWN_PLACEHOLDER));
            break;
    }
}

void report_interaction_pattern(Reader& r, const std::string& path, const QString& pattern, InteractionPatternError error)
{
    switch (error)
    {
        case InteractionPatternError::EMPTY:
            r.add(Code::OUT_OF_RANGE, path, "this is empty. Enter a line, for example \"Spoke with {person}\".",
                  empty_detail());
            break;
        case InteractionPatternError::UNBALANCED_BRACE:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) +
                      " has a stray brace. Write {{ or }} for a literal brace, and close every {person} and {value}.",
                  problem_detail(ConfigError::Problem::STRAY_BRACE));
            break;
        case InteractionPatternError::UNKNOWN_PLACEHOLDER:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) + " uses a placeholder the app does not know. Only {person} and {value} are allowed.",
                  problem_detail(ConfigError::Problem::UNKNOWN_PLACEHOLDER));
            break;
        case InteractionPatternError::REPEATED_PLACEHOLDER:
            r.add(Code::BAD_PATTERN, path,
                  shown(pattern) + " has {person} or {value} more than once. Use each at most once.",
                  problem_detail(ConfigError::Problem::REPEATED_PLACEHOLDER));
            break;
    }
}

/// The optional `interaction_types` list: the tiles of the second board. Without the key the list stays empty.
void read_interaction_types(Reader& r, const QJsonObject& root, Configuration& out)
{
    const auto array = r.list(root, "interaction_types", false);
    if (!array)
    {
        return;
    }
    std::unordered_set<InteractionTypeId> ids;
    std::unordered_map<std::string, std::size_t> names;
    std::unordered_map<int, std::size_t> positions;
    for (int i = 0; i < array->size(); ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        const auto path = child("/interaction_types", index);
        const auto object = r.item(*array, i, path);
        if (!object)
        {
            continue;
        }
        InteractionType entry;
        bool usable = true;
        if (const auto id = r.id<InteractionTypeId>(*object, path, ids))
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
        if (const auto pattern = r.text(*object, "line_pattern", path, false, MAX_PATTERN))
        {
            const auto checked = validate_interaction_pattern(pattern->toStdString());
            if (checked)
            {
                entry.m_LinePattern = pattern->toStdString();
                entry.m_Asks = interaction_asks_for(entry.m_LinePattern);
            }
            else
            {
                usable = false;
                report_interaction_pattern(r, child(path, "line_pattern"), *pattern, checked.error());
            }
        }
        if (name)
        {
            r.check_name_unique(names, *name, entry.m_Retired, path, index, "interaction type");
        }
        if (position && !entry.m_Retired)
        {
            const auto [it, inserted] = positions.emplace(*position, index);
            if (!inserted)
            {
                Detail detail = problem_detail(ConfigError::Problem::POSITION_TAKEN);
                detail.m_Other = it->second + 1;
                detail.m_Value = std::to_string(*position);
                r.add(Code::DUPLICATE, child(path, "board_position"),
                      "board position " + std::to_string(*position) + " is already used by interaction type number " +
                          std::to_string(it->second + 1) + ". Give each type on the board its own position.",
                      std::move(detail));
            }
        }
        r.reject_unknown(*object, path, {"id", "name", "retired", "board_position", "line_pattern"});
        if (usable)
        {
            out.m_InteractionTypes.push_back(std::move(entry));
        }
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
        if (const auto icon = r.text(*object, "icon", path, false, MAX_ICON))
        {
            const std::string name = icon->toStdString();
            const bool plain = std::ranges::all_of(
                name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
            if (plain)
            {
                entry.m_Icon = name;
            }
            else
            {
                r.add(Code::OUT_OF_RANGE, child(path, "icon"),
                      shown(*icon) + " is not an icon name. Use lower-case letters, digits and dashes, for example "
                                     "\"music\".");
            }
        }
        if (const auto order = r.integer(*object, "sort_order", path, -MAX_SORT_ORDER, MAX_SORT_ORDER,
                                         "between -1000000 and 1000000"))
        {
            entry.m_SortOrder = *order;
        }
        r.reject_unknown(*object, path, {"id", "name", "value_prompt", "output_pattern", "icon", "sort_order"});
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
    read_interaction_types(r, root, configuration);
    r.reject_unknown(root, "",
                     {"schema_version", "preferences", "error_types", "locations", "note_templates", "interaction_types"});

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
    write_string(out, configuration.m_Tester);
    out += ",\n    \"unit_prefix\": ";
    write_string(out, configuration.m_UnitPrefix);
    if (configuration.m_UnitEnvironments != default_unit_environments())
    {
        out += ",\n    \"unit_environments\": [";
        for (std::size_t i = 0; i < configuration.m_UnitEnvironments.size(); ++i)
        {
            out += i == 0 ? "\n      " : ",\n      ";
            write_string(out, configuration.m_UnitEnvironments[i]);
        }
        out += "\n    ]";
    }
    if (configuration.m_ErrorPrefix != export_format::DEFAULT_ERROR_PREFIX)
    {
        out += ",\n    \"error_prefix\": ";
        write_string(out, configuration.m_ErrorPrefix);
    }
    if (configuration.m_InteractionsBoardEnabled)
    {
        out += std::string(",\n    \"interactions_board_enabled\": ") +
               boolean_text(*configuration.m_InteractionsBoardEnabled);
    }
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
        if (!t.m_Icon.empty())
        {
            out += ",\n      \"icon\": ";
            write_string(out, t.m_Icon);
        }
        out += ",\n      \"sort_order\": " + std::to_string(t.m_SortOrder) + "\n    }";
    }
    out += configuration.m_Templates.empty() ? "]" : "\n  ]";

    if (!configuration.m_InteractionTypes.empty())
    {
        out += ",\n  \"interaction_types\": [";
        for (std::size_t i = 0; i < configuration.m_InteractionTypes.size(); ++i)
        {
            const auto& t = configuration.m_InteractionTypes[i];
            out += i == 0 ? "\n    {\n" : ",\n    {\n";
            out += "      \"id\": ";
            write_string(out, t.m_Id.format());
            out += ",\n      \"name\": ";
            write_string(out, t.m_Name);
            out += std::string(",\n      \"retired\": ") + boolean_text(t.m_Retired);
            if (t.m_BoardPosition)
            {
                out += ",\n      \"board_position\": " + std::to_string(*t.m_BoardPosition);
            }
            if (t.m_LinePattern)
            {
                out += ",\n      \"line_pattern\": ";
                write_string(out, *t.m_LinePattern);
            }
            out += "\n    }";
        }
        out += "\n  ]";
    }
    out += "\n}\n";
    return out;
}

std::string trim_error_prefix(std::string_view text)
{
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(' ');
    return std::string(text.substr(first, last - first + 1));
}

ErrorPrefixProblem error_prefix_problem(std::string_view trimmed)
{
    int characters = 0;
    for (std::size_t i = 0; i < trimmed.size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(trimmed[i]);
        if (byte < 0x20 || byte == 0x7F)
        {
            return ErrorPrefixProblem::CONTROL_CHARACTER;
        }
        // U+0085, U+2028 and U+2029 are line breaks too.
        if (trimmed.substr(i, 2) == "\xC2\x85" || trimmed.substr(i, 3) == "\xE2\x80\xA8" ||
            trimmed.substr(i, 3) == "\xE2\x80\xA9")
        {
            return ErrorPrefixProblem::CONTROL_CHARACTER;
        }
        if ((byte & 0xC0) != 0x80) // not a continuation byte: a new character
        {
            ++characters;
        }
    }
    return characters > export_format::MAX_ERROR_PREFIX_CHARACTERS ? ErrorPrefixProblem::TOO_LONG
                                                                    : ErrorPrefixProblem::NONE;
}

}  // namespace ej
