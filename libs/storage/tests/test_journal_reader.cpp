// SPDX-License-Identifier: GPL-3.0-or-later
//
// The journal cases J1 to J12 of docs/plan/stage-2.md (section 5.2), at the level of the reader: bytes in, what is a
// record out. Recovery (a later work package) decides what to do with the answer.
#include "error_journaling/journal_reader.hpp"
#include "storage_samples.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 100;

std::string line_of(const JournalRecord& record)
{
    const auto line = encode_record(record);
    REQUIRE(line.has_value());
    return *line + "\n";
}

/// A journal of `count` records: the create_log, then error entries.
std::vector<JournalRecord> records_of(std::size_t count)
{
    std::vector<JournalRecord> records;
    records.push_back(sample_record(1, CreateLogBody{sample_header(), sample_time(0), std::nullopt}, LOG));
    for (std::size_t seq = 2; seq <= count; ++seq)
    {
        records.push_back(sample_record(seq, AddEntryBody{sample_error(seq, static_cast<std::int64_t>(seq))}, LOG));
    }
    return records;
}

std::string bytes_of(const std::vector<JournalRecord>& records)
{
    std::string bytes;
    for (const JournalRecord& record : records)
    {
        bytes += line_of(record);
    }
    return bytes;
}

JournalScan scan(std::string_view bytes, std::optional<LogId> expected = std::nullopt)
{
    auto result = scan_journal_bytes(bytes, expected);
    REQUIRE(result.has_value());
    return std::move(*result);
}

std::vector<std::uint64_t> seqs_of(const std::vector<JournalRecord>& records)
{
    std::vector<std::uint64_t> seqs;
    for (const JournalRecord& record : records)
    {
        seqs.push_back(record.m_Seq);
    }
    return seqs;
}

}  // namespace

TEST_CASE("An empty journal has nothing in it and nothing wrong", "[journal-reader]")
{
    const JournalScan result = scan("");
    CHECK(result.m_Records.empty());
    CHECK(result.last_seq() == 0);
    CHECK(result.is_clean());
    CHECK_FALSE(result.m_Log.has_value());
}

TEST_CASE("A clean journal is accepted whole", "[journal-reader]")
{
    const std::string bytes = bytes_of(records_of(12));
    const JournalScan result = scan(bytes);
    CHECK(result.m_Records.size() == 12);
    CHECK(result.last_seq() == 12);
    CHECK(result.m_Log == sample_id<LogId>(LOG));
    CHECK(result.m_FileBytes == bytes.size());
    CHECK(result.m_CompleteBytes == bytes.size());
    CHECK(result.m_TornTailBytes == 0);
    CHECK(result.m_Aside.empty());
    CHECK(result.is_clean());
}

TEST_CASE("J1: a torn tail is dropped wherever the last record was cut", "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(6);
    const std::string whole = bytes_of(records);
    const std::string last = line_of(records.back());
    const std::size_t last_start = whole.size() - last.size();

    // Every cut inside the last line, up to and including everything but its newline.
    for (std::size_t cut = last_start + 1; cut < whole.size(); ++cut)
    {
        INFO("cut at " << cut);
        const JournalScan result = scan(std::string_view(whole).substr(0, cut));
        CHECK(result.m_Records.size() == 5);
        CHECK(result.m_TornTailBytes == cut - last_start);
        CHECK(result.m_CompleteBytes == last_start);
        CHECK(result.m_BadRecords == 0);
        CHECK_FALSE(result.is_clean());
    }
    // Cut exactly at a record boundary: nothing is torn, the journal is just shorter.
    const JournalScan boundary = scan(std::string_view(whole).substr(0, last_start));
    CHECK(boundary.m_Records.size() == 5);
    CHECK(boundary.is_clean());
}

TEST_CASE("J2: a zero-filled tail is dropped, whole or after half a record", "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(5);
    const std::string whole = bytes_of(records);

    const JournalScan zeros = scan(whole + std::string(4096, '\0'));
    CHECK(zeros.m_Records.size() == 5);
    CHECK(zeros.m_TornTailBytes == 4096);
    CHECK(zeros.m_CompleteBytes == whole.size());

    const std::string next = line_of(sample_record(6, AddEntryBody{sample_error(6, 6)}, LOG));
    const std::string half = next.substr(0, next.size() / 2);
    const JournalScan half_then_zeros = scan(whole + half + std::string(4096, '\0'));
    CHECK(half_then_zeros.m_Records.size() == 5);
    CHECK(half_then_zeros.m_TornTailBytes == half.size() + 4096);
    CHECK(half_then_zeros.m_BadRecords == 0);

    // A whole record whose newline never arrived is not a record either.
    const JournalScan no_newline = scan(whole + next.substr(0, next.size() - 1));
    CHECK(no_newline.m_Records.size() == 5);
    CHECK(no_newline.m_TornTailBytes == next.size() - 1);
}

TEST_CASE("J3: a bad record in the middle is counted, the run ends at the gap it leaves, later records are set aside",
          "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(8);
    std::string bytes;
    std::size_t damaged_at = 0;
    for (const JournalRecord& record : records)
    {
        if (record.m_Seq == 4)
        {
            damaged_at = bytes.size() + 40;
        }
        bytes += line_of(record);
    }
    bytes[damaged_at] = static_cast<char>(bytes[damaged_at] ^ 0x01);

    const JournalScan result = scan(bytes);
    CHECK(seqs_of(result.m_Records) == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(result.m_BadRecords == 1);
    CHECK(result.m_HasProblem);
    CHECK(seqs_of(result.m_Aside) == std::vector<std::uint64_t>{5, 6, 7, 8});
    CHECK_FALSE(result.is_clean());
}

TEST_CASE("J6: copies of records already accepted are dropped and do no harm", "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(14);
    std::string bytes = bytes_of(records);
    for (std::size_t i = 4; i < records.size(); ++i)  // the last ten
    {
        bytes += line_of(records[i]);
    }
    const JournalScan result = scan(bytes);
    CHECK(result.m_Records.size() == 14);
    CHECK(result.m_DuplicateRecords == 10);
    CHECK(result.m_BadRecords == 0);
    CHECK(result.m_Aside.empty());
    CHECK(result.is_clean());
}

TEST_CASE("Two different records with one number stop the run", "[journal-reader]")
{
    std::vector<JournalRecord> records = records_of(4);
    std::string bytes = bytes_of(records);
    bytes += line_of(sample_record(3, AddEntryBody{sample_error(99, 9)}, LOG));  // not the 3 already accepted
    bytes += line_of(sample_record(5, AddEntryBody{sample_error(5, 5)}, LOG));
    const JournalScan result = scan(bytes);
    CHECK(result.m_Records.size() == 4);
    CHECK(result.m_BadRecords == 1);
    CHECK(result.m_HasProblem);
    CHECK(seqs_of(result.m_Aside) == std::vector<std::uint64_t>{5});
}

TEST_CASE("J7: a gap in the sequence ends the run there and nothing is guessed", "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(9);
    std::string bytes;
    for (const JournalRecord& record : records)
    {
        if (record.m_Seq != 5)
        {
            bytes += line_of(record);
        }
    }
    const JournalScan result = scan(bytes);
    CHECK(seqs_of(result.m_Records) == std::vector<std::uint64_t>{1, 2, 3, 4});
    CHECK(seqs_of(result.m_Aside) == std::vector<std::uint64_t>{6, 7, 8, 9});
    CHECK(result.m_BadRecords == 0);
    CHECK(result.m_HasProblem);
    CHECK_FALSE(result.is_clean());
}

TEST_CASE("J8: a line that is not UTF-8 is skipped and counted, and the records around it still follow on",
          "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(6);
    std::string bytes;
    for (const JournalRecord& record : records)
    {
        bytes += line_of(record);
        if (record.m_Seq == 3)
        {
            bytes += std::string("\xFF\xFE\xC3garbage\x80\n");
        }
    }
    const JournalScan result = scan(bytes);
    CHECK(result.m_Records.size() == 6);  // the noise took no number, so nothing is missing
    CHECK(result.m_BadRecords == 1);
    CHECK_FALSE(result.m_HasProblem);
    CHECK_FALSE(result.is_clean());  // but something was in the file that should not be

    const JournalScan blank_lines = scan("\n\n" + bytes_of(records));
    CHECK(blank_lines.m_Records.size() == 6);
    CHECK(blank_lines.m_BadRecords == 2);
}

TEST_CASE("J9: a record of another log is refused for the log being opened", "[journal-reader]")
{
    std::string bytes = bytes_of(records_of(3));
    bytes += line_of(sample_record(4, AddEntryBody{sample_error(4, 4)}, LOG + 1));

    const auto refused = scan_journal_bytes(bytes, sample_id<LogId>(LOG));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().m_Kind == StorageErrorKind::JOURNAL);

    const auto wrong_from_the_start = scan_journal_bytes(bytes_of(records_of(3)), sample_id<LogId>(LOG + 1));
    CHECK_FALSE(wrong_from_the_start.has_value());

    // With no expectation, the first record decides, and a later stranger is counted and not accepted.
    const JournalScan result = scan(bytes);
    CHECK(result.m_Log == sample_id<LogId>(LOG));
    CHECK(result.m_Records.size() == 3);
    CHECK(result.m_ForeignRecords == 1);
    CHECK(result.m_BadRecords == 1);
}

TEST_CASE("J11: a record and its void resolve to nothing", "[journal-reader]")
{
    std::vector<JournalRecord> records = records_of(3);
    records.push_back(sample_record(4, AddEntryBody{sample_error(4, 4)}, LOG));
    records.push_back(sample_record(5, VoidBody{4}, LOG));
    const JournalScan result = scan(bytes_of(records));
    REQUIRE(result.m_Records.size() == 5);

    const VoidResolution resolved = resolve_voids(result.m_Records);
    CHECK(seqs_of(resolved.m_Effective) == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(resolved.m_Voided == 1);
    CHECK(resolved.m_OrphanVoids == 0);
}

TEST_CASE("A void that names nothing earlier is counted and cancels nothing", "[journal-reader]")
{
    std::vector<JournalRecord> records = records_of(3);
    records.push_back(sample_record(4, VoidBody{9}, LOG));  // nothing 9 yet
    records.push_back(sample_record(5, VoidBody{5}, LOG));  // itself
    const VoidResolution resolved = resolve_voids(records);
    CHECK(seqs_of(resolved.m_Effective) == std::vector<std::uint64_t>{1, 2, 3});
    CHECK(resolved.m_Voided == 0);
    CHECK(resolved.m_OrphanVoids == 2);
}

TEST_CASE("Voiding the same record twice counts it once", "[journal-reader]")
{
    std::vector<JournalRecord> records = records_of(3);
    records.push_back(sample_record(4, VoidBody{3}, LOG));
    records.push_back(sample_record(5, VoidBody{3}, LOG));
    const VoidResolution resolved = resolve_voids(records);
    CHECK(seqs_of(resolved.m_Effective) == std::vector<std::uint64_t>{1, 2});
    CHECK(resolved.m_Voided == 1);
}

namespace
{

/// Hands out `total` bytes without ever holding them, so the test can read a "file" of any size.
class EndlessLine final : public ByteSource
{
public:
    EndlessLine(std::uint64_t total, std::string prefix = {}, std::string suffix = {})
        : m_Total(total), m_Prefix(std::move(prefix)), m_Suffix(std::move(suffix))
    {
    }

    Result<std::size_t, StorageError> read(char* out, std::size_t max) override
    {
        m_LargestRequest = std::max(m_LargestRequest, max);
        std::size_t written = 0;
        while (written < max && m_Position < m_Prefix.size() + m_Total + m_Suffix.size())
        {
            if (m_Position < m_Prefix.size())
            {
                out[written++] = m_Prefix[static_cast<std::size_t>(m_Position)];
            }
            else if (m_Position < m_Prefix.size() + m_Total)
            {
                out[written++] = 'x';
            }
            else
            {
                out[written++] = m_Suffix[static_cast<std::size_t>(m_Position - m_Prefix.size() - m_Total)];
            }
            ++m_Position;
        }
        return written;
    }

    std::size_t m_LargestRequest = 0;

private:
    std::uint64_t m_Total;
    std::string m_Prefix;
    std::string m_Suffix;
    std::uint64_t m_Position = 0;
};

}  // namespace

TEST_CASE("J12: a line of ten megabytes is rejected by the size bound, not read into memory", "[journal-reader]")
{
    const std::uint64_t ten_mb = 10ULL * 1024 * 1024;

    // No newline at all: the whole thing is a torn tail.
    EndlessLine endless(ten_mb);
    const auto torn = scan_journal(endless);
    REQUIRE(torn.has_value());
    CHECK(torn->m_TornTailBytes == ten_mb);
    CHECK(torn->m_Records.empty());
    CHECK(endless.m_LargestRequest <= 64 * 1024);  // read in pieces

    // With a newline: the first record is read, the monster is one bad line, the record after it follows on.
    const std::string first =
        line_of(sample_record(1, CreateLogBody{sample_header(), sample_time(0), std::nullopt}, LOG));
    const std::string second = line_of(sample_record(2, AddEntryBody{sample_error(2, 2)}, LOG));
    EndlessLine with_newline(ten_mb, first, "\n" + second);
    const auto skipped = scan_journal(with_newline);
    REQUIRE(skipped.has_value());
    CHECK(skipped->m_Records.size() == 2);
    CHECK(skipped->m_BadRecords == 1);
    CHECK(skipped->m_Aside.empty());
    CHECK(with_newline.m_LargestRequest <= 64 * 1024);
}

TEST_CASE("A line one byte over the bound is bad and one at the bound is read", "[journal-reader]")
{
    EndlessLine over(MAX_RECORD_BYTES + 1, {}, "\n");
    const auto over_result = scan_journal(over);
    REQUIRE(over_result.has_value());
    CHECK(over_result->m_BadRecords == 1);

    EndlessLine at(MAX_RECORD_BYTES, {}, "\n");
    const auto at_result = scan_journal(at);
    REQUIRE(at_result.has_value());
    CHECK(at_result->m_BadRecords == 1);  // within the bound, but not a record
    CHECK(at_result->m_Records.empty());
}

TEST_CASE("A record after an oversize line follows on", "[journal-reader]")
{
    const std::vector<JournalRecord> records = records_of(4);
    std::string bytes = line_of(records[0]);
    bytes += std::string(MAX_RECORD_BYTES * 3, 'x') + "\n";
    bytes += line_of(records[1]) + line_of(records[2]) + line_of(records[3]);
    const JournalScan result = scan(bytes);
    CHECK(result.m_Records.size() == 4);
    CHECK(result.m_BadRecords == 1);
}

TEST_CASE("A record with an unsupported version is counted and not accepted", "[journal-reader]")
{
    std::string bytes = bytes_of(records_of(2));
    // A version 3 record with a good checksum (version 2 is the interaction record, which this build reads).
    std::string v3 =
        "{\"v\":3,\"seq\":3,\"op\":\"archive\",\"log\":\"00000000-0000-4000-8000-000000000064\",\"body\":{}}";
    char digits[16];
    std::snprintf(digits, sizeof digits, "%08x", crc32(v3));
    v3 = v3.substr(0, v3.size() - 1) + ",\"crc\":\"" + digits + "\"}\n";
    const JournalScan result = scan(bytes + v3);
    CHECK(result.m_Records.size() == 2);
    CHECK(result.m_UnsupportedRecords == 1);
    CHECK(result.m_BadRecords == 1);
}

TEST_CASE("A file source reads a real file in pieces and reports one that is missing", "[journal-reader]")
{
    TempDir dir;
    const std::string bytes = bytes_of(records_of(40)) + "torn";
    {
        std::ofstream out(dir.path() / "j.jsonl", std::ios::binary);
        out << bytes;
    }
    auto source = open_file_source(dir.path() / "j.jsonl");
    REQUIRE(source.has_value());
    const auto result = scan_journal(**source);
    REQUIRE(result.has_value());
    CHECK(result->m_Records.size() == 40);
    CHECK(result->m_TornTailBytes == 4);
    CHECK(result->m_FileBytes == bytes.size());

    CHECK_FALSE(open_file_source(dir.path() / "missing.jsonl").has_value());
}

TEST_CASE("A source that fails stops the scan with its error", "[journal-reader]")
{
    class Failing final : public ByteSource
    {
    public:
        Result<std::size_t, StorageError> read(char*, std::size_t) override
        {
            return fail(make_error(StorageErrorKind::IO, "read failed"));
        }
    } failing;
    const auto result = scan_journal(failing);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().m_Kind == StorageErrorKind::IO);
}
