// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/journal_reader.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <string>

namespace ej
{

namespace
{

class StringSource final : public ByteSource
{
public:
    explicit StringSource(std::string_view bytes) : m_Bytes(bytes) {}

    Result<std::size_t, StorageError> read(char* out, std::size_t max) override
    {
        const std::size_t count = std::min(max, m_Bytes.size() - m_Offset);
        std::copy_n(m_Bytes.data() + m_Offset, count, out);
        m_Offset += count;
        return count;
    }

private:
    std::string_view m_Bytes;
    std::size_t m_Offset = 0;
};

class FileSource final : public ByteSource
{
public:
    explicit FileSource(std::ifstream in) : m_In(std::move(in)) {}

    Result<std::size_t, StorageError> read(char* out, std::size_t max) override
    {
        m_In.read(out, static_cast<std::streamsize>(max));
        if (m_In.bad())
        {
            return fail(make_error(StorageErrorKind::IO, "a journal file could not be read"));
        }
        return static_cast<std::size_t>(m_In.gcount());
    }

private:
    std::ifstream m_In;
};

/// Reads a journal line by line, with state across the pieces it is given.
class Scanner
{
public:
    explicit Scanner(std::optional<LogId> expected) : m_Expected(expected) { m_Scan.m_Log = expected; }

    Result<void, StorageError> feed(const char* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            const char c = data[i];
            ++m_Scan.m_FileBytes;
            if (c != '\n')
            {
                ++m_SegmentBytes;
                if (m_Oversize)
                {
                    continue;
                }
                if (m_Line.size() >= MAX_RECORD_BYTES)
                {
                    m_Oversize = true;  // too long to be a record: stop buffering it
                    m_Line.clear();
                    continue;
                }
                m_Line.push_back(c);
                continue;
            }
            if (auto accepted = line_complete(); !accepted)
            {
                return accepted;
            }
            m_Scan.m_CompleteBytes = m_Scan.m_FileBytes;
            m_Line.clear();
            m_Oversize = false;
            m_SegmentBytes = 0;
        }
        return {};
    }

    JournalScan finish()
    {
        m_Scan.m_TornTailBytes = m_SegmentBytes;
        return std::move(m_Scan);
    }

private:
    Result<void, StorageError> line_complete()
    {
        if (m_Oversize)
        {
            return bad_line();
        }
        auto record = decode_record(m_Line);
        if (!record)
        {
            m_Scan.m_UnsupportedRecords += record.error() == DecodeError::UNSUPPORTED_VERSION ? 1 : 0;
            return bad_line();
        }
        if (!m_Scan.m_Log)
        {
            m_Scan.m_Log = record->m_Log;
        }
        if (record->m_Log != *m_Scan.m_Log)
        {
            if (m_Expected)
            {
                return fail(make_error(StorageErrorKind::JOURNAL, "the journal belongs to another log"));
            }
            ++m_Scan.m_ForeignRecords;
            return bad_line();
        }
        if (m_Scan.m_HasProblem)
        {
            m_Scan.m_Aside.push_back(std::move(*record));
            return {};
        }
        const std::uint64_t expected_seq = m_Scan.last_seq() + 1;
        if (record->m_Seq == expected_seq)
        {
            m_Checksums.push_back(crc32(m_Line));
            m_Scan.m_Records.push_back(std::move(*record));
        }
        else if (record->m_Seq < expected_seq)
        {
            // A copy of a record already accepted is harmless; a different record with the same number is not.
            if (m_Checksums[record->m_Seq - 1] == crc32(m_Line))
            {
                ++m_Scan.m_DuplicateRecords;
            }
            else
            {
                // Two different records claim one number: nothing after this can be trusted to follow on.
                m_Scan.m_HasProblem = true;
                return bad_line();
            }
        }
        else
        {
            m_Scan.m_HasProblem = true;  // a gap: records are missing
            m_Scan.m_Aside.push_back(std::move(*record));
        }
        return {};
    }

    /// A line that is not a record. If it was a record that damage made unreadable, the next record shows a gap; if it
    /// was noise, the records after it follow on and are accepted.
    Result<void, StorageError> bad_line()
    {
        ++m_Scan.m_BadRecords;
        return {};
    }

    std::optional<LogId> m_Expected;
    JournalScan m_Scan;
    std::string m_Line;
    bool m_Oversize = false;
    std::uint64_t m_SegmentBytes = 0;
    std::vector<std::uint32_t> m_Checksums;  // of the accepted lines, by sequence number - 1
};

}  // namespace

Result<std::unique_ptr<ByteSource>, StorageError> open_file_source(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return fail(make_error(StorageErrorKind::IO, "a journal file could not be opened for reading"));
    }
    return std::unique_ptr<ByteSource>(new FileSource(std::move(in)));
}

std::unique_ptr<ByteSource> make_string_source(std::string_view bytes)
{
    return std::make_unique<StringSource>(bytes);
}

Result<JournalScan, StorageError> scan_journal(ByteSource& source, std::optional<LogId> expected_log)
{
    Scanner scanner(expected_log);
    std::array<char, 64 * 1024> chunk;
    for (;;)
    {
        const auto count = source.read(chunk.data(), chunk.size());
        if (!count)
        {
            return fail(count.error());
        }
        if (*count == 0)
        {
            break;
        }
        if (auto fed = scanner.feed(chunk.data(), *count); !fed)
        {
            return fail(fed.error());
        }
    }
    return scanner.finish();
}

Result<JournalScan, StorageError> scan_journal_bytes(std::string_view bytes, std::optional<LogId> expected_log)
{
    const auto source = make_string_source(bytes);
    return scan_journal(*source, expected_log);
}

VoidResolution resolve_voids(std::span<const JournalRecord> records)
{
    VoidResolution result;
    std::set<std::uint64_t> seen;
    std::set<std::uint64_t> voided;
    for (const JournalRecord& record : records)
    {
        if (const auto* cancel = std::get_if<VoidBody>(&record.m_Body))
        {
            if (cancel->m_Target < record.m_Seq && seen.contains(cancel->m_Target))
            {
                voided.insert(cancel->m_Target);
            }
            else
            {
                ++result.m_OrphanVoids;
            }
        }
        seen.insert(record.m_Seq);
    }
    result.m_Voided = voided.size();
    for (const JournalRecord& record : records)
    {
        if (!std::holds_alternative<VoidBody>(record.m_Body) && !voided.contains(record.m_Seq))
        {
            result.m_Effective.push_back(record);
        }
    }
    return result;
}

}  // namespace ej
