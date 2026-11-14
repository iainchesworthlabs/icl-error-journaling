// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/import_messages.hpp"

#include <QCoreApplication>
#include <algorithm>
#include <cstddef>
#include <string>

namespace ej::viewmodel
{

namespace
{

using Code = ConfigError::Code;
using Problem = ConfigError::Problem;

QString translate(const char* source)
{
    return QCoreApplication::translate("ej::viewmodel::ImportMessages", source);
}

QString text_of(const std::string& text)
{
    return QString::fromStdString(text);
}

/// The kind of thing a list holds, for one and for two or more ("Location", "Locations").
struct Noun
{
    QString m_One;
    QString m_Many;
};

Noun noun_of(const std::string& section)
{
    if (section == "error_types")
    {
        return {translate("Error type"), translate("Error types")};
    }
    if (section == "locations")
    {
        return {translate("Location"), translate("Locations")};
    }
    if (section == "interaction_types")
    {
        return {translate("Interaction type"), translate("Interaction types")};
    }
    return {translate("Note template"), translate("Note templates")};
}

/// "the name" for a field of a list item; the field's own key when the app has no word for it.
QString field_name(const std::string& field)
{
    if (field == "name")
    {
        return translate("name");
    }
    if (field == "value_prompt")
    {
        return translate("question to ask");
    }
    if (field == "output_pattern" || field == "line_pattern")
    {
        return translate("line in the log");
    }
    if (field == "board_position")
    {
        return translate("board position");
    }
    if (field == "sort_order")
    {
        return translate("sort order");
    }
    if (field == "retired")
    {
        return translate("retired setting");
    }
    if (field == "icon")
    {
        return translate("icon name");
    }
    if (field == "id")
    {
        return translate("id");
    }
    return text_of(field);
}

/// The key itself of a field such as "time_format/seconds".
QString last_part(const std::string& field)
{
    const std::size_t slash = field.rfind('/');
    return text_of(slash == std::string::npos ? field : field.substr(slash + 1));
}

bool is_list_section(const std::string& section)
{
    return section == "error_types" || section == "locations" || section == "note_templates" ||
           section == "interaction_types";
}

QString whole_file_message(const ConfigError& error)
{
    if (error.m_Section == "schema_version")
    {
        switch (error.m_Code)
        {
        case Code::MISSING_FIELD:
            return translate("The file does not say which version of the setup it is.");
        case Code::UNSUPPORTED_VERSION:
            return translate("The file was made by a newer version of the app. Update the app, then try again.");
        default:
            return translate("The version of the setup in the file is not valid.");
        }
    }
    if (error.m_Section.empty())
    {
        switch (error.m_Code)
        {
        case Code::OUT_OF_RANGE:
            return translate("The file is too large to be a setup file.");
        case Code::WRONG_TYPE:
            return translate("The file is not laid out like a setup file.");
        default:
            return translate("The file could not be read as a setup file.");
        }
    }
    if (error.m_Problem == Problem::PEOPLE_IN_FILE)
    {
        return translate("The file has a list of people. A setup file never holds people: remove it, and add people "
                         "in the app.");
    }
    if (error.m_Code == Code::UNKNOWN_FIELD)
    {
        return translate("The file has a section this app does not use: \xE2\x80\x9C%1\xE2\x80\x9D.")
            .arg(text_of(error.m_Section));
    }
    return translate("The file is missing a part of the setup.");
}

QString list_message(const ConfigError& error)
{
    const Noun noun = noun_of(error.m_Section);
    if (error.m_Item == 0)
    {
        // The list itself.
        switch (error.m_Code)
        {
        case Code::MISSING_FIELD:
            return translate("The file has no list of %1.").arg(noun.m_Many.toLower());
        case Code::OUT_OF_RANGE:
            return translate("There are too many %1 in the file (%2 at most).")
                .arg(noun.m_Many.toLower())
                .arg(static_cast<int>(CONFIG_MAX_ENTRIES_PER_LIST));
        default:
            return translate("The %1 are not written as a list.").arg(noun.m_Many.toLower());
        }
    }

    const qulonglong item = error.m_Item;
    switch (error.m_Problem)
    {
    case Problem::EMPTY:
        return translate("%1 %2 has no %3.").arg(noun.m_One).arg(item).arg(field_name(error.m_Field));
    case Problem::TOO_LONG:
        return translate("%1 %2: the %3 is longer than %4 characters.")
            .arg(noun.m_One)
            .arg(item)
            .arg(field_name(error.m_Field))
            .arg(error.m_Limit);
    case Problem::NAME_TAKEN:
    {
        const qulonglong first = std::min<qulonglong>(error.m_Other, item);
        const qulonglong second = std::max<qulonglong>(error.m_Other, item);
        return translate("%1 %2 and %3 are both called %4.")
            .arg(noun.m_Many)
            .arg(first)
            .arg(second)
            .arg(text_of(error.m_Value));
    }
    case Problem::POSITION_TAKEN:
    {
        const qulonglong first = std::min<qulonglong>(error.m_Other, item);
        const qulonglong second = std::max<qulonglong>(error.m_Other, item);
        return translate("%1 %2 and %3 are both on board position %4.")
            .arg(noun.m_Many)
            .arg(first)
            .arg(second)
            .arg(text_of(error.m_Value));
    }
    case Problem::NO_PLACEHOLDER:
        return translate("%1 %2: the line in the log has no {value} where the answer goes.").arg(noun.m_One).arg(item);
    case Problem::STRAY_BRACE:
        return translate("%1 %2: the line in the log has a stray curly bracket. Write {{ or }} to show one.")
            .arg(noun.m_One)
            .arg(item);
    case Problem::UNKNOWN_PLACEHOLDER:
        return translate("%1 %2: the line in the log has a word in curly brackets the app does not know. Only {value} "
                         "is allowed.")
            .arg(noun.m_One)
            .arg(item);
    case Problem::REPEATED_PLACEHOLDER:
        return translate("%1 %2: the line in the log has {person} or {value} more than once. Each is allowed once.")
            .arg(noun.m_One)
            .arg(item);
    case Problem::PEOPLE_IN_FILE:
    case Problem::OTHER:
        break;
    }

    switch (error.m_Code)
    {
    case Code::UNKNOWN_FIELD:
        if (error.m_Problem == Problem::PEOPLE_IN_FILE)
        {
            break;
        }
        return translate("%1 %2 has a setting this app does not use: \xE2\x80\x9C%3\xE2\x80\x9D.")
            .arg(noun.m_One)
            .arg(item)
            .arg(text_of(error.m_Field));
    case Code::BAD_ID:
        return translate("%1 %2 has an id that is not valid.").arg(noun.m_One).arg(item);
    case Code::DUPLICATE:
        return translate("%1 %2 uses an id that another one already has.").arg(noun.m_One).arg(item);
    case Code::WRONG_TYPE:
        if (error.m_Field.empty())
        {
            return translate("%1 %2 is not written as an entry.").arg(noun.m_One).arg(item);
        }
        return translate("%1 %2: the %3 is the wrong kind of value.")
            .arg(noun.m_One)
            .arg(item)
            .arg(field_name(error.m_Field));
    case Code::OUT_OF_RANGE:
        if (error.m_Field == "board_position")
        {
            return translate("%1 %2: the board position must be from 1 to 11.").arg(noun.m_One).arg(item);
        }
        if (error.m_Field == "icon")
        {
            return translate("%1 %2: the icon name can use only lower-case letters, digits and dashes.")
                .arg(noun.m_One)
                .arg(item);
        }
        break;
    default:
        break;
    }
    return translate("%1 %2: the %3 is not valid.").arg(noun.m_One).arg(item).arg(field_name(error.m_Field));
}

/// A problem with the list of environments of a unit (PRF-11) or with one of its names; `m_Item` is 0 for the list.
QString environments_message(const ConfigError& error)
{
    const qulonglong item = error.m_Item;
    if (item == 0)
    {
        if (error.m_Problem == Problem::EMPTY)
        {
            return translate("The list of environments is empty. Name at least one, for example prod.");
        }
        if (error.m_Code == Code::WRONG_TYPE)
        {
            return translate("The environments are not written as a list of names.");
        }
        return translate("There are too many environments in the file (%1 at most).")
            .arg(static_cast<int>(CONFIG_MAX_ENTRIES_PER_LIST));
    }
    switch (error.m_Problem)
    {
    case Problem::EMPTY:
        return translate("Environment %1 has no name.").arg(item);
    case Problem::TOO_LONG:
        return translate("Environment %1 is longer than %2 characters.").arg(item).arg(error.m_Limit);
    case Problem::NAME_TAKEN:
    {
        const qulonglong first = std::min<qulonglong>(error.m_Other, item);
        const qulonglong second = std::max<qulonglong>(error.m_Other, item);
        return translate("Environments %1 and %2 are both called %3.")
            .arg(first)
            .arg(second)
            .arg(text_of(error.m_Value));
    }
    default:
        break;
    }
    if (error.m_Code == Code::WRONG_TYPE)
    {
        return translate("Environment %1 is not written as text.").arg(item);
    }
    return translate(
               "Environment %1 can use only lower-case letters, digits and dashes, and cannot start or end with a "
               "dash.")
        .arg(item);
}

QString preferences_message(const ConfigError& error)
{
    const std::string& field = error.m_Field;
    if (field.empty())
    {
        if (error.m_Code == Code::MISSING_FIELD)
        {
            return translate("The file has no preferences.");
        }
        return translate("The preferences are not written as expected.");
    }
    if (error.m_Code == Code::UNKNOWN_FIELD)
    {
        return translate("The preferences have a setting this app does not use: \xE2\x80\x9C%1\xE2\x80\x9D.")
            .arg(last_part(field));
    }
    if (field == "unit_environments")
    {
        return environments_message(error);
    }
    if (field == "facilitator" || field == "unit_prefix")
    {
        const QString name = field == "facilitator" ? translate("facilitator") : translate("unit prefix");
        if (error.m_Problem == Problem::EMPTY)
        {
            return translate("The %1 is missing.").arg(name);
        }
        if (error.m_Problem == Problem::TOO_LONG)
        {
            return translate("The %1 is longer than %2 characters.").arg(name).arg(error.m_Limit);
        }
        if (field == "unit_prefix" && error.m_Code == Code::OUT_OF_RANGE)
        {
            return translate("The unit prefix should be letters only, for example UNIT. The app adds the dash and "
                             "the number itself.");
        }
        return translate("The %1 is not valid.").arg(name);
    }
    if (field == "error_prefix")
    {
        if (error.m_Problem == Problem::TOO_LONG)
        {
            return translate("The error prefix is longer than %1 characters.").arg(error.m_Limit);
        }
        if (error.m_Code == Code::WRONG_TYPE)
        {
            return translate("The error prefix must be text.");
        }
        return translate("The error prefix must be on one line, with no tab or line break.");
    }
    if (field == "time_format/style")
    {
        return translate("The clock style must be 24h or 12h.");
    }
    if (field == "time_format/meridiem")
    {
        return translate("The AM and PM style must be upper or lower.");
    }
    if (field == "time_format/seconds" || field == "include_totals")
    {
        return translate("The \xE2\x80\x9C%1\xE2\x80\x9D setting must be true or false.").arg(last_part(field));
    }
    if (field == "time_format")
    {
        return translate("The time format is not written as expected.");
    }
    return translate("The setting \xE2\x80\x9C%1\xE2\x80\x9D is not valid.").arg(last_part(field));
}

} // namespace

QString plain_import_message(const ConfigError& error)
{
    if (is_list_section(error.m_Section))
    {
        return list_message(error);
    }
    if (error.m_Section == "preferences")
    {
        return preferences_message(error);
    }
    return whole_file_message(error);
}

} // namespace ej::viewmodel
