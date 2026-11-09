// SPDX-License-Identifier: GPL-3.0-or-later
//
// The environment choices of a unit (PRF-11) in SettingsModel: the default list, a setup file that replaces it, the
// export that writes the key only when the list is not the default, and the plain sentences for a bad list. Unit tests
// on the fake store and the manual gateway.
#include "settings_rig.hpp"
#include "viewmodel/import_messages.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

using namespace ej;
using namespace ej::test;
using viewmodel::plain_import_message;
using viewmodel::SettingsModel;

namespace
{

std::string sample_text()
{
    return export_configuration(sample_configuration());
}

const std::string PREFIX_LINE = "\"unit_prefix\": \"UNIT\",";

/// The sample with a `unit_environments` list after the prefix.
std::string with_environments(const std::string& list)
{
    std::string text = sample_text();
    const auto at = text.find(PREFIX_LINE);
    REQUIRE(at != std::string::npos);
    text.insert(at + PREFIX_LINE.size(), "\n    \"unit_environments\": " + list + ",");
    return text;
}

QStringList messages_of(const std::string& text)
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

} // namespace

TEST_CASE("The choices of the environment are prod and dev until a setup file says otherwise",
          "[viewmodel][settings][environment]")
{
    SettingsRig rig;
    // Before anything is set up (first launch, manual setup) the list is the default.
    CHECK(rig.m_Settings.unit_environments() == QStringList{"prod", "dev"});
    rig.load_sample();
    CHECK(rig.m_Settings.unit_environments() == QStringList{"prod", "dev"});
    CHECK(rig.m_Settings.property("unitEnvironments").toStringList() == QStringList{"prod", "dev"});
    // The sample has no such key, so it is still saved and exported without one.
    CHECK(rig.store().saved() == sample_text());
}

TEST_CASE("A setup file with its own list replaces the choices, and the export keeps it",
          "[viewmodel][settings][environment]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy changed(&rig.m_Settings, &SettingsModel::configurationChanged);
    const std::string text = with_environments("[\n      \"staging\",\n      \"prod\"\n    ]");
    rig.store().add_file("/files/setup.json", text);

    rig.m_Settings.loadImport("/files/setup.json");
    rig.m_Gateway.run_all();
    rig.m_Settings.applyImport();
    rig.commit();

    CHECK(rig.m_Settings.unit_environments() == QStringList{"staging", "prod"});
    CHECK(changed.count() >= 1);
    CHECK(*rig.store().saved() == text);

    // The choices come back after a restart.
    SettingsRig again;
    again.load_text(text);
    CHECK(again.m_Settings.unit_environments() == QStringList{"staging", "prod"});

    rig.m_Settings.exportTo("/out/setup.json");
    rig.m_Gateway.run_all();
    CHECK(rig.store().files().at("/out/setup.json") == text);
}

TEST_CASE("Importing a file without the key puts the defaults back", "[viewmodel][settings][environment]")
{
    SettingsRig rig;
    Configuration custom = sample_configuration();
    custom.m_UnitEnvironments = {"qa"};
    rig.load_configuration(custom);
    REQUIRE(rig.m_Settings.unit_environments() == QStringList{"qa"});
    rig.store().add_file("/files/setup.json", sample_text());

    rig.m_Settings.loadImport("/files/setup.json");
    rig.m_Gateway.run_all();
    rig.m_Settings.applyImport();
    rig.commit();

    CHECK(rig.m_Settings.unit_environments() == QStringList{"prod", "dev"});
    CHECK(*rig.store().saved() == sample_text());
}

TEST_CASE("A bad list of environments is refused in a plain sentence and changes nothing",
          "[viewmodel][settings][environment]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::importFailed);

    const auto refused = [&](const std::string& list)
    {
        rig.store().add_file("/f.json", with_environments(list));
        rig.m_Settings.loadImport("/f.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() >= 1);
        const QStringList messages = failed.last().at(0).toStringList();
        failed.clear();
        return messages;
    };

    CHECK(refused("[]") == QStringList{"The list of environments is empty. Name at least one, for example prod."});
    CHECK(refused("[\"prod\", \"Dev\"]") ==
          QStringList{"Environment 2 can use only lower-case letters, digits and dashes, and cannot start or end with "
                      "a dash."});
    CHECK(refused("[\"prod\", \"prod\"]") == QStringList{"Environments 1 and 2 are both called prod."});
    CHECK(prepared.count() == 0);
    CHECK(rig.m_Settings.unit_environments() == QStringList{"prod", "dev"});
    CHECK(rig.store().saves().size() == 0);
}

TEST_CASE("Every problem of the environments has a sentence of its own, without a path of the file",
          "[viewmodel][import][unit][environment]")
{
    // The list itself.
    CHECK(messages_of(with_environments("[]")) ==
          QStringList{"The list of environments is empty. Name at least one, for example prod."});
    CHECK(messages_of(with_environments("\"prod\"")) ==
          QStringList{"The environments are not written as a list of names."});
    CHECK(messages_of(with_environments("{}")) == QStringList{"The environments are not written as a list of names."});

    // One name, by its place in the list, counting from 1.
    CHECK(messages_of(with_environments("[\"prod\", 5]")) == QStringList{"Environment 2 is not written as text."});
    CHECK(messages_of(with_environments("[\"\"]")) == QStringList{"Environment 1 has no name."});
    CHECK(messages_of(with_environments("[\"prod\", \"" + std::string(21, 'a') + "\"]")) ==
          QStringList{"Environment 2 is longer than 20 characters."});
    for (const char* bad : {"\"Prod\"", "\"pre prod\"", "\"pre_prod\"", "\"-prod\"", "\"prod-\""})
    {
        INFO(bad);
        CHECK(messages_of(with_environments(std::string("[") + bad + "]")) ==
              QStringList{"Environment 1 can use only lower-case letters, digits and dashes, and cannot start or end "
                          "with a dash."});
    }
    CHECK(messages_of(with_environments("[\"prod\", \"dev\", \"prod\"]")) ==
          QStringList{"Environments 1 and 3 are both called prod."});

    // Two problems are two sentences, in the order of the file.
    CHECK(messages_of(with_environments("[\"Prod\", \"dev\", \"dev\"]")) ==
          QStringList{"Environment 1 can use only lower-case letters, digits and dashes, and cannot start or end with "
                      "a dash.",
                      "Environments 2 and 3 are both called dev."});

    for (const QString& message : messages_of(with_environments("[\"Prod\", \"\", 5]")))
    {
        INFO(message.toStdString());
        CHECK_FALSE(message.contains('/'));
        CHECK(message.endsWith('.'));
    }
}
