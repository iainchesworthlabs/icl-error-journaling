// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, package I-7: the People screen's list, on the real store and a storage thread. The names are the stand-ins
// of the docs.
#include "error_journaling/uuid.hpp"
#include "support/rig.hpp"
#include "viewmodel/people_admin_model.hpp"
#include "viewmodel/people_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>

using namespace ej;
using namespace ej::test;
using viewmodel::PeopleAdminModel;
using viewmodel::PeopleModel;

namespace
{

QString add(Rig& rig, const QString& name)
{
    PeopleAdminModel& admin = *rig.root().people_admin();
    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.addPerson(name, {}).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    REQUIRE(admin.count() >= 1);
    for (int row = 0; row < admin.rowCount(); ++row)
    {
        if (admin.data(admin.index(row), PeopleAdminModel::NAME).toString() == name.simplified())
        {
            return admin.data(admin.index(row), PeopleAdminModel::PERSON_ID).toString();
        }
    }
    FAIL("the person was not listed");
    return {};
}

} // namespace

TEST_CASE("settings.people_model: add, rename, retire and bring back, and the picker follows",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    PeopleModel& picker = *rig.root().people();
    rig.settle();
    REQUIRE(admin.available());

    const QString alex = add(rig, QStringLiteral("  Alex   Sample "));
    CHECK(admin.count() == 1);
    CHECK(admin.data(admin.index(0), PeopleAdminModel::NAME).toString() == QStringLiteral("Alex Sample"));
    CHECK(picker.count() == 1);

    // A repeated name is refused at once, by case, spacing and accents.
    CHECK_FALSE(admin.addPerson(QStringLiteral("alex sample"), {}).isEmpty());
    CHECK_FALSE(admin.addPerson(QStringLiteral("   "), {}).isEmpty());
    CHECK_FALSE(admin.addPerson(QString(61, QLatin1Char('x')), {}).isEmpty());

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.renamePerson(alex, QStringLiteral("Alex Example")).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    CHECK(admin.data(admin.index(0), PeopleAdminModel::NAME).toString() == QStringLiteral("Alex Example"));
    CHECK(picker.nameOf(alex) == QStringLiteral("Alex Example"));

    // Retired: gone from the picker, still in this list, and the export still knows the name.
    REQUIRE(admin.setRetired(alex, true).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 2; });
    rig.settle();
    CHECK(admin.count() == 1);
    CHECK(admin.data(admin.index(0), PeopleAdminModel::RETIRED).toBool());
    CHECK(picker.count() == 0);
    CHECK(picker.name_of(PersonId(*Uuid::parse(alex.toStdString()))).value_or("") == "Alex Example");

    REQUIRE(admin.setRetired(alex, false).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 3; });
    rig.settle();
    CHECK_FALSE(admin.data(admin.index(0), PeopleAdminModel::RETIRED).toBool());
    CHECK(picker.count() == 1);
}

TEST_CASE("settings.people_model: a person is moved to another location and to none, and nobody else changes",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    REQUIRE(admin.available());
    const QString alex = add(rig, QStringLiteral("Alex Sample"));
    const QString jordan = add(rig, QStringLiteral("Jordan Placeholder"));
    const QString north = QString::fromStdString(Uuid::parse("00000000-0000-4000-8000-0000000000a1")->format());

    const auto location_of = [&admin](const QString& id)
    {
        for (int row = 0; row < admin.rowCount(); ++row)
        {
            if (admin.data(admin.index(row), PeopleAdminModel::PERSON_ID).toString() == id)
            {
                return admin.data(admin.index(row), PeopleAdminModel::LOCATION_ID).toString();
            }
        }
        return QStringLiteral("not listed");
    };
    CHECK(location_of(alex).isEmpty());

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.setLocation(alex, north).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    CHECK(location_of(alex) == north);
    CHECK(location_of(jordan).isEmpty());
    CHECK(admin.count() == 2);

    REQUIRE(admin.setLocation(alex, {}).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 2; });
    rig.settle();
    CHECK(location_of(alex).isEmpty());

    // Nobody who is not listed can be moved.
    CHECK_FALSE(admin.setLocation(QStringLiteral("not an id"), north).isEmpty());
    CHECK(admin.count() == 2);
}

TEST_CASE("settings.people_model: Forget removes the person from both lists and the name from the export",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    PeopleModel& picker = *rig.root().people();
    rig.settle();
    const QString alex = add(rig, QStringLiteral("Alex Sample"));
    add(rig, QStringLiteral("Jordan Placeholder"));
    REQUIRE(admin.count() == 2);

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.forgetPerson(alex).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    CHECK(admin.count() == 1);
    CHECK(admin.data(admin.index(0), PeopleAdminModel::NAME).toString() == QStringLiteral("Jordan Placeholder"));
    CHECK(picker.count() == 1);
    CHECK(picker.nameOf(alex).isEmpty());
    CHECK_FALSE(picker.name_of(PersonId(*Uuid::parse(alex.toStdString()))).has_value());

    // The name is not in the store any more, and the same name can be added again.
    const auto stored = rig.in_store(
        [](viewmodel::StorageContext& context)
        {
            std::size_t found = 0;
            for (const Person& person : context.m_People->people())
            {
                found += person.m_DisplayName == "Alex Sample" ? std::size_t{1} : std::size_t{0};
            }
            return found;
        });
    CHECK(stored == 0);
    CHECK(admin.addPerson(QStringLiteral("Alex Sample"), {}).isEmpty());
}

TEST_CASE(
    "settings.people_model: a retired person's name can be taken by a new person, and bringing both back is refused",
    "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    const QString first = add(rig, QStringLiteral("Alex Sample"));
    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.setRetired(first, true).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();

    const QString second = add(rig, QStringLiteral("Alex Sample"));
    CHECK(second != first);
    CHECK_FALSE(admin.setRetired(first, false).isEmpty());
}
