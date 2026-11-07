// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/recovery.hpp"

#include "error_journaling/journal.hpp"
#include "error_journaling/journal_reader.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "error_journaling/stored_log.hpp"
#include "journal_seed.hpp"
#include "quarantine.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace ej
{

namespace
{

using Failure = std::unexpected<StorageError>;

constexpr std::string_view SQLITE_MAGIC("SQLite format 3\0", 16);
constexpr std::uintmax_t SQLITE_HEADER_SIZE = 100;

StorageError io_error(const char* what, const std::error_code& code)
{
    const bool denied = code == std::errc::permission_denied || code == std::errc::read_only_file_system;
    const bool full = code == std::errc::no_space_on_device;
    return make_error(full     ? StorageErrorKind::DISK_FULL
                      : denied ? StorageErrorKind::PERMISSION
                               : StorageErrorKind::IO,
                      what, code.value());
}

/// One journal file found on disk and what was read from it.
struct JournalInfo
{
    LogId m_Log;
    std::filesystem::path m_Path;
    bool m_Archived = false;  // it is in journal/archive/
    bool m_Foreign = false;   // it holds another log's records, or is set aside: not to be used
    bool m_Scanned = false;   // false: only its tail was checked, and it agrees with the database
    JournalScan m_Scan;
    VoidResolution m_Resolved;  // of the accepted records
};

/// What a journal says its log is, and what has to be done about it.
struct LogPlan
{
    LogId m_Log;
    JournalInfo* m_Journal = nullptr;      // null where the journal is missing or was set aside
    std::optional<StoredLog> m_Folded;     // the log as the journal describes it; nothing when it cannot be built
    std::vector<JournalRecord> m_Records;  // effective records from the create_log on, then the usable ones set aside
    std::uint64_t m_LastSeq = 0;           // of the journal file's accepted run
    std::size_t m_Skipped = 0;             // records that were not usable, for the report
    std::size_t m_Lost = 0;                // records known to be gone that nothing else holds
    bool m_Reseed = false;                 // the journal has to be rewritten from the log
    bool m_FromDatabase = false;           // and the log to write it from is the database's
};

bool starts_with_create(const std::vector<JournalRecord>& records)
{
    return !records.empty() && std::holds_alternative<CreateLogBody>(records.front().m_Body);
}

bool has_archive(const std::vector<JournalRecord>& records)
{
    return std::any_of(records.begin(), records.end(),
                       [](const JournalRecord& record) { return std::holds_alternative<ArchiveBody>(record.m_Body); });
}

class RecoveryRun
{
public:
    RecoveryRun(const StorageLocation& location, const Clock& clock, RecoveryOptions options)
        : m_Location(location), m_Clock(clock), m_Options(std::move(options)), m_Quarantine(location, clock)
    {
    }

    Result<RecoveryReport, StorageError> execute();

private:
    enum class DatabaseState
    {
        FRESH,    // no database and no journal: a new store
        HEALTHY,  // opened, and passed its checks
        DAMAGED   // missing with journals, truncated, not a database, or failing its checks
    };

    Result<DatabaseState, StorageError> classify_database();
    Result<void, StorageError> at_crash_point();

    Result<void, StorageError> find_journals();
    Result<void, StorageError> scan_journal_file(JournalInfo& info);
    [[nodiscard]] bool tail_agrees_with(const JournalInfo& info, std::uint64_t applied_seq) const;
    LogPlan plan_for(JournalInfo& info, std::uint64_t applied_seq) const;
    static void order_by_chain(std::vector<LogPlan*>& plans);
    Result<void, StorageError> retire_orphans(const std::set<LogId>& known_in_database);
    Result<void, StorageError> set_aside(JournalInfo& info);

    Result<void, StorageError> heal();
    Result<void, StorageError> rebuild(const std::string& reason, bool database_existed);
    Result<void, StorageError> finish_journals(std::vector<LogPlan>& plans, SqliteLogRepository* repository);

    void note(std::string text) { m_Report.m_Notes.push_back(std::move(text)); }
    void count(const LogPlan& plan);

    StorageLocation m_Location;
    const Clock& m_Clock;
    RecoveryOptions m_Options;
    Quarantine m_Quarantine;
    RecoveryReport m_Report;
    std::unique_ptr<SqliteLogRepository> m_Repository;
    std::vector<JournalInfo> m_Journals;
    NullJournal m_NullJournal;
    std::size_t m_Lost = 0;
};

Result<void, StorageError> RecoveryRun::at_crash_point()
{
    return m_Options.m_Hook ? m_Options.m_Hook(CrashPoint::MID_RECOVERY) : Result<void, StorageError>{};
}

// ---------------------------------------------------------------------------------------------------------------------
// The journals
// ---------------------------------------------------------------------------------------------------------------------

Result<void, StorageError> RecoveryRun::find_journals()
{
    std::map<LogId, JournalInfo> found;
    for (const bool archived : {false, true})
    {
        const std::filesystem::path folder = archived ? m_Location.archived_journal_dir() : m_Location.journal_dir();
        std::error_code code;
        for (const auto& entry : std::filesystem::directory_iterator(folder, code))
        {
            if (!entry.is_regular_file(code))
            {
                continue;
            }
            if (entry.path().extension() == ".tmp")
            {
                std::filesystem::remove(entry.path(), code);  // a rewrite that was killed: ours, and incomplete
                continue;
            }
            const auto id = Uuid::parse(entry.path().stem().string());
            if (entry.path().extension() != ".jsonl" || !id)
            {
                continue;  // not ours
            }
            JournalInfo info;
            info.m_Log = LogId{*id};
            info.m_Path = entry.path();
            info.m_Archived = archived;
            const auto existing = found.find(info.m_Log);
            if (existing == found.end())
            {
                found.emplace(info.m_Log, std::move(info));
                continue;
            }
            // The same log in both folders (an interrupted move, or a copy made by hand): keep the longer file and set
            // the other aside.
            const std::uintmax_t kept = std::filesystem::file_size(existing->second.m_Path, code);
            const std::uintmax_t other = std::filesystem::file_size(info.m_Path, code);
            const std::filesystem::path loser = other > kept ? existing->second.m_Path : info.m_Path;
            if (other > kept)
            {
                existing->second = std::move(info);
            }
            if (auto moved = m_Quarantine.take(loser); !moved)
            {
                return Failure(moved.error());
            }
            note("a log had a journal in both folders and the shorter was set aside");
        }
    }
    m_Journals.clear();
    for (auto& [id, info] : found)
    {
        m_Journals.push_back(std::move(info));
    }
    return {};
}

Result<void, StorageError> RecoveryRun::scan_journal_file(JournalInfo& info)
{
    auto source = open_file_source(info.m_Path);
    if (!source)
    {
        return Failure(source.error());
    }
    auto scan = scan_journal(**source, info.m_Log);
    if (!scan)
    {
        if (scan.error().m_Kind == StorageErrorKind::JOURNAL)
        {
            info.m_Foreign = true;  // another log's records are in this file (J9)
            info.m_Scanned = true;
            return {};
        }
        return Failure(scan.error());
    }
    info.m_Scan = std::move(*scan);
    info.m_Resolved = resolve_voids(info.m_Scan.m_Records);
    info.m_Scanned = true;
    return {};
}

/// Does the file end in a whole record whose number is the database's? Then nothing in it needs reading: an archived
/// log's journal is checked this way at every launch, which keeps the cost of a launch off the number of days kept.
bool RecoveryRun::tail_agrees_with(const JournalInfo& info, std::uint64_t applied_seq) const
{
    std::ifstream in(info.m_Path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size <= 1)
    {
        return false;
    }
    const std::streamoff window = std::min<std::streamoff>(size, static_cast<std::streamoff>(MAX_RECORD_BYTES) + 2);
    std::string tail(static_cast<std::size_t>(window), '\0');
    in.seekg(size - window);
    in.read(tail.data(), window);
    if (!in || tail.back() != '\n')
    {
        return false;
    }
    tail.pop_back();
    const std::size_t start = tail.rfind('\n');
    if (start == std::string::npos && window < size)
    {
        return false;  // the last line is longer than any record
    }
    const std::string_view last =
        start == std::string::npos ? std::string_view(tail) : std::string_view(tail).substr(start + 1);
    const auto record = decode_record(last);
    return record && record->m_Log == info.m_Log && record->m_Seq == applied_seq &&
           !std::holds_alternative<VoidBody>(record->m_Body);
}

LogPlan RecoveryRun::plan_for(JournalInfo& info, std::uint64_t applied_seq) const
{
    LogPlan plan;
    plan.m_Log = info.m_Log;
    plan.m_Journal = &info;
    if (info.m_Foreign)
    {
        return plan;
    }
    plan.m_LastSeq = info.m_Scan.last_seq();
    plan.m_Records = info.m_Resolved.m_Effective;
    if (!starts_with_create(plan.m_Records))
    {
        plan.m_Records.clear();
        return plan;
    }
    auto folded = make_stored_log(info.m_Log, std::get<CreateLogBody>(plan.m_Records.front().m_Body));
    if (!folded)
    {
        plan.m_Records.clear();
        return plan;
    }
    for (std::size_t i = 1; i < plan.m_Records.size(); ++i)
    {
        if (!apply_record(*folded, plan.m_Records[i], ApplyMode::REPLAY))
        {
            // A record of the accepted run that cannot be applied: counted, and the journal is rewritten without it.
            plan.m_Records.erase(plan.m_Records.begin() + static_cast<std::ptrdiff_t>(i));
            --i;
            ++plan.m_Skipped;
            if (plan.m_Records.empty() || plan.m_Records[std::min(i, plan.m_Records.size() - 1)].m_Seq > applied_seq)
            {
                ++plan.m_Lost;
            }
        }
    }

    // Records after a gap are kept only if each can stand on its own (an entry that is added, a state that is set). One
    // that refers to something missing is counted and skipped: the log is not carried on as if it were whole
    // (section 5.2).
    const VoidResolution aside = resolve_voids(info.m_Scan.m_Aside);
    std::uint64_t expected = plan.m_LastSeq + 1;
    for (const JournalRecord& record : aside.m_Effective)
    {
        if (record.m_Seq > expected && record.m_Seq > applied_seq)
        {
            plan.m_Lost += static_cast<std::size_t>(record.m_Seq - std::max(expected, applied_seq + 1));
        }
        expected = std::max(expected, record.m_Seq + 1);
        if (record.m_Seq <= applied_seq)
        {
            ++plan.m_Skipped;  // the database has it, so nothing is lost, but it is not taken from here
            continue;
        }
        StoredLog trial = *folded;
        if (!std::holds_alternative<CreateLogBody>(record.m_Body) && apply_record(trial, record, ApplyMode::REPLAY))
        {
            *folded = std::move(trial);
            plan.m_Records.push_back(record);
        }
        else
        {
            ++plan.m_Skipped;
            ++plan.m_Lost;
        }
    }
    plan.m_Reseed = info.m_Scan.m_BadRecords > 0 || info.m_Scan.m_HasProblem || info.m_Scan.m_ForeignRecords > 0 ||
                    plan.m_Skipped > 0 || !info.m_Scan.m_Aside.empty();
    plan.m_Folded = std::move(*folded);
    return plan;
}

void RecoveryRun::order_by_chain(std::vector<LogPlan*>& plans)
{
    std::stable_sort(plans.begin(), plans.end(),
                     [](const LogPlan* a, const LogPlan* b)
                     {
                         if (a->m_Folded->m_Meta.m_CreatedUtc != b->m_Folded->m_Meta.m_CreatedUtc)
                         {
                             return a->m_Folded->m_Meta.m_CreatedUtc < b->m_Folded->m_Meta.m_CreatedUtc;
                         }
                         return a->m_Log < b->m_Log;
                     });
    // A log that replaced another comes after it, whatever the clock said.
    for (std::size_t round = 0; round < plans.size(); ++round)
    {
        bool moved = false;
        for (std::size_t i = 0; i < plans.size() && !moved; ++i)
        {
            const auto previous = plans[i]->m_Folded->m_Meta.m_Previous;
            if (!previous)
            {
                continue;
            }
            const auto at =
                std::find_if(plans.begin(), plans.end(), [&](const LogPlan* p) { return p->m_Log == *previous; });
            if (at != plans.end() && static_cast<std::size_t>(at - plans.begin()) > i)
            {
                LogPlan* mover = plans[i];
                const LogPlan* after = *at;
                plans.erase(plans.begin() + static_cast<std::ptrdiff_t>(i));
                plans.insert(std::find(plans.begin(), plans.end(), after) + 1, mover);
                moved = true;
            }
        }
        if (!moved)
        {
            break;
        }
    }
}

/// A journal that cannot be used (it holds another log's records, or is the unfinished start of a new log) is moved to
/// quarantine and treated as absent for the rest of the run.
Result<void, StorageError> RecoveryRun::set_aside(JournalInfo& info)
{
    if (auto moved = m_Quarantine.take(info.m_Path); !moved)
    {
        return Failure(moved.error());
    }
    info.m_Foreign = true;
    info.m_Scanned = true;
    info.m_Scan = JournalScan{};
    info.m_Resolved = VoidResolution{};
    return {};
}

/// A start of a new log that was interrupted before it was acknowledged: the new log's journal exists (it is written
/// first) but the old log's journal has no `archive` record and the database does not have the new log. The old log
/// stays open and the new journal is set aside (section 2.9).
Result<void, StorageError> RecoveryRun::retire_orphans(const std::set<LogId>& known_in_database)
{
    for (JournalInfo& info : m_Journals)
    {
        if (info.m_Archived || !info.m_Scanned || info.m_Foreign || known_in_database.contains(info.m_Log) ||
            !starts_with_create(info.m_Resolved.m_Effective))
        {
            continue;
        }
        const auto& create = std::get<CreateLogBody>(info.m_Resolved.m_Effective.front().m_Body);
        if (!create.m_Previous)
        {
            continue;
        }
        const auto previous = std::find_if(m_Journals.begin(), m_Journals.end(),
                                           [&](const JournalInfo& j) { return j.m_Log == *create.m_Previous; });
        if (previous == m_Journals.end() || previous->m_Archived || !previous->m_Scanned || previous->m_Foreign ||
            has_archive(previous->m_Resolved.m_Effective))
        {
            continue;
        }
        if (auto aside = set_aside(info); !aside)
        {
            return aside;
        }
        note("the start of a new log had not finished, and its journal was set aside");
    }
    return {};
}

void RecoveryRun::count(const LogPlan& plan)
{
    if (plan.m_Journal == nullptr)
    {
        return;
    }
    const JournalScan& scan = plan.m_Journal->m_Scan;
    m_Report.m_DiscardedRecords += scan.m_BadRecords + plan.m_Skipped;
    m_Report.m_DiscardedTailBytes += scan.m_TornTailBytes;
    m_Report.m_VoidedRecords += plan.m_Journal->m_Resolved.m_Voided;
    m_Lost += plan.m_Lost;
}

// ---------------------------------------------------------------------------------------------------------------------
// The database
// ---------------------------------------------------------------------------------------------------------------------

Result<RecoveryRun::DatabaseState, StorageError> RecoveryRun::classify_database()
{
    const std::filesystem::path path = m_Location.database_path();
    std::error_code code;
    const bool has_journals = !m_Journals.empty();
    if (!std::filesystem::exists(path, code))
    {
        return has_journals ? DatabaseState::DAMAGED : DatabaseState::FRESH;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code)
    {
        return Failure(io_error("the database could not be measured", code));
    }
    if (size == 0)
    {
        return has_journals ? DatabaseState::DAMAGED : DatabaseState::FRESH;
    }

    // Can it be read at all? A file that is locked or may not be read is not corrupt, and must not be rebuilt (F7,
    // C13).
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return Failure(make_error(StorageErrorKind::PERMISSION, "the database cannot be read and has been left alone"));
    }
    char header[16] = {};
    in.read(header, sizeof header);
    in.close();
    if (size < SQLITE_HEADER_SIZE || std::string_view(header, sizeof header) != SQLITE_MAGIC)
    {
        return DatabaseState::DAMAGED;
    }

    auto repository = SqliteLogRepository::open(path, m_Clock, m_NullJournal);
    if (!repository)
    {
        if (repository.error().m_Kind == StorageErrorKind::CORRUPT)
        {
            m_Report.m_IntegrityMessage = repository.error().m_Message.substr(0, 200);
            return DatabaseState::DAMAGED;
        }
        // Not ours, newer, locked, unreadable, a full disk: a different error, never a rebuild (C13, C14, C16).
        return Failure(repository.error());
    }
    m_Repository = std::move(*repository);

    // The quick check runs at every launch; the full one after a failed quick check and on the first launch after an
    // upgrade (decision 6).
    auto quick = m_Repository->quick_check();
    if (quick && quick->m_Ok && !m_Options.m_FullCheck)
    {
        m_Report.m_IntegrityMessage = "ok";
        return DatabaseState::HEALTHY;
    }
    if (!quick && quick.error().m_Kind != StorageErrorKind::CORRUPT)
    {
        return Failure(quick.error());
    }
    auto full = m_Repository->integrity_check();
    if (!full && full.error().m_Kind != StorageErrorKind::CORRUPT)
    {
        return Failure(full.error());
    }
    if (quick && quick->m_Ok && full && full->m_Ok)
    {
        m_Report.m_IntegrityMessage = "ok";
        return DatabaseState::HEALTHY;
    }
    const std::string why = full ? full->m_Message : quick ? quick->m_Message : full.error().m_Message;
    m_Report.m_IntegrityMessage = why.substr(0, 200);
    m_Repository.reset();
    return DatabaseState::DAMAGED;
}

// ---------------------------------------------------------------------------------------------------------------------
// Writing the journals right
// ---------------------------------------------------------------------------------------------------------------------

Result<void, StorageError> RecoveryRun::finish_journals(std::vector<LogPlan>& plans, SqliteLogRepository* repository)
{
    for (LogPlan& plan : plans)
    {
        const bool usable_file = plan.m_Journal != nullptr && !plan.m_Journal->m_Foreign;
        if (plan.m_Reseed)
        {
            // Rewritten from the log: from the database when it is healthy, otherwise from what the journal described.
            StoredLog state{LogMeta{}, Log(HeaderValues{})};
            if (repository != nullptr)
            {
                auto loaded = repository->load_log(plan.m_Log);
                if (!loaded)
                {
                    return Failure(loaded.error());
                }
                state = std::move(*loaded);
            }
            else if (plan.m_Folded)
            {
                state = *plan.m_Folded;
            }
            else
            {
                continue;
            }
            if (repository != nullptr)
            {
                // The database first: if the run is killed between the two, the journal is the longer and the next run
                // finds either nothing to do or the journal behind the database and rewrites it again.
                if (auto applied = repository->set_applied_seq(plan.m_Log, seed_records(state).size()); !applied)
                {
                    return applied;
                }
            }
            const std::filesystem::path target =
                usable_file ? plan.m_Journal->m_Path
                            : (state.m_Meta.m_Archived ? m_Location.archived_journal_dir() : m_Location.journal_dir()) /
                                  (plan.m_Log.format() + ".jsonl");
            // A copy of the damaged file first; the file is then replaced by a rename, so there is no moment without
            // one.
            if (auto copied = m_Quarantine.take_copy(target); !copied)
            {
                return Failure(copied.error());
            }
            if (auto written = write_journal_file(target, seed_records(state)); !written)
            {
                return written;
            }
            note("a journal was written again from the log it describes");
        }
        else if (usable_file && plan.m_Journal->m_Scan.m_TornTailBytes > 0)
        {
            if (auto cut = truncate_journal_file(plan.m_Journal->m_Path, plan.m_Journal->m_Scan.m_CompleteBytes); !cut)
            {
                return cut;
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------
// A healthy database
// ---------------------------------------------------------------------------------------------------------------------

Result<void, StorageError> RecoveryRun::heal()
{
    auto applied_result = m_Repository->applied_seqs();
    if (!applied_result)
    {
        return Failure(applied_result.error());
    }
    const std::map<LogId, std::uint64_t> applied = std::move(*applied_result);
    auto open_before = m_Repository->open_log();
    if (!open_before)
    {
        return Failure(open_before.error());
    }
    const std::optional<LogId> open_id =
        *open_before ? std::optional<LogId>((*open_before)->m_Meta.m_Id) : std::nullopt;
    std::set<LogId> in_database;
    for (const auto& [id, seq] : applied)
    {
        in_database.insert(id);
    }

    // Read the journals. The open log's is read in full at every launch, because it is compared with the database. An
    // archived log's journal is only checked at its tail when that agrees with the database, unless a full check was
    // asked.
    for (JournalInfo& info : m_Journals)
    {
        const auto row = applied.find(info.m_Log);
        if (!m_Options.m_FullCheck && info.m_Archived && row != applied.end() && tail_agrees_with(info, row->second))
        {
            continue;
        }
        if (auto scanned = scan_journal_file(info); !scanned)
        {
            return scanned;
        }
    }
    if (auto retired = retire_orphans(in_database); !retired)
    {
        return retired;
    }

    // What each journal says, and what the database lacks. `plans` does not grow after this reserve, so the pointers
    // into it stay good.
    std::vector<LogPlan> plans;
    plans.reserve(m_Journals.size() + applied.size());
    for (JournalInfo& info : m_Journals)
    {
        if (!info.m_Scanned)
        {
            continue;
        }
        const auto row = applied.find(info.m_Log);
        if (info.m_Foreign)
        {
            if (std::filesystem::exists(info.m_Path))
            {
                if (auto aside = set_aside(info); !aside)
                {
                    return aside;
                }
                note("a journal held another log's records and was set aside");
            }
            continue;
        }
        plans.push_back(plan_for(info, row == applied.end() ? 0 : row->second));
    }
    // A log in the database whose journal is gone, or was unusable, is written again from the database.
    for (const auto& [id, seq] : applied)
    {
        const auto plan = std::find_if(plans.begin(), plans.end(), [&](const LogPlan& p) { return p.m_Log == id; });
        if (plan != plans.end() && plan->m_Folded)
        {
            continue;
        }
        const auto info =
            std::find_if(m_Journals.begin(), m_Journals.end(), [&](const JournalInfo& j) { return j.m_Log == id; });
        if (info != m_Journals.end() && !info->m_Scanned)
        {
            continue;  // its tail agreed with the database, so it was not read
        }
        LogPlan reseed;
        reseed.m_Log = id;
        reseed.m_Reseed = true;
        reseed.m_FromDatabase = true;
        if (info != m_Journals.end() && std::filesystem::exists(info->m_Path))
        {
            reseed.m_Journal = &*info;  // present but unusable as a journal of this log: replaced, after a copy
        }
        if (plan != plans.end())
        {
            *plan = reseed;
        }
        else
        {
            plans.push_back(reseed);
        }
        note("a journal was missing or unusable and was written again from the database");
    }

    std::vector<LogPlan*> ordered;
    for (LogPlan& plan : plans)
    {
        if (plan.m_Folded)
        {
            ordered.push_back(&plan);
        }
    }
    order_by_chain(ordered);

    std::vector<JournalRecord> to_replay;
    for (LogPlan* plan : ordered)
    {
        const auto row = applied.find(plan->m_Log);
        const std::uint64_t have = row == applied.end() ? 0 : row->second;
        for (const JournalRecord& record : plan->m_Records)
        {
            if (record.m_Seq > have)
            {
                to_replay.push_back(record);
            }
        }
        count(*plan);
        if (row != applied.end() && plan->m_LastSeq < have)
        {
            plan->m_Reseed = true;  // the journal is behind the database (J5)
            note("a journal was behind the database and was written again from it");
        }
    }
    for (const LogPlan& plan : plans)
    {
        if (!plan.m_Folded)
        {
            m_Lost += plan.m_Lost;
        }
    }

    // One transaction brings the database up to the journals.
    if (!to_replay.empty())
    {
        const auto replayed = m_Repository->replay(to_replay);
        if (!replayed)
        {
            return Failure(replayed.error());
        }
        m_Report.m_ReplayedRecords = replayed->m_Applied;
    }

    // Compare the open log with its journal: a check can pass on wrong data (C15), and the journal is the log of
    // record.
    for (LogPlan* plan : ordered)
    {
        const bool is_open = open_id && *open_id == plan->m_Log;
        const auto row = applied.find(plan->m_Log);
        const bool journal_behind = row != applied.end() && plan->m_LastSeq < row->second;
        if (plan->m_Reseed || journal_behind || !(is_open || m_Options.m_FullCheck))
        {
            continue;
        }
        auto stored = m_Repository->load_log(plan->m_Log);
        if (!stored)
        {
            return Failure(stored.error());
        }
        if (digest_of(*stored) != digest_of(*plan->m_Folded))
        {
            if (auto repaired = m_Repository->replace_log(*plan->m_Folded, plan->m_LastSeq); !repaired)
            {
                return repaired;
            }
            ++m_Report.m_ReplayedRecords;
            note("the database differed from its journal for a log and was repaired from the journal");
        }
    }
    return finish_journals(plans, m_Repository.get());
}

// ---------------------------------------------------------------------------------------------------------------------
// A damaged or missing database
// ---------------------------------------------------------------------------------------------------------------------

Result<void, StorageError> RecoveryRun::rebuild(const std::string& reason, bool database_existed)
{
    note("the database was rebuilt from the journals: " + reason);
    for (JournalInfo& info : m_Journals)
    {
        if (auto scanned = scan_journal_file(info); !scanned)
        {
            return scanned;
        }
    }
    if (auto retired = retire_orphans({}); !retired)
    {
        return retired;
    }

    std::vector<LogPlan> plans;
    plans.reserve(m_Journals.size());
    for (JournalInfo& info : m_Journals)
    {
        if (info.m_Foreign)
        {
            if (std::filesystem::exists(info.m_Path))
            {
                if (auto aside = set_aside(info); !aside)
                {
                    return aside;
                }
                if (info.m_Archived)
                {
                    ++m_Report.m_ArchivedLogsLost;
                }
            }
            continue;
        }
        LogPlan plan = plan_for(info, 0);
        if (!plan.m_Folded)
        {
            if (auto aside = set_aside(info); !aside)
            {
                return aside;
            }
            if (info.m_Archived)
            {
                ++m_Report.m_ArchivedLogsLost;
            }
            continue;
        }
        plans.push_back(std::move(plan));
    }
    std::vector<LogPlan*> ordered;
    for (LogPlan& plan : plans)
    {
        ordered.push_back(&plan);
    }
    order_by_chain(ordered);

    // "Never two open": if several journals describe an open log, the newest stays open and the others are closed.
    std::size_t open_count = 0;
    for (const LogPlan* plan : ordered)
    {
        if (!plan->m_Folded->m_Meta.m_Archived)
        {
            ++open_count;
        }
    }
    for (std::size_t i = 0; i < ordered.size() && open_count > 1; ++i)
    {
        LogPlan* plan = ordered[i];
        if (!plan->m_Folded->m_Meta.m_Archived)
        {
            plan->m_Folded->m_Meta.m_Archived = true;
            plan->m_Reseed = true;
            --open_count;
            note("two logs were open in the journals and the older was closed");
        }
    }

    // The records the new database is built from: the journal's own, or, where the journal has to be rewritten, the
    // records of the rewritten journal, so that the database and the journal count the same.
    std::vector<JournalRecord> records;
    for (LogPlan* plan : ordered)
    {
        count(*plan);
        const auto source = plan->m_Reseed ? seed_records(*plan->m_Folded) : plan->m_Records;
        records.insert(records.end(), source.begin(), source.end());
    }

    // Build the new database beside the old one.
    const std::filesystem::path target = m_Location.database_path();
    std::filesystem::path building = target;
    building += ".rebuild";
    std::error_code code;
    for (const char* suffix : {"", "-wal", "-shm"})
    {
        std::filesystem::path stale = building;
        stale += suffix;
        std::filesystem::remove(stale, code);  // an earlier run's unfinished copy: ours, and only ever a copy
    }
    const std::optional<Draft> draft = database_existed ? SqliteLogRepository::salvage_draft(target) : std::nullopt;
    {
        auto fresh = SqliteLogRepository::open(building, m_Clock, m_NullJournal);
        if (!fresh)
        {
            return Failure(fresh.error());
        }
        // The draft goes in before the journals are replayed: replaying a note takes out a draft that was written no later
        // than the note was captured, because that draft is what the note was made from (a kill between the journal and the
        // commit leaves both), and a draft typed after the note stays.
        const bool draft_salvaged = draft && (*fresh)->set_draft(draft->m_Text, draft->m_UpdatedUtc);
        if (!records.empty())
        {
            const auto replayed = (*fresh)->replay(records);
            if (!replayed)
            {
                return Failure(replayed.error());
            }
            m_Report.m_RebuiltRecords = replayed->m_Applied;
        }
        if (draft_salvaged)
        {
            const auto kept = (*fresh)->get_draft();
            m_Report.m_DraftRecovered = kept && kept->has_value();
        }
        // Check the new file before it replaces anything: it must pass its own checks and agree with the journals.
        const auto quick = (*fresh)->quick_check();
        if (!quick || !quick->m_Ok)
        {
            return Failure(make_error(StorageErrorKind::DATABASE, "the rebuilt database failed its own check"));
        }
        for (const LogPlan* plan : ordered)
        {
            const auto stored = (*fresh)->load_log(plan->m_Log);
            if (!stored || digest_of(*stored) != digest_of(*plan->m_Folded))
            {
                return Failure(
                    make_error(StorageErrorKind::DATABASE, "the rebuilt database does not match the journals"));
            }
        }
        if (auto trimmed = (*fresh)->checkpoint(); !trimmed)
        {
            return Failure(trimmed.error());
        }
    }
    if (auto hooked = at_crash_point(); !hooked)
    {
        return hooked;
    }

    // The journals, then the swap: the damaged files go to quarantine and the new file takes their place by a rename.
    if (auto done = finish_journals(plans, nullptr); !done)
    {
        return done;
    }
    for (const char* suffix : {"", "-wal", "-shm"})
    {
        std::filesystem::path damaged = target;
        damaged += suffix;
        if (auto moved = m_Quarantine.take(damaged); !moved)
        {
            return Failure(moved.error());
        }
    }
    if (auto hooked = at_crash_point(); !hooked)
    {
        return hooked;
    }
    for (const char* suffix : {"-wal", "-shm"})
    {
        std::filesystem::path leftover = building;
        leftover += suffix;
        if (auto moved = m_Quarantine.take(leftover); !moved)
        {
            return Failure(moved.error());
        }
    }
    std::filesystem::rename(building, target, code);
    if (code)
    {
        return Failure(io_error("the rebuilt database could not be moved into place", code));
    }
    return at_crash_point();
}

// ---------------------------------------------------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------------------------------------------------

Result<RecoveryReport, StorageError> RecoveryRun::execute()
{
    if (auto created = m_Location.create_directories(); !created)
    {
        return Failure(created.error());
    }
    if (auto found = find_journals(); !found)
    {
        return Failure(found.error());
    }
    const auto state = classify_database();
    if (!state)
    {
        return Failure(state.error());
    }

    const bool database_existed = std::filesystem::exists(m_Location.database_path());
    switch (*state)
    {
    case DatabaseState::FRESH:
    {
        auto fresh = SqliteLogRepository::open(m_Location.database_path(), m_Clock, m_NullJournal);
        if (!fresh)
        {
            return Failure(fresh.error());
        }
        m_Repository = std::move(*fresh);
        m_Report.m_IntegrityMessage = "ok";
        break;
    }
    case DatabaseState::HEALTHY:
        if (auto healed = heal(); !healed)
        {
            return Failure(healed.error());
        }
        break;
    case DatabaseState::DAMAGED:
    {
        m_Repository.reset();
        const std::string reason =
            m_Report.m_IntegrityMessage.empty() ? "it could not be used" : m_Report.m_IntegrityMessage;
        if (auto rebuilt = rebuild(reason, database_existed); !rebuilt)
        {
            return Failure(rebuilt.error());
        }
        auto reopened = SqliteLogRepository::open(m_Location.database_path(), m_Clock, m_NullJournal);
        if (!reopened)
        {
            return Failure(reopened.error());
        }
        m_Repository = std::move(*reopened);
        break;
    }
    }

    // The summary.
    const auto open = m_Repository->open_log();
    if (!open)
    {
        return Failure(open.error());
    }
    m_Report.m_OpenLogEntries = *open ? (*open)->m_Log.entries().size() : 0;
    m_Report.m_QuarantinedPaths = m_Quarantine.taken();

    const bool rebuilt = *state == DatabaseState::DAMAGED;
    if (rebuilt && m_Report.m_RebuiltRecords == 0)
    {
        m_Report.m_Outcome = RecoveryOutcome::UNRECOVERABLE;
        note("nothing in the journals could rebuild the logs, and a new empty store was started");
    }
    else if (m_Lost > 0)
    {
        m_Report.m_Outcome = RecoveryOutcome::PARTIAL_LOSS;
    }
    else if (rebuilt)
    {
        m_Report.m_Outcome = RecoveryOutcome::REBUILT;
    }
    else if (m_Report.m_ReplayedRecords > 0)
    {
        m_Report.m_Outcome = RecoveryOutcome::REPLAYED;
    }
    else
    {
        m_Report.m_Outcome = RecoveryOutcome::CLEAN;
    }

    if (!m_Quarantine.taken().empty())
    {
        if (auto pruned = m_Quarantine.prune(m_Options.m_KeepQuarantine); !pruned)
        {
            return Failure(pruned.error());
        }
    }
    m_Repository.reset();
    return m_Report;
}

}  // namespace

Result<RecoveryReport, StorageError> Recovery::run(const StorageLocation& location, const Clock& clock,
                                                   RecoveryOptions options)
{
    return RecoveryRun(location, clock, std::move(options)).execute();
}

}  // namespace ej
