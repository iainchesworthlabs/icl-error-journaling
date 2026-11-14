// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsModel's interaction types (Stage 14, package I-7; T-I7-1 `settings.interaction_types`): add, rename, retire,
// move, swap, and the lines. Unit tests on the fake store and the manual gateway.
#include "settings_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>

using namespace ej;
using namespace ej::test;
using viewmodel::SettingsModel;

namespace
{

QStringList names_of(const QAbstractItemModel& model)
{
    const int role = role_named(model, "name");
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        names.push_back(text_at(model, row, role));
    }
    return names;
}

QString id_at(const QAbstractItemModel& model, int row)
{
    return text_at(model, row, role_named(model, "typeId"));
}

int position_at(const QAbstractItemModel& model, int row)
{
    return number_at(model, row, role_named(model, "boardPosition"));
}

QString text_of_role(const QAbstractItemModel& model, int row, const char* role)
{
    return text_at(model, row, role_named(model, role));
}

} // namespace

TEST_CASE("settings.interaction_types: a type is added at the first empty tile with its line and asks",
          "[viewmodel][settings][interaction_types]")
{
    SettingsRig rig;
    rig.load_sample();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const std::size_t before = rig.m_Settings.configuration().m_InteractionTypes.size();

    CHECK(rig.m_Settings.addInteractionType("  Greeted ", "Greeted [person]").isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.addInteractionType("Sang", "Sang [answer]").isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.addInteractionType("Waved", "").isEmpty());
    rig.commit();

    const auto& stored = rig.m_Settings.configuration().m_InteractionTypes;
    REQUIRE(stored.size() == before + 3);
    const InteractionType& spoke = stored[before];
    CHECK(spoke.m_Name == "Greeted");
    CHECK(spoke.m_LinePattern == std::optional<std::string>("Greeted {person}"));
    CHECK(spoke.m_Asks == InteractionAsks::PERSON);
    CHECK(stored[before + 1].m_LinePattern == std::optional<std::string>("Sang {value}"));
    CHECK(stored[before + 1].m_Asks == InteractionAsks::TEXT);
    // No line: the log shows the name, and nothing is asked.
    CHECK_FALSE(stored[before + 2].m_LinePattern.has_value());
    CHECK(stored[before + 2].m_Asks == InteractionAsks::NOTHING);
    CHECK(spoke.m_BoardPosition.has_value());
    CHECK(import_configuration(*rig.store().saved()).has_value());

    const int last = types.rowCount() - 3;
    CHECK(text_of_role(types, last, "line") == "Greeted [person]");
    CHECK(text_of_role(types, last, "asks") == "person");
    CHECK(text_of_role(types, last + 1, "asks") == "text");
    CHECK(text_of_role(types, last + 2, "asks") == "nothing");
}

TEST_CASE("settings.interaction_types: a name must be given and differ, ignoring case; a bad line is refused",
          "[viewmodel][settings][interaction_types]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Greeted", "Greeted [person]").isEmpty());
    rig.commit();
    const auto saves = rig.store().saves();

    CHECK_FALSE(rig.m_Settings.addInteractionType("   ", "").isEmpty());
    CHECK_FALSE(rig.m_Settings.addInteractionType("GREETED", "").isEmpty());
    CHECK_FALSE(rig.m_Settings.addInteractionType("Two", "[person] and [person]").isEmpty());
    CHECK_FALSE(rig.m_Settings.addInteractionType("Brace", "Oops {").isEmpty());
    CHECK_FALSE(rig.m_Settings.addInteractionType("Unknown", "{name}").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK(rig.store().saves() == saves);

    CHECK(rig.m_Settings.validateInteractionLine("").isEmpty());
    CHECK(rig.m_Settings.validateInteractionLine("Spoke with [person] about [answer]").isEmpty());
    CHECK_FALSE(rig.m_Settings.validateInteractionLine("[answer] [answer]").isEmpty());
}

TEST_CASE("settings.interaction_types: a type is edited, and a type with no [person] never asks for one",
          "[viewmodel][settings][interaction_types]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Greeted", "Greeted [person]").isEmpty());
    rig.commit();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const int row = types.rowCount() - 1;
    const QString id = id_at(types, row);

    CHECK(rig.m_Settings.updateInteractionType(id, "Chatted", "Played [answer]").isEmpty());
    rig.commit();
    CHECK(text_of_role(types, row, "name") == "Chatted");
    CHECK(text_of_role(types, row, "asks") == "text");

    CHECK(rig.m_Settings.updateInteractionType(id, "Chatted", "Chatted").isEmpty());
    rig.commit();
    CHECK(text_of_role(types, row, "asks") == "nothing");

    // Nothing changes: nothing is saved.
    CHECK(rig.m_Settings.updateInteractionType(id, "Chatted", "Chatted").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK_FALSE(rig.m_Settings.updateInteractionType("not-an-id", "X", "").isEmpty());
}

TEST_CASE("settings.interaction_types: a type is retired and stays in the file, and its name can be used again",
          "[viewmodel][settings][interaction_types]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Greeted", "Greeted [person]").isEmpty());
    rig.commit();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const int rows = types.rowCount();
    const QString id = id_at(types, rows - 1);

    CHECK(rig.m_Settings.retireInteractionType(id).isEmpty());
    rig.commit();
    CHECK(types.rowCount() == rows - 1);
    CHECK_FALSE(names_of(types).contains("Greeted"));
    CHECK(rig.m_Settings.configuration().m_InteractionTypes.back().m_Retired);
    CHECK_FALSE(rig.m_Settings.retireInteractionType(id).isEmpty());
    CHECK(rig.m_Settings.addInteractionType("Greeted", "").isEmpty());
    rig.commit();
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("settings.interaction_types: tiles move, swap and leave the board", "[viewmodel][settings][interaction_types]")
{
    SettingsRig rig;
    rig.load_sample();
    for (const char* name : {"One", "Two", "Three"})
    {
        CHECK(rig.m_Settings.addInteractionType(name, "").isEmpty());
        rig.commit();
    }
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const int n = types.rowCount();
    const QString one = id_at(types, n - 3);
    const QString three = id_at(types, n - 1);
    const int first = position_at(types, n - 3);
    CHECK(position_at(types, n - 2) == first + 1);
    CHECK(position_at(types, n - 1) == first + 2);

    // One moves two places along: Two and Three shift back.
    CHECK(rig.m_Settings.moveInteractionTile(first, first + 2).isEmpty());
    rig.commit();
    CHECK(position_at(types, n - 3) == first + 2);
    CHECK(position_at(types, n - 2) == first);

    CHECK_FALSE(rig.m_Settings.moveInteractionTile(0, 1).isEmpty());
    CHECK_FALSE(rig.m_Settings.moveInteractionTile(1, 12).isEmpty());

    // Assigning One to Three's tile swaps them.
    const int three_at = position_at(types, n - 1);
    const int one_at = position_at(types, n - 3);
    CHECK(rig.m_Settings.assignInteractionTile(three_at, one).isEmpty());
    rig.commit();
    CHECK(position_at(types, n - 3) == three_at);
    CHECK(position_at(types, n - 1) == one_at);

    CHECK(rig.m_Settings.removeInteractionTile(three).isEmpty());
    rig.commit();
    CHECK(position_at(types, n - 1) == 0);
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("settings.interaction_types: the example line shows a sample person and answer",
          "[viewmodel][settings][interaction_types]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.previewInteractionLine("Spoke with", "Spoke with [person]") == "Spoke with Alex Sample");
    CHECK(rig.m_Settings.previewInteractionLine("Played song", "Played [answer]") == "Played Morning Medley");
    CHECK(rig.m_Settings.previewInteractionLine("Waved", "") == "Waved");
    CHECK(rig.m_Settings.previewInteractionLine("X", "{") == "Fix the line to see an example.");
}
