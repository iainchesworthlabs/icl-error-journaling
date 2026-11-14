// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"
#include "error_journaling/person.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/timestamp.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ej
{

/// The newest version of a people journal line this build reads. Its own number: the people journal is a different file
/// with a different record set from a log's journal (Stage 14, section 3.4). A line is written with the lowest version
/// that has its record: `set_person_location` is `v:2`, the others stay `v:1`, so a journal that never changed a
/// person's location is still readable by an older build.
inline constexpr std::uint32_t PEOPLE_JOURNAL_VERSION = 2;

enum class PeopleOp
{
    ADD_PERSON,
    RENAME_PERSON,
    RETIRE_PERSON,
    TOUCH_PERSON,
    FORGET_PERSON,
    SET_PERSON_LOCATION
};

struct AddPersonBody
{
    Person m_Person; // the id, the name, the location and the creation time; the last use is a touch of its own
};

struct RenamePersonBody
{
    PersonId m_Id;
    std::string m_Name;
};

struct RetirePersonBody
{
    PersonId m_Id;
    bool m_Retired = true; // false brings the person back
};

struct TouchPersonBody
{
    PersonId m_Id;
    UtcMillis m_Utc;
};

struct ForgetPersonBody
{
    PersonId m_Id;
};

struct SetPersonLocationBody
{
    PersonId m_Id;
    std::optional<LocationId> m_Location; // empty: the person belongs to no location
};

using PeopleBody = std::variant<AddPersonBody, RenamePersonBody, RetirePersonBody, TouchPersonBody, ForgetPersonBody,
                                SetPersonLocationBody>;

/// One line of `people.jsonl`: `{"body":{..},"crc":"xxxxxxxx","op":"..","seq":n,"v":1}`. The checksum covers the object
/// without the `crc` key, as in a log's journal. `m_Version` is what a decoded line said; the encoder writes the
/// version the body needs (`people_record_version`).
struct PeopleRecord
{
    std::uint32_t m_Version = 1;
    std::uint64_t m_Seq = 0; // gapless, starting at 1
    PeopleBody m_Body;
};

[[nodiscard]] PeopleOp people_op(const PeopleBody& body);
[[nodiscard]] std::string_view people_op_name(PeopleOp op);
/// The version a record of this body is written with.
[[nodiscard]] std::uint32_t people_record_version(const PeopleBody& body);
[[nodiscard]] Result<std::string, StorageError> encode_people_record(const PeopleRecord& record);

enum class PeopleDecodeError
{
    MALFORMED,
    BAD_CRC,
    UNSUPPORTED_VERSION,
    BAD_FIELD
};

[[nodiscard]] Result<PeopleRecord, PeopleDecodeError> decode_people_record(std::string_view line);

/// The people a journal describes: the folded state, which the database's `person` table caches. A forgotten person is
/// kept as an id only, so an entry that holds the id is known to be forgotten and not lost.
class PeopleState
{
public:
    /// Applies one record. Refused (and the state left as it was) for an id that is added twice, a name that is not
    /// valid or matches another person who is not retired, or a change to a person who is not there or was forgotten. A
    /// location is not checked against the setup: the store's caller knows the locations.
    [[nodiscard]] Result<void, StorageError> apply(const PeopleBody& body);

    /// The people who were not forgotten, in no particular order.
    [[nodiscard]] std::vector<Person> people() const;
    [[nodiscard]] const Person* find(const PersonId& id) const;
    [[nodiscard]] bool is_forgotten(const PersonId& id) const
    {
        return m_Forgotten.contains(id);
    }
    [[nodiscard]] bool knows(const PersonId& id) const
    {
        return m_People.contains(id) || m_Forgotten.contains(id);
    }
    [[nodiscard]] const std::set<PersonId>& forgotten() const
    {
        return m_Forgotten;
    }
    /// The person who has this name (by the matching rule of `person_names_match`) and is not retired, if any.
    [[nodiscard]] const Person* find_active_by_name(std::string_view name) const;
    [[nodiscard]] std::size_t size() const
    {
        return m_People.size();
    }

    /// The records that rebuild this state, from sequence number 1. The journal is written from these after a Forget:
    /// a forgotten person is one record, and nothing else of them remains.
    [[nodiscard]] std::vector<PeopleRecord> seed_records() const;

private:
    std::map<PersonId, Person> m_People;
    std::set<PersonId> m_Forgotten;
};

/// What a people journal holds, read the way a log's journal is: a record counts only when it ends in a newline and its
/// checksum matches; the accepted records are the longest run from sequence number 1 with no gap and no record that the
/// state refuses.
struct PeopleScan
{
    std::vector<PeopleRecord> m_Records;
    PeopleState m_State;
    std::uint64_t m_FileBytes = 0;
    std::uint64_t m_CompleteBytes = 0; // up to and including the last newline
    std::uint64_t m_TornTailBytes = 0;
    std::size_t m_BadRecords = 0; // complete lines that are not accepted
    bool m_HasProblem = false;    // the accepted run ended before the end of the complete lines

    [[nodiscard]] std::uint64_t last_seq() const
    {
        return m_Records.empty() ? 0 : m_Records.back().m_Seq;
    }
    [[nodiscard]] bool is_clean() const
    {
        return m_TornTailBytes == 0 && !m_HasProblem && m_BadRecords == 0;
    }
};

[[nodiscard]] PeopleScan scan_people_journal(std::string_view bytes);

/// Writes `records` as a new people journal at `path`: to a temporary file beside it, synced, then renamed over `path`,
/// and the folder synced. A kill leaves the old file or the new one, never half of either.
[[nodiscard]] Result<void, StorageError> write_people_file(const std::filesystem::path& path,
                                                           const std::vector<PeopleRecord>& records);

} // namespace ej
