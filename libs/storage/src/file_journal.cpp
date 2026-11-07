// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/file_journal.hpp"

#include "platform/durable_sync.hpp"
#include "platform/file_protection.hpp"

#include <algorithm>
#include <system_error>

namespace ej
{

namespace
{

std::filesystem::path file_name(LogId log)
{
    return std::filesystem::path(log.format() + ".jsonl");
}

StorageError io_error(const char* what, const std::error_code& code)
{
    const bool denied = code == std::errc::permission_denied || code == std::errc::read_only_file_system;
    const bool full = code == std::errc::no_space_on_device;
    return make_error(full     ? StorageErrorKind::DISK_FULL
                      : denied ? StorageErrorKind::PERMISSION
                               : StorageErrorKind::IO,
                      what, code.value());
}

}  // namespace

FileJournal::FileJournal(StorageLocation location, Options options)
    : m_Location(std::move(location)), m_Options(std::move(options))
{
    if (!m_Options.m_Opener)
    {
        m_Options.m_Opener = [](const std::filesystem::path& path) { return open_journal_file(path); };
    }
}

FileJournal::~FileJournal() = default;

std::filesystem::path FileJournal::open_path(LogId log) const
{
    return m_Location.journal_dir() / file_name(log);
}

std::filesystem::path FileJournal::archived_path(LogId log) const
{
    return m_Location.archived_journal_dir() / file_name(log);
}

Result<JournalWriter*, StorageError> FileJournal::writer_for(LogId log, bool create)
{
    if (const auto it = m_Writers.find(log); it != m_Writers.end())
    {
        return it->second.get();
    }

    std::error_code code;
    std::filesystem::path path = open_path(log);
    bool is_new = false;
    if (!std::filesystem::exists(path, code))
    {
        path = archived_path(log);
        if (!std::filesystem::exists(path, code))
        {
            if (!create)
            {
                return static_cast<JournalWriter*>(nullptr);
            }
            path = open_path(log);
            is_new = true;
            std::filesystem::create_directories(path.parent_path(), code);
            if (code)
            {
                return fail(io_error("the journal folder could not be created", code));
            }
        }
    }

    auto file = m_Options.m_Opener(path);
    if (!file)
    {
        return fail(file.error());
    }
    if (is_new)
    {
        // The file exists now; make sure its entry in the folder survives a power loss, or the whole file could vanish
        // with the records it holds (F16).
        if (auto synced = platform::sync_directory(path.parent_path()); !synced)
        {
            return fail(synced.error());
        }
        if (auto protected_file = platform::apply_file_protection(path); !protected_file)
        {
            return fail(protected_file.error());
        }
    }
    auto writer = JournalWriter::open(std::move(*file), log, m_Options.m_Hook);
    if (!writer)
    {
        return fail(writer.error());
    }
    JournalWriter* raw = writer->get();
    m_Writers.emplace(log, std::move(*writer));
    return raw;
}

Result<std::uint64_t, StorageError> FileJournal::next_seq(LogId log, std::uint64_t applied_seq)
{
    auto writer = writer_for(log, false);
    if (!writer)
    {
        return fail(writer.error());
    }
    if (*writer == nullptr)
    {
        if (applied_seq != 0)
        {
            // The database has records of this log and the journal does not exist: recovery seeds it from the database.
            return fail(make_error(StorageErrorKind::JOURNAL, "the journal of a log is missing and needs recovery"));
        }
        return 1;
    }
    return std::max((*writer)->last_seq(), applied_seq) + 1;
}

Result<JournalMark, StorageError> FileJournal::append(const JournalRecord& record)
{
    const bool starts_log = record.m_Seq == 1 && std::holds_alternative<CreateLogBody>(record.m_Body);
    auto writer = writer_for(record.m_Log, starts_log);
    if (!writer)
    {
        return fail(writer.error());
    }
    if (*writer == nullptr)
    {
        return fail(make_error(StorageErrorKind::JOURNAL, "the journal of a log is missing and needs recovery"));
    }
    return (*writer)->append(record);
}

Result<void, StorageError> FileJournal::cancel(const JournalMark& mark)
{
    const auto it = m_Writers.find(mark.m_Log);
    if (it == m_Writers.end())
    {
        return fail(make_error(StorageErrorKind::INTERNAL, "a record was cancelled in a journal that is not open"));
    }
    return it->second->cancel(mark);
}

Result<void, StorageError> FileJournal::archive(LogId log)
{
    m_Writers.erase(log);  // closes the file and releases its lock
    std::error_code code;
    const std::filesystem::path from = open_path(log);
    if (!std::filesystem::exists(from, code))
    {
        return {};  // already in the archive folder, or never written
    }
    std::filesystem::create_directories(m_Location.archived_journal_dir(), code);
    if (code)
    {
        return fail(io_error("the archive folder could not be created", code));
    }
    std::filesystem::rename(from, archived_path(log), code);
    if (code)
    {
        return fail(io_error("a journal could not be moved to the archive", code));
    }
    if (auto synced = platform::sync_directory(m_Location.archived_journal_dir()); !synced)
    {
        return synced;
    }
    return platform::sync_directory(m_Location.journal_dir());
}

Result<void, StorageError> FileJournal::remove(LogId log)
{
    m_Writers.erase(log);
    for (const auto& path : {open_path(log), archived_path(log)})
    {
        std::error_code code;
        if (!std::filesystem::exists(path, code))
        {
            continue;
        }
        std::filesystem::remove(path, code);
        if (code)
        {
            return fail(io_error("a journal could not be removed", code));
        }
        if (auto synced = platform::sync_directory(path.parent_path()); !synced)
        {
            return synced;
        }
    }
    return {};
}

}  // namespace ej
