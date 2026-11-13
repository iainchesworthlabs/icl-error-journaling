// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the visual pass binds to in SettingsModel and the lists it owns: the wording of the refusals, the preview of a
// template, the colour names, the template icon and the end of an export. Unit tests on the fake store and the manual
// gateway.
#include "settings_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <string>

using namespace ej;
using namespace ej::test;
using viewmodel::SettingsModel;

namespace
{

constexpr const char* WRONG_RESPONSE = "00000000-0000-4000-8000-0000000000a1";
constexpr const char* PLAYED_SONG = "00000000-0000-4000-8000-0000000000c1";
constexpr const char* NORTH_SITE = "00000000-0000-4000-8000-0000000000b1";

int role_named(const QAbstractItemModel& model, const char* name)
{
    const auto names = model.roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
    {
        if (it.value() == name)
        {
            return it.key();
        }
    }
    FAIL("no role called " << name);
    return -1;
}

QString text_at(const QAbstractItemModel& model, int row, const char* role)
{
    return model.data(model.index(row, 0), role_named(model, role)).toString();
}

QString curly(const QString& text)
{
    return QStringLiteral("“") + text + QStringLiteral("”");
}

} // namespace

// ------------------------------------------------------------------------------------------------------- the wording

TEST_CASE("A name in use is refused in the words of the designs, for each kind of list", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    const QString error_type =
        "There is already an error type called " + curly("Mute Delay") + ". Names must be different.";
    CHECK(rig.m_Settings.addErrorType("Mute Delay") == error_type);
    CHECK(rig.m_Settings.renameErrorType(WRONG_RESPONSE, "mute delay") ==
          "There is already an error type called " + curly("mute delay") + ". Names must be different.");

    CHECK(rig.m_Settings.addLocation("North Site") ==
          "There is already a location called " + curly("North Site") + ". Names must be different.");
    CHECK(rig.m_Settings.renameLocation("00000000-0000-4000-8000-0000000000b2", "North Site") ==
          "There is already a location called " + curly("North Site") + ". Names must be different.");

    const QString note_template =
        "There is already a note template called " + curly("Visitor interaction") + ". Names must be different.";
    CHECK(rig.m_Settings.addTemplate("Visitor interaction", "Who?", "Seen {value}") == note_template);
    CHECK(rig.m_Settings.renameTemplate(PLAYED_SONG, "Visitor interaction") == note_template);
    CHECK(rig.m_Settings.updateTemplate(PLAYED_SONG, "Visitor interaction", "Who?", "Seen {value}") == note_template);
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("A line without the answer is refused in the words of the designs", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    const QString expected =
        "The line needs the answer in it. Tap Insert answer to put it where it goes, for example " +
        curly("Played [answer]") + ".";

    CHECK(rig.m_Settings.validatePattern("Played") == expected);
    CHECK(rig.m_Settings.validatePattern("") == expected);
    CHECK(rig.m_Settings.addTemplate("Seen", "Who?", "Seen") == expected);
    CHECK(rig.m_Settings.updateTemplate(PLAYED_SONG, "Played song", "Which song?", "Played") == expected);
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("The hint under the unit prefix is the model's", "[viewmodel][settings]")
{
    SettingsRig rig;

    CHECK(rig.m_Settings.unit_prefix_hint() == "Added in front of the number you type each day.");
    CHECK(rig.m_Settings.property("unitPrefixHint").toString() == "Added in front of the number you type each day.");
}

// ----------------------------------------------------------------------------------------------------- colour names

TEST_CASE("Each colour slot has a name, and the error types carry theirs", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    const QStringList names = {"Sky",   "Lavender",   "Apricot", "Aqua", "Orchid", "Butter",
                               "Steel", "Periwinkle", "Sand",    "Oat",  "Blush"};
    for (int slot = 1; slot <= 11; ++slot)
    {
        CHECK(rig.m_Settings.colourName(slot) == names.at(slot - 1));
    }
    CHECK(rig.m_Settings.colourName(0).isEmpty());
    CHECK(rig.m_Settings.colourName(12).isEmpty());
    CHECK(rig.m_Settings.colourName(-1).isEmpty());

    const QAbstractItemModel& types = *rig.m_Settings.error_types();
    REQUIRE(types.rowCount() >= 8);
    CHECK(text_at(types, 0, "colourName") == "Sky");
    CHECK(text_at(types, 7, "name") == "Mute Delay");
    CHECK(text_at(types, 7, "colourSlot") == "8");
    CHECK(text_at(types, 7, "colourName") == "Periwinkle");
    for (int row = 0; row < types.rowCount(); ++row)
    {
        CHECK(text_at(types, row, "colourName") ==
              rig.m_Settings.colourName(text_at(types, row, "colourSlot").toInt()));
    }
}

// -------------------------------------------------------------------------------------------- the preview of a line

TEST_CASE("A template value is previewed as the log line it would make", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    SECTION("at a time the caller names")
    {
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "Morning Medley", 10 * 3600 + 30 * 60) ==
              "10:30 Played Morning Medley");
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x", 0) == "00:00 Played x");
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x", 24 * 3600 - 1) == "23:59 Played x");
    }
    SECTION("now, in the zone of the device")
    {
        // The clock of the rig is 08:55 in Melbourne.
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "Morning Medley") ==
              "08:55 Played Morning Medley");
        rig.m_World.m_DeviceZone.set("Australia/Perth");
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "Morning Medley", -1) ==
              "05:55 Played Morning Medley");
    }
    SECTION("a blank value is the designs' sample, and the value is trimmed")
    {
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "  ", 37800) ==
              "10:30 Played Morning Medley");
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "  Evening Song \n", 37800) ==
              "10:30 Played Evening Song");
    }
    SECTION("the time follows the time format")
    {
        REQUIRE(rig.m_Settings.setTimeFormat("12h", false, "upper").isEmpty());
        rig.commit();
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x", 15 * 3600 + 5 * 60) ==
              "03:05 PM Played x");
    }
    SECTION("a line that is not valid has no example")
    {
        const QString fix = "Fix the line to see an example.";
        CHECK(rig.m_Settings.previewTemplateLine("Played", "x", 37800) == fix);
        CHECK(rig.m_Settings.previewTemplateLine("", "x") == fix);
        CHECK(rig.m_Settings.previewTemplateLine("Played {name}", "x") == fix);
        CHECK(rig.m_Settings.previewTemplateLine("Played {value", "x") == fix);
    }
    SECTION("a time of day that does not exist")
    {
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x", 24 * 3600).isEmpty());
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x", 100000).isEmpty());
    }
    SECTION("a zone the provider does not know")
    {
        rig.m_World.m_DeviceZone.set("Nowhere/Land");
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x", 37800).isEmpty());
        CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "x").isEmpty());
    }
    SECTION("nothing is saved")
    {
        const std::size_t saves = rig.store().saves().size();
        (void)rig.m_Settings.previewTemplateLine("Played {value}", "x", 37800);
        CHECK(rig.store().saves().size() == saves);
        CHECK(rig.m_Gateway.pending() == 0);
    }
}

TEST_CASE("The preview works at first launch, before any setup is saved", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    REQUIRE_FALSE(rig.m_Settings.configured());

    CHECK(rig.m_Settings.previewTemplateLine("Played {value}", "Morning Medley", 37800) ==
          "10:30 Played Morning Medley");
}

// ----------------------------------------------------------------------------------------------------- template icon

TEST_CASE("A template has the icon its setup file names, or the default one", "[viewmodel][settings][templates]")
{
    Configuration configuration = sample_configuration();
    configuration.m_Templates[0].m_Icon = "music";
    configuration.m_Templates[1].m_Icon = "";
    SettingsRig rig;
    rig.load_configuration(configuration);
    const QAbstractItemModel& templates = *rig.m_Settings.templates();

    CHECK(text_at(templates, 0, "name") == "Played song");
    CHECK(text_at(templates, 0, "icon") == "music");
    CHECK(text_at(templates, 1, "icon") == "note");

    SECTION("a template added in the app has the default icon, and the file leaves the icon out")
    {
        REQUIRE(rig.m_Settings.addTemplate("Seen", "Who?", "Seen {value}").isEmpty());
        rig.commit();
        CHECK(text_at(templates, 2, "icon") == "note");
        const std::string saved = *rig.store().saved();
        CHECK(saved.find("\"icon\": \"music\"") != std::string::npos);
        CHECK(saved.find("\"icon\"") == saved.rfind("\"icon\""));
    }
    SECTION("editing a template keeps its icon")
    {
        REQUIRE(rig.m_Settings.updateTemplate(PLAYED_SONG, "Song played", "Which track?", "Track {value}").isEmpty());
        rig.commit();
        CHECK(text_at(templates, 0, "name") == "Song played");
        CHECK(text_at(templates, 0, "icon") == "music");
        REQUIRE(rig.m_Settings.renameTemplate(PLAYED_SONG, "Song heard").isEmpty());
        rig.commit();
        CHECK(text_at(templates, 0, "icon") == "music");
    }
}

TEST_CASE("The sample setup gives Played song a music note and Visitor interaction a speech bubble",
          "[viewmodel][settings][templates]")
{
    SettingsRig rig;
    rig.load_sample();
    const QAbstractItemModel& templates = *rig.m_Settings.templates();

    CHECK(text_at(templates, 0, "name") == "Played song");
    CHECK(text_at(templates, 0, "icon") == "music");
    CHECK(text_at(templates, 1, "name") == "Visitor interaction");
    CHECK(text_at(templates, 1, "icon") == "speech");
}

TEST_CASE("A setup file with an icon is read, and a file without one is read as before", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::importFailed);

    SECTION("a file that names no icon, as every file did before")
    {
        Configuration configuration = sample_configuration();
        for (NoteTemplate& note_template : configuration.m_Templates)
        {
            note_template.m_Icon.clear();
        }
        const std::string text = export_configuration(configuration);
        REQUIRE(text.find("\"icon\"") == std::string::npos);
        rig.store().add_file("/old.json", text);
        rig.m_Settings.loadImport("/old.json");
        rig.m_Gateway.run_all();
        REQUIRE(prepared.count() == 1);
        rig.m_Settings.applyImport();
        rig.commit();
        CHECK(text_at(*rig.m_Settings.templates(), 0, "icon") == "note");
    }
    SECTION("a file that names one")
    {
        Configuration configuration = sample_configuration();
        configuration.m_Templates[1].m_Icon = "speech";
        rig.store().add_file("/new.json", export_configuration(configuration));
        rig.m_Settings.loadImport("/new.json");
        rig.m_Gateway.run_all();
        REQUIRE(prepared.count() == 1);
        rig.m_Settings.applyImport();
        rig.commit();
        CHECK(text_at(*rig.m_Settings.templates(), 1, "icon") == "speech");
    }
    SECTION("a file that names an icon with capitals")
    {
        Configuration configuration = sample_configuration();
        configuration.m_Templates[1].m_Icon = "Speech Bubble";
        rig.store().add_file("/bad.json", export_configuration(configuration));
        rig.m_Settings.loadImport("/bad.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() == 1);
        CHECK(failed.at(0).at(0).toStringList() ==
              QStringList{"Note template 2: the icon name can use only lower-case letters, digits and dashes."});
    }
}

// ------------------------------------------------------------------------------------------------ the import errors

TEST_CASE("A refused file is described in the plain sentences of the design, in the order of the file",
          "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::importFailed);
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[2].m_Name = "";
    configuration.m_Locations[1].m_Name = "North Site";
    configuration.m_Templates[0].m_OutputPattern = "Played";
    rig.store().add_file("/f.json", export_configuration(configuration));

    rig.m_Settings.loadImport("/f.json");
    rig.m_Gateway.run_all();

    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toStringList() ==
          QStringList{"Error type 3 has no name.", "Locations 1 and 2 are both called North Site.",
                      "Note template 1: the line in the log has no {value} where the answer goes."});
    CHECK_FALSE(rig.m_Settings.configured());
}

// ------------------------------------------------------------------------------------------------- the export result

TEST_CASE("An export reports the file name, the title and the message of the banner", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy result(&rig.m_Settings, &SettingsModel::exportResult);
    QSignalSpy finished(&rig.m_Settings, &SettingsModel::exportFinished);

    rig.m_Settings.exportTo("/tmp/exports/field-test-setup.ejconfig");
    rig.m_Gateway.run_all();

    REQUIRE(result.count() == 1);
    CHECK(result.at(0).at(0).toBool());
    CHECK(result.at(0).at(1).toString() == "Exported to Files");
    CHECK(result.at(0).at(2).toString() == "field-test-setup.ejconfig");
    CHECK(result.at(0).at(3).toString() == "field-test-setup.ejconfig is ready to send.");
    // The older signal says the same, so a screen that listens to it shows the real name too.
    REQUIRE(finished.count() == 1);
    CHECK(finished.at(0).at(0).toBool());
    CHECK(finished.at(0).at(1).toString() == "field-test-setup.ejconfig is ready to send.");
}

TEST_CASE("An export that could not be written says so with the name of the file", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy result(&rig.m_Settings, &SettingsModel::exportResult);
    QSignalSpy finished(&rig.m_Settings, &SettingsModel::exportFinished);
    rig.store().fail_next_write_file(make_error(StorageErrorKind::DISK_FULL, "no room"));

    rig.m_Settings.exportTo("/out/setup.ejconfig");
    rig.m_Gateway.run_all();

    REQUIRE(result.count() == 1);
    CHECK_FALSE(result.at(0).at(0).toBool());
    CHECK(result.at(0).at(1).toString() == "Export failed");
    CHECK(result.at(0).at(2).toString() == "setup.ejconfig");
    CHECK_FALSE(result.at(0).at(3).toString().isEmpty());
    REQUIRE(finished.count() == 1);
    CHECK_FALSE(finished.at(0).at(0).toBool());
    CHECK(finished.at(0).at(1).toString() == result.at(0).at(3).toString());
}

TEST_CASE("Nothing is exported before a configuration is in force, and the result says so", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    QSignalSpy result(&rig.m_Settings, &SettingsModel::exportResult);

    rig.m_Settings.exportTo("/out/setup.ejconfig");

    REQUIRE(result.count() == 1);
    CHECK_FALSE(result.at(0).at(0).toBool());
    CHECK(result.at(0).at(1).toString() == "Export failed");
    CHECK(result.at(0).at(2).toString().isEmpty());
}

TEST_CASE("The location the sample names is North Site", "[viewmodel][settings]")
{
    // The ids used above are those of the sample file; this fails first if the sample changes.
    SettingsRig rig;
    rig.load_sample();
    const QAbstractItemModel& locations = *rig.m_Settings.locations();

    CHECK(text_at(locations, 0, "locationId") == NORTH_SITE);
    CHECK(text_at(locations, 0, "name") == "North Site");
}
