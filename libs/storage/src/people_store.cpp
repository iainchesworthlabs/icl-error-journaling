// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/people_store.hpp"

#include "protect.hpp"

#include <algorithm>
#include <system_error>

namespace ej
{

namespace
{

using Failure = std::unexpected<StorageError>;

Failure from_name_error(NameError error)
{
    switch (error)
    {
    case NameError::TOO_LONG:
        return Failure(make_error(StorageErrorKind::LIMIT_EXCEEDED, "a person's name is too long"));
    case NameError::EMPTY:
        return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, "a person's name is empty"));
    case NameError::NOT_UTF8:
        break;
    }
    return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, "a person's name is not valid text"));
}

PersonId id_of(const PeopleBody& body)
{
    return std::visit(
        [](const auto& b) -> PersonId
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(b)>, AddPersonBody>)
            {
                return b.m_Person.m_Id;
            }
            else
            {
                return b.m_Id;
            }
        },
        body);
}

} // namespace

PeopleStore::PeopleStore(StorageLocation location, const Clock& clock, Options options)
    : m_Location(std::move(location)), m_Clock(clock), m_Options(std::move(options))
{
    if (!m_Options.m_Opener)
    {
        m_Options.m_Opener = [](const std::filesystem::path& path) { return open_journal_file(path); };
    }
}

PeopleStore::~PeopleStore() = default;

Result<std::unique_ptr<JournalFile>, StorageError> PeopleStore::open_file() const
{
    const std::filesystem::path path = m_Location.people_path();
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    auto file = m_Options.m_Opener(path);
    if (file)
    {
        protect_path(path);
    }
    return file;
}

Result<std::unique_ptr<PeopleStore>, StorageError> PeopleStore::open(const StorageLocation& location,
                                                                     const Clock& clock, Options options)
{
    std::unique_ptr<PeopleStore> store(new PeopleStore(location, clock, std::move(options)));
    auto file = store->open_file();
    if (!file)
    {
        return Failure(file.error());
    }
    auto content = (*file)->read_all();
    if (!content)
    {
        return Failure(content.error());
    }
    PeopleScan scan = scan_people_journal(*content);
    if (!scan.is_clean())
    {
        return Failure(
            make_error(StorageErrorKind::JOURNAL, "the people journal needs recovery before it can be used"));
    }
    store->m_File = std::move(*file);
    store->m_State = std::move(scan.m_State);
    store->m_LastSeq = scan.last_seq();
    return store;
}

// --- reading ---------------------------------------------------------------------------------------------------------

std::vector<Person> PeopleStore::people() const
{
    std::vector<Person> result = m_State.people();
    std::sort(result.begin(), result.end(),
              [](const Person& a, const Person& b)
              {
                  if (a.m_LastUsedUtc != b.m_LastUsedUtc)
                  {
                      if (!a.m_LastUsedUtc || !b.m_LastUsedUtc)
                      {
                          return a.m_LastUsedUtc.has_value();
                      }
                      return *a.m_LastUsedUtc > *b.m_LastUsedUtc;
                  }
                  const std::string key_a = person_name_key(a.m_DisplayName);
                  const std::string key_b = person_name_key(b.m_DisplayName);
                  return key_a != key_b ? key_a < key_b : a.m_Id < b.m_Id;
              });
    return result;
}

std::optional<Person> PeopleStore::find(const PersonId& id) const
{
    const Person* found = m_State.find(id);
    return found == nullptr ? std::nullopt : std::optional<Person>(*found);
}

std::optional<std::string> PeopleStore::name_of(const PersonId& id) const
{
    const Person* found = m_State.find(id);
    return found == nullptr ? std::nullopt : std::optional<std::string>(found->m_DisplayName);
}

// --- writing ---------------------------------------------------------------------------------------------------------

Result<void, StorageError> PeopleStore::append_line(const PeopleRecord& record, bool durable)
{
    if (!m_File)
    {
        return Failure(make_error(StorageErrorKind::JOURNAL, "the people journal is not open"));
    }
    auto line = encode_people_record(record);
    if (!line)
    {
        return Failure(line.error());
    }
    line->push_back('\n');
    const std::uint64_t offset = m_File->size();
    // Whatever goes wrong from here, the file goes back to what it was, so that no half line is left in front of the
    // next record. Best effort: if it fails too, the next launch reads the rest as a torn tail.
    const auto undo = [this, offset](StorageError error)
    {
        if (m_File->truncate(offset))
        {
            (void)m_File->sync();
        }
        return error;
    };
    if (auto appended = m_File->append(*line); !appended)
    {
        return Failure(undo(appended.error()));
    }
    if (!durable)
    {
        return {}; // a touch: the next sync of this file makes it durable
    }
    if (m_Options.m_Hook)
    {
        if (auto hooked = m_Options.m_Hook(CrashPoint::AFTER_JOURNAL_APPEND); !hooked)
        {
            return Failure(undo(hooked.error()));
        }
    }
    if (auto synced = m_File->sync(); !synced)
    {
        return Failure(undo(synced.error()));
    }
    if (m_Options.m_Hook)
    {
        if (auto hooked = m_Options.m_Hook(CrashPoint::AFTER_JOURNAL_SYNC); !hooked)
        {
            return Failure(undo(hooked.error()));
        }
    }
    return {};
}

Result<void, StorageError> PeopleStore::reopen()
{
    auto file = open_file();
    if (!file)
    {
        return Failure(file.error());
    }
    m_File = std::move(*file);
    return {};
}

Result<void, StorageError> PeopleStore::rewrite(const PeopleState& state)
{
    // The lock is held by the open file, and a file that is open cannot be replaced on every system: let go of it,
    // write the new file beside the old and rename it over, then take the lock again. A kill in between leaves the old
    // file or the new one.
    m_File.reset();
    const std::vector<PeopleRecord> records = state.seed_records();
    auto written = write_people_file(m_Location.people_path(), records);
    if (auto reopened = reopen(); !reopened)
    {
        return written ? reopened : written; // the write's own error is the one that matters
    }
    if (!written)
    {
        return written;
    }
    m_LastSeq = records.size();
    return {};
}

void PeopleStore::update_cache(const PeopleBody& body)
{
    PeopleCache* cache = m_Options.m_Cache;
    if (cache == nullptr)
    {
        return;
    }
    // The journal is the record and is durable already: a cache that cannot be written is made right from the journal
    // at the next write or the next launch, and is no failure of this call.
    if (m_CacheStale)
    {
        if (cache->replace_people(m_State.people(), m_State.forgotten()))
        {
            m_CacheStale = false;
        }
        return;
    }
    const PersonId id = id_of(body);
    Result<void, StorageError> done;
    if (std::holds_alternative<ForgetPersonBody>(body))
    {
        done = cache->forget_person(id);
    }
    else if (const Person* person = m_State.find(id))
    {
        done = cache->put_person(*person);
    }
    if (!done)
    {
        m_CacheStale = true;
    }
}

Result<void, StorageError> PeopleStore::commit(PeopleBody body, bool durable)
{
    PeopleState trial = m_State;
    if (auto applied = trial.apply(body); !applied)
    {
        return applied;
    }
    if (std::holds_alternative<ForgetPersonBody>(body))
    {
        if (auto written = rewrite(trial); !written)
        {
            return written;
        }
    }
    else
    {
        PeopleRecord record;
        record.m_Seq = m_LastSeq + 1;
        record.m_Body = body;
        if (auto appended = append_line(record, durable); !appended)
        {
            return appended;
        }
        m_LastSeq = record.m_Seq;
    }
    m_State = std::move(trial);
    update_cache(body);
    return {};
}

Result<Person, StorageError> PeopleStore::add_person(PersonId id, std::string_view typed_name,
                                                     std::optional<LocationId> location)
{
    auto name = normalise_person_name(typed_name);
    if (!name)
    {
        return from_name_error(name.error());
    }
    Person person;
    person.m_Id = id;
    person.m_DisplayName = std::move(*name);
    person.m_Location = location;
    person.m_CreatedUtc = m_Clock.now();
    if (auto added = commit(AddPersonBody{person}, true); !added)
    {
        return Failure(added.error());
    }
    return person;
}

Result<void, StorageError> PeopleStore::rename_person(PersonId id, std::string_view typed_name)
{
    auto name = normalise_person_name(typed_name);
    if (!name)
    {
        return from_name_error(name.error());
    }
    return commit(RenamePersonBody{id, std::move(*name)}, true);
}

Result<void, StorageError> PeopleStore::set_person_location(PersonId id, std::optional<LocationId> location)
{
    const Person* found = m_State.find(id);
    if (found != nullptr && found->m_Location == location)
    {
        return {};
    }
    return commit(SetPersonLocationBody{id, location}, true);
}

Result<void, StorageError> PeopleStore::retire_person(PersonId id, bool retired)
{
    return commit(RetirePersonBody{id, retired}, true);
}

Result<void, StorageError> PeopleStore::touch_person(PersonId id)
{
    return commit(TouchPersonBody{id, m_Clock.now()}, false);
}

Result<void, StorageError> PeopleStore::forget_person(PersonId id)
{
    return commit(ForgetPersonBody{id}, true);
}

} // namespace ej
