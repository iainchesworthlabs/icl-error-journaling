// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/people_admin_model.hpp"

#include "error_journaling/uuid.hpp"

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

/// People in use first, then the retired; each by name.
bool before(const Person& a, const Person& b)
{
    if (a.m_Retired != b.m_Retired)
    {
        return !a.m_Retired;
    }
    return person_name_key(a.m_DisplayName) < person_name_key(b.m_DisplayName);
}

struct Snapshot
{
    bool m_Available = false;
    std::vector<Person> m_People;
};

} // namespace

PeopleAdminModel::PeopleAdminModel(const Environment& environment, StorageGateway& gateway, QObject* parent)
    : QAbstractListModel(parent), m_Environment(environment), m_Gateway(gateway)
{
    reload();
}

PeopleAdminModel::~PeopleAdminModel() = default;

void PeopleAdminModel::reload()
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

int PeopleAdminModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_People.size());
}

QVariant PeopleAdminModel::data(const QModelIndex& index, int role) const
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
    case RETIRED:
        return person.m_Retired;
    case LOCATION_ID:
        return person.m_Location ? text_of(person.m_Location->format()) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> PeopleAdminModel::roleNames() const
{
    return {{PERSON_ID, "personId"}, {NAME, "name"}, {RETIRED, "retired"}, {LOCATION_ID, "locationId"}};
}

void PeopleAdminModel::apply_loaded(bool available, std::vector<Person> people)
{
    std::ranges::sort(people, before);
    const int before_count = count();
    beginResetModel();
    m_People = std::move(people);
    endResetModel();
    if (count() != before_count)
    {
        emit countChanged();
    }
    if (available != m_Available)
    {
        m_Available = available;
        emit availableChanged();
    }
}

int PeopleAdminModel::row_of(const PersonId& id) const
{
    const auto it = std::ranges::find(m_People, id, &Person::m_Id);
    return it == m_People.end() ? -1 : static_cast<int>(it - m_People.begin());
}

QString PeopleAdminModel::validateName(const QString& typed, const QString& personId) const
{
    const auto name = normalise_person_name(typed.toStdString());
    if (!name)
    {
        return name.error() == NameError::TOO_LONG ? tr("That name is too long.") : tr("Type a name.");
    }
    const auto own = parse_person(personId);
    for (const Person& person : m_People)
    {
        if (person.m_Retired || (own && person.m_Id == *own))
        {
            continue;
        }
        if (person_names_match(person.m_DisplayName, *name))
        {
            return tr("That name is in the list already.");
        }
    }
    return {};
}

template <typename Work>
void PeopleAdminModel::commit(Work work)
{
    m_Gateway.run(
        [work = std::move(work)](StorageContext& context) -> Result<void, StorageError>
        {
            if (context.m_People == nullptr)
            {
                return std::unexpected(StorageError{});
            }
            return work(*context.m_People);
        },
        [this, alive = std::weak_ptr<void>(m_Alive)](Result<void, StorageError> result)
        {
            if (alive.expired())
            {
                return;
            }
            if (!result)
            {
                emit failed(tr("That change could not be saved. Please try again."));
            }
            reload();
            if (result)
            {
                emit changed();
            }
        });
}

QString PeopleAdminModel::addPerson(const QString& typed, const QString& locationId)
{
    if (!m_Available)
    {
        return tr("People are not available right now.");
    }
    if (const QString message = validateName(typed); !message.isEmpty())
    {
        return message;
    }
    const PersonId id = m_Environment.m_Ids.next_as<PersonId>();
    const std::optional<LocationId> location = parse_location(locationId);
    const std::string name = typed.toStdString();
    commit(
        [id, name, location](PeopleStore& store) -> Result<void, StorageError>
        {
            auto added = store.add_person(id, name, location);
            if (!added)
            {
                return std::unexpected(added.error());
            }
            return {};
        });
    return {};
}

QString PeopleAdminModel::renamePerson(const QString& personId, const QString& typed)
{
    const auto id = parse_person(personId);
    if (!m_Available || !id || row_of(*id) < 0)
    {
        return tr("People are not available right now.");
    }
    if (const QString message = validateName(typed, personId); !message.isEmpty())
    {
        return message;
    }
    const std::string name = typed.toStdString();
    commit([id = *id, name](PeopleStore& store) { return store.rename_person(id, name); });
    return {};
}

QString PeopleAdminModel::setRetired(const QString& personId, bool retired)
{
    const auto id = parse_person(personId);
    const int row = id ? row_of(*id) : -1;
    if (!m_Available || row < 0)
    {
        return tr("People are not available right now.");
    }
    if (!retired)
    {
        // Bringing someone back must not make two people of the same name.
        const Person& person = m_People[static_cast<std::size_t>(row)];
        if (const QString message = validateName(text_of(person.m_DisplayName), personId); !message.isEmpty())
        {
            return message;
        }
    }
    commit([id = *id, retired](PeopleStore& store) { return store.retire_person(id, retired); });
    return {};
}

QString PeopleAdminModel::forgetPerson(const QString& personId)
{
    const auto id = parse_person(personId);
    if (!m_Available || !id || row_of(*id) < 0)
    {
        return tr("People are not available right now.");
    }
    commit([id = *id](PeopleStore& store) { return store.forget_person(id); });
    return {};
}

} // namespace ej::viewmodel
