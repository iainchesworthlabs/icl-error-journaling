// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, packages I-6 and I-7: the refusals and the quiet paths of the list of people (the picker's model and the
// People screen's model), on the real store and a storage thread, and with no store at all. The names are the
// stand-ins of the docs.
#include "error_journaling/uuid.hpp"
#include "support/rig.hpp"
#include "viewmodel/people_admin_model.hpp"
#include "viewmodel/people_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <chrono>
#include <optional>

using namespace ej;
using namespace ej::test;
using viewmodel::PeopleAdminModel;
using viewmodel::PeopleModel;

namespace
{

const QString NORTH = QStringLiteral("00000000-0000-4000-8000-0000000000b1");
const QString SOUTH = QStringLiteral("00000000-0000-4000-8000-0000000000b2");

QString add(Rig& rig, const QString& name, const QString& location = {})
{
    PeopleModel& people = *rig.root().people();
    QSignalSpy added(&people, &PeopleModel::personAdded);
    REQUIRE(people.addPerson(name, location));
    rig.wait_until([&added] { return added.count() == 1; });
    return added.at(0).at(0).toString();
}

QString unknown_person()
{
    return QString::fromStdString(Uuid::from_random(31, 32).format());
}

QString name_at(const PeopleModel& people, int row)
{
    return people.data(people.index(row), PeopleModel::NAME).toString();
}

/// Makes the people admin model reload and waits for it, so that its mirror is the store's.
void reload_admin(Rig& rig)
{
    rig.root().people_admin()->reload();
    rig.settle();
}

} // namespace

TEST_CASE("picker.unavailable: with no list of people the picker says so and changes nothing",
          "[viewmodel][people][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    REQUIRE_FALSE(people.available());
    CHECK(people.count() == 0);

    QSignalSpy failed(&people, &PeopleModel::addFailed);
    CHECK_FALSE(people.addPerson("Alex Sample", {}));
    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == "People are not available right now.");

    // Touching someone with no list is quiet, and a name is still told as "someone".
    people.touch({unknown_person(), "not an id"});
    people.touch({});
    CHECK(people.labelFor(unknown_person()) == "someone");
    CHECK(people.labelFor("not an id") == "someone");
    CHECK(people.nameOf(unknown_person()).isEmpty());
    CHECK_FALSE(people.isListed(unknown_person()));
    CHECK(people.recentIds(3).isEmpty());
    CHECK(people.everyoneAt(NORTH).isEmpty());
}

TEST_CASE("settings.people_model: with no list of people every change is refused with one message",
          "[viewmodel][people][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    REQUIRE_FALSE(admin.available());

    const QString who = unknown_person();
    CHECK(admin.addPerson("Alex Sample", {}) == "People are not available right now.");
    CHECK(admin.renamePerson(who, "Alex Sample") == "People are not available right now.");
    CHECK(admin.setLocation(who, NORTH) == "People are not available right now.");
    CHECK(admin.setRetired(who, true) == "People are not available right now.");
    CHECK(admin.setRetired(who, false) == "People are not available right now.");
    CHECK(admin.forgetPerson(who) == "People are not available right now.");
    CHECK(admin.count() == 0);
}

TEST_CASE("picker.model: the rows answer only for a row that is there, and the roles are the ones QML reads",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    const QString alex = add(rig, "Alex Sample", NORTH);
    const QString jordan = add(rig, "Jordan Placeholder");

    CHECK(people.rowCount() == 2);
    CHECK(people.rowCount(people.index(0)) == 0);
    CHECK_FALSE(people.data(QModelIndex(), PeopleModel::NAME).isValid());
    CHECK_FALSE(people.data(people.index(7), PeopleModel::NAME).isValid());
    CHECK_FALSE(people.data(people.index(0), PeopleModel::NAME + 100).isValid());
    CHECK(people.data(people.index(0), Qt::DisplayRole).toString() == people.data(people.index(0), PeopleModel::NAME));
    CHECK(people.roleNames().size() == 3);

    // The role of the location: the id for a person with one, empty for a person with none.
    for (int row = 0; row < people.rowCount(); ++row)
    {
        const QString id = people.data(people.index(row), PeopleModel::PERSON_ID).toString();
        const QString location = people.data(people.index(row), PeopleModel::LOCATION_ID).toString();
        CHECK(location == (id == alex ? NORTH : QString()));
    }
    CHECK(people.isListed(jordan));
    CHECK_FALSE(people.isListed("not an id"));
    CHECK_FALSE(people.isListed(unknown_person()));
    CHECK(people.nameOf("not an id").isEmpty());
    CHECK(people.nameOf(unknown_person()).isEmpty());
    CHECK(people.nameOf(jordan) == "Jordan Placeholder");
}

TEST_CASE("picker.add: a name that is empty, too long, repeated or lands on a bad location is told, not added",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    QSignalSpy failed(&people, &PeopleModel::addFailed);

    CHECK_FALSE(people.addPerson("", {}));
    CHECK_FALSE(people.addPerson(QString(61, 'x'), {}));
    REQUIRE(failed.count() == 2);
    CHECK(failed.at(0).at(0).toString() == "Type a name.");
    CHECK(failed.at(1).at(0).toString() == "That name is too long.");
    CHECK(people.validateName("Alex Sample").isEmpty());
    CHECK(people.matchFor("").isEmpty());
    CHECK(people.matchFor(QString(61, 'x')).isEmpty());

    // A location that is not an id is no location.
    const QString id = add(rig, "Alex Sample", "not an id");
    CHECK(people.data(people.index(0), PeopleModel::LOCATION_ID).toString().isEmpty());
    CHECK(people.matchFor("alex sample") == id);
    CHECK(people.matchFor("Jordan Placeholder").isEmpty());
    CHECK(people.count() == 1);
}

TEST_CASE("picker.write_order: a name the store refuses after the mirror allowed it is told, and the list stays",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();

    // The store has the name already (another writer); the picker's mirror does not know yet.
    const bool stored = rig.in_store(
        [](viewmodel::StorageContext& context)
        {
            return context.m_People->add_person(PersonId(Uuid::from_random(7, 8)), "Alex Sample", std::nullopt)
                .has_value();
        });
    REQUIRE(stored);

    QSignalSpy failed(&people, &PeopleModel::addFailed);
    QSignalSpy added(&people, &PeopleModel::personAdded);
    REQUIRE(people.addPerson("Alex Sample", {}));
    rig.wait_until([&failed] { return failed.count() == 1; });
    CHECK(failed.at(0).at(0).toString() == "That name could not be saved. Please try again.");
    CHECK(added.count() == 0);
    CHECK(people.count() == 0);
}

TEST_CASE("picker.order: last used first, a person never used last, then by name; the location in force before all",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    const QString pat = add(rig, "Pat Stand-in", SOUTH);
    const QString alex = add(rig, "Alex Sample", SOUTH);
    const QString jordan = add(rig, "Jordan Placeholder", NORTH);

    // None used: by name.
    CHECK(name_at(people, 0) == "Alex Sample");
    CHECK(name_at(people, 1) == "Jordan Placeholder");
    CHECK(name_at(people, 2) == "Pat Stand-in");
    CHECK(people.recentIds(0).isEmpty());
    CHECK(people.recentIds(-1).isEmpty());

    // One used: first, the others still by name.
    people.touch({pat});
    CHECK(name_at(people, 0) == "Pat Stand-in");
    CHECK(name_at(people, 1) == "Alex Sample");
    CHECK(name_at(people, 2) == "Jordan Placeholder");

    // Two used at different times: the later first; one used at all only after.
    rig.advance(std::chrono::seconds(5));
    people.touch({jordan});
    CHECK(name_at(people, 0) == "Jordan Placeholder");
    CHECK(name_at(people, 1) == "Pat Stand-in");
    CHECK(name_at(people, 2) == "Alex Sample");

    // The location in force first: South's two, the used one before the other.
    people.setCurrentLocation(SOUTH);
    CHECK(name_at(people, 0) == "Pat Stand-in");
    CHECK(name_at(people, 1) == "Alex Sample");
    CHECK(name_at(people, 2) == "Jordan Placeholder");

    // The same location again, and one that is not an id (no location), and back to none.
    people.setCurrentLocation(SOUTH);
    CHECK(name_at(people, 0) == "Pat Stand-in");
    people.setCurrentLocation("not an id");
    CHECK(name_at(people, 0) == "Jordan Placeholder");
    people.setCurrentLocation("");
    CHECK(name_at(people, 0) == "Jordan Placeholder");

    // Touch with nothing to touch: invalid ids, a person who is not listed, and an empty list.
    people.touch({});
    people.touch({"not an id"});
    people.touch({unknown_person()});
    CHECK(name_at(people, 0) == "Jordan Placeholder");
    CHECK(people.recentIds(10).size() == 2);
    CHECK(people.recentIds(10).contains(alex) == false);
}

TEST_CASE("picker.group: Everyone at a location that is not an id or has nobody is empty",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    rig.settle();
    const QString alex = add(rig, "Alex Sample", NORTH);
    add(rig, "Pat Stand-in");

    CHECK(people.everyoneAt("not an id").isEmpty());
    CHECK(people.everyoneAt(SOUTH).isEmpty());
    CHECK(people.everyoneAt(NORTH) == QStringList{alex});
}

TEST_CASE("picker.retired: a retired person leaves the picker but a line that names them still reads their name",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleModel& people = *rig.root().people();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    const QString alex = add(rig, "Alex Sample");
    reload_admin(rig);

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.setRetired(alex, true).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    people.reload();
    rig.settle();

    CHECK_FALSE(people.isListed(alex));
    CHECK(people.nameOf(alex).isEmpty());
    CHECK(people.labelFor(alex) == "Alex Sample");
    CHECK(people.matchFor("Alex Sample").isEmpty());
    CHECK(people.count() == 0);
}

TEST_CASE("settings.people_model: the rows answer only for a row that is there, retired people last",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    PeopleModel& picker = *rig.root().people();
    rig.settle();
    const QString alex = add(rig, "Alex Sample", NORTH);
    const QString jordan = add(rig, "Jordan Placeholder");
    const QString pat = add(rig, "Pat Stand-in");
    reload_admin(rig);
    REQUIRE(admin.count() == 3);

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.setRetired(alex, true).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();

    // In use first by name, the retired last.
    CHECK(admin.data(admin.index(0), PeopleAdminModel::NAME).toString() == "Jordan Placeholder");
    CHECK(admin.data(admin.index(1), PeopleAdminModel::NAME).toString() == "Pat Stand-in");
    CHECK(admin.data(admin.index(2), PeopleAdminModel::NAME).toString() == "Alex Sample");
    CHECK(admin.data(admin.index(2), PeopleAdminModel::RETIRED).toBool());
    CHECK_FALSE(admin.data(admin.index(0), PeopleAdminModel::RETIRED).toBool());
    CHECK(admin.data(admin.index(2), PeopleAdminModel::LOCATION_ID).toString() == NORTH);
    CHECK(admin.data(admin.index(0), PeopleAdminModel::LOCATION_ID).toString().isEmpty());
    CHECK(admin.data(admin.index(0), Qt::DisplayRole).toString() == "Jordan Placeholder");
    CHECK(admin.data(admin.index(0), PeopleAdminModel::PERSON_ID).toString() == jordan);
    CHECK(admin.data(admin.index(1), PeopleAdminModel::PERSON_ID).toString() == pat);
    CHECK_FALSE(admin.data(admin.index(0), PeopleAdminModel::NAME + 100).isValid());
    CHECK_FALSE(admin.data(QModelIndex(), PeopleAdminModel::NAME).isValid());
    CHECK_FALSE(admin.data(admin.index(9), PeopleAdminModel::NAME).isValid());
    CHECK(admin.rowCount(admin.index(0)) == 0);
    CHECK(admin.roleNames().size() == 4);
    CHECK(picker.count() == 2);
}

TEST_CASE("settings.people_model: a name is checked against the others, not against itself or a retired one",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    const QString alex = add(rig, "Alex Sample");
    const QString jordan = add(rig, "Jordan Placeholder");
    reload_admin(rig);

    CHECK(admin.validateName("", {}) == "Type a name.");
    CHECK(admin.validateName("   ", {}) == "Type a name.");
    CHECK(admin.validateName(QString(61, 'x'), {}) == "That name is too long.");
    CHECK(admin.validateName("alex  SAMPLE", {}) == "That name is in the list already.");
    CHECK(admin.validateName("alex  SAMPLE", jordan) == "That name is in the list already.");
    CHECK(admin.validateName("alex  SAMPLE", alex).isEmpty());
    CHECK(admin.validateName("alex  SAMPLE", "not an id") == "That name is in the list already.");
    CHECK(admin.validateName("Pat Stand-in", {}).isEmpty());

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.setRetired(alex, true).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    CHECK(admin.validateName("Alex Sample", {}).isEmpty());
}

TEST_CASE("settings.people_model: a change to a person who is not there, or with a bad name, is refused at once",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    const QString alex = add(rig, "Alex Sample");
    const QString jordan = add(rig, "Jordan Placeholder");
    reload_admin(rig);
    const QString who = unknown_person();

    CHECK_FALSE(admin.addPerson("", {}).isEmpty());
    CHECK_FALSE(admin.addPerson(QString(61, 'x'), {}).isEmpty());
    CHECK_FALSE(admin.renamePerson("not an id", "Pat Stand-in").isEmpty());
    CHECK_FALSE(admin.renamePerson(who, "Pat Stand-in").isEmpty());
    CHECK_FALSE(admin.renamePerson(alex, "").isEmpty());
    CHECK_FALSE(admin.renamePerson(alex, "jordan placeholder").isEmpty());
    CHECK_FALSE(admin.setLocation("not an id", NORTH).isEmpty());
    CHECK_FALSE(admin.setLocation(who, NORTH).isEmpty());
    CHECK_FALSE(admin.setRetired("not an id", true).isEmpty());
    CHECK_FALSE(admin.setRetired(who, false).isEmpty());
    CHECK_FALSE(admin.forgetPerson("not an id").isEmpty());
    CHECK_FALSE(admin.forgetPerson(who).isEmpty());
    CHECK(admin.count() == 2);

    // A rename to the same name with other spacing is the person's own name: allowed.
    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    CHECK(admin.renamePerson(jordan, "  JORDAN   Placeholder ").isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
}

TEST_CASE("settings.people_model: a location is set, changed to another and taken off, and an invalid one is none",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    const QString alex = add(rig, "Alex Sample");
    reload_admin(rig);

    const auto location_now = [&admin]()
    { return admin.data(admin.index(0), PeopleAdminModel::LOCATION_ID).toString(); };
    for (const QString& next : {NORTH, SOUTH, QString(), NORTH, QStringLiteral("not an id")})
    {
        QSignalSpy changed(&admin, &PeopleAdminModel::changed);
        REQUIRE(admin.setLocation(alex, next).isEmpty());
        rig.wait_until([&changed] { return changed.count() == 1; });
        rig.settle();
        const bool valid = next == NORTH || next == SOUTH;
        CHECK(location_now() == (valid ? next : QString()));
    }
}

TEST_CASE("settings.people_model: a change the store refuses is told, and the list is read again",
          "[viewmodel][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    rig.settle();
    const QString alex = add(rig, "Alex Sample");
    const QString jordan = add(rig, "Jordan Placeholder");
    const QString pat = add(rig, "Pat Stand-in");
    reload_admin(rig);
    REQUIRE(admin.count() == 3);

    // Behind the model's back the store forgets all three: its mirror still lists them.
    const auto forgotten = rig.in_store(
        [alex, jordan, pat](viewmodel::StorageContext& context)
        {
            bool all = true;
            for (const QString& text : {alex, jordan, pat})
            {
                all = context.m_People->forget_person(PersonId(*Uuid::parse(text.toStdString()))).has_value() && all;
            }
            return all;
        });
    REQUIRE(forgotten);

    QSignalSpy failed(&admin, &PeopleAdminModel::failed);
    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    // All three are asked before the first answer comes back: the first failure reads the list again.
    REQUIRE(admin.renamePerson(alex, "Alex Sample Two").isEmpty());
    REQUIRE(admin.setLocation(jordan, NORTH).isEmpty());
    REQUIRE(admin.setRetired(pat, true).isEmpty());
    rig.wait_until([&failed] { return failed.count() == 3; });
    CHECK(failed.at(0).at(0).toString() == "That change could not be saved. Please try again.");
    rig.settle();
    CHECK(changed.count() == 0);
}
