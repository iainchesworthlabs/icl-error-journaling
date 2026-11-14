// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The list of people for the picker (Stage 14, package I-6; docs/plan/stage-14.md, 3.4 and 4.4). A GUI-side mirror of
// the people store (`PeopleStore`, which lives on the storage thread), listing the people who are not retired, last
// used first and then by name. The mirror is read once at start and changed only after a job has committed, so the
// screen never shows a name the store does not have.
//
// A new name is durable before anything refers to it: `addPerson` queues the store write and tells `personAdded` only
// after it has committed; the picker then ticks the new person and the entry is edited after that, never before.
//
// Names are shown on the device only. This model is never given to the export.
//
// GUI thread only.

#include "error_journaling/person.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace ej::viewmodel
{

class PeopleModel final : public QAbstractListModel
{
    Q_OBJECT
    /// The list of people could be opened. False: the picker says so and offers nothing to add.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role
    {
        PERSON_ID = Qt::UserRole + 1,
        NAME,
        LOCATION_ID
    };
    // roleNames(): personId, name, locationId

    PeopleModel(const Environment& environment, StorageGateway& gateway, QObject* parent = nullptr);
    ~PeopleModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool available() const
    {
        return m_Available;
    }
    [[nodiscard]] int count() const
    {
        return static_cast<int>(m_People.size());
    }

    /// The inline check of a typed name: empty when it is a valid name, else the reason ("Type a name.", "That name is
    /// too long.").
    Q_INVOKABLE QString validateName(const QString& typed) const;
    /// The id of the person whose name matches `typed` by case, spacing and accents, or empty.
    Q_INVOKABLE QString matchFor(const QString& typed) const;
    /// The name of the person, or empty.
    Q_INVOKABLE QString nameOf(const QString& personId) const;
    /// The people of the location `locationId`, for the Everyone row. Empty for an empty location id.
    Q_INVOKABLE QStringList everyoneAt(const QString& locationId) const;

    /// Adds a person. `locationId` is the location in force, or empty for none. Refused (false, with `addFailed` of a
    /// plain sentence) for a name that is empty, too long or already there. The person is in the list, and
    /// `personAdded(personId, name)` is emitted, once the store has committed it.
    Q_INVOKABLE bool addPerson(const QString& typed, const QString& locationId);

    /// Marks these people as used now (they move to the front). Not waited for: a lost mark costs only the order.
    Q_INVOKABLE void touch(const QStringList& personIds);

public slots:
    /// Reads the list again from the store (after a rename, retire or Forget made elsewhere).
    void reload();

signals:
    void availableChanged();
    void countChanged();
    void personAdded(const QString& personId, const QString& name);
    void addFailed(const QString& message);

private:
    struct Loaded;

    void apply_loaded(bool available, std::vector<Person> people);
    [[nodiscard]] int row_of(const PersonId& id) const;

    Environment m_Environment;
    StorageGateway& m_Gateway;
    std::vector<Person> m_People;
    bool m_Available = false;

    // Completions hold a weak reference to this; a model that has gone is never called back.
    std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
};

} // namespace ej::viewmodel
