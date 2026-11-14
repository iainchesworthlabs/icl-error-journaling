// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The People screen of Settings (Stage 14, package I-7; docs/plan/stage-14.md, 4.5): every person who was not
// forgotten, retired people included, with add, rename, retire (and bring back) and Forget. It is a GUI-side mirror of
// the people store
// (`PeopleStore`, on the storage thread), like `PeopleModel` for the picker, and it is changed only after a job has
// committed, so the screen never shows what the store does not have. After each commit `changed()` is emitted; the app
// connects it to `PeopleModel::reload` so that the picker follows.
//
// Names are shown on the device only. This model is never given to the export.
//
// GUI thread only.

#include "error_journaling/person.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <QAbstractListModel>
#include <QString>
#include <memory>
#include <vector>

namespace ej::viewmodel
{

class PeopleAdminModel final : public QAbstractListModel
{
    Q_OBJECT
    /// The list of people could be opened. False: the screen says so and offers nothing to change.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role
    {
        PERSON_ID = Qt::UserRole + 1,
        NAME,
        RETIRED,
        LOCATION_ID
    };
    // roleNames(): personId, name, retired, locationId

    PeopleAdminModel(const Environment& environment, StorageGateway& gateway, QObject* parent = nullptr);
    ~PeopleAdminModel() override;

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

    /// Adds a person, with the location `locationId` (empty for none). Returns "" when the change was queued, else the
    /// plain reason it was refused (an empty or too long name, a name that is in the list already). `changed()` follows
    /// the commit.
    Q_INVOKABLE QString addPerson(const QString& typed, const QString& locationId);
    /// Gives the person a new name. The history follows, since an entry names a person by id. Same return as
    /// `addPerson`.
    Q_INVOKABLE QString renamePerson(const QString& personId, const QString& typed);
    /// Moves the person to the location `locationId` (empty for none). The history is not touched: entries name a
    /// person by id. Same return as `addPerson`. Nothing is written when the person already has that location.
    Q_INVOKABLE QString setLocation(const QString& personId, const QString& locationId);
    /// Retires the person (they leave the picker and stay on past entries), or brings them back.
    Q_INVOKABLE QString setRetired(const QString& personId, bool retired);
    /// Removes the person's name from the journal and the list. Entries keep the id and show "someone".
    Q_INVOKABLE QString forgetPerson(const QString& personId);
    /// The inline check of a typed name for the person `personId` (empty: a new person): "" when it is a valid name.
    Q_INVOKABLE QString validateName(const QString& typed, const QString& personId = {}) const;

public slots:
    /// Reads the list again from the store.
    void reload();

signals:
    void availableChanged();
    void countChanged();
    /// A change has been committed and the list is up to date.
    void changed();
    /// A change could not be saved; `message` is a plain sentence.
    void failed(const QString& message);

private:
    void apply_loaded(bool available, std::vector<Person> people);
    [[nodiscard]] int row_of(const PersonId& id) const;
    /// Runs `work` on the store and, when it succeeded, reloads. `work` returns the store's result.
    template <typename Work>
    void commit(Work work);

    Environment m_Environment;
    StorageGateway& m_Gateway;
    std::vector<Person> m_People;
    bool m_Available = false;
    std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
};

} // namespace ej::viewmodel
