// SPDX-License-Identifier: GPL-3.0-or-later
#include "kill_workload.hpp"

#include "error_journaling/memory_log_repository.hpp"
#include "storage_samples.hpp"

#include <algorithm>
#include <chrono>

namespace ej::test::kill
{

namespace
{

constexpr std::uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ULL;

std::uint64_t mix(std::uint64_t value)
{
    value += GOLDEN_GAMMA;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

/// A minute of the sample day for operation `number`: not in order, so that the timeline has to sort entries and break
/// ties by sequence number.
std::int64_t minute_of(std::uint64_t number)
{
    return static_cast<std::int64_t>((number * 37) % 1400);
}

std::string short_id(LogId log)
{
    return log.format().substr(0, 8);
}

} // namespace

std::uint64_t SplitMix64::next()
{
    m_State += GOLDEN_GAMMA;
    std::uint64_t z = m_State;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::uint64_t SplitMix64::below(std::uint64_t bound)
{
    return next() % bound;
}

std::uint64_t SplitMix64::between(std::uint64_t low, std::uint64_t high)
{
    return low + below(high - low + 1);
}

std::uint64_t iteration_seed(std::uint64_t master_seed, std::uint64_t iteration)
{
    return mix(master_seed ^ mix(iteration));
}

const char* operation_name(OperationKind kind)
{
    switch (kind)
    {
    case OperationKind::CREATE_LOG:
        return "create_log";
    case OperationKind::ADD_ENTRY:
        return "add_entry";
    case OperationKind::EDIT_ENTRY:
        return "edit_entry";
    case OperationKind::SET_DELETED:
        return "set_deleted";
    case OperationKind::CORRECT_HEADER:
        return "correct_header";
    case OperationKind::MARK_SHARED:
        return "mark_shared";
    case OperationKind::START_NEW_LOG:
        return "start_new_log";
    case OperationKind::SET_DRAFT:
        return "set_draft";
    case OperationKind::ADD_PERSON:
        return "add_person";
    case OperationKind::RENAME_PERSON:
        return "rename_person";
    case OperationKind::RETIRE_PERSON:
        return "retire_person";
    case OperationKind::TOUCH_PERSON:
        return "touch_person";
    case OperationKind::FORGET_PERSON:
        return "forget_person";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------------------------------------------------
// Workload
// ---------------------------------------------------------------------------------------------------------------------

Workload::Workload(std::uint64_t seed, std::uint64_t forced_archive_at)
    : m_Seed(seed), m_ForcedArchiveAt(forced_archive_at), m_Rng(mix(seed ^ 0x4F50ULL)),
      m_PeopleRng(mix(seed ^ 0x5045ULL))
{
}

LogId Workload::next_log_id()
{
    return sample_id<LogId>(m_Seed % 1000003ULL * 1000ULL + ++m_LogCounter);
}

EntryId Workload::next_entry_id()
{
    return sample_id<EntryId>(++m_EntryCounter);
}

HeaderValues Workload::header_for_day(std::uint64_t day) const
{
    HeaderValues header = sample_header();
    header.m_LocalDate = std::chrono::sys_days{std::chrono::year{2026} / 11 / 7} + std::chrono::days(day);
    return header;
}

LogId Workload::pick_log()
{
    if (m_Logs.size() == 1 || m_Rng.below(4) != 0)
    {
        return m_Logs.back();
    }
    return m_Logs[static_cast<std::size_t>(m_Rng.below(m_Logs.size() - 1))];
}

Operation Workload::next()
{
    Operation base;
    base.m_Number = ++m_Count;
    base.m_Time = sample_time(1500 + static_cast<std::int64_t>(base.m_Number));

    if (base.m_Number == 1)
    {
        base.m_Kind = OperationKind::CREATE_LOG;
        base.m_Log = next_log_id();
        base.m_Header = header_for_day(0);
        m_Logs.push_back(base.m_Log);
        return base;
    }
    if (base.m_Number == m_ForcedArchiveAt)
    {
        return make_start_new_log(std::move(base));
    }
    const std::uint64_t roll = m_Rng.below(100);
    // Every ninth operation is about the people (the roll is drawn all the same, so the logs' own stream is not
    // shifted).
    if (base.m_Number % 9 == 0)
    {
        return make_person_operation(std::move(base));
    }
    if (roll < 60)
    {
        // Every fourth of the adds is an interaction, logged with nobody named (log first, name second, Stage 14).
        return base.m_Number % 4 == 3 ? make_add_interaction(std::move(base)) : make_add(std::move(base));
    }
    if (roll < 75)
    {
        base.m_Kind = OperationKind::ADD_ENTRY;
        base.m_Log = m_Logs.back();
        const EntryId id = next_entry_id();
        base.m_Entry = sample_note(0, minute_of(base.m_Number),
                                   "Played a song for Sam Example, take " + std::to_string(base.m_Number));
        base.m_Entry.m_Id = id;
        // A note is captured when it is committed, which is after every draft typed before it: a note replayed from the
        // journal takes out a draft written no later than that (docs/plan/stage-2.md, section 11).
        base.m_Entry.m_Captured.m_Utc = base.m_Time;
        m_Entries.push_back(Tracked{base.m_Log, id, base.m_Entry.m_Captured, true, false, false, false});
        return base;
    }
    if (roll < 83)
    {
        return make_edit(std::move(base));
    }
    if (roll < 90)
    {
        return make_delete(std::move(base));
    }
    if (roll < 94)
    {
        return roll < 92 ? make_correct(std::move(base)) : make_add(std::move(base), true); // a Change is an add
    }
    if (roll < 96)
    {
        return make_share(std::move(base));
    }
    if (roll < 97)
    {
        return make_start_new_log(std::move(base));
    }
    base.m_Kind = OperationKind::SET_DRAFT;
    // One draft in five clears it, as the note field does when it is emptied.
    base.m_Text = m_Rng.below(5) == 0 ? std::string() : "half a note for Sam Example, " + std::to_string(base.m_Number);
    return base;
}

Operation Workload::make_add(Operation base, bool header_change)
{
    base.m_Kind = OperationKind::ADD_ENTRY;
    base.m_Log = m_Logs.back();
    const EntryId id = next_entry_id();
    if (header_change)
    {
        const std::uint64_t n = ++m_HeaderCounter;
        switch (n % 3)
        {
        case 0:
            base.m_Entry =
                sample_change(0, minute_of(base.m_Number), HeaderField::UNIT_NUMBER, std::to_string(100 + n));
            break;
        case 1:
            base.m_Entry =
                sample_change(0, minute_of(base.m_Number), HeaderField::LOCATION, "North Site " + std::to_string(n));
            break;
        default:
            base.m_Entry =
                sample_change(0, minute_of(base.m_Number), HeaderField::SOFTWARE_VERSION, "2.0." + std::to_string(n));
            break;
        }
    }
    else if (base.m_Number % 5 == 0)
    {
        base.m_Entry = sample_error(0, minute_of(base.m_Number), "Wrong Response",
                                    "the robot paused, entry " + std::to_string(base.m_Number));
    }
    else
    {
        base.m_Entry = sample_error(0, minute_of(base.m_Number), "Wrong Response");
    }
    base.m_Entry.m_Id = id;
    m_Entries.push_back(Tracked{base.m_Log, id, base.m_Entry.m_Captured, false, header_change, false, false});
    return base;
}

Operation Workload::make_add_interaction(Operation base)
{
    base.m_Kind = OperationKind::ADD_ENTRY;
    base.m_Log = m_Logs.back();
    const EntryId id = next_entry_id();
    base.m_Entry = sample_interaction_entry(0, minute_of(base.m_Number));
    base.m_Entry.m_Id = id;
    m_Entries.push_back(Tracked{base.m_Log, id, base.m_Entry.m_Captured, false, false, false, true});
    return base;
}

Operation Workload::make_person_operation(Operation base)
{
    std::vector<std::size_t> live;
    for (std::size_t i = 0; i < m_People.size(); ++i)
    {
        if (!m_People[i].m_Forgotten)
        {
            live.push_back(i);
        }
    }
    const std::uint64_t roll = m_PeopleRng.below(100);
    const PersonId fresh = sample_id<PersonId>(m_Seed % 1000003ULL * 1000ULL + m_PersonCounter + 1);
    // The names are all different, so that no add or rename is refused for matching another person's.
    if (live.empty() || (roll < 35 && live.size() < 10))
    {
        base.m_Kind = OperationKind::ADD_PERSON;
        base.m_Person = fresh;
        base.m_Text = "Alex Sample " + std::to_string(++m_PersonCounter);
        m_People.push_back(TrackedPerson{fresh, false, false});
        return base;
    }
    TrackedPerson& target = m_People[live[static_cast<std::size_t>(m_PeopleRng.below(live.size()))]];
    base.m_Person = target.m_Id;
    if (roll < 55)
    {
        base.m_Kind = OperationKind::RENAME_PERSON;
        base.m_Text = "Pat Stand-in " + std::to_string(++m_PersonCounter);
    }
    else if (roll < 80)
    {
        base.m_Kind = OperationKind::TOUCH_PERSON;
    }
    else if (roll < 92)
    {
        base.m_Kind = OperationKind::RETIRE_PERSON;
        target.m_Retired = !target.m_Retired;
        base.m_Retired = target.m_Retired;
    }
    else
    {
        base.m_Kind = OperationKind::FORGET_PERSON;
        target.m_Forgotten = true;
    }
    return base;
}

Operation Workload::make_edit(Operation base)
{
    const LogId log = pick_log();
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < m_Entries.size(); ++i)
    {
        if (m_Entries[i].m_Log == log && !m_Entries[i].m_IsHeaderChange)
        {
            candidates.push_back(i);
        }
    }
    if (candidates.empty())
    {
        return make_add(std::move(base));
    }
    const Tracked& target = m_Entries[candidates[static_cast<std::size_t>(m_Rng.below(candidates.size()))]];
    base.m_Kind = OperationKind::EDIT_ENTRY;
    base.m_Log = log;
    if (target.m_IsInteraction)
    {
        // The name comes second: up to three of the people there are, in an order that depends on the operation, the
        // way the picker sets them. Forgotten people are among them: an entry keeps the id.
        std::vector<PersonId> picked;
        for (std::size_t k = 0; k < m_People.size() && picked.size() < 3; ++k)
        {
            picked.push_back(m_People[(k + base.m_Number) % m_People.size()].m_Id);
        }
        base.m_Entry = sample_interaction_entry(
            0, 0, std::move(picked),
            base.m_Number % 2 == 0 ? std::optional<std::string>("edit " + std::to_string(base.m_Number))
                                   : std::nullopt);
    }
    else if (target.m_IsNote)
    {
        base.m_Entry =
            sample_note(0, 0, "Edited, the robot took the song for Sam Example, edit " + std::to_string(base.m_Number));
    }
    else
    {
        base.m_Entry = sample_error(0, 0, "Wrong Response", "edited at operation " + std::to_string(base.m_Number));
    }
    base.m_Entry.m_Id = target.m_Id;
    base.m_Entry.m_Captured = target.m_Captured;
    base.m_Entry.m_Deleted = target.m_Deleted;
    return base;
}

Operation Workload::make_delete(Operation base)
{
    const LogId log = pick_log();
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < m_Entries.size(); ++i)
    {
        if (m_Entries[i].m_Log == log)
        {
            candidates.push_back(i);
        }
    }
    if (candidates.empty())
    {
        return make_add(std::move(base));
    }
    Tracked& target = m_Entries[candidates[static_cast<std::size_t>(m_Rng.below(candidates.size()))]];
    target.m_Deleted = !target.m_Deleted;
    base.m_Kind = OperationKind::SET_DELETED;
    base.m_Log = log;
    base.m_Entry.m_Id = target.m_Id;
    base.m_Deleted = target.m_Deleted;
    return base;
}

Operation Workload::make_correct(Operation base)
{
    base.m_Kind = OperationKind::CORRECT_HEADER;
    base.m_Log = m_Logs.back();
    const std::uint64_t n = ++m_HeaderCounter;
    switch (n % 3)
    {
    case 0:
        base.m_Field = HeaderField::UNIT_NUMBER;
        base.m_Text = std::to_string(100 + n);
        break;
    case 1:
        base.m_Field = HeaderField::LOCATION;
        base.m_Text = "North Site " + std::to_string(n);
        break;
    default:
        base.m_Field = HeaderField::SOFTWARE_VERSION;
        base.m_Text = "2.0." + std::to_string(n);
        break;
    }
    return base;
}

Operation Workload::make_share(Operation base)
{
    base.m_Kind = OperationKind::MARK_SHARED;
    base.m_Log = pick_log();
    return base;
}

Operation Workload::make_start_new_log(Operation base)
{
    base.m_Kind = OperationKind::START_NEW_LOG;
    base.m_Log = m_Logs.back();
    base.m_NewLog = next_log_id();
    base.m_Header = header_for_day(m_Logs.size());
    m_Logs.push_back(base.m_NewLog);
    return base;
}

std::string draft_text(std::uint64_t seed, std::uint64_t step)
{
    return "typing a note for Sam Example, change " + std::to_string(step) + " of run " + std::to_string(seed % 1000) +
           std::string(static_cast<std::size_t>(step % 37), '.');
}

// ---------------------------------------------------------------------------------------------------------------------
// Pacer
// ---------------------------------------------------------------------------------------------------------------------

Pacer::Pacer(std::uint64_t seed, std::uint64_t base_us) : m_Rng(mix(seed ^ 0x5041ULL)), m_BaseUs(base_us) {}

std::uint64_t Pacer::next_delay_us()
{
    if (m_BurstLeft > 0)
    {
        --m_BurstLeft;
        return 0;
    }
    if (m_Rng.below(10) == 0)
    {
        m_BurstLeft = m_Rng.between(3, 12);
        return 0;
    }
    return m_BaseUs == 0 ? 0 : m_Rng.between(m_BaseUs / 2, m_BaseUs + m_BaseUs / 2);
}

// ---------------------------------------------------------------------------------------------------------------------
// Applying and the model
// ---------------------------------------------------------------------------------------------------------------------

Result<void, StorageError> apply_operation(LogRepository& repository, ScriptedClock& clock, const Operation& operation)
{
    return apply_operation(repository, nullptr, clock, operation);
}

Result<void, StorageError> apply_operation(LogRepository& repository, PeopleStore* people, ScriptedClock& clock,
                                           const Operation& operation)
{
    clock.set(operation.m_Time);
    if (is_people_operation(operation.m_Kind))
    {
        if (people == nullptr)
        {
            return fail(make_error(StorageErrorKind::INTERNAL, "a people operation without a people store"));
        }
        switch (operation.m_Kind)
        {
        case OperationKind::ADD_PERSON:
            return people->add_person(operation.m_Person, operation.m_Text).transform([](const Person&) {});
        case OperationKind::RENAME_PERSON:
            return people->rename_person(operation.m_Person, operation.m_Text);
        case OperationKind::RETIRE_PERSON:
            return people->retire_person(operation.m_Person, operation.m_Retired);
        case OperationKind::TOUCH_PERSON:
            return people->touch_person(operation.m_Person);
        default:
            return people->forget_person(operation.m_Person);
        }
    }
    switch (operation.m_Kind)
    {
    case OperationKind::CREATE_LOG:
        return repository.create_log(operation.m_Log, operation.m_Header);
    case OperationKind::ADD_ENTRY:
        return repository.add_entry(operation.m_Log, operation.m_Entry);
    case OperationKind::EDIT_ENTRY:
        return repository.edit_entry(operation.m_Log, operation.m_Entry, operation.m_Time);
    case OperationKind::SET_DELETED:
        return repository.set_entry_deleted(operation.m_Log, operation.m_Entry.m_Id, operation.m_Deleted);
    case OperationKind::CORRECT_HEADER:
        return repository.correct_header(operation.m_Log, operation.m_Field, operation.m_Text);
    case OperationKind::MARK_SHARED:
        return repository.mark_shared(operation.m_Log, operation.m_Time);
    case OperationKind::START_NEW_LOG:
        return repository.start_new_log(operation.m_Log, operation.m_NewLog, operation.m_Header);
    case OperationKind::SET_DRAFT:
        return repository.set_draft(operation.m_Text, operation.m_Time);
    default:
        break;
    }
    return fail(make_error(StorageErrorKind::INTERNAL, "an operation of a kind the workload does not make"));
}

Result<ModelState, StorageError> state_of(LogRepository& repository, const PeopleStore* people)
{
    ModelState state;
    if (people != nullptr)
    {
        for (const Person& person : people->people())
        {
            state.m_People.m_People.emplace(person.m_Id, std::make_pair(person.m_DisplayName, person.m_Retired));
        }
        state.m_People.m_Forgotten = people->forgotten();
    }
    auto summaries = repository.list_logs();
    if (!summaries)
    {
        return fail(summaries.error());
    }
    for (const LogSummary& summary : *summaries)
    {
        auto stored = repository.load_log(summary.m_Id);
        if (!stored)
        {
            return fail(stored.error());
        }
        state.m_Digests.emplace(summary.m_Id, digest_of(*stored));
        state.m_Entries.emplace(summary.m_Id, summary.m_TotalEntryCount);
    }
    auto open = repository.open_log();
    if (!open)
    {
        return fail(open.error());
    }
    if (*open)
    {
        state.m_Open = (*open)->m_Meta.m_Id;
    }
    auto draft = repository.get_draft();
    if (!draft)
    {
        return fail(draft.error());
    }
    if (*draft)
    {
        state.m_Draft = (*draft)->m_Text;
    }
    return state;
}

ModelState model_after(std::uint64_t seed, std::uint64_t forced_archive_at, std::uint64_t operations)
{
    ScriptedClock clock;
    MemoryLogRepository repository(clock);
    Workload workload(seed, forced_archive_at);
    PeopleState people; // the people, folded by the rules of the journal, with no file
    for (std::uint64_t i = 0; i < operations; ++i)
    {
        const Operation operation = workload.next();
        Result<void, StorageError> applied;
        switch (operation.m_Kind)
        {
        case OperationKind::ADD_PERSON:
        {
            Person person;
            person.m_Id = operation.m_Person;
            person.m_DisplayName = operation.m_Text;
            applied = people.apply(AddPersonBody{person});
            break;
        }
        case OperationKind::RENAME_PERSON:
            applied = people.apply(RenamePersonBody{operation.m_Person, operation.m_Text});
            break;
        case OperationKind::RETIRE_PERSON:
            applied = people.apply(RetirePersonBody{operation.m_Person, operation.m_Retired});
            break;
        case OperationKind::TOUCH_PERSON:
            applied = people.apply(TouchPersonBody{operation.m_Person, operation.m_Time});
            break;
        case OperationKind::FORGET_PERSON:
            applied = people.apply(ForgetPersonBody{operation.m_Person});
            break;
        default:
            applied = apply_operation(repository, clock, operation);
            break;
        }
        if (!applied)
        {
            // The workload only makes valid operations, so this is a bug in the workload or the model. Say so loudly:
            // a state that silently skipped an operation would make the comparison meaningless.
            ModelState broken;
            broken.m_Draft =
                "model failed at operation " + std::to_string(operation.m_Number) + ": " + describe(applied.error());
            return broken;
        }
    }
    auto state = state_of(repository);
    if (!state)
    {
        return ModelState{};
    }
    for (const Person& person : people.people())
    {
        state->m_People.m_People.emplace(person.m_Id, std::make_pair(person.m_DisplayName, person.m_Retired));
    }
    state->m_People.m_Forgotten = people.forgotten();
    return std::move(*state);
}

std::string difference(const ModelState& expected, const ModelState& actual)
{
    std::string out;
    const auto add = [&out](const std::string& line) { out += (out.empty() ? "" : "; ") + line; };
    if (expected.m_Digests.size() != actual.m_Digests.size())
    {
        add("logs " + std::to_string(actual.m_Digests.size()) + " but expected " +
            std::to_string(expected.m_Digests.size()));
    }
    for (const auto& [log, digest] : expected.m_Digests)
    {
        const auto found = actual.m_Digests.find(log);
        if (found == actual.m_Digests.end())
        {
            add("log " + short_id(log) + " is missing");
        }
        else if (!(found->second == digest))
        {
            add("log " + short_id(log) + " has " + std::to_string(found->second.m_Entries) + " entries, expected " +
                std::to_string(digest.m_Entries) +
                (found->second.m_Entries == digest.m_Entries ? " (same count, different content)" : ""));
        }
    }
    if (expected.m_Open != actual.m_Open)
    {
        add("the open log is " + (actual.m_Open ? short_id(*actual.m_Open) : std::string("none")) + ", expected " +
            (expected.m_Open ? short_id(*expected.m_Open) : std::string("none")));
    }
    if (!(expected.m_People == actual.m_People))
    {
        add("the people are " + std::to_string(actual.m_People.m_People.size()) + " and " +
            std::to_string(actual.m_People.m_Forgotten.size()) + " forgotten, expected " +
            std::to_string(expected.m_People.m_People.size()) + " and " +
            std::to_string(expected.m_People.m_Forgotten.size()) + " forgotten");
    }
    if (expected.m_Draft != actual.m_Draft)
    {
        add(std::string("the draft is ") +
            (actual.m_Draft ? std::to_string(actual.m_Draft->size()) + " bytes" : "absent") + ", expected " +
            (expected.m_Draft ? std::to_string(expected.m_Draft->size()) + " bytes" : "absent"));
    }
    return out;
}

} // namespace ej::test::kill
