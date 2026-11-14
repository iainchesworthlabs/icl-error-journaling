// SPDX-License-Identifier: GPL-3.0-or-later
#include "journal_seed.hpp"

#include "platform/durable_sync.hpp"
#include "protect.hpp"

#include <system_error>
#include <utility>

namespace ej
{

std::vector<JournalRecord> seed_records(const StoredLog& log)
{
    std::vector<JournalRecord> records;
    std::uint64_t seq = 1;
    // Each body is built in place in the record: a body built apart and moved in trips a false warning in GCC's variant
    // code.
    const auto add = [&records, &log, &seq]<typename Body>(std::in_place_type_t<Body>, auto&&... arguments)
    {
        JournalRecord record;
        record.m_Seq = seq++;
        record.m_Log = log.m_Meta.m_Id;
        record.m_Body.template emplace<Body>(std::forward<decltype(arguments)>(arguments)...);
        record.m_Version = journal_version_for(record.m_Body); // an interaction is written at version 2
        records.push_back(std::move(record));
    };
    add(std::in_place_type<CreateLogBody>, log.m_Log.initial_header(), log.m_Meta.m_CreatedUtc, log.m_Meta.m_Previous);
    // A log that changed after its share has the share before its last entry, so that replaying the records leaves a
    // change after the share, as the log has. (Which entry changed is not known, and does not matter: any one will do.)
    // A shared log that is unchanged has the share after all of them.
    const auto entries = log.m_Log.entries();
    const bool share_before_last = log.m_Meta.m_LastSharedUtc && log.m_Meta.m_ChangedSinceShare && !entries.empty();
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (share_before_last && i + 1 == entries.size())
        {
            add(std::in_place_type<MarkSharedBody>, *log.m_Meta.m_LastSharedUtc);
        }
        add(std::in_place_type<AddEntryBody>, entries[i]);
    }
    if (log.m_Meta.m_LastSharedUtc && !share_before_last)
    {
        add(std::in_place_type<MarkSharedBody>, *log.m_Meta.m_LastSharedUtc);
    }
    if (log.m_Meta.m_Archived)
    {
        add(std::in_place_type<ArchiveBody>);
    }
    return records;
}

Result<void, StorageError> write_journal_file(const std::filesystem::path& path,
                                              const std::vector<JournalRecord>& records)
{
    std::string text;
    for (const JournalRecord& record : records)
    {
        auto line = encode_record(record);
        if (!line)
        {
            return fail(line.error());
        }
        text += *line;
        text.push_back('\n');
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code code;
    std::filesystem::remove(temporary, code);  // a leftover of a run that was killed: ours, and incomplete
    {
        auto file = platform::open_for_append(temporary);
        if (!file)
        {
            return fail(file.error());
        }
        protect_path(temporary);  // the rename keeps the file, so the new journal never exists unprotected
        if (std::fwrite(text.data(), 1, text.size(), file->get()) != text.size())
        {
            return fail(make_error(StorageErrorKind::IO, "a new journal could not be written"));
        }
        if (auto synced = platform::durable_sync(file->get()); !synced)
        {
            return synced;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code)
    {
        return fail(make_error(StorageErrorKind::IO, "a new journal could not be moved into place", code.value()));
    }
    return platform::sync_directory(path.parent_path());
}

Result<void, StorageError> truncate_journal_file(const std::filesystem::path& path, std::uint64_t length)
{
    auto file = platform::open_for_append(path);
    if (!file)
    {
        return fail(file.error());
    }
    if (auto cut = platform::truncate_file(file->get(), length); !cut)
    {
        return cut;
    }
    return platform::durable_sync(file->get());
}

}  // namespace ej
