// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, package I-9 (T-I9-1): the rows of Review say a person's name as the list of people has it now, on the real
// store and a storage thread: the name appears when the list has loaded, follows a rename, and reads "someone" once the
// person is forgotten. The names are the stand-ins of the docs.
#include "support/rig.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/people_admin_model.hpp"
#include "viewmodel/timeline_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QStringList>

using namespace ej;
using namespace ej::test;
using viewmodel::PeopleAdminModel;
using viewmodel::TimelineModel;

namespace
{

constexpr auto SPOKE_WITH = "00000000-0000-4000-8000-0000000000d1";

QString row_line(TimelineModel& timeline, int row)
{
    return timeline.data(timeline.index(row), TimelineModel::FULL_LINE).toString();
}

}  // namespace

TEST_CASE("review.interaction_rows: a name follows the list of people, a rename and Forget",
          "[viewmodel][review][interaction][people][integration]")
{
    Rig rig(RigStore::SQLITE);
    rig.import_sample_configuration();
    PeopleAdminModel& admin = *rig.root().people_admin();
    TimelineModel& timeline = *rig.root().timeline();
    rig.settle();
    REQUIRE(admin.available());

    QSignalSpy changed(&admin, &PeopleAdminModel::changed);
    REQUIRE(admin.addPerson(QStringLiteral("Alex Sample"), {}).isEmpty());
    rig.wait_until([&changed] { return changed.count() == 1; });
    rig.settle();
    QString alex;
    for (int row = 0; row < admin.rowCount(); ++row)
    {
        alex = admin.data(admin.index(row), PeopleAdminModel::PERSON_ID).toString();
    }
    REQUIRE_FALSE(alex.isEmpty());

    auto& session = *rig.root().session();
    const QString entry = session.recordInteractionEntry(QString::fromLatin1(SPOKE_WITH));
    REQUIRE_FALSE(entry.isEmpty());
    rig.settle();
    REQUIRE(session.setInteractionPeople(entry, QStringList{alex}));
    rig.settle();
    REQUIRE(timeline.rowCount() == 1);
    CHECK(row_line(timeline, 0).endsWith(QStringLiteral("(Interaction) Spoke with Alex Sample")));

    changed.clear();
    REQUIRE(admin.renamePerson(alex, QStringLiteral("Alex Sampler")).isEmpty());
    rig.wait_until([&changed] { return changed.count() >= 1; });
    rig.settle();
    rig.wait_until([&timeline] { return row_line(timeline, 0).endsWith(QStringLiteral("Alex Sampler")); });
    CHECK(row_line(timeline, 0).endsWith(QStringLiteral("Spoke with Alex Sampler")));

    changed.clear();
    REQUIRE(admin.forgetPerson(alex).isEmpty());
    rig.wait_until([&changed] { return changed.count() >= 1; });
    rig.settle();
    rig.wait_until([&timeline] { return row_line(timeline, 0).endsWith(QStringLiteral("someone")); });
    CHECK(row_line(timeline, 0).endsWith(QStringLiteral("Spoke with someone")));
    CHECK(row_line(timeline, 0).contains(QStringLiteral("Sampler")) == false);
}
