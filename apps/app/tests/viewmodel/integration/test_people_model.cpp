// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, package I-6: the list of people for the picker, on the real store and a storage thread. The names are the
// stand-ins of the docs.
#include "support/rig.hpp"
#include "viewmodel/interaction_types.hpp"
#include "viewmodel/people_model.hpp"
#include "viewmodel/timeline_model.hpp"

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

TEST_CASE("export.name_levels: copy and share of a log of the real people write the chosen level, never a list",
          "[viewmodel][people][export][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    rig.settle();
    const QString alex = add(rig, "Alex Sample");
    const QString jordan = add(rig, "Jordan Placeholder");
    add(rig, "Pat Stand-in"); // in the list, but on no entry: must never leave the phone

    const auto types = viewmodel::interaction_types_in_force(rig.settings().configuration());
    const QString entry = rig.session().recordInteractionEntry(QString::fromStdString(types[0].m_Id.format()));
    REQUIRE_FALSE(entry.isEmpty());
    rig.settle();
    REQUIRE(rig.session().setInteractionPeople(entry, {alex, jordan}));
    rig.settle();

    const auto count_of = [](const std::string& text, const std::string& what)
    {
        std::size_t found = 0;
        for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        {
            ++found;
        }
        return found;
    };
    const auto copied = [&rig]
    {
        REQUIRE(rig.timeline().copyAll());
        return rig.clipboard().text();
    };
    const auto shared = [&rig]
    {
        const std::size_t before = rig.platform().sheets_presented();
        REQUIRE(rig.share().shareOpenLog());
        rig.settle();
        REQUIRE(rig.platform().sheets_presented() == before + 1);
        return rig.platform().shared_texts().back();
    };

    // The default is initials.
    for (const std::string& text : {copied(), shared()})
    {
        CHECK(count_of(text, "(Interaction) Spoke with A.S. and J.P.") == 1);
        CHECK(text.find("Alex") == std::string::npos);
        CHECK(text.find("Jordan") == std::string::npos);
        CHECK(text.find("Pat") == std::string::npos);
        CHECK(text.find("T|") == std::string::npos);
    }

    rig.export_preferences().set_name_export("full");
    for (const std::string& text : {copied(), shared()})
    {
        CHECK(count_of(text, "Spoke with Alex Sample and Jordan Placeholder") == 1);
        // Each name once, in its line: no list of people anywhere else, and the third person is not there.
        CHECK(count_of(text, "Alex Sample") == 1);
        CHECK(count_of(text, "Jordan Placeholder") == 1);
        CHECK(text.find("Pat") == std::string::npos);
    }

    rig.export_preferences().set_name_export("hidden");
    for (const std::string& text : {copied(), shared()})
    {
        CHECK(count_of(text, "(Interaction) Spoke with someone") == 1);
        CHECK(text.find("Sample") == std::string::npos);
        CHECK(text.find("Placeholder") == std::string::npos);
        CHECK(text.find("Pat") == std::string::npos);
    }

    // The screen keeps the full name whatever the choice is.
    CHECK(rig.timeline().data(rig.timeline().index(0), viewmodel::TimelineModel::FULL_LINE).toString().contains("Alex Sample"));
}
