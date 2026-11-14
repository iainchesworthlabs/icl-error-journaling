// SPDX-License-Identifier: GPL-3.0-or-later
// Interaction entries in Review and in the Edit entry sheet (docs/plan/stage-14.md, 4.6): review.interaction_rows
// (T-I9-1) and edit.interaction (T-I9-2). The rows read as the export does, at full names, and swipe, copy, delete and
// undo work as for an error; the sheet changes the type, the people, the answer, the time and the note, with Save and
// Cancel explicit, and gives the people to an entry that was logged without.
#include "review_test_support.hpp"
// The other headers of the tests come after the project's, as the support header says.
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractListModel>
#include <QHash>
#include <QMetaObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::review;
using ej::viewmodel::PersonNames;
using ej::viewmodel::TimelineModel;

namespace
{

class FixedPeople final : public PersonNames
{
public:
    FixedPeople()
    {
        m_Names[test::test_id<PersonId>(test::PERSON_ALEX)] = "Alex Sample";
        m_Names[test::test_id<PersonId>(test::PERSON_JORDAN)] = "Jordan Placeholder";
    }

    [[nodiscard]] std::optional<std::string> name_of(const PersonId& id) const override
    {
        const auto it = m_Names.find(id);
        return it == m_Names.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

private:
    std::map<PersonId, std::string> m_Names;
};

/// The list of people, with the names of `PeopleModel`, in memory: a name is added at once and `personAdded` follows.
class FakePeopleList : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    struct Row
    {
        QString m_Id;
        QString m_Name;
        QString m_Location;
    };

    [[nodiscard]] bool available() const
    {
        return true;
    }
    [[nodiscard]] int count() const
    {
        return static_cast<int>(m_Rows.size());
    }
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : count();
    }
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override
    {
        const Row& row = m_Rows.at(static_cast<std::size_t>(index.row()));
        return role == Qt::UserRole + 1 ? row.m_Id : role == Qt::UserRole + 2 ? row.m_Name : row.m_Location;
    }
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "personId"}, {Qt::UserRole + 2, "name"}, {Qt::UserRole + 3, "locationId"}};
    }

    Q_INVOKABLE QString matchFor(const QString& typed) const
    {
        for (const Row& row : m_Rows)
        {
            if (row.m_Name.compare(typed.simplified(), Qt::CaseInsensitive) == 0)
            {
                return row.m_Id;
            }
        }
        return {};
    }
    Q_INVOKABLE bool isListed(const QString& id) const
    {
        return !nameOf(id).isEmpty();
    }
    Q_INVOKABLE QString labelFor(const QString& id) const
    {
        const QString name = nameOf(id);
        return name.isEmpty() ? m_Retired.value(id, QStringLiteral("someone")) : name;
    }
    Q_INVOKABLE QString nameOf(const QString& id) const
    {
        for (const Row& row : m_Rows)
        {
            if (row.m_Id == id)
            {
                return row.m_Name;
            }
        }
        return {};
    }
    Q_INVOKABLE QStringList everyoneAt(const QString& location) const
    {
        QStringList out;
        for (const Row& row : m_Rows)
        {
            if (!location.isEmpty() && row.m_Location == location)
            {
                out.append(row.m_Id);
            }
        }
        return out;
    }
    Q_INVOKABLE bool addPerson(const QString& typed, const QString& location)
    {
        const QString name = typed.simplified();
        if (name.isEmpty() || !matchFor(name).isEmpty())
        {
            emit addFailed(QStringLiteral("That name is in the list already."));
            return false;
        }
        const QString id = QString::fromStdString(test::sample_id<PersonId>(100 + m_Rows.size()).format());
        beginInsertRows({}, count(), count());
        m_Rows.push_back({id, name, location});
        endInsertRows();
        emit countChanged();
        emit personAdded(id, name);
        return true;
    }
    Q_INVOKABLE void touch(const QStringList& ids)
    {
        m_Touched = ids;
    }
    Q_INVOKABLE void setCurrentLocation(const QString& location)
    {
        m_CurrentLocation = location;
    }
    Q_INVOKABLE QStringList recentIds(int limit) const
    {
        return m_Recent.mid(0, limit);
    }

    void seed(std::uint64_t number, const QString& name, const QString& location = {})
    {
        const QString id = QString::fromStdString(test::test_id<PersonId>(number).format());
        beginInsertRows({}, count(), count());
        m_Rows.push_back({id, name, location});
        endInsertRows();
        emit countChanged();
    }

    std::vector<Row> m_Rows;
    QHash<QString, QString> m_Retired; // people kept by name who are not listed
    QStringList m_Touched;
    QStringList m_Recent;
    QString m_CurrentLocation;

signals:
    void countChanged();
    void personAdded(const QString& personId, const QString& name);
    void addFailed(const QString& message);
};

/// The list of the two people of the examples.
std::unique_ptr<FakePeopleList> people_list()
{
    auto list = std::make_unique<FakePeopleList>();
    list->seed(test::PERSON_ALEX, QStringLiteral("Alex Sample"));
    list->seed(test::PERSON_JORDAN, QStringLiteral("Jordan Placeholder"));
    return list;
}

InteractionType make_type(std::uint64_t number, std::string name, std::optional<std::string> pattern,
                          InteractionAsks asks)
{
    InteractionType type;
    type.m_Id = test::test_id<InteractionTypeId>(number);
    type.m_Name = std::move(name);
    type.m_LinePattern = std::move(pattern);
    type.m_Asks = asks;
    return type;
}

/// Entries 1 to 4: spoke with Alex; spoke with nobody (skipped); gave a toy with a note; greeted.
std::vector<Entry> interaction_log()
{
    return {test::interaction_entry(1, test::on_day(10, 10), test::INTERACTION_SPOKE_WITH, {test::PERSON_ALEX}),
            test::interaction_entry(2, test::on_day(10, 20), test::INTERACTION_SPOKE_WITH, {}),
            test::interaction_entry(3, test::on_day(10, 25), test::INTERACTION_GAVE, {}, "a toy", "was pleased"),
            test::interaction_entry(4, test::on_day(10, 40), test::INTERACTION_GREETED, {})};
}

Options interaction_options(const PersonNames* people, QObject* people_model)
{
    Options options;
    options.entries = interaction_log();
    options.people = people;
    options.people_model = people_model;
    options.interaction_types = {
        make_type(test::INTERACTION_SPOKE_WITH, "Spoke with", "Spoke with {person}", InteractionAsks::PERSON),
        make_type(test::INTERACTION_GAVE, "Gave", "Gave {value}", InteractionAsks::TEXT),
        make_type(test::INTERACTION_GREETED, "Greeted", std::nullopt, InteractionAsks::NOTHING)};
    return options;
}

/// The Review screen over an interaction log, with the sheet and what a test does in it.
struct Editor : Screen
{
    Editor(const PersonNames* people, QObject* people_model) : Screen(interaction_options(people, people_model)) {}

    void open(std::uint64_t entry_number)
    {
        REQUIRE(QMetaObject::invokeMethod(m_Review, "openEdit", Q_ARG(QString, id_text(entry_number))));
        m_Rig.advance(std::chrono::milliseconds(500));
    }

    [[nodiscard]] QQuickItem* in(const char* name)
    {
        QQuickItem* sheet = named("editSheet");
        REQUIRE(sheet != nullptr);
        return require_in(m_Rig, QString::fromLatin1(name), sheet);
    }

    void tap(const char* name)
    {
        QQuickItem* item = in(name);
        REQUIRE(item != nullptr);
        m_Rig.tap(item);
        m_Rig.advance(std::chrono::milliseconds(500));
    }

    [[nodiscard]] QString page()
    {
        return named("editSheet")->property("page").toString();
    }

    [[nodiscard]] QString preview()
    {
        return in("editPreview")->property("text").toString();
    }

    /// Taps the row named `row_name` in the sheet whose text is `text`.
    void tap_row(const char* row_name, const QString& text)
    {
        for (QQuickItem* row : all_named(m_Rig, QString::fromLatin1(row_name), named("editSheet")))
        {
            if (row->property("text").toString() == text)
            {
                m_Rig.tap(row);
                m_Rig.advance(std::chrono::milliseconds(300));
                return;
            }
        }
        FAIL("no row " << row_name << " with the text " << text.toStdString());
    }

    [[nodiscard]] QQuickItem* picker()
    {
        QQuickItem* found = named("entryPeoplePicker");
        REQUIRE(found != nullptr);
        return found;
    }

    /// Opens the person picker from the People row and ticks `ids` (the ones ticked already are taken off) and Done.
    void pick(const std::vector<std::uint64_t>& toggles)
    {
        tap("editPeople");
        REQUIRE(picker()->property("editing").toBool());
        REQUIRE(picker()->property("isOpen").toBool());
        for (const std::uint64_t number : toggles)
        {
            QMetaObject::invokeMethod(picker(), "toggle",
                                      Q_ARG(QString, QString::fromStdString(test::test_id<PersonId>(number).format())));
        }
        QMetaObject::invokeMethod(picker(), "finish");
        m_Rig.advance(std::chrono::milliseconds(600));
    }

    [[nodiscard]] std::vector<QString> lines()
    {
        return row_lines(m_Rig);
    }
};

}  // namespace

TEST_CASE("review.interaction_rows: wording equals the export at full names; swipe, copy, delete and undo",
          "[ui][review][interaction]")
{
    FixedPeople people;
    auto list = people_list();
    Editor e(&people, list.get());

    const std::vector<QString> expected = {QStringLiteral("10:10 (Interaction) Spoke with Alex Sample"),
                                           QStringLiteral("10:20 (Interaction) Spoke with (person not set)"),
                                           QStringLiteral("10:25 (Interaction) Gave a toy – was pleased"),
                                           QStringLiteral("10:40 (Interaction) Greeted")};
    CHECK(joined(e.lines()) == joined(expected));

    SECTION("Copy puts the line on the clipboard at the sharing level")
    {
        e.tap_action(0, "copyAction");
        CHECK(QString::fromStdString(e.m_World.clipboard().text()) ==
              QStringLiteral("10:10 (Interaction) Spoke with A.S."));
        e.commit();
        CHECK(joined(e.lines()) == joined(expected));
    }

    SECTION("Delete removes the row and Undo brings it back; the deleted one does not count")
    {
        e.tap_action(1, "deleteAction");
        e.commit();
        CHECK(e.lines().size() == 3);
        CHECK(e.m_World.timeline().entry_count() == 3);
        CHECK(e.m_World.undo().text() == QStringLiteral("Entry deleted"));
        REQUIRE(e.m_World.undo().undo());
        e.commit();
        CHECK(joined(e.lines()) == joined(expected));
        CHECK(e.m_World.timeline().entry_count() == 4);
    }
}

TEST_CASE("edit.interaction: the sheet shows an interaction and Save and Cancel are explicit",
          "[ui][edit][interaction]")
{
    FixedPeople people;
    auto list = people_list();
    Editor e(&people, list.get());

    e.open(1);
    CHECK(e.in("editType")->property("text").toString() == QStringLiteral("Interaction"));
    CHECK(e.in("editType")->property("value").toString() == QStringLiteral("Spoke with"));
    CHECK(e.in("editPeople")->property("value").toString() == QStringLiteral("Alex Sample"));
    CHECK(e.preview() == QStringLiteral("10:10 (Interaction) Spoke with Alex Sample"));

    SECTION("a change shows in the preview and Cancel writes nothing")
    {
        e.pick({test::PERSON_JORDAN});
        CHECK(e.preview() == QStringLiteral("10:10 (Interaction) Spoke with Alex Sample and Jordan Placeholder"));
        e.tap("navLeading"); // Cancel
        e.commit();
        CHECK(e.lines()[0] == QStringLiteral("10:10 (Interaction) Spoke with Alex Sample"));
    }

    SECTION("Save writes the change")
    {
        e.pick({test::PERSON_ALEX, test::PERSON_JORDAN}); // Alex off, Jordan on
        e.tap("navTrailing");                             // Save
        e.commit();
        CHECK(e.lines()[0] == QStringLiteral("10:10 (Interaction) Spoke with Jordan Placeholder"));
    }

    SECTION("the type is changed on a page of its own")
    {
        e.tap("editType");
        REQUIRE(e.page() == QStringLiteral("type"));
        e.tap_row("interactionTypeRow", QStringLiteral("Greeted"));
        REQUIRE(e.page() == QStringLiteral("main"));
        CHECK(e.preview() == QStringLiteral("10:10 (Interaction) Greeted"));
        e.tap("navTrailing");
        e.commit();
        CHECK(e.lines()[0] == QStringLiteral("10:10 (Interaction) Greeted"));
    }
}

TEST_CASE("edit.interaction: the people are set on a skipped entry, the answer and the note are edited",
          "[ui][edit][interaction]")
{
    FixedPeople people;
    auto list = people_list();
    Editor e(&people, list.get());

    SECTION("a skipped entry shows (person not set) and takes the people in the order picked")
    {
        e.open(2);
        CHECK(e.in("editPeople")->property("value").toString() == QStringLiteral("(person not set)"));
        e.pick({test::PERSON_JORDAN, test::PERSON_ALEX});
        CHECK(e.in("editPeople")->property("value").toString() == QStringLiteral("Jordan Placeholder, Alex Sample"));
        e.tap("navTrailing");
        e.commit();
        CHECK(e.lines()[1] == QStringLiteral("10:20 (Interaction) Spoke with Jordan Placeholder and Alex Sample"));
    }

    SECTION("the answer and the note")
    {
        e.open(3);
        CHECK(e.in("editAnswer")->property("text").toString() == QStringLiteral("a toy"));
        e.in("editAnswer")->setProperty("text", QStringLiteral("a book"));
        e.in("editNote")->setProperty("text", QString());
        e.m_Rig.advance(std::chrono::milliseconds(50));
        CHECK(e.preview() == QStringLiteral("10:25 (Interaction) Gave a book"));
        e.tap("navTrailing");
        e.commit();
        CHECK(e.lines()[2] == QStringLiteral("10:25 (Interaction) Gave a book"));
    }

    SECTION("a type that asks for nothing has no People row and no answer")
    {
        e.open(4);
        CHECK_FALSE(e.in("editPeople")->isVisible());
        CHECK_FALSE(e.in("editAnswer")->isVisible());
    }
}

#include "interaction_review_test.moc"
