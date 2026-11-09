// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/configuration.hpp"
#include "golden_scenarios.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/import_messages.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <string>

using namespace ej;
using namespace ej::test;
using viewmodel::plain_import_message;

namespace
{

QJsonObject sample_json()
{
    const std::string text = export_configuration(sample_configuration());
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(text));
    REQUIRE(document.isObject());
    return document.object();
}

/// The plain sentences for the problems of `text`, in the order the importer found them.
QStringList messages_of_text(const std::string& text)
{
    const auto result = import_configuration(text);
    REQUIRE_FALSE(result.has_value());
    QStringList messages;
    for (const ConfigError& error : result.error())
    {
        messages.push_back(plain_import_message(error));
    }
    return messages;
}

QStringList messages_of(const QJsonObject& object)
{
    return messages_of_text(QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString());
}

/// Sets (or, with an undefined value, removes) `key` of item `index` of the list `list`.
void set_item(QJsonObject& root, const char* list, int index, const char* key, const QJsonValue& value)
{
    QJsonArray array = root.value(list).toArray();
    QJsonObject item = array.at(index).toObject();
    if (value.isUndefined())
    {
        item.remove(key);
    }
    else
    {
        item.insert(key, value);
    }
    array.replace(index, item);
    root.insert(list, array);
}

void set_preference(QJsonObject& root, const char* key, const QJsonValue& value)
{
    QJsonObject preferences = root.value("preferences").toObject();
    if (value.isUndefined())
    {
        preferences.remove(key);
    }
    else
    {
        preferences.insert(key, value);
    }
    root.insert("preferences", preferences);
}

void set_time_format(QJsonObject& root, const char* key, const QJsonValue& value)
{
    QJsonObject preferences = root.value("preferences").toObject();
    QJsonObject time_format = preferences.value("time_format").toObject();
    time_format.insert(key, value);
    preferences.insert("time_format", time_format);
    root.insert("preferences", preferences);
}

constexpr auto UNDEFINED = QJsonValue::Undefined;

QString curly(const QString& text)
{
    return QStringLiteral("“") + text + QStringLiteral("”");
}

} // namespace

TEST_CASE("The three problems of the import designs are the three plain sentences of the design",
          "[viewmodel][import][unit]")
{
    // ImportErrors.dc.html: an error type with no name, two locations with one name, a template without {value}.
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[2].m_Name = "";
    configuration.m_Locations[1].m_Name = "North Site";
    configuration.m_Templates[0].m_OutputPattern = "Played";

    const QStringList messages = messages_of_text(export_configuration(configuration));

    REQUIRE(messages.size() == 3);
    CHECK(messages.at(0) == "Error type 3 has no name.");
    CHECK(messages.at(1) == "Locations 1 and 2 are both called North Site.");
    CHECK(messages.at(2) == "Note template 1: the line in the log has no {value} where the answer goes.");
}

TEST_CASE("No sentence shows a path of the file or a quoted value", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();
    set_item(root, "error_types", 0, "name", QString());
    set_item(root, "error_types", 1, "colour", "red");
    set_item(root, "locations", 0, "sort_order", 99999999);
    set_preference(root, "facilitator", UNDEFINED);

    for (const QString& message : messages_of(root))
    {
        INFO(message.toStdString());
        CHECK_FALSE(message.contains('/'));
        CHECK(message.endsWith('.'));
    }
}

TEST_CASE("A name that is empty, missing or too long is said in a sentence about the item", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();

    SECTION("an empty name")
    {
        set_item(root, "locations", 1, "name", "   ");
        CHECK(messages_of(root) == QStringList{"Location 2 has no name."});
    }
    SECTION("a missing name")
    {
        set_item(root, "error_types", 0, "name", UNDEFINED);
        CHECK(messages_of(root) == QStringList{"Error type 1 has no name."});
    }
    SECTION("a missing question and a missing id")
    {
        set_item(root, "note_templates", 0, "value_prompt", UNDEFINED);
        set_item(root, "error_types", 0, "id", UNDEFINED);
        CHECK(messages_of(root) == QStringList{"Error type 1 has no id.", "Note template 1 has no question to ask."});
    }
    SECTION("a name that is too long")
    {
        set_item(root, "error_types", 0, "name", QString(201, 'x'));
        CHECK(messages_of(root) == QStringList{"Error type 1: the name is longer than 200 characters."});
    }
    SECTION("a name of the wrong kind")
    {
        set_item(root, "error_types", 0, "name", 7);
        CHECK(messages_of(root) == QStringList{"Error type 1: the name is the wrong kind of value."});
    }
}

TEST_CASE("Two items with one name are named by their places in the list, smaller first", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();

    SECTION("error types")
    {
        set_item(root, "error_types", 3, "name", "Wrong Response");
        CHECK(messages_of(root) == QStringList{"Error types 1 and 4 are both called Wrong Response."});
    }
    SECTION("note templates")
    {
        set_item(root, "note_templates", 1, "name", "Played song");
        CHECK(messages_of(root) == QStringList{"Note templates 1 and 2 are both called Played song."});
    }
    SECTION("the case does not matter")
    {
        set_item(root, "locations", 1, "name", "north site");
        CHECK(messages_of(root) == QStringList{"Locations 1 and 2 are both called north site."});
    }
}

TEST_CASE("Two types on one board position are named by their places", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();
    set_item(root, "error_types", 2, "board_position", 1);

    CHECK(messages_of(root) == QStringList{"Error types 1 and 3 are both on board position 1."});
}

TEST_CASE("A pattern is refused in a sentence for each of its three faults", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();

    SECTION("no placeholder")
    {
        set_item(root, "note_templates", 1, "output_pattern", "Visitor");
        CHECK(messages_of(root) ==
              QStringList{"Note template 2: the line in the log has no {value} where the answer goes."});
    }
    SECTION("a stray brace")
    {
        set_item(root, "note_templates", 0, "output_pattern", "Played {value");
        CHECK(
            messages_of(root) ==
            QStringList{"Note template 1: the line in the log has a stray curly bracket. Write {{ or }} to show one."});
    }
    SECTION("a placeholder the app does not know")
    {
        set_item(root, "note_templates", 0, "output_pattern", "Played {name}");
        CHECK(messages_of(root) ==
              QStringList{"Note template 1: the line in the log has a word in curly brackets the app does not "
                          "know. Only {value} is allowed."});
    }
}

TEST_CASE("Other faults of an item have a sentence each", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();

    SECTION("a setting the app does not use")
    {
        set_item(root, "error_types", 0, "colour", "red");
        CHECK(messages_of(root) ==
              QStringList{"Error type 1 has a setting this app does not use: " + curly("colour") + "."});
    }
    SECTION("an id that is not an id")
    {
        set_item(root, "locations", 0, "id", "not an id");
        CHECK(messages_of(root) == QStringList{"Location 1 has an id that is not valid."});
    }
    SECTION("an id that is used twice")
    {
        set_item(root, "locations", 1, "id", "00000000-0000-4000-8000-0000000000b1");
        CHECK(messages_of(root) == QStringList{"Location 2 uses an id that another one already has."});
    }
    SECTION("an id of the wrong kind")
    {
        set_item(root, "locations", 1, "id", 5);
        CHECK(messages_of(root) == QStringList{"Location 2: the id is the wrong kind of value."});
    }
    SECTION("an entry that is not an entry")
    {
        QJsonArray locations = root.value("locations").toArray();
        locations.replace(0, 5);
        root.insert("locations", locations);
        CHECK(messages_of(root) == QStringList{"Location 1 is not written as an entry."});
    }
    SECTION("a board position off the board")
    {
        set_item(root, "error_types", 0, "board_position", 12);
        CHECK(messages_of(root) == QStringList{"Error type 1: the board position must be from 1 to 11."});
    }
    SECTION("a sort order out of range")
    {
        set_item(root, "locations", 0, "sort_order", 99999999);
        CHECK(messages_of(root) == QStringList{"Location 1: the sort order is not valid."});
    }
    SECTION("a flag that is not true or false")
    {
        set_item(root, "locations", 0, "retired", "no");
        CHECK(messages_of(root) == QStringList{"Location 1: the retired setting is the wrong kind of value."});
    }
    SECTION("an icon name with capitals")
    {
        set_item(root, "note_templates", 0, "icon", "Music Note");
        CHECK(messages_of(root) ==
              QStringList{"Note template 1: the icon name can use only lower-case letters, digits and dashes."});
    }
}

TEST_CASE("A list that is missing, not a list or too long is said as a list", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();

    SECTION("missing")
    {
        root.remove("note_templates");
        CHECK(messages_of(root) == QStringList{"The file has no list of note templates."});
    }
    SECTION("not a list")
    {
        root.insert("locations", "North Site");
        CHECK(messages_of(root) == QStringList{"The locations are not written as a list."});
    }
    SECTION("too long")
    {
        QJsonArray types;
        for (int i = 0; i < 501; ++i)
        {
            types.append(QJsonObject());
        }
        root.insert("error_types", types);
        CHECK(messages_of(root) == QStringList{"There are too many error types in the file (500 at most)."});
    }
}

TEST_CASE("The preferences are refused in sentences about the setting", "[viewmodel][import][unit]")
{
    QJsonObject root = sample_json();

    SECTION("a missing facilitator and a missing prefix")
    {
        set_preference(root, "facilitator", UNDEFINED);
        set_preference(root, "unit_prefix", "");
        CHECK(messages_of(root) == QStringList{"The facilitator is missing.", "The unit prefix is missing."});
    }
    SECTION("a facilitator that is too long")
    {
        set_preference(root, "facilitator", QString(201, 'x'));
        CHECK(messages_of(root) == QStringList{"The facilitator is longer than 200 characters."});
    }
    SECTION("a prefix with a dash")
    {
        set_preference(root, "unit_prefix", "UNIT-");
        CHECK(messages_of(root) ==
              QStringList{"The unit prefix should be letters only, for example UNIT. The app adds the dash and the "
                          "number itself."});
    }
    SECTION("a prefix of the wrong kind")
    {
        set_preference(root, "unit_prefix", 5);
        CHECK(messages_of(root) == QStringList{"The unit prefix is not valid."});
    }
    SECTION("a clock style and an AM and PM style that are not known")
    {
        set_time_format(root, "style", "13h");
        set_time_format(root, "meridiem", "mixed");
        CHECK(messages_of(root) ==
              QStringList{"The clock style must be 24h or 12h.", "The AM and PM style must be upper or lower."});
    }
    SECTION("flags that are not true or false")
    {
        set_time_format(root, "seconds", "yes");
        set_preference(root, "include_totals", 1);
        CHECK(messages_of(root) == QStringList{"The " + curly("seconds") + " setting must be true or false.",
                                               "The " + curly("include_totals") + " setting must be true or false."});
    }
    SECTION("a time format that is not an object")
    {
        set_preference(root, "time_format", 5);
        CHECK(messages_of(root) == QStringList{"The time format is not written as expected."});
    }
    SECTION("preferences that are not an object")
    {
        root.insert("preferences", 5);
        CHECK(messages_of(root) == QStringList{"The preferences are not written as expected."});
    }
    SECTION("no preferences")
    {
        root.remove("preferences");
        CHECK(messages_of(root) == QStringList{"The file has no preferences."});
    }
    SECTION("settings the app does not use")
    {
        set_preference(root, "theme", "dark");
        set_time_format(root, "zone", "UTC");
        CHECK(messages_of(root) ==
              QStringList{"The preferences have a setting this app does not use: " + curly("zone") + ".",
                          "The preferences have a setting this app does not use: " + curly("theme") + "."});
    }
    SECTION("a setting of the wrong kind that has no word of its own")
    {
        set_preference(root, "facilitator", true);
        CHECK(messages_of(root) == QStringList{"The facilitator is not valid."});
    }
}

TEST_CASE("A file that is not a setup file is refused in a sentence about the file", "[viewmodel][import][unit]")
{
    SECTION("not JSON")
    {
        CHECK(messages_of_text("{ \"schema_version\": ") == QStringList{"The file could not be read as a setup file."});
    }
    SECTION("a list at the top")
    {
        CHECK(messages_of_text("[1, 2]") == QStringList{"The file is not laid out like a setup file."});
    }
    SECTION("too large")
    {
        CHECK(messages_of_text(std::string(2 * 1024 * 1024, ' ')) ==
              QStringList{"The file is too large to be a setup file."});
    }
    SECTION("no version")
    {
        CHECK(messages_of_text("{}") == QStringList{"The file does not say which version of the setup it is."});
    }
    SECTION("a version from a newer app")
    {
        CHECK(messages_of_text("{\"schema_version\": 7}") ==
              QStringList{"The file was made by a newer version of the app. Update the app, then try again."});
    }
    SECTION("a version that is not a version")
    {
        CHECK(messages_of_text("{\"schema_version\": \"one\"}") ==
              QStringList{"The version of the setup in the file is not valid."});
        CHECK(messages_of_text("{\"schema_version\": 0}") ==
              QStringList{"The version of the setup in the file is not valid."});
    }
    SECTION("a part of the file the app does not use")
    {
        QJsonObject root = sample_json();
        root.insert("extra", 1);
        CHECK(messages_of(root) ==
              QStringList{"The file has a section this app does not use: " + curly("extra") + "."});
    }
}

TEST_CASE("The structured fields of an error carry what the sentence needs", "[viewmodel][import][unit]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[2].m_Name = "";
    configuration.m_Locations[1].m_Name = "North Site";
    const auto result = import_configuration(export_configuration(configuration));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().size() == 2);

    const ConfigError& empty = result.error().at(0);
    CHECK(empty.m_Problem == ConfigError::Problem::EMPTY);
    CHECK(empty.m_Section == "error_types");
    CHECK(empty.m_Item == 3);
    CHECK(empty.m_Field == "name");
    // The wording for a person who edits the file by hand is still there, with the path.
    CHECK(empty.m_Message.starts_with("/error_types/2/name: "));

    const ConfigError& taken = result.error().at(1);
    CHECK(taken.m_Problem == ConfigError::Problem::NAME_TAKEN);
    CHECK(taken.m_Item == 2);
    CHECK(taken.m_Other == 1);
    CHECK(taken.m_Value == "North Site");
}
