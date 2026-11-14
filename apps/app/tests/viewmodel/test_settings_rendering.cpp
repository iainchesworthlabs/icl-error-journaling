// SPDX-License-Identifier: GPL-3.0-or-later
//
// What a change of setting does to what is rendered (work package 3.3): the unit prefix, the time format and the totals
// option apply to every log, past days included; a retired type or location still renders; the tester applies to
// logs made afterwards. The renderer's context is built from the configuration in force, as `ExportContextBuilder`
// (3.2) will do; the log is a past day's, made before the change.
#include "settings_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ej;
using namespace ej::test;

namespace
{

std::string render(const Scenario& scenario, const Configuration& configuration)
{
    const auto text = render_export(scenario.m_Log, context_from(configuration, *scenario.m_Context.m_Zones));
    REQUIRE(text.has_value());
    return *text;
}

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("A new unit prefix re-renders a past log", "[viewmodel][settings][render]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    const Scenario past = make_scenario("attached-note");
    const std::string before = render(past, rig.m_Settings.configuration());
    REQUIRE(before.starts_with("Tester: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\n"));

    REQUIRE(rig.m_Settings.setUnitPrefix("DEMO").isEmpty());
    rig.commit();

    CHECK(render(past, rig.m_Settings.configuration())
              .starts_with("Tester: Sam Example\nDate: 7 Nov 2026\nDEMO #: DEMO-27\n"));
}

TEST_CASE("A new time format re-renders a past log", "[viewmodel][settings][render]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    const Scenario past = make_scenario("attached-note");
    CHECK(contains(render(past, rig.m_Settings.configuration()), "\n10:05 T| Wrong Response"));

    REQUIRE(rig.m_Settings.setTimeFormat("12h", false, "upper").isEmpty());
    rig.commit();
    CHECK(contains(render(past, rig.m_Settings.configuration()), "\n10:05 AM T| Wrong Response"));

    REQUIRE(rig.m_Settings.setTimeFormat("12h", true, "lower").isEmpty());
    rig.commit();
    CHECK(contains(render(past, rig.m_Settings.configuration()), "\n10:05:00 am T| Wrong Response"));
}

TEST_CASE("The totals option reaches the renderer's context", "[viewmodel][settings][render]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    const Scenario past = make_scenario("attached-note");
    const std::string without = render(past, rig.m_Settings.configuration());

    REQUIRE(rig.m_Settings.setIncludeTotals(true).isEmpty());
    rig.commit();
    const std::string with = render(past, rig.m_Settings.configuration());

    CHECK(with.size() > without.size());
    CHECK(with.starts_with(without));
}

TEST_CASE("A retired location and a retired error type still render on a past log", "[viewmodel][settings][render]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    Scenario past = blank_scenario();
    REQUIRE(past.m_Log.add(error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE)).has_value());
    REQUIRE(past.m_Log.add(error_entry(2, on_day(10, 6), ERROR_ECHO_DELAY_RETIRED)).has_value());
    const std::string before = render(past, rig.m_Settings.configuration());

    const auto& config = rig.m_Settings.configuration();
    REQUIRE(rig.m_Settings.removeLocation(QString::fromStdString(config.m_Locations[0].m_Id.format())).isEmpty());
    rig.commit();
    REQUIRE(rig.m_Settings.retireErrorType(QString::fromStdString(config.m_ErrorTypes[0].m_Id.format())).isEmpty());
    rig.commit();

    CHECK(config.m_Locations[0].m_Retired);
    CHECK(config.m_ErrorTypes[0].m_Retired);
    const std::string after = render(past, config);
    CHECK(after == before);
    CHECK(contains(after, "Location: North Site"));
    CHECK(contains(after, "T| Wrong Response"));
    CHECK(contains(after, "T| Echo Delay"));
}

TEST_CASE("A renamed location is named by its new name on a past log", "[viewmodel][settings][render]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    const Scenario past = blank_scenario();

    REQUIRE(rig.m_Settings
                .renameLocation(QString::fromStdString(rig.m_Settings.configuration().m_Locations[0].m_Id.format()),
                                "Main Site")
                .isEmpty());
    rig.commit();

    CHECK(contains(render(past, rig.m_Settings.configuration()), "Location: Main Site"));
}

TEST_CASE("A new tester applies to logs made afterwards and not to the one already made",
          "[viewmodel][settings][render]")
{
    SettingsRig rig;
    rig.load_configuration(scenario_configuration());
    // A log's header holds its own tester, copied when the log is made.
    Scenario open_log = blank_scenario();
    open_log.m_Log.set_initial_header(
        [&]
        {
            HeaderValues header = open_log.m_Log.initial_header();
            header.m_Tester = rig.m_Settings.configuration().m_Tester;
            return header;
        }());

    REQUIRE(rig.m_Settings.setTester("Alex Example").isEmpty());
    rig.commit();

    HeaderValues next = open_log.m_Log.initial_header();
    next.m_Tester = rig.m_Settings.configuration().m_Tester;
    CHECK(open_log.m_Log.initial_header().m_Tester == "Sam Example");
    CHECK(next.m_Tester == "Alex Example");
    CHECK(rig.m_Observer.changes() == 2); // the load and the change: the observers hear of it
}
