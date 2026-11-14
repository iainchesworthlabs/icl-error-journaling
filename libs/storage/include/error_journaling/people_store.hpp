// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/crash_point.hpp"
#include "error_journaling/file_journal.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/journal_file.hpp"
#include "error_journaling/people_journal.hpp"
#include "error_journaling/person.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/storage_location.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace ej
{

/// What the database's `person` table holds.
struct CachedPeople
{
    std::vector<Person> m_People;
    std::set<PersonId> m_Forgotten;
};

/// The `person` table of the database, a cache of the people journal (Stage 14, section 3.4). The journal is the
/// record: a cache that is behind, ahead or empty is made right from it at launch, so the store never fails a write
/// because the cache did.
class PeopleCache
{
public:
    PeopleCache() = default;
    PeopleCache(const PeopleCache&) = delete;
    PeopleCache& operator=(const PeopleCache&) = delete;
    virtual ~PeopleCache() = default;

    /// Inserts the person's row, or replaces it.
    [[nodiscard]] virtual Result<void, StorageError> put_person(const Person& person) = 0;
    /// Blanks the person's name and marks the row forgotten; the id stays. The bytes of the name are overwritten and
    /// the write-ahead log is folded in, so the name does not stay in the file.
    [[nodiscard]] virtual Result<void, StorageError> forget_person(const PersonId& id) = 0;
    /// Makes the table hold exactly these people, in one transaction.
    [[nodiscard]] virtual Result<void, StorageError> replace_people(const std::vector<Person>& people,
                                                                    const std::set<PersonId>& forgotten) = 0;
    [[nodiscard]] virtual Result<CachedPeople, StorageError> read_people() = 0;
    /// The ids of the people that entries refer to, in any log.
    [[nodiscard]] virtual Result<std::set<PersonId>, StorageError> referenced_person_ids() = 0;
};

struct PeopleStoreOptions
{
    /// Called at the crash points of a durable record (after the append, after the sync); null in production.
    CrashHook m_Hook;
    /// Opens `people.jsonl` (and locks it). Empty: `open_journal_file`.
    JournalFileOpener m_Opener;
    /// The `person` table, kept in step after each record. May be null (nothing is cached).
    PeopleCache* m_Cache = nullptr;
};

/// The list of people (the optional name list): `people.jsonl`, a small journal of its own with its own sequence
/// numbers, checksums and exclusive lock, and the same durability rules as a log's journal (Stage 14, sections 3.4
/// and 3.5).
///
/// - A record is appended and made durable before the call returns, except `touch_person`, which is appended and left
/// to
///   the next sync (a lost touch costs only the sort order).
/// - Names live here and in the database's `person` table only. An entry refers to a person by id.
/// - A new person is durable before the entry edit that names them: the caller adds the person first, and acts on the
///   entry only when this call has returned.
/// - Forget removes the name from the journal: the file is written again without that person's earlier records, to a
/// new
///   file that replaces the old by a rename.
///
/// Use from one thread. The file must be clean (`JOURNAL` error otherwise): `Recovery::run` makes it so at launch.
class PeopleStore
{
public:
    using Options = PeopleStoreOptions;

    [[nodiscard]] static Result<std::unique_ptr<PeopleStore>, StorageError>
    open(const StorageLocation& location, const Clock& clock, Options options = {});

    ~PeopleStore();

    /// Adds a person. The name is trimmed and its spaces collapsed (`normalise_person_name`); a name that is empty, too
    /// long, or that matches (case, spacing, accents) a person who is not retired is refused (`INVALID_ARGUMENT`,
    /// `LIMIT_EXCEEDED`, `ALREADY_EXISTS`). The id is the caller's, as a log's is, so that adding twice is refused.
    [[nodiscard]] Result<Person, StorageError> add_person(PersonId id, std::string_view typed_name,
                                                          std::optional<LocationId> location = std::nullopt);
    [[nodiscard]] Result<void, StorageError> rename_person(PersonId id, std::string_view typed_name);
    /// Retires a person (they leave the picker and stay on past entries), or brings them back with `retired = false`.
    [[nodiscard]] Result<void, StorageError> retire_person(PersonId id, bool retired = true);
    /// Marks the person as used now. Not synced.
    [[nodiscard]] Result<void, StorageError> touch_person(PersonId id);
    /// Removes the person's name from the journal and the table. Entries keep the id.
    [[nodiscard]] Result<void, StorageError> forget_person(PersonId id);

    /// The people who were not forgotten, last used first, then by name.
    [[nodiscard]] std::vector<Person> people() const;
    [[nodiscard]] std::optional<Person> find(const PersonId& id) const;
    /// The name of a person who is known and not forgotten.
    [[nodiscard]] std::optional<std::string> name_of(const PersonId& id) const;
    [[nodiscard]] bool is_forgotten(const PersonId& id) const
    {
        return m_State.is_forgotten(id);
    }
    [[nodiscard]] const std::set<PersonId>& forgotten() const
    {
        return m_State.forgotten();
    }
    [[nodiscard]] std::uint64_t last_seq() const
    {
        return m_LastSeq;
    }

private:
    PeopleStore(StorageLocation location, const Clock& clock, Options options);

    [[nodiscard]] Result<void, StorageError> commit(PeopleBody body, bool durable);
    [[nodiscard]] Result<void, StorageError> append_line(const PeopleRecord& record, bool durable);
    [[nodiscard]] Result<void, StorageError> rewrite(const PeopleState& state);
    [[nodiscard]] Result<void, StorageError> reopen();
    void update_cache(const PeopleBody& body);
    [[nodiscard]] Result<std::unique_ptr<JournalFile>, StorageError> open_file() const;

    StorageLocation m_Location;
    const Clock& m_Clock;
    Options m_Options;
    std::unique_ptr<JournalFile> m_File;
    PeopleState m_State;
    std::uint64_t m_LastSeq = 0;
    bool m_CacheStale = false;
};

} // namespace ej
