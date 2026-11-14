// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsModel's interaction types, the refusals and the quiet paths (Stage 14, package I-7; T-I7-1
// `settings.interaction_types`): an id that is not a type, a retired type, a tile with nothing on it, a full board, a
// full list, and the checks of a change. Unit tests on the fake store and the manual gateway.
#include "error_journaling/uuid.hpp"
#include "settings_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>

using namespace ej;
using namespace ej::test;

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

QString name_at(const QAbstractItemModel& model, int row)
{
    return text_at(model, row, role_named(model, "name"));
}

QString unknown_type_id()
{
    return QString::fromStdString(Uuid::from_random(5, 6).format());
}

} // namespace

TEST_CASE("settings.interaction_types: an id that is not a type, or not a live one, is refused everywhere",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const QString first = id_at(types, 0);
    const QString unknown = unknown_type_id();

    CHECK_FALSE(rig.m_Settings.retireInteractionType("not an id").isEmpty());
    CHECK_FALSE(rig.m_Settings.retireInteractionType(unknown).isEmpty());
    CHECK_FALSE(rig.m_Settings.updateInteractionType(unknown, "Sang", "").isEmpty());
    CHECK_FALSE(rig.m_Settings.assignInteractionTile(1, "not an id").isEmpty());
    CHECK_FALSE(rig.m_Settings.assignInteractionTile(1, unknown).isEmpty());
    CHECK_FALSE(rig.m_Settings.assignInteractionTile(0, first).isEmpty());
    CHECK_FALSE(rig.m_Settings.assignInteractionTile(12, first).isEmpty());
    CHECK_FALSE(rig.m_Settings.removeInteractionTile("not an id").isEmpty());
    CHECK_FALSE(rig.m_Settings.removeInteractionTile(unknown).isEmpty());
}

TEST_CASE("settings.interaction_types: a retired type cannot be edited, placed or taken off a tile",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Waved at", "").isEmpty());
    rig.commit();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const QString id = id_at(types, types.rowCount() - 1);
    CHECK(rig.m_Settings.retireInteractionType(id).isEmpty());
    rig.commit();

    CHECK_FALSE(rig.m_Settings.updateInteractionType(id, "Waved", "").isEmpty());
    CHECK_FALSE(rig.m_Settings.assignInteractionTile(1, id).isEmpty());
    CHECK_FALSE(rig.m_Settings.removeInteractionTile(id).isEmpty());
}

TEST_CASE("settings.interaction_types: taking a type off the board twice changes nothing the second time",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Waved at", "").isEmpty());
    rig.commit();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const int row = types.rowCount() - 1;
    const QString id = id_at(types, row);

    CHECK(rig.m_Settings.removeInteractionTile(id).isEmpty());
    rig.commit();
    CHECK(position_at(types, row) == 0);
    CHECK(rig.m_Settings.removeInteractionTile(id).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("settings.interaction_types: a type is put on a tile that is empty, on its own tile and from off the board",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Waved at", "").isEmpty());
    rig.commit();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const int row = types.rowCount() - 1;
    const QString id = id_at(types, row);
    const int home = position_at(types, row);
    REQUIRE(home >= 1);
    REQUIRE(home < 11);

    // Its own tile: nothing changes, nothing is saved.
    CHECK(rig.m_Settings.assignInteractionTile(home, id).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    // An empty tile: the type moves there and leaves its own.
    CHECK(rig.m_Settings.assignInteractionTile(home + 1, id).isEmpty());
    rig.commit();
    CHECK(position_at(types, row) == home + 1);

    // From off the board: it takes the tile.
    CHECK(rig.m_Settings.removeInteractionTile(id).isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.assignInteractionTile(home, id).isEmpty());
    rig.commit();
    CHECK(position_at(types, row) == home);
}

TEST_CASE("settings.interaction_types: a tile with nothing on it cannot be moved, and a tile moved onto itself stays",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const QAbstractItemModel& layout = *rig.m_Settings.interaction_layout();
    REQUIRE(layout.rowCount() == 12);
    REQUIRE(text_at(layout, 10, role_named(layout, "typeId")).isEmpty());

    CHECK_FALSE(rig.m_Settings.moveInteractionTile(11, 1).isEmpty());
    CHECK_FALSE(rig.m_Settings.moveInteractionTile(1, 0).isEmpty());
    CHECK(rig.m_Settings.moveInteractionTile(1, 1).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    // Towards the front: the tiles in between shift along.
    const QString third = id_at(types, 2);
    CHECK(rig.m_Settings.moveInteractionTile(3, 1).isEmpty());
    rig.commit();
    CHECK(position_at(types, 2) == 1);
    CHECK(id_at(types, 2) == third);
}

TEST_CASE("settings.interaction_types: a full board still takes types, and a full list refuses the next",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();

    QString refusal;
    int added = 0;
    for (int i = 0; i < 600 && refusal.isEmpty(); ++i)
    {
        refusal = rig.m_Settings.addInteractionType(QStringLiteral("Filler %1").arg(i), "");
        if (refusal.isEmpty())
        {
            rig.commit();
            ++added;
        }
    }
    CHECK_FALSE(refusal.isEmpty());
    CHECK(added > 11);
    // The last type found no empty tile: it is in the file but on no tile.
    CHECK(position_at(types, types.rowCount() - 1) == 0);
}

TEST_CASE("settings.interaction_types: the line and the name are checked on a change as on an add",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.addInteractionType("Waved at", "").isEmpty());
    rig.commit();
    const QAbstractItemModel& types = *rig.m_Settings.interaction_types();
    const int row = types.rowCount() - 1;
    const QString id = id_at(types, row);
    const QString taken = names_of(types).front();

    CHECK_FALSE(rig.m_Settings.updateInteractionType(id, "   ", "").isEmpty());
    CHECK_FALSE(rig.m_Settings.updateInteractionType(id, "Waved at", "Oops {").isEmpty());
    CHECK_FALSE(rig.m_Settings.updateInteractionType(id, taken.toUpper(), "").isEmpty());
    CHECK_FALSE(rig.m_Settings.updateInteractionType(id, "Waved at", QString(501, 'x')).isEmpty());
    CHECK_FALSE(rig.m_Settings.addInteractionType("Longer", QString(501, 'x')).isEmpty());
    CHECK_FALSE(rig.m_Settings.validateInteractionLine(QString(501, 'x')).isEmpty());
    CHECK_FALSE(rig.m_Settings.validateInteractionLine("Waved {name}").isEmpty());
    CHECK(rig.m_Settings.validateInteractionLine("   ").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    // A type can change the case of its own name.
    CHECK(rig.m_Settings.updateInteractionType(id, "WAVED AT", "Waved at [person]").isEmpty());
    rig.commit();
    CHECK(name_at(types, row) == "WAVED AT");
}

TEST_CASE("settings.interaction_types: the example line uses a plain label when the name is empty",
          "[viewmodel][settings][interaction_types][unit]")
{
    SettingsRig rig;
    rig.load_sample();
    CHECK(rig.m_Settings.previewInteractionLine("", "") == "Interaction");
    CHECK(rig.m_Settings.previewInteractionLine("   ", "") == "Interaction");
    CHECK(rig.m_Settings.previewInteractionLine("Greeted", "Greeted [person] and [answer]")
              .startsWith("Greeted Alex Sample"));
}
