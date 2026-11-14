// SPDX-License-Identifier: GPL-3.0-or-later
// People, the People screen of Settings (Stage 14, package I-7): T-I7-2 `settings.people`. The list of people is a
// stand-in in memory with the names and the calls of `PeopleAdminModel`, and the names are the stand-ins of the docs.
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractListModel>
#include <QString>
#include <vector>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

class FakePeopleAdmin : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    struct Row
    {
        QString m_Id;
        QString m_Name;
        bool m_Retired = false;
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
        switch (role)
        {
        case Qt::UserRole + 1:
            return row.m_Id;
        case Qt::UserRole + 2:
            return row.m_Name;
        case Qt::UserRole + 3:
            return row.m_Retired;
        default:
            return row.m_Location;
        }
    }
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "personId"},
                {Qt::UserRole + 2, "name"},
                {Qt::UserRole + 3, "retired"},
                {Qt::UserRole + 4, "locationId"}};
    }

    void seed(const QString& name, const QString& location = {})
    {
        beginInsertRows({}, count(), count());
        m_Rows.push_back({QStringLiteral("p%1").arg(m_Rows.size() + 1), name, false, location});
        endInsertRows();
        emit countChanged();
    }

    Q_INVOKABLE QString validateName(const QString& typed, const QString& id = {}) const
    {
        const QString name = typed.simplified();
        if (name.isEmpty())
        {
            return QStringLiteral("Type a name.");
        }
        for (const Row& row : m_Rows)
        {
            if (row.m_Id != id && !row.m_Retired && row.m_Name.compare(name, Qt::CaseInsensitive) == 0)
            {
                return QStringLiteral("That name is in the list already.");
            }
        }
        return {};
    }
    Q_INVOKABLE QString addPerson(const QString& typed, const QString& location)
    {
        if (const QString message = validateName(typed); !message.isEmpty())
        {
            return message;
        }
        seed(typed.simplified(), location);
        return {};
    }
    Q_INVOKABLE QString renamePerson(const QString& id, const QString& typed)
    {
        if (const QString message = validateName(typed, id); !message.isEmpty())
        {
            return message;
        }
        change(id, [&typed](Row& row) { row.m_Name = typed.simplified(); });
        return {};
    }
    Q_INVOKABLE QString setLocation(const QString& id, const QString& location)
    {
        ++m_LocationCalls;
        change(id, [&location](Row& row) { row.m_Location = location; });
        return {};
    }
    Q_INVOKABLE QString setRetired(const QString& id, bool retired)
    {
        change(id, [retired](Row& row) { row.m_Retired = retired; });
        return {};
    }
    Q_INVOKABLE QString forgetPerson(const QString& id)
    {
        for (std::size_t i = 0; i < m_Rows.size(); ++i)
        {
            if (m_Rows[i].m_Id == id)
            {
                beginRemoveRows({}, static_cast<int>(i), static_cast<int>(i));
                m_Rows.erase(m_Rows.begin() + static_cast<std::ptrdiff_t>(i));
                endRemoveRows();
                emit countChanged();
                m_Forgotten.push_back(id);
                return {};
            }
        }
        return QStringLiteral("not found");
    }

    std::vector<QString> m_Forgotten;
    int m_LocationCalls = 0;

signals:
    void countChanged();
    void failed(const QString& message);

private:
    template <typename Edit>
    void change(const QString& id, Edit edit)
    {
        for (std::size_t i = 0; i < m_Rows.size(); ++i)
        {
            if (m_Rows[i].m_Id == id)
            {
                edit(m_Rows[i]);
                emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)));
                return;
            }
        }
    }

    std::vector<Row> m_Rows;
};

constexpr auto ROWS = "personRow";

QStringList names(World& world)
{
    return row_names(world, ROWS);
}

QVariant as_property(FakePeopleAdmin& people)
{
    return QVariant::fromValue(static_cast<QObject*>(&people));
}

} // namespace

TEST_CASE("settings.people: a name is added, a repeated or empty name is refused, and the list shows it",
          "[ui][editor][people]")
{
    World world;
    FakePeopleAdmin people;
    people.seed(QStringLiteral("Alex Sample"));
    world.load("People", {{QStringLiteral("people"), as_property(people)}});
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("Alex Sample")});

    // An empty name is refused under the field.
    world.m_Ui.tap(item(world, QStringLiteral("addPersonButton")));
    world.settle();
    world.m_Ui.layout();
    CHECK_FALSE(field_error(world, QStringLiteral("newPersonField")).isEmpty());

    // The same name, by case, is refused too.
    enter_text(world, QStringLiteral("newPersonField"), QStringLiteral("alex sample"));
    world.m_Ui.tap(item(world, QStringLiteral("addPersonButton")));
    world.settle();
    world.m_Ui.layout();
    CHECK_FALSE(field_error(world, QStringLiteral("newPersonField")).isEmpty());
    CHECK(names(world).size() == 1);

    enter_text(world, QStringLiteral("newPersonField"), QStringLiteral("Jordan Placeholder"));
    world.m_Ui.tap(item(world, QStringLiteral("addPersonButton")));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("Alex Sample"), QStringLiteral("Jordan Placeholder")});
    CHECK(field_error(world, QStringLiteral("newPersonField")).isEmpty());
}

TEST_CASE("settings.people: a person is renamed in the sheet, and the name is checked as it is typed",
          "[ui][editor][people]")
{
    World world;
    FakePeopleAdmin people;
    people.seed(QStringLiteral("Alex Sample"));
    people.seed(QStringLiteral("Jordan Placeholder"));
    world.load("People", {{QStringLiteral("people"), as_property(people)}});
    world.m_Ui.layout();

    world.m_Ui.tap(label_of(row_with(world, ROWS, QStringLiteral("Alex Sample"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));

    enter_text(world, QStringLiteral("personNameField"), QStringLiteral("jordan placeholder"));
    CHECK_FALSE(field_error(world, QStringLiteral("personNameField")).isEmpty());
    CHECK_FALSE(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());

    enter_text(world, QStringLiteral("personNameField"), QStringLiteral("Alex Example"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world) == QStringList{QStringLiteral("Alex Example"), QStringLiteral("Jordan Placeholder")});
}

TEST_CASE("settings.people: a person's location is chosen in the sheet, changed and cleared", "[ui][editor][people]")
{
    World world;
    FakePeopleAdmin people;
    people.seed(QStringLiteral("Alex Sample"));
    people.seed(QStringLiteral("Jordan Placeholder"));
    world.load("People", {{QStringLiteral("people"), as_property(people)}});
    world.m_Ui.layout();

    const auto open_alex = [&world]
    {
        world.m_Ui.tap(label_of(row_with(world, ROWS, QStringLiteral("Alex Sample"))));
        world.play(400);
        world.m_Ui.layout();
        REQUIRE(sheet_is_open(world));
    };
    const auto alex_detail = [&world]
    {
        QQuickItem* row = row_with(world, ROWS, QStringLiteral("Alex Sample"));
        REQUIRE(row != nullptr);
        return row->property("detail").toString();
    };

    CHECK(alex_detail().isEmpty());
    open_alex();
    const QStringList places = row_names(world, QStringLiteral("personLocation"), panel(world));
    REQUIRE(places.size() >= 2);
    // No location is ticked at first, and the setup's locations follow it.
    REQUIRE(in_sheet(world, QStringLiteral("personNoLocation")) != nullptr);
    CHECK(in_sheet(world, QStringLiteral("personNoLocation"))->property("checked").toBool());

    // Choose the second location and save: the row shows it, and the person was moved once.
    world.m_Ui.tap(row_with(world, QStringLiteral("personLocation"), places[1], panel(world)));
    world.m_Ui.layout();
    CHECK_FALSE(in_sheet(world, QStringLiteral("personNoLocation"))->property("checked").toBool());
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(alex_detail() == places[1]);
    CHECK(people.m_LocationCalls == 1);

    // Opened again, the location is ticked; saving without a change moves nobody.
    open_alex();
    CHECK(row_with(world, QStringLiteral("personLocation"), places[1], panel(world))->property("checked").toBool());
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK(people.m_LocationCalls == 1);

    // Cancel keeps the old location.
    open_alex();
    world.m_Ui.tap(row_with(world, QStringLiteral("personLocation"), places[0], panel(world)));
    world.m_Ui.layout();
    tap_in_sheet(world, QStringLiteral("navLeading"));
    CHECK(alex_detail() == places[1]);
    CHECK(people.m_LocationCalls == 1);

    // No location clears it.
    open_alex();
    world.m_Ui.tap(in_sheet(world, QStringLiteral("personNoLocation")));
    world.m_Ui.layout();
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK(alex_detail().isEmpty());
    CHECK(people.m_LocationCalls == 2);
}

TEST_CASE("settings.people: Retire and Bring back, and Forget asks first", "[ui][editor][people]")
{
    World world;
    FakePeopleAdmin people;
    people.seed(QStringLiteral("Alex Sample"));
    people.seed(QStringLiteral("Jordan Placeholder"));
    world.load("People", {{QStringLiteral("people"), as_property(people)}});
    world.m_Ui.layout();

    // Retire.
    world.m_Ui.tap(label_of(row_with(world, ROWS, QStringLiteral("Alex Sample"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(in_sheet(world, QStringLiteral("personRetire"))->property("text").toString() == QStringLiteral("Retire"));
    tap_in_sheet(world, QStringLiteral("personRetire"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(row_with(world, ROWS, QStringLiteral("Alex Sample"))->property("detail").toString() ==
          QStringLiteral("Retired"));

    // Bring back.
    world.m_Ui.tap(label_of(row_with(world, ROWS, QStringLiteral("Alex Sample"))));
    world.play(400);
    world.m_Ui.layout();
    CHECK(in_sheet(world, QStringLiteral("personRetire"))->property("text").toString() == QStringLiteral("Bring back"));
    tap_in_sheet(world, QStringLiteral("personRetire"));
    CHECK(row_with(world, ROWS, QStringLiteral("Alex Sample"))->property("detail").toString().isEmpty());

    // Forget: the confirmation comes first, and Cancel keeps the person.
    world.m_Ui.tap(label_of(row_with(world, ROWS, QStringLiteral("Jordan Placeholder"))));
    world.play(400);
    world.m_Ui.layout();
    tap_in_sheet(world, QStringLiteral("personForget"));
    world.play(400);
    world.m_Ui.layout();
    QQuickItem* alert = find_item(world.m_Ui.root(), QStringLiteral("forgetAlert"));
    REQUIRE(alert != nullptr);
    CHECK(find_item(alert, QStringLiteral("alertTitle"))->property("text").toString() ==
          QStringLiteral("Forget Jordan Placeholder?"));
    world.m_Ui.tap(find_item(alert, QStringLiteral("alertCancel")));
    world.settle();
    world.play(400);
    CHECK(names(world).size() == 2);
    CHECK(people.m_Forgotten.empty());

    // Forget, confirmed.
    world.m_Ui.tap(label_of(row_with(world, ROWS, QStringLiteral("Jordan Placeholder"))));
    world.play(400);
    world.m_Ui.layout();
    tap_in_sheet(world, QStringLiteral("personForget"));
    world.play(400);
    world.m_Ui.layout();
    world.m_Ui.tap(
        find_item(find_item(world.m_Ui.root(), QStringLiteral("forgetAlert")), QStringLiteral("alertConfirm")));
    world.settle();
    world.play(400);
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("Alex Sample")});
    CHECK(people.m_Forgotten.size() == 1);
}

#include "people_test.moc"
