// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/people_journal.hpp"
#include "error_journaling/people_store.hpp"
#include "error_journaling/stored_log.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

/// The workload the kill harness runs (section 4.2 of docs/plan/stage-2.md). The worker process and the driver both
/// build it from `(master seed, iteration)` alone, so the driver can work out what the store must hold after any number
/// of acknowledged operations without asking the worker. All text is synthetic (Sam Example, UNIT, North Site, Wrong
/// Response).
namespace ej::test::kill
{

/// SplitMix64: a small, fixed generator, so the same seed gives the same sequence on every platform and every standard
/// library (the standard distributions do not promise that).
class SplitMix64
{
public:
    explicit SplitMix64(std::uint64_t seed) : m_State(seed) {}

    std::uint64_t next();
    /// A number in [0, bound). `bound` must not be 0.
    std::uint64_t below(std::uint64_t bound);
    /// A number in [low, high].
    std::uint64_t between(std::uint64_t low, std::uint64_t high);

private:
    std::uint64_t m_State;
};

/// The seed of one iteration: a mix of the master seed and the iteration number, so that iteration `i` depends on
/// nothing else and a long run can be split into ranges.
[[nodiscard]] std::uint64_t iteration_seed(std::uint64_t master_seed, std::uint64_t iteration);

enum class OperationKind
{
    CREATE_LOG,
    ADD_ENTRY, // an error entry, a note or a header change
    EDIT_ENTRY,
    SET_DELETED,
    CORRECT_HEADER,
    MARK_SHARED,
    START_NEW_LOG, // archive the open log and create the next: the pair of journal records
    SET_DRAFT,
    // People (Stage 14): written to people.jsonl, not to a log's journal, so the crash points of the database are not
    // reached.
    ADD_PERSON,
    RENAME_PERSON,
    RETIRE_PERSON,
    TOUCH_PERSON,
    FORGET_PERSON,
    SET_PERSON_LOCATION
};

/// A people operation: its own journal, its own crash points (after the append, after the sync).
[[nodiscard]] constexpr bool is_people_operation(OperationKind kind)
{
    return kind == OperationKind::ADD_PERSON || kind == OperationKind::RENAME_PERSON ||
           kind == OperationKind::RETIRE_PERSON || kind == OperationKind::TOUCH_PERSON ||
           kind == OperationKind::FORGET_PERSON || kind == OperationKind::SET_PERSON_LOCATION;
}

struct Operation
{
    OperationKind m_Kind = OperationKind::CREATE_LOG;
    std::uint64_t m_Number = 0; // 1 for the first operation
    LogId m_Log;                // the log the call is about (the old one for START_NEW_LOG)
    LogId m_NewLog;             // START_NEW_LOG only
    HeaderValues m_Header{};    // CREATE_LOG and START_NEW_LOG
    Entry m_Entry;              // ADD_ENTRY and EDIT_ENTRY; SET_DELETED names its entry by m_Entry.m_Id
    bool m_Deleted = false;     // SET_DELETED
    HeaderField m_Field = HeaderField::UNIT_NUMBER; // CORRECT_HEADER
    std::string m_Text;                             // CORRECT_HEADER's value, SET_DRAFT's text, a person's name
    PersonId m_Person;                              // the people operations
    bool m_Retired = true;                          // RETIRE_PERSON: retire, or bring back
    std::optional<LocationId> m_Location;           // SET_PERSON_LOCATION: the location, or none
    UtcMillis m_Time; // what the clock says during the call, and the edit, share or draft time
};

/// A short word for the kind, for the worker's log lines.
[[nodiscard]] const char* operation_name(OperationKind kind);

/// The operations of one iteration, one at a time. Operation 1 creates the first log. After that the kinds come by
/// weight: add an error entry 60%, add a note 15%, edit 8%, soft delete or restore 7%, correct or change a header 4%,
/// mark shared 2%, archive and start a new log 1%, set the draft 3%. Edits, deletes and shares reach the open log three
/// times in four and an archived log otherwise (decision 13).
class Workload
{
public:
    /// `forced_archive_at`: the operation number that is always a START_NEW_LOG, for the crash points that need one
    /// (K6, K9). 0 for none.
    explicit Workload(std::uint64_t seed, std::uint64_t forced_archive_at = 0);

    /// The next operation. It assumes the operation succeeds and moves its own picture of the logs on.
    Operation next();

    [[nodiscard]] std::uint64_t count() const
    {
        return m_Count;
    }

private:
    struct Tracked
    {
        LogId m_Log;
        EntryId m_Id;
        Timestamp m_Captured;
        bool m_IsNote = false;
        bool m_IsHeaderChange = false;
        bool m_Deleted = false;
        bool m_IsInteraction = false;
    };
    struct TrackedPerson
    {
        PersonId m_Id;
        bool m_Retired = false;
        bool m_Forgotten = false;
    };

    Operation make_add(Operation base, bool header_change = false);
    Operation make_edit(Operation base);
    Operation make_delete(Operation base);
    Operation make_correct(Operation base);
    Operation make_share(Operation base);
    Operation make_start_new_log(Operation base);
    Operation make_add_interaction(Operation base);
    Operation make_person_operation(Operation base);
    [[nodiscard]] LogId pick_log();
    [[nodiscard]] HeaderValues header_for_day(std::uint64_t day) const;
    [[nodiscard]] LogId next_log_id();
    [[nodiscard]] EntryId next_entry_id();

    std::uint64_t m_Seed;
    std::uint64_t m_ForcedArchiveAt;
    SplitMix64 m_Rng;
    std::uint64_t m_Count = 0;
    std::uint64_t m_EntryCounter = 0;
    std::uint64_t m_LogCounter = 0;
    std::uint64_t m_HeaderCounter = 0;
    std::uint64_t m_PersonCounter = 0;
    SplitMix64 m_PeopleRng; // its own stream: the people never change the operations of the logs
    std::vector<TrackedPerson> m_People;
    std::vector<LogId> m_Logs; // oldest first; the last is open
    std::vector<Tracked> m_Entries;
};

/// The text of the note field after the `step`th change of the typing workload (mode `draft` of the worker): different
/// for every step, so the driver can tell which change a stored draft is.
[[nodiscard]] std::string draft_text(std::uint64_t seed, std::uint64_t step);

/// How long the worker waits after each operation, in microseconds: steady, with bursts (several operations with no
/// pause). Its own stream, so changing the pace never changes the operations.
class Pacer
{
public:
    Pacer(std::uint64_t seed, std::uint64_t base_us);
    [[nodiscard]] std::uint64_t next_delay_us();

private:
    SplitMix64 m_Rng;
    std::uint64_t m_BaseUs;
    std::uint64_t m_BurstLeft = 0;
};

/// Runs one operation against a repository, setting `clock` to the operation's time first.
[[nodiscard]] Result<void, StorageError> apply_operation(LogRepository& repository, ScriptedClock& clock,
                                                         const Operation& operation);
/// The same with the people store, which the people operations need (without one they fail).
[[nodiscard]] Result<void, StorageError> apply_operation(LogRepository& repository, PeopleStore* people,
                                                         ScriptedClock& clock, const Operation& operation);

/// The people a store holds: each person's name, whether they are retired and their location, and who was forgotten.
/// The time a person was last used is left out: a touch is not synced, so a kill may cost it.
struct PeopleSummary
{
    std::map<PersonId, std::tuple<std::string, bool, std::optional<LocationId>>> m_People;
    std::set<PersonId> m_Forgotten;
    friend bool operator==(const PeopleSummary&, const PeopleSummary&) = default;
};

/// What a store must hold after the first `operations` operations: every log's digest, which log is open, the draft and
/// the people.
struct ModelState
{
    PeopleSummary m_People;
    std::map<LogId, StateDigest> m_Digests;
    std::optional<LogId> m_Open;
    std::optional<std::string> m_Draft;
    std::map<LogId, std::size_t> m_Entries; // total entries, for the failure message
};

/// Applies the first `operations` operations of the workload to the in-memory repository and reads the result back.
[[nodiscard]] ModelState model_after(std::uint64_t seed, std::uint64_t forced_archive_at, std::uint64_t operations);

/// The state of a store read through `repository`, in the same terms.
[[nodiscard]] Result<ModelState, StorageError> state_of(LogRepository& repository, const PeopleStore* people = nullptr);

/// Empty when the two are the same; otherwise a plain line of counts and short ids, never entry text.
[[nodiscard]] std::string difference(const ModelState& expected, const ModelState& actual);

} // namespace ej::test::kill
