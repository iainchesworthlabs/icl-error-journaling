// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/people_model.hpp"

#include "error_journaling/uuid.hpp"

#include <QCoreApplication>
#include <algorithm>
#include <optional>
#include <utility>

namespace ej::viewmodel
{

namespace
{

QString text_of(const std::string& text)
{
    return QString::fromStdString(text);
}

std::optional<PersonId> parse_person(const QString& text)
{
    const auto uuid = Uuid::parse(text.toStdString());
    if (!uuid)
    {
        return std::nullopt;
    }
    return PersonId(*uuid);
}

std::optional<LocationId> parse_location(const QString& text)
{
    const auto uuid = Uuid::parse(text.toStdString());
    if (!uuid)
    {
        return std::nullopt;
    }
    return LocationId(*uuid);
}

/// Last used first (a person never used last), then by name.
bool before(const Person& a, const Person& b)
{
    if (a.m_LastUsedUtc != b.m_LastUsedUtc)
    {
        if (!a.m_LastUsedUtc || !b.m_LastUsedUtc)
        {
            return a.m_LastUsedUtc.has_value();
        }
        return *a.m_LastUsedUtc > *b.m_LastUsedUtc;
    }
    return person_name_key(a.m_DisplayName) < person_name_key(b.m_DisplayName);
}

struct Snapshot
{
    bool m_Available = false;
    std::vector<Person> m_People;
};

} // namespace

PeopleModel::PeopleModel(const Environment& environment, StorageGateway& gateway, QObject* parent)
    : QAbstractListModel(parent), m_Environment(environment), m_Gateway(gateway)
{
    reload();
}

void PeopleModel::reload()
{
    m_Gateway.run(
        [](StorageContext& context)
        {
            Snapshot snapshot;
            if (context.m_People != nullptr)
            {
                snapshot.m_Available = true;
                snapshot.m_People = context.m_People->people();
            }
            return snapshot;
        },
        [this, alive = std::weak_ptr<void>(m_Alive)](Snapshot snapshot)
        {
            if (!alive.expired())
            {
                apply_loaded(snapshot.m_Available, std::move(snapshot.m_People));
            }
        });
}

PeopleModel::~PeopleModel() = default;

int PeopleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_People.size());
}

QVariant PeopleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(m_People.size()))
    {
        return {};
    }
    const Person& person = m_People[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case PERSON_ID:
        return text_of(person.m_Id.format());
    case NAME:
    case Qt::DisplayRole:
        return text_of(person.m_DisplayName);
    case LOCATION_ID:
        return person.m_Location ? text_of(person.m_Location->format()) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> PeopleModel::roleNames() const
{
    return {{PERSON_ID, "personId"}, {NAME, "name"}, {LOCATION_ID, "locationId"}};
}

void PeopleModel::apply_loaded(bool available, std::vector<Person> people)
{
    std::erase_if(people, [](const Person& person) { return person.m_Retired; });
    std::ranges::sort(people, before);
    beginResetModel();
    m_People = std::move(people);
    endResetModel();
    emit countChanged();
    if (available != m_Available)
    {
        m_Available = available;
        emit availableChanged();
    }
}

int PeopleModel::row_of(const PersonId& id) const
{
    const auto it = std::ranges::find(m_People, id, &Person::m_Id);
    return it == m_People.end() ? -1 : static_cast<int>(it - m_People.begin());
}

QString PeopleModel::validateName(const QString& typed) const
{
    const auto name = normalise_person_name(typed.toStdString());
    if (name)
    {
        return {};
    }
    return name.error() == NameError::TOO_LONG ? tr("That name is too long.") : tr("Type a name.");
}

QString PeopleModel::matchFor(const QString& typed) const
{
    const auto name = normalise_person_name(typed.toStdString());
    if (!name)
    {
        return {};
    }
    const auto it = std::ranges::find_if(m_People, [&name](const Person& person)
                                         { return person_names_match(person.m_DisplayName, *name); });
    return it == m_People.end() ? QString() : text_of(it->m_Id.format());
}

QString PeopleModel::nameOf(const QString& personId) const
{
    const auto id = parse_person(personId);
    const int row = id ? row_of(*id) : -1;
    return row < 0 ? QString() : text_of(m_People[static_cast<std::size_t>(row)].m_DisplayName);
}

QStringList PeopleModel::everyoneAt(const QString& locationId) const
{
    QStringList ids;
    const auto location = parse_location(locationId);
    if (!location)
    {
        return ids;
    }
    for (const Person& person : m_People)
    {
        if (person.m_Location && *person.m_Location == *location)
        {
            ids.append(text_of(person.m_Id.format()));
        }
    }
    return ids;
}

bool PeopleModel::addPerson(const QString& typed, const QString& locationId)
{
    if (!m_Available)
    {
        emit addFailed(tr("People are not available right now."));
        return false;
    }
    const auto name = normalise_person_name(typed.toStdString());
    if (!name)
    {
        emit addFailed(validateName(typed));
        return false;
    }
    if (!matchFor(typed).isEmpty())
    {
        emit addFailed(tr("That name is in the list already."));
        return false;
    }
    const PersonId id = m_Environment.m_Ids.next_as<PersonId>();
    const std::optional<LocationId> location = parse_location(locationId);
    m_Gateway.run(
        [id, name = *name, location](StorageContext& context) -> Result<Person, StorageError>
        {
            if (context.m_People == nullptr)
            {
                return std::unexpected(StorageError{});
            }
            return context.m_People->add_person(id, name, location);
        },
        [this, alive = std::weak_ptr<void>(m_Alive)](Result<Person, StorageError> result)
        {
            if (alive.expired())
            {
                return;
            }
            if (!result)
            {
                emit addFailed(tr("That name could not be saved. Please try again."));
                return;
            }
            Person person = std::move(*result);
            const QString id_text = text_of(person.m_Id.format());
            const QString name_text = text_of(person.m_DisplayName);
            const auto at = std::ranges::lower_bound(m_People, person, before);
            const int row = static_cast<int>(at - m_People.begin());
            beginInsertRows({}, row, row);
            m_People.insert(at, std::move(person));
            endInsertRows();
            emit countChanged();
            emit personAdded(id_text, name_text);
        });
    return true;
}

void PeopleModel::touch(const QStringList& personIds)
{
    std::vector<PersonId> ids;
    for (const QString& text : personIds)
    {
        if (const auto id = parse_person(text))
        {
            ids.push_back(*id);
        }
    }
    if (ids.empty() || !m_Available)
    {
        return;
    }
    const UtcMillis now = m_Environment.m_Clock.now();
    // The mirror moves them to the front now; the store keeps the same time on its own clock.
    for (const PersonId& id : ids)
    {
        if (const int row = row_of(id); row >= 0)
        {
            m_People[static_cast<std::size_t>(row)].m_LastUsedUtc = now;
        }
    }
    beginResetModel();
    std::ranges::stable_sort(m_People, before);
    endResetModel();
    m_Gateway.run(
        [ids](StorageContext& context)
        {
            if (context.m_People != nullptr)
            {
                for (const PersonId& id : ids)
                {
                    (void)context.m_People->touch_person(id);
                }
            }
        },
        []() {});
}

} // namespace ej::viewmodel
