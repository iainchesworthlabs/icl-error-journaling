// SPDX-License-Identifier: GPL-3.0-or-later
#include "journal_seed.hpp"

#include "platform/durable_sync.hpp"

#include <system_error>

namespace ej
{

std::vector<JournalRecord> seed_records(const StoredLog& log)
{
    std::vector<JournalRecord> records;
    std::uint64_t seq = 1;
    const auto add = [&records, &log, &seq](JournalBody body)
    {
        JournalRecord record;
        record.m_Seq = seq++;
        record.m_Log = log.m_Meta.m_Id;
        record.m_Body = std::move(body);
        records.push_back(std::move(record));
    };
    add(CreateLogBody{log.m_Log.initial_header(), log.m_Meta.m_CreatedUtc, log.m_Meta.m_Previous});
    for (const Entry& entry : log.m_Log.entries())
    {
        add(AddEntryBody{entry});
    }
    if (log.m_Meta.m_LastSharedUtc)
    {
        add(MarkSharedBody{*log.m_Meta.m_LastSharedUtc});
    }
    if (log.m_Meta.m_Archived)
    {
        add(ArchiveBody{});
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
