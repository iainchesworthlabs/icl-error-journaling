// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsModel: loading, the preferences, saving through the store, first launch, import and export, and the time
// formatting QML calls (work package 3.3). Unit tests on the fake store and the manual gateway; the real file store is
// in integration/test_file_config_store.cpp.
#include "settings_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

using namespace ej;
using namespace ej::test;
using viewmodel::ConfigEditError;
using viewmodel::SettingsModel;

namespace
{

/// The text of `config/sample-config.json`, found from this file's place in the tree.
std::string sample_text()
{
    const std::filesystem::path path =
        std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / ".." / "config" / "sample-config.json";
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

StorageError disk_full()
{
    return make_error(StorageErrorKind::DISK_FULL, "no room");
}

std::string replace_first(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

} // namespace

// ------------------------------------------------------------------------------------------------------------ loading

TEST_CASE("Until the saved file has been read the model is loading and offers no setup", "[viewmodel][settings]")
{
    SettingsRig rig;

    CHECK(rig.m_Settings.loading());
    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.m_Settings.error_types()->rowCount() == 0);
    CHECK_FALSE(rig.m_Settings.setFacilitator("Sam Example").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("A first launch has no file: not configured, nothing to tell the observers", "[viewmodel][settings]")
{
    SettingsRig rig;
    QSignalSpy changed(&rig.m_Settings, &SettingsModel::configurationChanged);

    rig.load();

    CHECK_FALSE(rig.m_Settings.loading());
    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.m_Settings.load_problem().isEmpty());
    CHECK(rig.m_Settings.configuration().m_ErrorTypes.empty());
    CHECK(changed.count() == 1);
    CHECK(rig.m_Observer.changes() == 0);
    CHECK(rig.store().loads() == 1);
}

TEST_CASE("A valid saved file puts the configuration in force and tells the observers once", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.store().set_saved(sample_text());

    rig.load();

    CHECK(rig.m_Settings.configured());
    CHECK(rig.m_Settings.facilitator() == "Sam Example");
    CHECK(rig.m_Settings.unit_prefix() == "UNIT");
    CHECK(rig.m_Settings.time_style() == "24h");
    CHECK_FALSE(rig.m_Settings.time_seconds());
    CHECK(rig.m_Settings.time_meridiem() == "upper");
    CHECK_FALSE(rig.m_Settings.include_totals());
    CHECK(rig.m_Settings.error_types()->rowCount() == 11);
    CHECK(rig.m_Settings.locations()->rowCount() == 2);
    CHECK(rig.m_Settings.templates()->rowCount() == 2);
    CHECK(rig.m_Settings.board_layout()->rowCount() == 12);
    CHECK(rig.m_Observer.changes() == 1);
    CHECK(rig.m_Observer.last_source() == &rig.m_Settings);
}

TEST_CASE("The model loads once", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.store().set_saved(sample_text());

    rig.m_Settings.load();
    rig.m_Settings.load();
    rig.m_Gateway.run_all();

    CHECK(rig.store().loads() == 1);
}

TEST_CASE("A saved file that does not validate starts setup again and is not touched", "[viewmodel][settings]")
{
    SettingsRig rig;
    const std::string broken = replace_first(sample_text(), "\"unit_prefix\": \"UNIT\"", "\"unit_prefix\": \"UN-IT\"");
    rig.store().set_saved(broken);

    rig.load();

    CHECK_FALSE(rig.m_Settings.configured());
    CHECK_FALSE(rig.m_Settings.load_problem().isEmpty());
    CHECK(rig.m_Observer.changes() == 0);
    CHECK(rig.store().saved() == broken);
    CHECK(rig.store().saves().empty());
}

TEST_CASE("A file from a newer build is not used and says to update the app", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.store().set_saved(replace_first(sample_text(), "\"schema_version\": 1", "\"schema_version\": 2"));

    rig.load();

    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.m_Settings.load_problem().contains("newer", Qt::CaseInsensitive));
}

TEST_CASE("A store that cannot be read gives setup, a problem and a write failure", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.store().fail_next_load(make_error(StorageErrorKind::LOCKED, "locked"));
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::writeFailed);

    rig.load();

    CHECK_FALSE(rig.m_Settings.configured());
    CHECK_FALSE(rig.m_Settings.load_problem().isEmpty());
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == "load");
}

// ------------------------------------------------------------------------------------------------------------ saving

TEST_CASE("A setting changes the mirror only after the save has succeeded", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy changed(&rig.m_Settings, &SettingsModel::configurationChanged);
    const std::size_t told = rig.m_Observer.changes();

    CHECK(rig.m_Settings.setFacilitator("Alex Example").isEmpty());

    // Asked, not yet saved.
    CHECK(rig.m_Settings.saving());
    CHECK(rig.m_Settings.configuration().m_Facilitator == "Sam Example");
    CHECK(rig.m_Settings.facilitator() == "Sam Example");
    CHECK(rig.store().saves().empty());
    CHECK(changed.count() == 0);
    CHECK(rig.m_Observer.changes() == told);

    rig.commit();

    CHECK_FALSE(rig.m_Settings.saving());
    CHECK(rig.m_Settings.configuration().m_Facilitator == "Alex Example");
    CHECK(rig.m_Settings.facilitator() == "Alex Example");
    CHECK(rig.store().saves().size() == 1);
    CHECK(changed.count() == 1);
    CHECK(rig.m_Observer.changes() == told + 1);
}

TEST_CASE("What is saved is the configuration file and reads back as the mirror", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    REQUIRE(rig.m_Settings.setUnitPrefix("DEMO").isEmpty());
    rig.commit();

    REQUIRE(rig.store().saved());
    const auto read_back = import_configuration(*rig.store().saved());
    REQUIRE(read_back.has_value());
    CHECK(export_configuration(*read_back) == export_configuration(rig.m_Settings.configuration()));
    CHECK(read_back->m_UnitPrefix == "DEMO");
}

TEST_CASE("A failing save changes nothing and reports; the next change works", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::writeFailed);
    QSignalSpy changed(&rig.m_Settings, &SettingsModel::configurationChanged);
    const std::size_t told = rig.m_Observer.changes();
    const std::string before = export_configuration(rig.m_Settings.configuration());

    rig.store().fail_next_save(disk_full());
    REQUIRE(rig.m_Settings.setIncludeTotals(true).isEmpty());
    rig.commit();

    CHECK(export_configuration(rig.m_Settings.configuration()) == before);
    CHECK_FALSE(rig.m_Settings.include_totals());
    CHECK(rig.m_Observer.changes() == told);
    CHECK(changed.count() == 0);
    CHECK_FALSE(rig.m_Settings.saving());
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == "totals");
    CHECK_FALSE(failed.at(0).at(1).toString().isEmpty());

    CHECK(rig.m_Settings.setIncludeTotals(true).isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.include_totals());
}

TEST_CASE("A change that arrives while a save is in flight is refused, not built on it", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    REQUIRE(rig.m_Settings.setFacilitator("Alex Example").isEmpty());

    CHECK_FALSE(rig.m_Settings.setUnitPrefix("DEMO").isEmpty());
    const auto added = rig.m_Settings.add_error_type("Late Greeting");
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().m_Code == ConfigEditError::Code::QUEUE_FULL);
    CHECK(rig.m_Gateway.pending() == 1);

    rig.commit();
    CHECK(rig.m_Settings.setUnitPrefix("DEMO").isEmpty());
}

TEST_CASE("A change to the value already in force saves nothing", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK(rig.m_Settings.setFacilitator("Sam Example").isEmpty());
    CHECK(rig.m_Settings.setUnitPrefix("UNIT").isEmpty());
    CHECK(rig.m_Settings.setTimeFormat("24h", false, "upper").isEmpty());
    CHECK(rig.m_Settings.setIncludeTotals(false).isEmpty());

    CHECK(rig.m_Gateway.pending() == 0);
    CHECK_FALSE(rig.m_Settings.saving());
}

// ------------------------------------------------------------------------------------------------------ preferences

TEST_CASE("The facilitator is trimmed and must be 1 to 200 characters", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK_FALSE(rig.m_Settings.setFacilitator("").isEmpty());
    CHECK_FALSE(rig.m_Settings.setFacilitator("   ").isEmpty());
    CHECK_FALSE(rig.m_Settings.setFacilitator(QString(201, 'x')).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    CHECK(rig.m_Settings.setFacilitator("  Alex Example  ").isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.configuration().m_Facilitator == "Alex Example");

    CHECK(rig.m_Settings.setFacilitator(QString(200, 'x')).isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.configuration().m_Facilitator.size() == 200);
    // The longest accepted value is a file the importer accepts.
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("The unit prefix refuses a dash, a hash, nothing and more than 20 characters", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK_FALSE(rig.m_Settings.setUnitPrefix("").isEmpty());
    CHECK_FALSE(rig.m_Settings.setUnitPrefix("UN-IT").isEmpty());
    CHECK_FALSE(rig.m_Settings.setUnitPrefix("UNIT#").isEmpty());
    CHECK_FALSE(rig.m_Settings.setUnitPrefix(QString(21, 'U')).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK(rig.m_Settings.configuration().m_UnitPrefix == "UNIT");

    CHECK(rig.m_Settings.setUnitPrefix(QString(20, 'U')).isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.configuration().m_UnitPrefix.size() == 20);
}

TEST_CASE("All eight combinations of style, seconds and meridiem are accepted and saved", "[viewmodel][settings]")
{
    for (const QString& style : {QStringLiteral("24h"), QStringLiteral("12h")})
    {
        for (const bool seconds : {false, true})
        {
            for (const QString& meridiem : {QStringLiteral("upper"), QStringLiteral("lower")})
            {
                CAPTURE(style.toStdString(), seconds, meridiem.toStdString());
                SettingsRig rig;
                rig.load_sample();
                // Start from a format that differs from the target, so that the target is a change.
                REQUIRE(rig.m_Settings.setTimeFormat(style == "24h" ? "12h" : "24h", seconds, meridiem).isEmpty());
                if (rig.m_Gateway.pending() > 0)
                {
                    rig.commit();
                }

                REQUIRE(rig.m_Settings.setTimeFormat(style, seconds, meridiem).isEmpty());
                rig.commit();

                CHECK(rig.m_Settings.time_style() == style);
                CHECK(rig.m_Settings.time_seconds() == seconds);
                CHECK(rig.m_Settings.time_meridiem() == meridiem);
                const auto read_back = import_configuration(*rig.store().saved());
                REQUIRE(read_back.has_value());
                CHECK(read_back->m_TimeFormat.m_Style == (style == "12h" ? TimeStyle::H12 : TimeStyle::H24));
                CHECK(read_back->m_TimeFormat.m_Seconds == seconds);
                CHECK(read_back->m_TimeFormat.m_Meridiem ==
                      (meridiem == "lower" ? MeridiemCase::LOWER : MeridiemCase::UPPER));
            }
        }
    }
}

TEST_CASE("A time format that names no style or meridiem is refused", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK_FALSE(rig.m_Settings.setTimeFormat("36h", false, "upper").isEmpty());
    CHECK_FALSE(rig.m_Settings.setTimeFormat("12h", false, "Upper").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("The totals option is saved and reaches the configuration", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK(rig.m_Settings.setIncludeTotals(true).isEmpty());
    rig.commit();

    CHECK(rig.m_Settings.include_totals());
    CHECK(rig.m_Settings.configuration().m_IncludeTotals);
}

// ----------------------------------------------------------------------------------------------------- first launch

TEST_CASE("A manual setup is held until it has a facilitator and a prefix, then saved as one", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    QSignalSpy changed(&rig.m_Settings, &SettingsModel::configurationChanged);

    CHECK(rig.m_Settings.setFacilitator("Sam Example").isEmpty());
    CHECK(rig.m_Settings.addLocation("North Site").isEmpty());
    CHECK(rig.m_Settings.addErrorType("Wrong Response").isEmpty());

    // Shown, not saved, not in force.
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.m_Settings.facilitator() == "Sam Example");
    CHECK(rig.m_Settings.locations()->rowCount() == 1);
    CHECK(rig.m_Settings.error_types()->rowCount() == 1);
    CHECK(rig.m_Settings.configuration().m_Facilitator.empty());
    CHECK(rig.store().saves().empty());
    CHECK(rig.m_Observer.changes() == 0);
    CHECK(changed.count() == 3);

    CHECK(rig.m_Settings.setUnitPrefix("UNIT").isEmpty());
    CHECK(rig.m_Gateway.pending() == 1);
    CHECK_FALSE(rig.m_Settings.configured());

    rig.commit();

    CHECK(rig.m_Settings.configured());
    CHECK(rig.m_Observer.changes() == 1);
    CHECK(rig.m_Settings.configuration().m_Locations.size() == 1);
    REQUIRE(rig.store().saves().size() == 1);
    const auto read_back = import_configuration(rig.store().saves().front());
    REQUIRE(read_back.has_value());
    CHECK(read_back->m_ErrorTypes.size() == 1);
    CHECK(read_back->m_Facilitator == "Sam Example");
}

TEST_CASE("A setup whose first save fails stays a setup", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    REQUIRE(rig.m_Settings.setFacilitator("Sam Example").isEmpty());
    rig.store().fail_next_save(disk_full());
    REQUIRE(rig.m_Settings.setUnitPrefix("UNIT").isEmpty());

    rig.commit();

    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.m_Settings.facilitator() == "Sam Example");
    CHECK(rig.m_Settings.unit_prefix().isEmpty());
    CHECK(rig.m_Observer.changes() == 0);

    CHECK(rig.m_Settings.setUnitPrefix("UNIT").isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.configured());
}

TEST_CASE("The Other sheet cannot add a type before a configuration is in force", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();

    const auto added = rig.m_Settings.add_error_type("Late Greeting");

    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().m_Code == ConfigEditError::Code::NOT_READY);
}

// ------------------------------------------------------------------------------------------------- the editor (Other)

TEST_CASE("add_error_type returns the id now and the type appears after the save", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    const std::size_t told = rig.m_Observer.changes();

    const auto added = rig.m_Settings.add_error_type("Late Greeting");

    REQUIRE(added.has_value());
    CHECK(rig.m_Settings.configuration().m_ErrorTypes.size() == 11);
    CHECK(rig.m_Observer.changes() == told);

    rig.commit();

    const auto& types = rig.m_Settings.configuration().m_ErrorTypes;
    REQUIRE(types.size() == 12);
    CHECK(types.back().m_Id == *added);
    CHECK(types.back().m_Name == "Late Greeting");
    CHECK_FALSE(types.back().m_BoardPosition.has_value());
    CHECK_FALSE(types.back().m_Retired);
    CHECK(rig.m_Observer.changes() == told + 1);
}

TEST_CASE("add_error_type refuses an empty, duplicate or over-long name with a message", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();

    for (const std::string& name :
         {std::string{}, std::string("  "), std::string("wrong RESPONSE"), std::string(201, 'x')})
    {
        const auto added = rig.m_Settings.add_error_type(name);
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().m_Code == ConfigEditError::Code::REFUSED);
        CHECK_FALSE(added.error().m_Message.empty());
    }
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("add_error_type that fails to save leaves no trace", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    rig.store().fail_next_save(disk_full());

    REQUIRE(rig.m_Settings.add_error_type("Late Greeting").has_value());
    rig.commit();

    CHECK(rig.m_Settings.configuration().m_ErrorTypes.size() == 11);
    CHECK(rig.m_Settings.error_types()->rowCount() == 11);
}

// ----------------------------------------------------------------------------------------------------------- import

TEST_CASE("An import is read and checked first and changes nothing until it is applied", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    rig.store().add_file("/files/setup.json", sample_text());
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::importFailed);

    rig.m_Settings.loadImport("/files/setup.json");
    CHECK(rig.m_Gateway.pending() == 1);
    rig.m_Gateway.run_all();

    REQUIRE(prepared.count() == 1);
    CHECK(prepared.at(0).at(0).toInt() == 11);
    CHECK(prepared.at(0).at(1).toInt() == 2);
    CHECK(prepared.at(0).at(2).toInt() == 2);
    CHECK(failed.count() == 0);
    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.store().saves().empty());

    rig.m_Settings.applyImport();
    CHECK_FALSE(rig.m_Settings.configured());
    rig.commit();

    CHECK(rig.m_Settings.configured());
    CHECK(rig.m_Observer.changes() == 1);
    CHECK(export_configuration(rig.m_Settings.configuration()) == export_configuration(sample_configuration()));
    CHECK(rig.store().saved() == sample_text());
}

TEST_CASE("An import replaces the whole configuration in one save", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    rig.store().add_file("/files/setup.json", sample_text());
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);

    rig.m_Settings.loadImport("/files/setup.json");
    rig.m_Gateway.run_all();
    REQUIRE(prepared.count() == 1);
    rig.m_Settings.applyImport();
    rig.commit();

    CHECK(rig.store().saves().size() == 1);
    CHECK(rig.m_Settings.error_types()->rowCount() == 11);
    CHECK(rig.m_Settings.configuration().m_Locations.size() == 2);
}

TEST_CASE("A file that cannot be imported is refused with messages in file order and changes nothing",
          "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    const std::string before = export_configuration(rig.m_Settings.configuration());
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::importFailed);
    const std::size_t told = rig.m_Observer.changes();

    SECTION("malformed JSON")
    {
        rig.store().add_file("/f.json", "{ \"schema_version\": ");
        rig.m_Settings.loadImport("/f.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() == 1);
        CHECK(failed.at(0).at(0).toStringList().size() == 1);
    }
    SECTION("a future schema version asks for an update")
    {
        rig.store().add_file("/f.json", replace_first(sample_text(), "\"schema_version\": 1", "\"schema_version\": 7"));
        rig.m_Settings.loadImport("/f.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() == 1);
        const QStringList messages = failed.at(0).at(0).toStringList();
        REQUIRE(messages.size() == 1);
        CHECK(messages.front().contains("newer", Qt::CaseInsensitive));
    }
    SECTION("duplicate ids and names are all reported, in the order of the file")
    {
        std::string text = replace_first(sample_text(), "0000000000a2", "0000000000a1");
        text = replace_first(text, "Wrong Track Played", "Wrong Response");
        rig.store().add_file("/f.json", text);
        rig.m_Settings.loadImport("/f.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() == 1);
        const QStringList messages = failed.at(0).at(0).toStringList();
        REQUIRE(messages.size() == 2);
        CHECK(messages.at(0).startsWith("/error_types/1/id"));
        CHECK(messages.at(1).startsWith("/error_types/2/name"));
    }
    SECTION("a file of 2 MiB")
    {
        rig.store().add_file("/f.json", std::string(2 * 1024 * 1024, ' '));
        rig.m_Settings.loadImport("/f.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() == 1);
        CHECK(failed.at(0).at(0).toStringList().size() == 1);
        CHECK(failed.at(0).at(0).toStringList().front().contains("1 MiB"));
    }
    SECTION("a file that is not there")
    {
        rig.m_Settings.loadImport("/nothing.json");
        rig.m_Gateway.run_all();
        REQUIRE(failed.count() == 1);
        CHECK(failed.at(0).at(0).toStringList().size() == 1);
    }

    CHECK(prepared.count() == 0);
    CHECK(export_configuration(rig.m_Settings.configuration()) == before);
    CHECK(rig.store().saves().empty());
    CHECK(rig.m_Observer.changes() == told);

    // Nothing was prepared, so there is nothing to apply.
    QSignalSpy write_failed(&rig.m_Settings, &SettingsModel::writeFailed);
    rig.m_Settings.applyImport();
    CHECK(write_failed.count() == 1);
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("A file that is 1 MiB is read; the limit is the core's", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::importFailed);
    // Exactly at the limit passes the size check; it is then refused as JSON, not as too large.
    rig.store().add_file("/f.json", std::string(CONFIG_MAX_BYTES, ' '));

    rig.m_Settings.loadImport("/f.json");
    rig.m_Gateway.run_all();

    REQUIRE(failed.count() == 1);
    CHECK_FALSE(failed.at(0).at(0).toStringList().front().contains("1 MiB"));
}

TEST_CASE("Cancelling an import changes nothing and a later apply has nothing to apply", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    rig.store().add_file("/f.json", sample_text());
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);
    QSignalSpy write_failed(&rig.m_Settings, &SettingsModel::writeFailed);

    rig.m_Settings.loadImport("/f.json");
    rig.m_Gateway.run_all();
    REQUIRE(prepared.count() == 1);
    rig.m_Settings.discardImport();
    rig.m_Settings.applyImport();

    CHECK(write_failed.count() == 1);
    CHECK_FALSE(rig.m_Settings.configured());
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK(rig.store().saves().empty());
}

TEST_CASE("Only the last file asked for is prepared", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    rig.store().add_file("/a.json", sample_text());
    rig.store().add_file("/b.json", export_configuration(scenario_configuration()));
    QSignalSpy prepared(&rig.m_Settings, &SettingsModel::importPrepared);

    rig.m_Settings.loadImport("/a.json");
    rig.m_Settings.loadImport("/b.json");
    rig.m_Gateway.run_all();

    REQUIRE(prepared.count() == 1);
    CHECK(prepared.at(0).at(0).toInt() == 5); // the scenario's five live types, not the sample's eleven
}

TEST_CASE("A failed apply keeps the prepared file so that it can be tried again", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    rig.store().add_file("/f.json", sample_text());
    QSignalSpy write_failed(&rig.m_Settings, &SettingsModel::writeFailed);
    rig.m_Settings.loadImport("/f.json");
    rig.m_Gateway.run_all();

    rig.store().fail_next_save(disk_full());
    rig.m_Settings.applyImport();
    rig.commit();
    CHECK_FALSE(rig.m_Settings.configured());
    REQUIRE(write_failed.count() == 1);
    CHECK(write_failed.at(0).at(0).toString() == "import");

    rig.m_Settings.applyImport();
    rig.commit();
    CHECK(rig.m_Settings.configured());
}

// ----------------------------------------------------------------------------------------------------------- export

TEST_CASE("Import then export gives the file back byte for byte", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    rig.store().add_file("/in.json", sample_text());
    rig.m_Settings.loadImport("/in.json");
    rig.m_Gateway.run_all();
    rig.m_Settings.applyImport();
    rig.commit();
    QSignalSpy finished(&rig.m_Settings, &SettingsModel::exportFinished);

    rig.m_Settings.exportTo("/out/setup.json");
    rig.m_Gateway.run_all();

    REQUIRE(finished.count() == 1);
    CHECK(finished.at(0).at(0).toBool());
    CHECK_FALSE(finished.at(0).at(1).toString().isEmpty());
    REQUIRE(rig.store().files().contains("/out/setup.json"));
    CHECK(rig.store().files().at("/out/setup.json") == sample_text());
}

TEST_CASE("Export of a configuration that has been edited has the edits", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load_sample();
    REQUIRE(rig.m_Settings.setUnitPrefix("DEMO").isEmpty());
    rig.commit();

    rig.m_Settings.exportTo("/out.json");
    rig.m_Gateway.run_all();

    CHECK(rig.store().files().at("/out.json") == *rig.store().saved());
}

TEST_CASE("There is nothing to export before a configuration is in force", "[viewmodel][settings]")
{
    SettingsRig rig;
    rig.load();
    QSignalSpy finished(&rig.m_Settings, &SettingsModel::exportFinished);

    rig.m_Settings.exportTo("/out.json");

    REQUIRE(finished.count() == 1);
    CHECK_FALSE(finished.at(0).at(0).toBool());
    CHECK(rig.m_Gateway.pending() == 0);
}

// ---------------------------------------------------------------------------------------------------- formatting time

namespace
{

/// The settings model of the formatting tests, with the real time zone rules (the fixed-offset zones of the other
/// tests have no daylight saving).
struct RealZoneRig
{
    TestClock m_Clock;
    SequentialIds m_Ids;
    FakeDeviceZone m_DeviceZone;
    std::unique_ptr<TimeZoneProvider> m_Zones = make_qt_time_zone_provider();
    MemoryStorage m_Storage{m_Clock};
    ManualStorageGateway m_Gateway{m_Storage.context()};
    viewmodel::Environment m_Environment{m_Clock, m_Clock, m_Ids, m_DeviceZone, *m_Zones};
    SettingsModel m_Settings{m_Gateway, m_Environment};

    RealZoneRig()
    {
        m_Storage.config().set_saved(export_configuration(sample_configuration()));
        m_Settings.load();
        m_Gateway.run_all();
    }

    void set_format(const char* style, bool seconds, const char* meridiem)
    {
        REQUIRE(m_Settings.setTimeFormat(style, seconds, meridiem).isEmpty());
        REQUIRE(m_Gateway.run_next());
    }
};

qint64 utc_ms(int year, unsigned month, unsigned day, int hour, int minute, int second)
{
    using namespace std::chrono;
    const auto when = sys_days{std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day}} +
                      hours{hour} + minutes{minute} + seconds{second};
    return duration_cast<milliseconds>(when.time_since_epoch()).count();
}

} // namespace

TEST_CASE("formatTime gives a UTC time as the wall clock of a zone in the 24-hour format",
          "[viewmodel][settings][time]")
{
    RealZoneRig rig;

    // 2026-01-15 02:05:09 UTC is 13:05:09 in Melbourne in summer (+11:00).
    const qint64 summer = utc_ms(2026, 1, 15, 2, 5, 9);
    CHECK(rig.m_Settings.formatTime(summer, "Australia/Melbourne") == "13:05");
    CHECK(rig.m_Settings.formatTime(summer, "UTC") == "02:05");

    rig.set_format("24h", true, "upper");
    CHECK(rig.m_Settings.formatTime(summer, "Australia/Melbourne") == "13:05:09");
}

TEST_CASE("formatTime gives the 12-hour format with seconds and a lower-case meridiem", "[viewmodel][settings][time]")
{
    RealZoneRig rig;
    const qint64 summer = utc_ms(2026, 1, 15, 2, 5, 9);

    rig.set_format("12h", true, "lower");
    CHECK(rig.m_Settings.formatTime(summer, "Australia/Melbourne") == "01:05:09 pm");
    CHECK(rig.m_Settings.formatTime(summer, "UTC") == "02:05:09 am");

    rig.set_format("12h", false, "upper");
    CHECK(rig.m_Settings.formatTime(summer, "Australia/Melbourne") == "01:05 PM");
}

TEST_CASE("formatTime follows the daylight-saving rules of the date", "[viewmodel][settings][time]")
{
    RealZoneRig rig;
    rig.set_format("12h", true, "lower");

    // The same UTC time of day in January (+11:00) and in July (+10:00).
    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 1, 15, 2, 5, 9), "Australia/Melbourne") == "01:05:09 pm");
    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 7, 15, 2, 5, 9), "Australia/Melbourne") == "12:05:09 pm");
    // Across the change itself: 2026-04-04 15:59:59 UTC is 02:59:59 on Sunday the 5th (+11:00); a second later
    // the clocks go back to 02:00:00 (+10:00).
    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 4, 4, 15, 59, 59), "Australia/Melbourne") == "02:59:59 am");
    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 4, 4, 16, 0, 0), "Australia/Melbourne") == "02:00:00 am");
}

TEST_CASE("formatTime gives nothing for a zone that is not known", "[viewmodel][settings][time]")
{
    RealZoneRig rig;

    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 1, 15, 2, 5, 9), "Not/AZone").isEmpty());
    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 1, 15, 2, 5, 9), "").isEmpty());
}

TEST_CASE("A change of time format emits timeFormatChanged; other changes do not", "[viewmodel][settings][time]")
{
    RealZoneRig rig;
    QSignalSpy format_changed(&rig.m_Settings, &SettingsModel::timeFormatChanged);
    const QString before = rig.m_Settings.time_format_key();

    REQUIRE(rig.m_Settings.setFacilitator("Alex Example").isEmpty());
    REQUIRE(rig.m_Gateway.run_next());
    CHECK(format_changed.count() == 0);

    rig.set_format("12h", true, "lower");
    CHECK(format_changed.count() == 1);
    CHECK(rig.m_Settings.time_format_key() != before);
    CHECK(rig.m_Settings.time_format_key() == "12h-seconds-lower");

    // Saved but not applied yet, and a failing save, emit nothing.
    rig.m_Storage.config().fail_next_save(disk_full());
    REQUIRE(rig.m_Settings.setTimeFormat("24h", false, "upper").isEmpty());
    CHECK(format_changed.count() == 1);
    REQUIRE(rig.m_Gateway.run_next());
    CHECK(format_changed.count() == 1);
    CHECK(rig.m_Settings.time_style() == "12h");

    rig.set_format("24h", false, "upper");
    CHECK(format_changed.count() == 2);
    CHECK(rig.m_Settings.time_format_key() == before);
}

TEST_CASE("An import that changes the time format emits timeFormatChanged once", "[viewmodel][settings][time]")
{
    RealZoneRig rig;
    Configuration other = sample_configuration();
    other.m_TimeFormat = TimeFormat{TimeStyle::H12, true, MeridiemCase::LOWER};
    rig.m_Storage.config().add_file("/f.json", export_configuration(other));
    QSignalSpy format_changed(&rig.m_Settings, &SettingsModel::timeFormatChanged);

    rig.m_Settings.loadImport("/f.json");
    rig.m_Gateway.run_all();
    rig.m_Settings.applyImport();
    rig.m_Gateway.run_all();

    CHECK(format_changed.count() == 1);
    CHECK(rig.m_Settings.formatTime(utc_ms(2026, 1, 15, 2, 5, 9), "UTC") == "02:05:09 am");
}

TEST_CASE("The first load announces the time format of the saved file", "[viewmodel][settings][time]")
{
    SettingsRig rig;
    Configuration configuration = sample_configuration();
    configuration.m_TimeFormat = TimeFormat{TimeStyle::H12, false, MeridiemCase::UPPER};
    rig.store().set_saved(export_configuration(configuration));
    QSignalSpy format_changed(&rig.m_Settings, &SettingsModel::timeFormatChanged);

    rig.load();

    CHECK(format_changed.count() == 1);
    CHECK(rig.m_Settings.time_style() == "12h");
}
