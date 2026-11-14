// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, package I-6: the list of people for the picker, on the real store and a storage thread. The names are the
// stand-ins of the docs.
#include "support/rig.hpp"
#include "viewmodel/people_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QStringList>

using namespace ej;
using namespace ej::test;
using viewmodel::PeopleModel;

namespace
{

QString add(Rig& rig, const QString& name, const QString& location = {})
{
    PeopleModel& people = *rig.root().people();
    QSignalSpy added(&people, &PeopleModel::personAdded);
    REQUIRE(people.addPerson(name, location));
    rig.wait_until([&added] { return added.count() == 1; });
    return added.at(0).at(0).toString();
}

} // namespace

TEST_CASE("picker.write_order: a new name is in the store before personAdded is told",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    REQUIRE(people.available());

    QSignalSpy added(&people, &PeopleModel::personAdded);
    REQUIRE(people.addPerson("  Alex   Sample ", {}));
    rig.wait_until([&added] { return added.count() == 1; });
    const QString id = added.at(0).at(0).toString();
    CHECK(added.at(0).at(1).toString() == "Alex Sample");
    CHECK(people.nameOf(id) == "Alex Sample");

    // The store has it already: a read on the storage thread, queued after the add, finds the person by name.
    const auto stored =
        rig.in_store([](viewmodel::StorageContext& context) { return context.m_People->people().size(); });
    CHECK(stored == 1);
}

TEST_CASE("picker.search_and_add: a name that matches by case or spacing is not added twice",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    const QString id = add(rig, "Alex Sample");

    CHECK(people.matchFor("alex  SAMPLE") == id);
    QSignalSpy failed(&people, &PeopleModel::addFailed);
    CHECK_FALSE(people.addPerson("alex sample", {}));
    CHECK(failed.count() == 1);
    CHECK_FALSE(people.addPerson("   ", {}));
    CHECK(people.validateName("   ") == "Type a name.");
    CHECK(people.validateName(QString(61, 'x')) == "That name is too long.");
    CHECK(people.count() == 1);
}

TEST_CASE("picker.group: Everyone at a location is the people saved with that location",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    const QString north = "00000000-0000-4000-8000-0000000000b1";
    const QString a = add(rig, "Alex Sample", north);
    const QString b = add(rig, "Jordan Placeholder", north);
    add(rig, "Pat Stand-in");

    const QStringList everyone = people.everyoneAt(north);
    CHECK(everyone.size() == 2);
    CHECK(everyone.contains(a));
    CHECK(everyone.contains(b));
    CHECK(people.everyoneAt("").isEmpty());
}

TEST_CASE("The list of people is kept across a restart, last used first", "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    rig.settle();
    add(rig, "Alex Sample");
    const QString jordan = add(rig, "Jordan Placeholder");
    rig.root().people()->touch({jordan});
    rig.settle();
    rig.restart();
    rig.settle();

    PeopleModel& again = *rig.root().people();
    REQUIRE(again.available());
    REQUIRE(again.count() == 2);
    CHECK(again.data(again.index(0), PeopleModel::NAME).toString() == "Jordan Placeholder");
}

TEST_CASE("The export asks the list of people for a name by id", "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    rig.settle();
    const QString id = add(rig, "Alex Sample");
    viewmodel::PersonNamesForwarder forwarder;
    const PersonId person(*Uuid::parse(id.toStdString()));
    CHECK_FALSE(forwarder.name_of(person).has_value());
    forwarder.set(rig.root().people());
    CHECK(forwarder.name_of(person) == std::optional<std::string>("Alex Sample"));
    CHECK_FALSE(forwarder.name_of(PersonId(Uuid::from_random(1, 2))).has_value());
}
