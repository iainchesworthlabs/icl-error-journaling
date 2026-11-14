// SPDX-License-Identifier: GPL-3.0-or-later
//
// The people journal's records and the state they fold to (Stage 14, section 3.4; T-I2-4). All names are stand-ins.
#include "error_journaling/people_journal.hpp"
#include "people_names.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

PersonId person(std::uint64_t n)
{
    return sample_id<PersonId>(n);
}

Person make_person(std::uint64_t n, std::string_view name, std::int64_t created = 1)
{
    Person result;
    result.m_Id = person(n);
    result.m_DisplayName = std::string(name);
    result.m_CreatedUtc = sample_time(created);
    return result;
}

PeopleRecord record(std::uint64_t seq, PeopleBody body)
{
    PeopleRecord result;
    result.m_Seq = seq;
    result.m_Body = std::move(body);
    return result;
}

std::string line_of(const PeopleRecord& r)
{
    const auto line = encode_people_record(r);
    REQUIRE(line.has_value());
    return *line;
}

std::string file_of(const std::vector<PeopleRecord>& records)
{
    std::string bytes;
    for (const PeopleRecord& r : records)
    {
        bytes += line_of(r) + "\n";
    }
    return bytes;
}

std::vector<PeopleRecord> three_people()
{
    return {record(1, AddPersonBody{make_person(1, ALEX)}), record(2, AddPersonBody{make_person(2, JORDAN, 2)}),
            record(3, TouchPersonBody{person(1), sample_time(30)})};
}

} // namespace

TEST_CASE("Every people record encodes to one line and decodes to the same record", "[people][journal]")
{
    const std::vector<PeopleRecord> records = {
        record(1, AddPersonBody{make_person(1, ALEX)}),
        record(2, RenamePersonBody{person(1), std::string(PAT)}),
        record(3, RetirePersonBody{person(1), true}),
        record(4, RetirePersonBody{person(1), false}),
        record(5, TouchPersonBody{person(1), sample_time(30)}),
        record(6, ForgetPersonBody{person(1)}),
    };
    Person located = make_person(2, JORDAN);
    located.m_Location = sample_id<LocationId>(9);
    for (const PeopleRecord& original : records)
    {
        const std::string line = line_of(original);
        CHECK(line.find('\n') == std::string::npos);
        const auto decoded = decode_people_record(line);
        REQUIRE(decoded.has_value());
        CHECK(decoded->m_Seq == original.m_Seq);
        CHECK(people_op(decoded->m_Body) == people_op(original.m_Body));
        CHECK(line_of(*decoded) == line);
    }
    const auto with_location = decode_people_record(line_of(record(7, AddPersonBody{located})));
    REQUIRE(with_location.has_value());
    CHECK(std::get<AddPersonBody>(with_location->m_Body).m_Person.m_Location == sample_id<LocationId>(9));
}

TEST_CASE("A people record names its own version, its sequence number and its checksum", "[people][journal]")
{
    const std::string line = line_of(record(4, ForgetPersonBody{person(1)}));
    CHECK(line.find("\"v\":1") != std::string::npos);
    CHECK(line.find("\"seq\":4") != std::string::npos);
    CHECK(line.find("\"op\":\"forget_person\"") != std::string::npos);
    CHECK(line.find("\"crc\":\"") != std::string::npos);
    CHECK(line.find("\"log\"") == std::string::npos); // it belongs to no log
}

TEST_CASE("A damaged people record does not decode", "[people][journal]")
{
    const std::string good = line_of(record(1, AddPersonBody{make_person(1, ALEX)}));
    REQUIRE(decode_people_record(good).has_value());

    std::string changed = good;
    changed.replace(changed.find("Alex"), 4, "Alix");
    CHECK(decode_people_record(changed).error() == PeopleDecodeError::BAD_CRC);
    CHECK(decode_people_record(good.substr(0, good.size() - 3)).error() == PeopleDecodeError::MALFORMED);
    CHECK(decode_people_record("").error() == PeopleDecodeError::MALFORMED);
    CHECK(decode_people_record("not json").error() == PeopleDecodeError::MALFORMED);
    CHECK(decode_people_record("{}").error() == PeopleDecodeError::MALFORMED);
}

TEST_CASE("The state folds the records and refuses what cannot be", "[people][journal]")
{
    PeopleState state;
    REQUIRE(state.apply(AddPersonBody{make_person(1, ALEX)}).has_value());
    REQUIRE(state.apply(AddPersonBody{make_person(2, JORDAN)}).has_value());

    SECTION("an id twice, or a name that matches an active person, is refused")
    {
        CHECK(state.apply(AddPersonBody{make_person(1, PAT)}).error().m_Kind == StorageErrorKind::ALREADY_EXISTS);
        CHECK(state.apply(AddPersonBody{make_person(3, "alex  sample")}).error().m_Kind ==
              StorageErrorKind::INVALID_ARGUMENT); // not written as the rules keep it
        CHECK(state.apply(AddPersonBody{make_person(3, "alex sample")}).error().m_Kind ==
              StorageErrorKind::ALREADY_EXISTS);
        CHECK(state.size() == 2);
    }
    SECTION("a name that is empty or not as the rules keep it is refused")
    {
        CHECK_FALSE(state.apply(AddPersonBody{make_person(3, "")}).has_value());
        CHECK_FALSE(state.apply(AddPersonBody{make_person(3, " Pat Stand-in")}).has_value());
        CHECK_FALSE(
            state.apply(AddPersonBody{make_person(3, std::string(MAX_PERSON_NAME_CODE_POINTS + 1, 'x'))}).has_value());
    }
    SECTION("rename changes the name and refuses another person's")
    {
        REQUIRE(state.apply(RenamePersonBody{person(1), std::string(PAT)}).has_value());
        CHECK(state.find(person(1))->m_DisplayName == PAT);
        CHECK(state.apply(RenamePersonBody{person(1), std::string(JORDAN)}).error().m_Kind ==
              StorageErrorKind::ALREADY_EXISTS);
        CHECK(state.apply(RenamePersonBody{person(9), std::string(SAM)}).error().m_Kind == StorageErrorKind::NOT_FOUND);
        // The same person may change the case of their own name.
        REQUIRE(state.apply(RenamePersonBody{person(2), "JORDAN PLACEHOLDER"}).has_value());
    }
    SECTION("retire leaves the list, frees the name and can be undone")
    {
        REQUIRE(state.apply(RetirePersonBody{person(1), true}).has_value());
        CHECK(state.find(person(1))->m_Retired);
        CHECK(state.find_active_by_name(ALEX) == nullptr);
        REQUIRE(state.apply(AddPersonBody{make_person(3, ALEX)}).has_value()); // the name is free again
        CHECK(state.apply(RetirePersonBody{person(1), false}).error().m_Kind == StorageErrorKind::ALREADY_EXISTS);
        REQUIRE(state.apply(RetirePersonBody{person(3), true}).has_value());
        REQUIRE(state.apply(RetirePersonBody{person(1), false}).has_value());
    }
    SECTION("touch sets the last use")
    {
        REQUIRE(state.apply(TouchPersonBody{person(2), sample_time(77)}).has_value());
        CHECK(state.find(person(2))->m_LastUsedUtc == sample_time(77));
        CHECK_FALSE(state.find(person(1))->m_LastUsedUtc.has_value());
    }
    SECTION("forget keeps the id and nothing else, and a forgotten person cannot be changed")
    {
        REQUIRE(state.apply(ForgetPersonBody{person(1)}).has_value());
        CHECK(state.find(person(1)) == nullptr);
        CHECK(state.is_forgotten(person(1)));
        CHECK(state.knows(person(1)));
        CHECK(state.size() == 1);
        CHECK(state.apply(RenamePersonBody{person(1), std::string(PAT)}).error().m_Kind == StorageErrorKind::NOT_FOUND);
        CHECK(state.apply(AddPersonBody{make_person(1, ALEX)}).error().m_Kind == StorageErrorKind::ALREADY_EXISTS);
        REQUIRE(state.apply(AddPersonBody{make_person(5, ALEX)}).has_value()); // the name may be typed again
        REQUIRE(state.apply(ForgetPersonBody{person(1)}).has_value());         // and forgetting twice is fine
    }
}

TEST_CASE("A clean journal scans to its state", "[people][journal]")
{
    const PeopleScan scan = scan_people_journal(file_of(three_people()));
    CHECK(scan.is_clean());
    CHECK(scan.m_Records.size() == 3);
    CHECK(scan.last_seq() == 3);
    CHECK(scan.m_State.size() == 2);
    CHECK(scan.m_State.find(person(1))->m_LastUsedUtc == sample_time(30));
    CHECK(scan_people_journal("").is_clean());
}

TEST_CASE("A torn tail is cut off and everything before it is kept", "[people][journal]")
{
    const std::string whole = file_of(three_people());
    const std::string torn = whole + line_of(record(4, ForgetPersonBody{person(1)})).substr(0, 25);
    const PeopleScan scan = scan_people_journal(torn);
    CHECK_FALSE(scan.is_clean());
    CHECK(scan.m_Records.size() == 3);
    CHECK(scan.m_TornTailBytes == 25);
    CHECK(scan.m_CompleteBytes == whole.size());
    CHECK(scan.m_BadRecords == 0);
    CHECK(scan.m_State.size() == 2);

    const std::string zeros = whole + std::string(300, '\0');
    CHECK(scan_people_journal(zeros).m_TornTailBytes == 300);
}

TEST_CASE("A gap or a bad record ends the accepted run, and nothing after it is taken", "[people][journal]")
{
    const auto records = three_people();
    SECTION("a missing sequence number")
    {
        const std::string bytes = line_of(records[0]) + "\n" + line_of(records[2]) + "\n";
        const PeopleScan scan = scan_people_journal(bytes);
        CHECK(scan.m_Records.size() == 1);
        CHECK(scan.m_HasProblem);
        CHECK(scan.m_BadRecords == 1);
        CHECK(scan.m_State.size() == 1);
    }
    SECTION("a record with a bad checksum in the middle")
    {
        std::string second = line_of(records[1]);
        second.replace(second.find("Jordan"), 6, "Jorden");
        const std::string bytes = line_of(records[0]) + "\n" + second + "\n" + line_of(records[2]) + "\n";
        const PeopleScan scan = scan_people_journal(bytes);
        CHECK(scan.m_Records.size() == 1);
        CHECK(scan.m_BadRecords == 2); // the damaged one, and the one behind the hole
        CHECK_FALSE(scan.is_clean());
    }
    SECTION("a record the state refuses")
    {
        const std::string bytes =
            line_of(records[0]) + "\n" + line_of(record(2, RenamePersonBody{person(9), std::string(SAM)})) + "\n";
        const PeopleScan scan = scan_people_journal(bytes);
        CHECK(scan.m_Records.size() == 1);
        CHECK(scan.m_HasProblem);
    }
    SECTION("a copy of a record, as the second line")
    {
        const std::string bytes = line_of(records[0]) + "\n" + line_of(records[0]) + "\n";
        CHECK(scan_people_journal(bytes).m_Records.size() == 1);
    }
}

TEST_CASE("Losing a touch costs the sort order and nothing else", "[people][journal]")
{
    auto records = three_people();
    const PeopleScan with = scan_people_journal(file_of(records));
    records.pop_back(); // the touch was never synced
    const PeopleScan without = scan_people_journal(file_of(records));
    REQUIRE(with.m_State.size() == without.m_State.size());
    CHECK(with.m_State.find(person(1))->m_LastUsedUtc.has_value());
    CHECK_FALSE(without.m_State.find(person(1))->m_LastUsedUtc.has_value());
    CHECK(with.m_State.find(person(1))->m_DisplayName == without.m_State.find(person(1))->m_DisplayName);
    CHECK(without.is_clean());
}

TEST_CASE("The seed of a state rebuilds the same state, and holds a forgotten person as an id only",
          "[people][journal]")
{
    PeopleState state;
    REQUIRE(state.apply(AddPersonBody{make_person(1, ALEX, 5)}).has_value());
    REQUIRE(state.apply(AddPersonBody{make_person(2, JORDAN, 3)}).has_value());
    REQUIRE(state.apply(AddPersonBody{make_person(3, PAT, 4)}).has_value());
    REQUIRE(state.apply(RetirePersonBody{person(1), true}).has_value());
    REQUIRE(state.apply(AddPersonBody{make_person(4, ALEX, 6)}).has_value()); // the retired person's name, in use again
    REQUIRE(state.apply(TouchPersonBody{person(2), sample_time(90)}).has_value());
    REQUIRE(state.apply(RenamePersonBody{person(3), std::string(SAM)}).has_value());
    REQUIRE(state.apply(ForgetPersonBody{person(3)}).has_value());

    const std::vector<PeopleRecord> seed = state.seed_records();
    for (std::size_t i = 0; i < seed.size(); ++i)
    {
        CHECK(seed[i].m_Seq == i + 1);
    }
    const std::string bytes = file_of(seed);
    CHECK(bytes.find(PAT) == std::string::npos); // the forgotten person's names are nowhere in it...
    CHECK(bytes.find(SAM) == std::string::npos); // ...neither the name they had nor the one they were renamed to
    const PeopleScan scan = scan_people_journal(bytes);
    REQUIRE(scan.is_clean());
    CHECK(scan.m_State.size() == 3);
    CHECK(scan.m_State.is_forgotten(person(3)));
    CHECK(scan.m_State.find(person(1))->m_Retired);
    CHECK_FALSE(scan.m_State.find(person(4))->m_Retired);
    CHECK(scan.m_State.find(person(4))->m_DisplayName == ALEX);
    CHECK(scan.m_State.find(person(2))->m_LastUsedUtc == sample_time(90));
}
