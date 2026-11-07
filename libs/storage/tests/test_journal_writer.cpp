// SPDX-License-Identifier: GPL-3.0-or-later
//
// The journal writer over a file that models a power loss (`LossyFile`): no process and no disk, so it runs for
// thousands of seeds, and it covers the torn tail at volume (section 4.2 of the Stage 2 plan).
#include "error_journaling/journal_reader.hpp"
#include "error_journaling/journal_writer.hpp"
#include "lossy_file.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 100;

JournalRecord record_at(std::uint64_t seq)
{
    if (seq == 1)
    {
        return sample_record(1, CreateLogBody{sample_header(), sample_time(0), std::nullopt}, LOG);
    }
    return sample_record(seq, AddEntryBody{sample_error(seq, static_cast<std::int64_t>(seq))}, LOG);
}

struct Writer
{
    LossyFile* m_File = nullptr;
    std::unique_ptr<JournalWriter> m_Writer;
};

Writer open_writer(std::string existing = {}, CrashHook hook = {})
{
    auto file = std::make_unique<LossyFile>(std::move(existing));
    Writer result;
    result.m_File = file.get();
    auto opened = JournalWriter::open(std::move(file), sample_id<LogId>(LOG), std::move(hook));
    REQUIRE(opened.has_value());
    result.m_Writer = std::move(*opened);
    return result;
}

JournalScan scan(std::string_view bytes)
{
    auto result = scan_journal_bytes(bytes, sample_id<LogId>(LOG));
    REQUIRE(result.has_value());
    return std::move(*result);
}

}  // namespace

TEST_CASE("The writer appends a line per record and has it durable before it returns", "[journal-writer]")
{
    Writer w = open_writer();
    CHECK(w.m_Writer->last_seq() == 0);

    const auto first = w.m_Writer->append(record_at(1));
    REQUIRE(first.has_value());
    CHECK(first->m_Seq == 1);
    CHECK(first->m_Offset == 0);
    CHECK(w.m_File->pending().empty());  // synced
    CHECK(w.m_File->syncs() == 1);

    const auto second = w.m_Writer->append(record_at(2));
    REQUIRE(second.has_value());
    const std::size_t first_line = encode_record(record_at(1)).value().size() + 1;
    CHECK(second->m_Offset == first_line);  // the length of the journal before the second record
    CHECK(w.m_Writer->last_seq() == 2);
    CHECK(w.m_File->syncs() == 2);

    const JournalScan result = scan(w.m_File->durable());
    CHECK(result.m_Records.size() == 2);
    CHECK(result.is_clean());
    CHECK(w.m_File->durable().back() == '\n');
}

TEST_CASE("The writer refuses a record that is not the next of its own log", "[journal-writer]")
{
    Writer w = open_writer();
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    CHECK(w.m_Writer->append(record_at(3)).error().m_Kind == StorageErrorKind::INTERNAL);  // a gap
    CHECK(w.m_Writer->append(record_at(1)).error().m_Kind == StorageErrorKind::INTERNAL);  // a repeat
    JournalRecord stranger = record_at(2);
    stranger.m_Log = sample_id<LogId>(LOG + 1);
    CHECK(w.m_Writer->append(stranger).error().m_Kind == StorageErrorKind::INTERNAL);  // another log
    CHECK(w.m_Writer->last_seq() == 1);
    CHECK(scan(w.m_File->durable()).m_Records.size() == 1);
}

TEST_CASE("A record that is too long is refused before anything is written", "[journal-writer]")
{
    Writer w = open_writer();
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    const auto huge =
        w.m_Writer->append(sample_record(2, AddEntryBody{sample_note(2, 2, std::string(40'000, '\x01'))}, LOG));
    REQUIRE_FALSE(huge.has_value());
    CHECK(huge.error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(w.m_File->size() == w.m_File->durable().size());
    CHECK(scan(w.m_File->durable()).m_Records.size() == 1);
}

TEST_CASE("The writer continues a journal that is already there", "[journal-writer]")
{
    Writer first = open_writer();
    for (std::uint64_t seq = 1; seq <= 5; ++seq)
    {
        REQUIRE(first.m_Writer->append(record_at(seq)).has_value());
    }
    Writer second = open_writer(first.m_File->durable());
    CHECK(second.m_Writer->last_seq() == 5);
    CHECK(second.m_Writer->append(record_at(5)).has_value() == false);
    REQUIRE(second.m_Writer->append(record_at(6)).has_value());
    CHECK(scan(second.m_File->durable()).m_Records.size() == 6);
}

TEST_CASE("The writer will not append behind a journal that needs recovery", "[journal-writer]")
{
    Writer clean = open_writer();
    for (std::uint64_t seq = 1; seq <= 3; ++seq)
    {
        REQUIRE(clean.m_Writer->append(record_at(seq)).has_value());
    }
    const std::string good = clean.m_File->durable();

    const auto refused = [&](std::string content, std::uint64_t log = LOG)
    {
        auto opened = JournalWriter::open(std::make_unique<LossyFile>(std::move(content)), sample_id<LogId>(log));
        REQUIRE_FALSE(opened.has_value());
        return opened.error();
    };
    CHECK(refused(good + "{\"v\":1,\"seq\":4").m_Kind == StorageErrorKind::JOURNAL);    // a torn tail
    CHECK(refused(good + std::string(100, '\0')).m_Kind == StorageErrorKind::JOURNAL);  // a zero-filled tail
    CHECK(refused(good + "garbage\n").m_Kind == StorageErrorKind::JOURNAL);             // a bad line
    CHECK(refused(good + encode_record(record_at(5)).value() + "\n").m_Kind == StorageErrorKind::JOURNAL);  // a gap
    CHECK(refused(good, LOG + 1).m_Kind == StorageErrorKind::JOURNAL);  // another log's journal
}

TEST_CASE("A failed write or sync leaves the file as it was, so no half line stays in front of the next record",
          "[journal-writer]")
{
    Writer w = open_writer();
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    const std::string before = w.m_File->durable();

    SECTION("the append fails")
    {
        w.m_File->m_Fault = [](LossyFile::Op op) -> std::optional<StorageError>
        {
            return op == LossyFile::Op::APPEND ? std::optional(make_error(StorageErrorKind::DISK_FULL, "full"))
                                               : std::nullopt;
        };
        CHECK(w.m_Writer->append(record_at(2)).error().m_Kind == StorageErrorKind::DISK_FULL);
    }
    SECTION("the sync fails")
    {
        w.m_File->m_Fault = [first = true](LossyFile::Op op) mutable -> std::optional<StorageError>
        {
            if (op == LossyFile::Op::SYNC && first)
            {
                first = false;
                return make_error(StorageErrorKind::IO, "sync failed");
            }
            return std::nullopt;
        };
        CHECK(w.m_Writer->append(record_at(2)).error().m_Kind == StorageErrorKind::IO);
    }
    CHECK(w.m_File->durable() == before);
    CHECK(w.m_File->pending().empty());
    CHECK(w.m_Writer->last_seq() == 1);

    w.m_File->m_Fault = nullptr;
    REQUIRE(w.m_Writer->append(record_at(2)).has_value());  // the number was not used up
    CHECK(scan(w.m_File->durable()).m_Records.size() == 2);
}

TEST_CASE("Crash points between the append and the sync, and after the sync", "[journal-writer]")
{
    std::vector<CrashPoint> seen;
    Writer w = open_writer({},
                           [&seen](CrashPoint point) -> Result<void, StorageError>
                           {
                               seen.push_back(point);
                               return {};
                           });
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    CHECK(seen == std::vector<CrashPoint>{CrashPoint::AFTER_JOURNAL_APPEND, CrashPoint::AFTER_JOURNAL_SYNC});
}

TEST_CASE("A hook that fails at either crash point undoes the append", "[journal-writer]")
{
    for (const CrashPoint failing : {CrashPoint::AFTER_JOURNAL_APPEND, CrashPoint::AFTER_JOURNAL_SYNC})
    {
        Writer w = open_writer({},
                               [failing](CrashPoint point) -> Result<void, StorageError>
                               {
                                   if (point == failing)
                                   {
                                       return fail(make_error(StorageErrorKind::JOURNAL, "injected"));
                                   }
                                   return {};
                               });
        CHECK_FALSE(w.m_Writer->append(record_at(1)).has_value());
        CHECK(w.m_File->size() == 0);
        CHECK(w.m_Writer->last_seq() == 0);
    }
}

TEST_CASE("Cancelling a record writes a void record that names it", "[journal-writer]")
{
    Writer w = open_writer();
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    const auto mark = w.m_Writer->append(record_at(2));
    REQUIRE(mark.has_value());
    REQUIRE(w.m_Writer->cancel(*mark).has_value());
    CHECK(w.m_Writer->last_seq() == 3);

    const JournalScan result = scan(w.m_File->durable());
    REQUIRE(result.m_Records.size() == 3);
    CHECK(std::get<VoidBody>(result.m_Records[2].m_Body).m_Target == 2);
    CHECK(resolve_voids(result.m_Records).m_Effective.size() == 1);

    REQUIRE(w.m_Writer->append(record_at(4)).has_value());  // the next write goes on from there
    CHECK(scan(w.m_File->durable()).m_Records.size() == 4);
}

TEST_CASE("If the void record cannot be written, the journal is cut back to before the record", "[journal-writer]")
{
    Writer w = open_writer();
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    const std::string before = w.m_File->durable();
    const auto mark = w.m_Writer->append(record_at(2));
    REQUIRE(mark.has_value());

    int appends_seen = 0;
    w.m_File->m_Fault = [&appends_seen](LossyFile::Op op) -> std::optional<StorageError>
    {
        if (op == LossyFile::Op::APPEND && ++appends_seen == 1)  // the void record
        {
            return make_error(StorageErrorKind::DISK_FULL, "full");
        }
        return std::nullopt;
    };
    REQUIRE(w.m_Writer->cancel(*mark).has_value());
    CHECK(w.m_File->durable() == before);
    CHECK(w.m_Writer->last_seq() == 1);
    REQUIRE(w.m_Writer->append(record_at(2)).has_value());  // and the number is free again
}

TEST_CASE("If neither the void record nor the cut works, the error says so", "[journal-writer]")
{
    Writer w = open_writer();
    REQUIRE(w.m_Writer->append(record_at(1)).has_value());
    const auto mark = w.m_Writer->append(record_at(2));
    REQUIRE(mark.has_value());
    w.m_File->m_Fault = [](LossyFile::Op op) -> std::optional<StorageError>
    { return op == LossyFile::Op::SYNC ? std::optional(make_error(StorageErrorKind::IO, "dead disk")) : std::nullopt; };
    CHECK_FALSE(w.m_Writer->cancel(*mark).has_value());
}

TEST_CASE("Only the last record can be cut back", "[journal-writer]")
{
    Writer w = open_writer();
    const auto first = w.m_Writer->append(record_at(1));
    REQUIRE(first.has_value());
    REQUIRE(w.m_Writer->append(record_at(2)).has_value());
    w.m_File->m_Fault = [](LossyFile::Op op) -> std::optional<StorageError>
    {
        return op == LossyFile::Op::APPEND ? std::optional(make_error(StorageErrorKind::DISK_FULL, "full"))
                                           : std::nullopt;
    };
    CHECK(w.m_Writer->cancel(*first).error().m_Kind == StorageErrorKind::JOURNAL);
}

TEST_CASE("Power lost at any point of any append leaves every earlier record and at most one torn one",
          "[journal-writer][power-loss]")
{
    constexpr int SEEDS = 2000;
    int torn_tails = 0;
    int complete_in_flight = 0;
    for (int seed = 0; seed < SEEDS; ++seed)
    {
        std::mt19937_64 rng(static_cast<std::uint64_t>(seed) * 7919U + 1U);
        const std::uint64_t records = 2 + rng() % 40;
        const std::uint64_t in_flight = 1 + rng() % records;  // the record whose append loses power
        const bool after_sync = rng() % 4 == 0;               // lost power after the sync but before the ack

        std::string image;
        bool crashed = false;
        LossyFile* file = nullptr;
        auto lossy = std::make_unique<LossyFile>();
        file = lossy.get();
        std::uint64_t current = 0;
        auto opened = JournalWriter::open(
            std::move(lossy), sample_id<LogId>(LOG),
            [&](CrashPoint point) -> Result<void, StorageError>
            {
                if (current == in_flight && !crashed &&
                    point == (after_sync ? CrashPoint::AFTER_JOURNAL_SYNC : CrashPoint::AFTER_JOURNAL_APPEND))
                {
                    image = file->durable().size() == file->size() ? file->durable() : file->after_power_loss(rng);
                    crashed = true;
                    return fail(make_error(StorageErrorKind::JOURNAL, "power lost"));
                }
                return {};
            });
        REQUIRE(opened.has_value());
        for (current = 1; current <= records && !crashed; ++current)
        {
            (void)(*opened)->append(record_at(current));
        }
        REQUIRE(crashed);

        const JournalScan result = scan(image);
        INFO("seed " << seed << " in flight " << in_flight << " after_sync " << after_sync);
        // Everything acknowledged before the one in flight is there, in order, whole.
        REQUIRE(result.m_Records.size() >= in_flight - 1);
        REQUIRE(result.m_Records.size() <= in_flight);
        for (std::size_t i = 0; i < result.m_Records.size(); ++i)
        {
            CHECK(result.m_Records[i].m_Seq == i + 1);
        }
        // Nothing but a torn tail is wrong with the file: no bad line, no gap.
        CHECK(result.m_BadRecords == 0);
        CHECK_FALSE(result.m_HasProblem);
        if (result.m_Records.size() == in_flight)
        {
            ++complete_in_flight;
        }
        else if (result.m_TornTailBytes > 0)
        {
            ++torn_tails;  // else nothing of the record reached the disk, and the file simply ends before it
        }
    }
    CHECK(torn_tails > 100);          // the generator really does cut records
    CHECK(complete_in_flight > 100);  // and really does keep them whole
}
