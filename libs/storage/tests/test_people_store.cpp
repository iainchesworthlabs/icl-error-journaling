// SPDX-License-Identifier: GPL-3.0-or-later
//
// The people store: `people.jsonl`, its cache in the database, Forget and the order of writes (Stage 14, sections 3.4
// and 3.5; T-I2-4, T-I2-5). All names are stand-ins.
#include "error_journaling/people_store.hpp"
#include "lossy_file.hpp"
#include "people_names.hpp"
#include "raw_db.hpp"
#include "recovery_fixture.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>

using namespace ej;
using namespace ej::test;

namespace
{

PersonId person(std::uint64_t n)
{
    return sample_id<PersonId>(n);
}

std::unique_ptr<PeopleStore> open_people(OpenStore& store, PeopleCache* cache = nullptr, CrashHook hook = {},
                                         JournalFileOpener opener = {})
{
    PeopleStore::Options options;
    options.m_Cache = cache;
    options.m_Hook = std::move(hook);
    options.m_Opener = std::move(opener);
    auto opened = PeopleStore::open(store.location(), store.clock(), std::move(options));
    const std::string why = opened ? std::string() : describe(opened.error());
    INFO(why);
    REQUIRE(opened.has_value());
    return std::move(*opened);
}

bool contains(const std::string& bytes, std::string_view name)
{
    return bytes.find(name) != std::string::npos;
}

std::string people_file(const OpenStore& store)
{
    return read_bytes(store.location().people_path());
}

/// A journal file that passes everything on to another, so that a test can look at the other after the store has it.
class Forwarding final : public JournalFile
{
public:
    explicit Forwarding(LossyFile& target) : m_Target(target) {}
    [[nodiscard]] std::uint64_t size() const override
    {
        return m_Target.size();
    }
    Result<std::string, StorageError> read_all() override
    {
        return m_Target.read_all();
    }
    Result<void, StorageError> append(std::string_view bytes) override
    {
        return m_Target.append(bytes);
    }
    Result<void, StorageError> sync() override
    {
        return m_Target.sync();
    }
    Result<void, StorageError> truncate(std::uint64_t length) override
    {
        return m_Target.truncate(length);
    }

private:
    LossyFile& m_Target;
};

/// A cache that can be told to fail, and remembers what it was given.
class FlakyCache final : public PeopleCache
{
public:
    bool m_Fail = false;
    int m_Puts = 0;
    int m_Forgets = 0;
    int m_Replaces = 0;
    std::size_t m_LastReplaceSize = 0;

    Result<void, StorageError> put_person(const Person&) override
    {
        ++m_Puts;
        return result();
    }
    Result<void, StorageError> forget_person(const PersonId&) override
    {
        ++m_Forgets;
        return result();
    }
    Result<void, StorageError> replace_people(const std::vector<Person>& people, const std::set<PersonId>&) override
    {
        ++m_Replaces;
        m_LastReplaceSize = people.size();
        return result();
    }
    Result<CachedPeople, StorageError> read_people() override
    {
        return CachedPeople{};
    }
    Result<std::set<PersonId>, StorageError> referenced_person_ids() override
    {
        return std::set<PersonId>{};
    }

private:
    [[nodiscard]] Result<void, StorageError> result() const
    {
        if (m_Fail)
        {
            return fail(make_error(StorageErrorKind::DATABASE, "the cache failed"));
        }
        return {};
    }
};

} // namespace

TEST_CASE("People are added, renamed, retired, touched and listed, and the journal and the table agree",
          "[people][store]")
{
    TempDir dir;
    OpenStore store(dir.path());
    auto people = open_people(store, &store.repo());

    store.clock().set(sample_time(10));
    const auto alex = people->add_person(person(1), "  Alex   Sample ");
    REQUIRE(alex.has_value());
    CHECK(alex->m_DisplayName == ALEX); // trimmed, spaces collapsed
    CHECK(alex->m_CreatedUtc == sample_time(10));
    store.clock().set(sample_time(11));
    REQUIRE(people->add_person(person(2), JORDAN, sample_id<LocationId>(5)).has_value());
    REQUIRE(people->add_person(person(3), PAT).has_value());

    store.clock().set(sample_time(20));
    REQUIRE(people->touch_person(person(3)).has_value());
    store.clock().set(sample_time(21));
    REQUIRE(people->touch_person(person(2)).has_value());
    REQUIRE(people->rename_person(person(1), SAM).has_value());
    REQUIRE(people->retire_person(person(3)).has_value());

    // Last used first, then by name; the people who never were used come last.
    const auto listed = people->people();
    REQUIRE(listed.size() == 3);
    CHECK(listed[0].m_Id == person(2));
    CHECK(listed[1].m_Id == person(3));
    CHECK(listed[1].m_Retired);
    CHECK(listed[2].m_Id == person(1));
    CHECK(people->name_of(person(1)) == SAM);
    CHECK(people->find(person(2))->m_Location == sample_id<LocationId>(5));
    CHECK(people->last_seq() == 7);

    // The table is the same.
    const auto cached = store.repo().read_people();
    REQUIRE(cached.has_value());
    REQUIRE(cached->m_People.size() == 3);
    CHECK(cached->m_Forgotten.empty());
    for (const Person& row : cached->m_People)
    {
        const auto kept = people->find(row.m_Id);
        REQUIRE(kept.has_value());
        CHECK(row.m_DisplayName == kept->m_DisplayName);
        CHECK(row.m_Retired == kept->m_Retired);
        CHECK(row.m_LastUsedUtc == kept->m_LastUsedUtc);
        CHECK(row.m_Location == kept->m_Location);
        CHECK(row.m_CreatedUtc == kept->m_CreatedUtc);
    }

    // And a new store over the same file reads it all back.
    people.reset();
    auto again = open_people(store);
    CHECK(again->last_seq() == 7);
    CHECK(again->people().size() == 3);
    CHECK(again->name_of(person(1)) == SAM);
}

TEST_CASE("A name that breaks a rule, an id that is used and a change to nobody are refused and write nothing",
          "[people][store]")
{
    TempDir dir;
    OpenStore store(dir.path());
    auto people = open_people(store, &store.repo());
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    const std::string before = people_file(store);

    CHECK(people->add_person(person(2), "   ").error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);
    CHECK(people->add_person(person(2), std::string(MAX_PERSON_NAME_CODE_POINTS + 1, 'x')).error().m_Kind ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(people->add_person(person(2), "alex sample").error().m_Kind == StorageErrorKind::ALREADY_EXISTS);
    CHECK(people->add_person(person(1), JORDAN).error().m_Kind == StorageErrorKind::ALREADY_EXISTS);
    CHECK(people->rename_person(person(9), JORDAN).error().m_Kind == StorageErrorKind::NOT_FOUND);
    CHECK(people->retire_person(person(9)).error().m_Kind == StorageErrorKind::NOT_FOUND);
    CHECK(people->touch_person(person(9)).error().m_Kind == StorageErrorKind::NOT_FOUND);
    REQUIRE(people->add_person(person(2), JORDAN).has_value());
    CHECK(people->rename_person(person(2), "ALEX SAMPLE").error().m_Kind == StorageErrorKind::ALREADY_EXISTS);

    CHECK(people_file(store).starts_with(before));
    CHECK(people->people().size() == 2);
    CHECK(people->last_seq() == 2);
}

TEST_CASE("The people journal is locked: a second store over it is refused", "[people][store]")
{
    TempDir dir;
    OpenStore store(dir.path());
    auto people = open_people(store);
    const auto second = PeopleStore::open(store.location(), store.clock());
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().m_Kind == StorageErrorKind::LOCKED);
}

TEST_CASE("A people journal that needs recovery is not appended to", "[people][store]")
{
    TempDir dir;
    OpenStore store(dir.path());
    {
        auto people = open_people(store);
        REQUIRE(people->add_person(person(1), ALEX).has_value());
    }
    write_bytes(store.location().people_path(), people_file(store) + "{\"v\":1,\"seq\":2"); // a torn tail
    const auto opened = PeopleStore::open(store.location(), store.clock());
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind == StorageErrorKind::JOURNAL);
}

TEST_CASE("A new person is on the disk before the edit that names them, and a failing edit leaves the person",
          "[people][store][order]")
{
    TempDir dir;
    bool fail_commit = false;
    OpenStore store(dir.path(),
                    [&fail_commit](CrashPoint point) -> Result<void, StorageError>
                    {
                        if (fail_commit && point == CrashPoint::BEFORE_DB_COMMIT)
                        {
                            return fail(make_error(StorageErrorKind::DISK_FULL, "no room"));
                        }
                        return {};
                    });
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(1, 10)).has_value()); // logged first
    auto people = open_people(store, &store.repo());

    // The person first: when the call returns the record is in the file, whole and with its newline.
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    const PeopleScan scan = scan_people_journal(people_file(store));
    CHECK(scan.is_clean());
    CHECK(scan.m_State.find(person(1)) != nullptr);

    // Then the entry, which fails at the second write.
    fail_commit = true;
    CHECK_FALSE(
        store.repo().edit_entry(log_id(1), sample_interaction_entry(1, 10, {person(1)}), sample_time(50)).has_value());
    fail_commit = false;
    const auto log = store.repo().load_log(log_id(1)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People.empty());
    CHECK(people->find(person(1)).has_value());
    CHECK(store.repo().read_people()->m_People.size() == 1);

    REQUIRE(
        store.repo().edit_entry(log_id(1), sample_interaction_entry(1, 10, {person(1)}), sample_time(51)).has_value());
    CHECK(
        std::get<InteractionEntry>(store.repo().load_log(log_id(1)).value().m_Log.find(entry_id(1))->m_Body).m_People ==
        std::vector<PersonId>{person(1)});
}

TEST_CASE("A killed add leaves a person with no entries, and one that was acknowledged is never lost",
          "[people][store][order]")
{
    TempDir dir;
    OpenStore store(dir.path());
    LossyFile file;
    auto people = open_people(
        store, nullptr, {}, [&file](const std::filesystem::path&) -> Result<std::unique_ptr<JournalFile>, StorageError>
        { return std::unique_ptr<JournalFile>(new Forwarding(file)); });
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    REQUIRE(people->add_person(person(2), JORDAN).has_value());
    const int synced = file.syncs();
    REQUIRE(people->touch_person(person(1)).has_value());
    CHECK(file.syncs() == synced); // a touch is not synced
    CHECK_FALSE(file.pending().empty());

    // Whatever the power does to the touch, the two people are there, and the file is clean or has a torn tail only.
    std::mt19937_64 rng(7);
    for (int round = 0; round < 50; ++round)
    {
        const PeopleScan scan = scan_people_journal(file.after_power_loss(rng));
        CHECK(scan.m_BadRecords == 0);
        CHECK(scan.m_State.size() == 2);
        CHECK(scan.m_State.find(person(1))->m_DisplayName == ALEX);
        CHECK(scan.m_State.find(person(2))->m_DisplayName == JORDAN);
    }

    // The next durable record makes the touch durable with it.
    REQUIRE(people->retire_person(person(2)).has_value());
    const PeopleScan after = scan_people_journal(file.durable());
    CHECK(after.is_clean());
    CHECK(after.m_State.find(person(1))->m_LastUsedUtc.has_value());
}

TEST_CASE("A failed write of the file cuts the record back and the store goes on", "[people][store]")
{
    TempDir dir;
    OpenStore store(dir.path());
    LossyFile file;
    bool fail_sync = false;
    file.m_Fault = [&fail_sync](LossyFile::Op op) -> std::optional<StorageError>
    {
        if (fail_sync && op == LossyFile::Op::SYNC)
        {
            return make_error(StorageErrorKind::DISK_FULL, "no room");
        }
        return std::nullopt;
    };
    auto people = open_people(
        store, nullptr, {}, [&file](const std::filesystem::path&) -> Result<std::unique_ptr<JournalFile>, StorageError>
        { return std::unique_ptr<JournalFile>(new Forwarding(file)); });
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    fail_sync = true;
    CHECK(people->add_person(person(2), JORDAN).error().m_Kind == StorageErrorKind::DISK_FULL);
    fail_sync = false;
    CHECK(people->people().size() == 1);
    CHECK(scan_people_journal(file.read_all().value()).is_clean());
    REQUIRE(people->add_person(person(2), JORDAN).has_value());
    CHECK(scan_people_journal(file.read_all().value()).m_State.size() == 2);
}

TEST_CASE("A cache that fails does not fail the write, and is made right at the next one", "[people][store]")
{
    TempDir dir;
    OpenStore store(dir.path());
    FlakyCache cache;
    auto people = open_people(store, &cache);
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    CHECK(cache.m_Puts == 1);

    cache.m_Fail = true;
    REQUIRE(people->add_person(person(2), JORDAN).has_value()); // the journal has it; the table does not
    CHECK(cache.m_Puts == 2);
    cache.m_Fail = false;
    REQUIRE(people->add_person(person(3), PAT).has_value());
    CHECK(cache.m_Replaces == 1); // the whole table, from what the store knows
    CHECK(cache.m_LastReplaceSize == 3);
    REQUIRE(people->rename_person(person(3), SAM).has_value());
    CHECK(cache.m_Puts == 3); // and the one row again after that
}

TEST_CASE("Forget removes the name from the journal and the table and keeps the id", "[people][store][forget]")
{
    TempDir dir;
    OpenStore store(dir.path());
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    auto people = open_people(store, &store.repo());
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    REQUIRE(people->add_person(person(2), JORDAN).has_value());
    REQUIRE(people->rename_person(person(1), PAT).has_value()); // the name changes, the old one is in the journal too
    REQUIRE(people->touch_person(person(1)).has_value());
    REQUIRE(people->retire_person(person(2)).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(1, 10, {person(1), person(2)})).has_value());
    REQUIRE(contains(people_file(store), ALEX));
    REQUIRE(contains(people_file(store), PAT));

    REQUIRE(people->forget_person(person(1)).has_value());

    // The file is the old file's replacement: no record of that person's names, one record that holds the id.
    const std::string bytes = people_file(store);
    CHECK_FALSE(contains(bytes, ALEX));
    CHECK_FALSE(contains(bytes, PAT));
    CHECK(contains(bytes, JORDAN));
    const PeopleScan scan = scan_people_journal(bytes);
    CHECK(scan.is_clean());
    CHECK(scan.m_State.is_forgotten(person(1)));
    CHECK(scan.m_State.find(person(2))->m_Retired);
    CHECK(people->last_seq() == scan.last_seq());
    CHECK_FALSE(std::filesystem::exists(store.location().root() / "people.jsonl.tmp"));

    // The store goes on: the next record follows the renumbered file, and the lock is held again.
    CHECK(people->is_forgotten(person(1)));
    CHECK_FALSE(people->find(person(1)).has_value());
    REQUIRE(people->add_person(person(3), ALEX).has_value()); // the name may be typed again, as someone else
    CHECK(PeopleStore::open(store.location(), store.clock()).error().m_Kind == StorageErrorKind::LOCKED);
    people.reset();
    auto reopened = open_people(store, &store.repo());
    CHECK(reopened->is_forgotten(person(1)));
    CHECK(reopened->people().size() == 2);

    // The table: the row stays as an id with no name, and the name is not in the file any more.
    const auto cached = store.repo().read_people().value();
    CHECK(cached.m_Forgotten == std::set<PersonId>{person(1)});
    CHECK(cached.m_People.size() == 2);
    {
        RawDb raw(store.location().database_path());
        CHECK(raw.scalar(QStringLiteral("SELECT display_name FROM person WHERE id = ?"),
                         {QString::fromStdString(person(1).format())})
                  .toString()
                  .isEmpty());
    }
    std::string database = read_bytes(store.location().database_path());
    CHECK_FALSE(contains(database, PAT));
    CHECK_FALSE(contains(read_bytes(store.location().root() / "ej.db-wal"), PAT));
    CHECK_FALSE(contains(database, "Pat Stand"));

    // The entry keeps the id, in its place, whatever happened to the name.
    const auto log = store.repo().load_log(log_id(1)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People ==
          std::vector<PersonId>{person(1), person(2)});
}

TEST_CASE("Forgetting twice is fine, and so is forgetting someone the journal never had", "[people][store][forget]")
{
    TempDir dir;
    OpenStore store(dir.path());
    auto people = open_people(store, &store.repo());
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    REQUIRE(people->forget_person(person(1)).has_value());
    REQUIRE(people->forget_person(person(1)).has_value());
    REQUIRE(people->forget_person(person(7)).has_value());
    CHECK(people->is_forgotten(person(7)));
    CHECK(people->people().empty());
    CHECK(scan_people_journal(people_file(store)).m_State.forgotten().size() == 2);
}

TEST_CASE("A person's location is set, changed and cleared, in the journal and the table", "[people][store][location]")
{
    TempDir dir;
    OpenStore store(dir.path());
    auto people = open_people(store, &store.repo());
    const LocationId north = sample_id<LocationId>(5);
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    REQUIRE(people->add_person(person(2), JORDAN, north).has_value());
    CHECK(people->last_seq() == 2);

    REQUIRE(people->set_person_location(person(1), north).has_value());
    CHECK(people->find(person(1))->m_Location == north);
    REQUIRE(people->set_person_location(person(2), std::nullopt).has_value());
    CHECK_FALSE(people->find(person(2))->m_Location.has_value());
    CHECK(people->last_seq() == 4);

    // The same value writes nothing.
    REQUIRE(people->set_person_location(person(1), north).has_value());
    REQUIRE(people->set_person_location(person(2), std::nullopt).has_value());
    CHECK(people->last_seq() == 4);

    // Nobody, or someone who was forgotten, is refused and writes nothing.
    CHECK(people->set_person_location(person(9), north).error().m_Kind == StorageErrorKind::NOT_FOUND);
    REQUIRE(people->forget_person(person(2)).has_value());
    const std::uint64_t seq = people->last_seq();
    CHECK(people->set_person_location(person(2), north).error().m_Kind == StorageErrorKind::NOT_FOUND);
    CHECK(people->last_seq() == seq);

    const auto cached = store.repo().read_people().value();
    REQUIRE(cached.m_People.size() == 1);
    CHECK(cached.m_People.front().m_Location == north);

    // A new store over the file reads it back.
    people.reset();
    auto again = open_people(store);
    CHECK(again->find(person(1))->m_Location == north);
    CHECK(again->is_forgotten(person(2)));
}

TEST_CASE("Forget leaves no location record that names the person", "[people][store][forget][location]")
{
    TempDir dir;
    OpenStore store(dir.path());
    auto people = open_people(store, &store.repo());
    REQUIRE(people->add_person(person(1), ALEX).has_value());
    REQUIRE(people->add_person(person(2), JORDAN).has_value());
    REQUIRE(people->set_person_location(person(1), sample_id<LocationId>(5)).has_value());
    REQUIRE(people->set_person_location(person(2), sample_id<LocationId>(6)).has_value());
    REQUIRE(contains(people_file(store), person(1).format()));

    REQUIRE(people->forget_person(person(1)).has_value());
    const std::string bytes = people_file(store);
    const PeopleScan scan = scan_people_journal(bytes);
    REQUIRE(scan.is_clean());
    CHECK(scan.m_State.is_forgotten(person(1)));
    CHECK(contains(bytes, "forget_person"));
    CHECK_FALSE(contains(bytes, sample_id<LocationId>(5).format())); // the location of the forgotten person is gone
    CHECK(scan.m_State.find(person(2))->m_Location == sample_id<LocationId>(6));
    {
        RawDb raw(store.location().database_path());
        CHECK(raw.scalar(QStringLiteral("SELECT location_id FROM person WHERE id = ?"),
                         {QString::fromStdString(person(1).format())})
                  .isNull());
    }
}
