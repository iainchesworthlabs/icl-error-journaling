// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/journal_file.hpp"

#include "platform/durable_sync.hpp"
#include "platform/errno_error.hpp"
#include "platform/file_lock.hpp"

#include <cerrno>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ej
{

namespace
{

class StdioJournalFile final : public JournalFile
{
public:
    StdioJournalFile(std::filesystem::path path, platform::FilePtr file, std::uint64_t size)
        : m_Path(std::move(path)), m_File(std::move(file)), m_Size(size)
    {
    }

    [[nodiscard]] std::uint64_t size() const override { return m_Size; }

    Result<std::string, StorageError> read_all() override
    {
        std::ifstream in(m_Path, std::ios::binary);
        if (!in)
        {
            return fail(make_error(StorageErrorKind::IO, "a journal file could not be read"));
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    Result<void, StorageError> append(std::string_view bytes) override
    {
        if (std::fwrite(bytes.data(), 1, bytes.size(), m_File.get()) != bytes.size() || std::fflush(m_File.get()) != 0)
        {
            return fail(platform::error_from_errno("a journal file could not be written", errno));
        }
        m_Size += bytes.size();
        return {};
    }

    Result<void, StorageError> sync() override { return platform::durable_sync(m_File.get()); }

    Result<void, StorageError> truncate(std::uint64_t length) override
    {
        if (auto cut = platform::truncate_file(m_File.get(), length); !cut)
        {
            return cut;
        }
        m_Size = length;
        return {};
    }

private:
    std::filesystem::path m_Path;
    platform::FilePtr m_File;
    std::uint64_t m_Size;
};

}  // namespace

Result<std::unique_ptr<JournalFile>, StorageError> open_journal_file(const std::filesystem::path& path)
{
    auto file = platform::open_for_append(path);
    if (!file)
    {
        return fail(file.error());
    }
    if (auto locked = platform::lock_exclusive(file->get()); !locked)
    {
        return fail(locked.error());
    }
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code)
    {
        return fail(make_error(StorageErrorKind::IO, "a journal file could not be measured", code.value()));
    }
    return std::unique_ptr<JournalFile>(new StdioJournalFile(path, std::move(*file), size));
}

}  // namespace ej
