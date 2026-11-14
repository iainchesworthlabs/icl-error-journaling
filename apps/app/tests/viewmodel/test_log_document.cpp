// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/document_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::DocumentError;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using Origin = LogChange::Origin;

namespace
{

EntryId entry_id(std::uint64_t number)
{
    return sample_id<EntryId>(number);
}

/// Notes what the document looked like at the moment the observer was told.
class ProbeObserver final : public viewmodel::LogObserver
{
public:
    void on_log_changed(const LogDocument& document, const LogChange& change) override
    {
        m_EntryCounts.push_back(document.log().entries().size());
        m_Kinds.push_back(change.m_Kind);
        m_Origins.push_back(change.m_Origin);
        m_Entries.push_back(change.m_Entry);
        m_LastShared.push_back(document.last_shared());
    }

    std::vector<std::size_t> m_EntryCounts;
    std::vector<LogChange::Kind> m_Kinds;
    std::vector<Origin> m_Origins;
    std::vector<std::optional<EntryId>> m_Entries;
    std::vector<std::optional<UtcMillis>> m_LastShared;
};

/// True when the mirror holds what the store holds, entry for entry.
bool mirror_matches_store(DocumentRig& rig)
{
    const StoredLog stored = rig.stored();
    const Log& mirror = rig.document->log();
    if (mirror.entries().size() != stored.m_Log.entries().size() ||
        rig.document->last_shared() != stored.m_Meta.m_LastSharedUtc ||
        rig.document->archived() != stored.m_Meta.m_Archived)
    {
        return false;
    }
    for (std::size_t i = 0; i < mirror.entries().size(); ++i)
    {
        const Entry& a = mirror.entries()[i];
        const Entry& b = stored.m_Log.entries()[i];
        if (a.m_Id != b.m_Id || a.m_Deleted != b.m_Deleted || a.m_EditedUtc != b.m_EditedUtc ||
            a.m_Captured.m_Utc != b.m_Captured.m_Utc || a.m_Body.index() != b.m_Body.index())
        {
            return false;
        }
    }
    return header_field_text(mirror.initial_header(), HeaderField::UNIT_NUMBER) ==
               header_field_text(stored.m_Log.initial_header(), HeaderField::UNIT_NUMBER) &&
           header_field_text(mirror.initial_header(), HeaderField::SOFTWARE_VERSION) ==
               header_field_text(stored.m_Log.initial_header(), HeaderField::SOFTWARE_VERSION);
}

} // namespace

// ------------------------------------------------------------------------------------------- the mirror and the order

TEST_CASE("The mirror changes only after the commit, and the observers follow the mirror", "[viewmodel][document]")
{
    DocumentRig rig;
    ProbeObserver probe;
    rig.document->add_observer(probe);
    int done = 0;

    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.count(done)));

    SECTION("submitted: nothing has changed")
    {
        CHECK(rig.document->log().entries().empty());
        CHECK(rig.document->pending() == 1);
        CHECK(rig.storage().repository().mutating_call_count() == 0);
        CHECK(probe.m_Kinds.empty());
        CHECK(rig.observer.changes().empty());
        CHECK(done == 0);
    }

    SECTION("committed: the mirror, then the observers, then done")
    {
        rig.gateway().run_next();

        CHECK(rig.document->log().entries().size() == 1);
        CHECK(rig.document->pending() == 0);
        REQUIRE(probe.m_Kinds.size() == 1);
        CHECK(probe.m_Kinds[0] == LogChange::Kind::ENTRY_ADDED);
        CHECK(probe.m_EntryCounts[0] == 1); // the observer saw the mirror already changed
        CHECK(probe.m_Entries[0] == entry_id(1));
        CHECK(done == 1);

        const Event commit{EventKind::COMMIT, "add_entry " + entry_id(1).format()};
        const Event told{EventKind::LOG_CHANGE, "ENTRY_ADDED " + entry_id(1).format()};
        CHECK(rig.events().occurred_before(commit, told));
    }

    rig.document->remove_observer(probe);
}

TEST_CASE("Observers are told in the order they were added, and one removed is not told", "[viewmodel][document]")
{
    DocumentRig rig;
    FakeLogObserver second(&rig.events());
    ProbeObserver third;
    rig.document->add_observer(second);
    rig.document->add_observer(third);
    rig.document->remove_observer(second);

    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, {}));
    rig.gateway().run_all();

    CHECK(rig.observer.changes().size() == 1);
    CHECK(second.changes().empty());
    CHECK(third.m_Kinds.size() == 1);
    rig.document->remove_observer(third);
}

TEST_CASE("A failed commit changes nothing and tells no observer", "[viewmodel][document]")
{
    DocumentRig rig;
    ProbeObserver probe;
    rig.document->add_observer(probe);
    std::vector<Result<void, StorageError>> results;
    rig.storage().repository().fail_next(RepositoryOp::ADD_ENTRY);

    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.keep(results)));
    rig.gateway().run_all();

    CHECK(rig.document->log().entries().empty());
    CHECK(probe.m_Kinds.empty());
    CHECK(rig.observer.changes().empty());
    REQUIRE(results.size() == 1);
    REQUIRE_FALSE(results[0].has_value());
    CHECK(results[0].error().m_Kind == StorageErrorKind::IO);
    CHECK(rig.document->pending() == 0);

    // The document is usable afterwards.
    REQUIRE(rig.document->add_entry(sample_error(2, 11), Origin::USER, rig.keep(results)));
    rig.gateway().run_all();
    CHECK(rig.document->log().entries().size() == 1);
    REQUIRE(results.size() == 2);
    CHECK(results[1].has_value());
    CHECK(mirror_matches_store(rig));

    rig.document->remove_observer(probe);
}

TEST_CASE("Commands commit in submission order, one job at a time", "[viewmodel][document]")
{
    DocumentRig rig;
    std::vector<int> order;
    for (int i = 1; i <= 4; ++i)
    {
        REQUIRE(rig.document->add_entry(sample_error(static_cast<std::uint64_t>(i), 10 + i), Origin::USER,
                                        [&order, i](const Result<void, StorageError>&) { order.push_back(i); }));
    }

    // One in flight, three waiting: the gateway has one job, not four.
    CHECK(rig.gateway().pending() == 1);
    CHECK(rig.document->waiting() == 3);
    CHECK(rig.document->pending() == 4);

    rig.gateway().run_all();

    CHECK(order == std::vector<int>{1, 2, 3, 4});
    CHECK(rig.gateway().posted() == 4);
    CHECK(rig.events().details(EventKind::COMMIT) ==
          std::vector<std::string>{"add_entry " + entry_id(1).format(), "add_entry " + entry_id(2).format(),
                                   "add_entry " + entry_id(3).format(), "add_entry " + entry_id(4).format()});
    CHECK(rig.document->log().entries().size() == 4);
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("A command that fails its second check reports and does not block the queue", "[viewmodel][document]")
{
    DocumentRig rig;
    std::vector<Result<void, StorageError>> first;
    std::vector<Result<void, StorageError>> second;
    std::vector<Result<void, StorageError>> third;

    // Both are valid against the mirror as it is when they are submitted (1.4.2). After the first has committed the
    // second would change nothing, and it is refused when it reaches the front.
    REQUIRE(rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", {sample_time(100), "UTC"}, entry_id(1),
                                        rig.keep(first)));
    REQUIRE(rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", {sample_time(200), "UTC"}, entry_id(2),
                                        rig.keep(second)));
    REQUIRE(rig.document->add_entry(sample_error(3, 300), Origin::USER, rig.keep(third)));

    rig.gateway().run_all();

    REQUIRE(first.size() == 1);
    CHECK(first[0].has_value());
    REQUIRE(second.size() == 1);
    REQUIRE_FALSE(second[0].has_value());
    CHECK(second[0].error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);
    REQUIRE(third.size() == 1);
    CHECK(third[0].has_value());

    // Two jobs reached the store; the refused command was never queued there.
    CHECK(rig.gateway().posted() == 2);
    CHECK(rig.document->log().entries().size() == 2);
    CHECK(rig.document->log().find(entry_id(2)) == nullptr);
    CHECK(rig.observer.changes().size() == 2);
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("At most 64 commands wait; the next is refused and nothing is dropped", "[viewmodel][document]")
{
    DocumentRig rig;
    int done = 0;
    std::uint64_t next = 1;

    // The first goes in flight at once, so 65 are accepted: one running and 64 waiting.
    for (std::size_t i = 0; i <= LogDocument::MAX_WAITING; ++i, ++next)
    {
        REQUIRE(rig.document->add_entry(sample_error(next, 10), Origin::USER, rig.count(done)));
    }
    CHECK(rig.document->waiting() == LogDocument::MAX_WAITING);

    const auto refused = rig.document->add_entry(sample_error(next, 10), Origin::USER, rig.count(done));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().m_Code == DocumentError::Code::QUEUE_FULL);
    CHECK_FALSE(refused.error().m_Message.empty());
    CHECK(rig.document->pending() == LogDocument::MAX_WAITING + 1);

    rig.gateway().run_all();
    CHECK(done == 65);
    CHECK(rig.document->log().entries().size() == 65);

    // Room again.
    CHECK(rig.document->add_entry(sample_error(next, 10), Origin::USER, rig.count(done)).has_value());
}

TEST_CASE("Done runs exactly once for each accepted command, whatever became of it", "[viewmodel][document]")
{
    DocumentRig rig;
    int store_failed = 0;
    int committed = 0;
    int refused_at_front = 0;
    int deleted = 0;
    rig.storage().repository().fail_next(RepositoryOp::ADD_ENTRY);

    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.count(store_failed)));
    REQUIRE(rig.document->add_entry(sample_error(2, 11), Origin::USER, rig.count(committed)));
    rig.gateway().run_all();

    REQUIRE(rig.document->set_deleted(entry_id(2), true, Origin::USER, rig.count(deleted)));
    REQUIRE(rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", {sample_time(50), "UTC"}, entry_id(3),
                                        rig.count(committed)));
    REQUIRE(rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", {sample_time(60), "UTC"}, entry_id(4),
                                        rig.count(refused_at_front)));
    rig.gateway().run_all();

    CHECK(store_failed == 1);
    CHECK(committed == 2);
    CHECK(deleted == 1);
    CHECK(refused_at_front == 1);
    CHECK(rig.document->pending() == 0);
}

TEST_CASE("A Done may submit the next command; it joins the back of the queue", "[viewmodel][document]")
{
    DocumentRig rig;
    std::vector<int> order;

    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER,
                                    [&](const Result<void, StorageError>&)
                                    {
                                        order.push_back(1);
                                        REQUIRE(rig.document->add_entry(sample_error(3, 12), Origin::USER,
                                                                        [&](const Result<void, StorageError>&)
                                                                        { order.push_back(3); }));
                                    }));
    REQUIRE(rig.document->add_entry(sample_error(2, 11), Origin::USER,
                                    [&](const Result<void, StorageError>&) { order.push_back(2); }));

    rig.gateway().run_all();

    CHECK(order == std::vector<int>{1, 2, 3});
    CHECK(rig.document->log().entries().size() == 3);
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("A document destroyed with jobs in flight does not call back", "[viewmodel][document]")
{
    DocumentRig rig;
    int done = 0;
    for (std::uint64_t i = 1; i <= 3; ++i)
    {
        REQUIRE(rig.document->add_entry(sample_error(i, 10), Origin::USER, rig.count(done)));
    }
    REQUIRE(rig.gateway().pending() == 1);

    rig.document->remove_observer(rig.observer);
    rig.document.reset();
    rig.gateway().run_all(); // the job in flight finishes in the store; its completion finds nobody to call

    CHECK(done == 0);
    CHECK(rig.observer.changes().empty());
    CHECK(rig.storage().repository().mutating_call_count() == 1); // the two that waited were never sent
}

TEST_CASE("A document destroyed by its own Done stops cleanly", "[viewmodel][document]")
{
    DocumentRig rig;
    int done = 0;
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER,
                                    [&](const Result<void, StorageError>&)
                                    {
                                        ++done;
                                        rig.document->remove_observer(rig.observer);
                                        rig.document.reset();
                                    }));
    REQUIRE(rig.document->add_entry(sample_error(2, 11), Origin::USER, rig.count(done)));

    rig.gateway().run_all();

    CHECK(done == 1);
    CHECK(rig.storage().repository().mutating_call_count() == 1);
}

// ----------------------------------------------------------------------------------------- loading and replacing

TEST_CASE("LOADED and REPLACED are one notification each, however many entries the log has", "[viewmodel][document]")
{
    DocumentRig rig(false);
    for (std::uint64_t i = 1; i <= 50; ++i)
    {
        REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(i, static_cast<std::int64_t>(i))));
    }
    LogDocument document(rig.world().environment(), rig.gateway(), rig.log_id());
    FakeLogObserver observer;
    document.add_observer(observer);

    SECTION("a document made without a log refuses commands")
    {
        CHECK_FALSE(document.ready());
        CHECK(document.id() == rig.log_id());
        CHECK(document.log().entries().empty());

        const auto refused = document.add_entry(sample_error(99, 1), Origin::USER, {});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().m_Code == DocumentError::Code::NOT_READY);
        CHECK(rig.gateway().posted() == 0);
    }

    SECTION("load")
    {
        document.load(rig.stored());

        CHECK(document.ready());
        CHECK(document.log().entries().size() == 50);
        REQUIRE(observer.changes().size() == 1);
        CHECK(observer.changes()[0].m_Kind == LogChange::Kind::LOADED);
        CHECK_FALSE(observer.changes()[0].m_Entry.has_value());

        // The log is the next day's: 30 entries, one notification.
        const LogId next_log = sample_id<LogId>(2);
        REQUIRE(rig.storage().memory().start_new_log(rig.log_id(), next_log, sample_header()));
        for (std::uint64_t i = 1; i <= 30; ++i)
        {
            REQUIRE(rig.storage().memory().add_entry(next_log, sample_error(100 + i, static_cast<std::int64_t>(i))));
        }
        auto replacement = rig.storage().memory().load_log(next_log);
        REQUIRE(replacement.has_value());
        document.replace(std::move(*replacement));

        CHECK(document.id() == next_log);
        CHECK(document.log().entries().size() == 30);
        REQUIRE(observer.changes().size() == 2);
        CHECK(observer.changes()[1].m_Kind == LogChange::Kind::REPLACED);
    }

    document.remove_observer(observer);
}

TEST_CASE("Replacing the log fails the commands that waited and ignores the one in flight", "[viewmodel][document]")
{
    DocumentRig rig;
    std::vector<Result<void, StorageError>> results;
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.keep(results)));
    REQUIRE(rig.document->add_entry(sample_error(2, 11), Origin::USER, rig.keep(results)));
    REQUIRE(rig.document->add_entry(sample_error(3, 12), Origin::USER, rig.keep(results)));

    const LogId next_log = sample_id<LogId>(2);
    REQUIRE(rig.storage().memory().start_new_log(rig.log_id(), next_log, sample_header()));
    auto replacement = rig.storage().memory().load_log(next_log);
    REQUIRE(replacement.has_value());
    rig.document->replace(std::move(*replacement));

    // The two that waited are reported at once, and nothing of theirs is written.
    REQUIRE(results.size() == 2);
    CHECK(results[0].error().m_Kind == StorageErrorKind::NOT_OPEN_LOG);
    CHECK(results[1].error().m_Kind == StorageErrorKind::NOT_OPEN_LOG);
    CHECK(rig.document->waiting() == 0);

    rig.gateway().run_all();

    // The one in flight was for the old log: it is reported (the store has archived that log, so it may refuse the
    // entry), and the mirror, which is the new log, did not change either way.
    REQUIRE(results.size() == 3);
    CHECK(rig.document->id() == next_log);
    CHECK(rig.document->log().entries().empty());
    REQUIRE(rig.observer.changes().size() == 1);
    CHECK(rig.observer.changes()[0].m_Kind == LogChange::Kind::REPLACED);
    CHECK(rig.document->pending() == 0);
}

// ----------------------------------------------------------------------------------------------- validation, first
// check

TEST_CASE("A command the mirror refuses is answered at once and queues nothing", "[viewmodel][document]")
{
    DocumentRig rig;
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, {}));
    rig.gateway().run_all();
    const auto posted = rig.gateway().posted();

    SECTION("an unknown entry")
    {
        const auto deleted = rig.document->set_deleted(entry_id(9), true, Origin::USER, {});
        REQUIRE_FALSE(deleted.has_value());
        CHECK(deleted.error().m_Code == DocumentError::Code::REJECTED);
        CHECK(deleted.error().m_Log == Log::Error::UNKNOWN_ENTRY);

        const auto edited = rig.document->edit_entry(entry_id(9), sample_error(9, 10), {});
        REQUIRE_FALSE(edited.has_value());
        CHECK(edited.error().m_Log == Log::Error::UNKNOWN_ENTRY);
    }

    SECTION("an id that is in the log already")
    {
        const auto added = rig.document->add_entry(sample_error(1, 12), Origin::USER, {});
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().m_Code == DocumentError::Code::REJECTED);
        CHECK(added.error().m_Log == Log::Error::DUPLICATE_ID);
    }

    SECTION("an error entry without the name of its type")
    {
        const auto added = rig.document->add_entry(sample_error(2, 12, ""), Origin::USER, {});
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().m_Code == DocumentError::Code::REJECTED);
        CHECK_FALSE(added.error().m_Message.empty());
    }

    SECTION("a header change made through add_entry")
    {
        const auto added =
            rig.document->add_entry(sample_change(2, 12, HeaderField::SOFTWARE_VERSION, "1.5.0"), Origin::USER, {});
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().m_Code == DocumentError::Code::REJECTED);
    }

    SECTION("a header value the core refuses")
    {
        const auto invalid = rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.4", {sample_time(20), "UTC"},
                                                         entry_id(2), {});
        REQUIRE_FALSE(invalid.has_value());
        REQUIRE(invalid.error().m_Header.has_value());
        CHECK(invalid.error().m_Header->m_Kind == HeaderError::Kind::INVALID_SOFTWARE_VERSION);

        const auto same = rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.4.2", {sample_time(20), "UTC"},
                                                      entry_id(2), {});
        REQUIRE_FALSE(same.has_value());
        REQUIRE(same.error().m_Header.has_value());
        CHECK(same.error().m_Header->m_Kind == HeaderError::Kind::UNCHANGED);

        const auto unit = rig.document->correct_header(HeaderField::UNIT_NUMBER, "abc", {});
        REQUIRE_FALSE(unit.has_value());
        REQUIRE(unit.error().m_Header.has_value());
        CHECK(unit.error().m_Header->m_Kind == HeaderError::Kind::INVALID_UNIT_NUMBER);
    }

    SECTION("a correction of an entry that is not a header change")
    {
        const auto corrected = rig.document->correct_header_change(entry_id(1), "1.5.0", {});
        REQUIRE_FALSE(corrected.has_value());
        REQUIRE(corrected.error().m_Header.has_value());
        CHECK(corrected.error().m_Header->m_Kind == HeaderError::Kind::NOT_A_HEADER_CHANGE);
    }

    CHECK(rig.gateway().posted() == posted);
    CHECK(rig.document->pending() == 0);
    CHECK(rig.document->log().entries().size() == 1);
}

// ------------------------------------------------------------------------------------------------------------ commands

TEST_CASE("A delete and a restore name their kind, and the origin reaches the observers", "[viewmodel][document]")
{
    DocumentRig rig;
    ProbeObserver probe;
    rig.document->add_observer(probe);
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, {}));
    rig.gateway().run_all();
    const ErrorTypeId type = sample_id<ErrorTypeId>(1);
    CHECK(rig.document->log().count_for(type) == 1);

    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::UNDO, {}));
    rig.gateway().run_all();
    CHECK(rig.document->log().count_for(type) == 0);
    CHECK(rig.document->log().find(entry_id(1))->m_Deleted);

    REQUIRE(rig.document->set_deleted(entry_id(1), false, Origin::USER, {}));
    rig.gateway().run_all();
    CHECK(rig.document->log().count_for(type) == 1);

    REQUIRE(probe.m_Kinds.size() == 3);
    CHECK(probe.m_Kinds[1] == LogChange::Kind::ENTRY_DELETED);
    CHECK(probe.m_Origins[1] == Origin::UNDO);
    CHECK(probe.m_Kinds[2] == LogChange::Kind::ENTRY_RESTORED);
    CHECK(probe.m_Origins[2] == Origin::USER);
    CHECK(probe.m_Entries[2] == entry_id(1));
    CHECK(mirror_matches_store(rig));
    rig.document->remove_observer(probe);
}

TEST_CASE("An edit takes the time of the call, not of the commit, and keeps the entry's place", "[viewmodel][document]")
{
    DocumentRig rig;
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, {}));
    rig.gateway().run_all();
    const UtcMillis asked = rig.world().m_Clock.now();

    REQUIRE(
        rig.document->edit_entry(entry_id(1), sample_error(77, 10, "Wrong Response", "asked for Evening Song"), {}));
    rig.world().m_Clock.advance(std::chrono::seconds(30)); // the commit is late
    rig.gateway().run_all();

    const Entry* edited = rig.document->log().find(entry_id(1));
    REQUIRE(edited != nullptr);
    CHECK(edited->m_EditedUtc == asked);
    const auto* body = std::get_if<ErrorEntry>(&edited->m_Body);
    REQUIRE(body != nullptr);
    CHECK(body->m_AttachedNote == std::optional<std::string>("asked for Evening Song"));
    CHECK(rig.document->log().find(entry_id(77)) == nullptr); // the id of the argument is ignored
    CHECK(rig.document->log().entries().size() == 1);
    REQUIRE(rig.observer.changes().size() == 2);
    CHECK(rig.observer.changes()[1].m_Kind == LogChange::Kind::ENTRY_EDITED);
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("Correct fixes a header value and writes no entry", "[viewmodel][document]")
{
    DocumentRig rig;

    REQUIRE(rig.document->correct_header(HeaderField::UNIT_NUMBER, "28", {}));
    rig.gateway().run_all();
    REQUIRE(rig.document->correct_header(HeaderField::UNIT_NUMBER, "27", {}));
    rig.gateway().run_all();

    CHECK(header_field_text(rig.document->log().initial_header(), HeaderField::UNIT_NUMBER) ==
          std::optional<std::string>("27"));
    CHECK(rig.document->log().entries().empty());
    REQUIRE(rig.observer.changes().size() == 2);
    CHECK(rig.observer.changes()[0].m_Kind == LogChange::Kind::HEADER_CORRECTED);
    CHECK_FALSE(rig.observer.changes()[0].m_Entry.has_value());
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("Change records an entry, and correcting that entry flows through the mirror", "[viewmodel][document]")
{
    DocumentRig rig;
    const Timestamp when{sample_time(100), "Australia/Melbourne"};

    REQUIRE(rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.1", when, entry_id(5), {}));
    rig.gateway().run_all();

    REQUIRE(rig.observer.changes().size() == 1);
    CHECK(rig.observer.changes()[0].m_Kind == LogChange::Kind::HEADER_CHANGED);
    CHECK(rig.observer.changes()[0].m_Entry == entry_id(5));
    const Entry* change = rig.document->log().find(entry_id(5));
    REQUIRE(change != nullptr);
    CHECK(std::get<HeaderChangeEntry>(change->m_Body).m_NewValue == "1.5.1");
    CHECK_FALSE(change->m_EditedUtc.has_value());

    SECTION("a correction to the typo's fix")
    {
        const UtcMillis asked = rig.world().m_Clock.now();
        REQUIRE(rig.document->correct_header_change(entry_id(5), "1.5.0", {}));
        rig.gateway().run_all();

        change = rig.document->log().find(entry_id(5));
        REQUIRE(change != nullptr);
        CHECK(std::get<HeaderChangeEntry>(change->m_Body).m_NewValue == "1.5.0");
        CHECK(change->m_EditedUtc == asked); // the store has no operation for it but an edit
        REQUIRE(rig.observer.changes().size() == 2);
        CHECK(rig.observer.changes()[1].m_Kind == LogChange::Kind::HEADER_CORRECTED);
        CHECK(rig.observer.changes()[1].m_Entry == entry_id(5));
        CHECK(rig.document->log().entries().size() == 1);
        CHECK(mirror_matches_store(rig));
    }

    SECTION("a correction to the value before the change is refused")
    {
        const auto refused = rig.document->correct_header_change(entry_id(5), "1.4.2", {});
        REQUIRE_FALSE(refused.has_value());
        REQUIRE(refused.error().m_Header.has_value());
        CHECK(refused.error().m_Header->m_Kind == HeaderError::Kind::UNCHANGED);
    }
}

TEST_CASE("Sharing is recorded after the commit, on an open log and on an archived one", "[viewmodel][document]")
{
    DocumentRig rig;
    ProbeObserver probe;
    rig.document->add_observer(probe);
    const UtcMillis shared = rig.world().m_Clock.now() + std::chrono::seconds(20);

    REQUIRE(rig.document->mark_shared(shared, {}));
    CHECK_FALSE(rig.document->last_shared().has_value());
    rig.gateway().run_all();

    CHECK(rig.document->last_shared() == shared);
    REQUIRE(probe.m_Kinds.size() == 1);
    CHECK(probe.m_Kinds[0] == LogChange::Kind::SHARED);
    CHECK(probe.m_LastShared[0] == shared); // the mirror was changed before the observer was told
    CHECK(mirror_matches_store(rig));
    rig.document->remove_observer(probe);
}

// ----------------------------------------------------------------------------------------------------- archived logs

TEST_CASE("Edits, deletes and shares of an archived log are journalled; new entries are not accepted",
          "[viewmodel][document][archived]")
{
    DocumentRig rig(false);
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(1, 10)));
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(2, 11)));
    REQUIRE(rig.storage().memory().start_new_log(rig.log_id(), sample_id<LogId>(2), sample_header()));
    rig.document = std::make_shared<LogDocument>(rig.world().environment(), rig.gateway(), rig.stored());
    rig.document->add_observer(rig.observer);
    REQUIRE(rig.document->archived());
    const std::size_t journalled = rig.storage().memory().records(rig.log_id()).size();

    const auto added = rig.document->add_entry(sample_error(3, 12), Origin::USER, {});
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().m_Code == DocumentError::Code::REJECTED);
    CHECK(added.error().m_Message.find("archived") != std::string::npos);
    CHECK_FALSE(
        rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", {sample_time(20), "UTC"}, entry_id(4), {})
            .has_value());

    const UtcMillis asked = rig.world().m_Clock.now();
    const UtcMillis shared = asked + std::chrono::minutes(5);
    REQUIRE(rig.document->edit_entry(entry_id(1), sample_error(1, 10, "Wrong Response", "asked for Evening Song"), {}));
    REQUIRE(rig.document->set_deleted(entry_id(2), true, Origin::USER, {}));
    REQUIRE(rig.document->mark_shared(shared, {}));
    rig.gateway().run_all();

    const auto& records = rig.storage().memory().records(rig.log_id());
    REQUIRE(records.size() == journalled + 3);
    CHECK(journal_op(records[journalled].m_Body) == JournalOp::EDIT_ENTRY);
    CHECK(journal_op(records[journalled + 1].m_Body) == JournalOp::SET_DELETED);
    CHECK(journal_op(records[journalled + 2].m_Body) == JournalOp::MARK_SHARED);

    CHECK(rig.document->log().find(entry_id(1))->m_EditedUtc == asked);
    CHECK(rig.document->log().find(entry_id(2))->m_Deleted);
    CHECK(rig.document->last_shared() == shared);
    CHECK(rig.document->archived());
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("A log archived while its document is open stops taking entries", "[viewmodel][document][archived]")
{
    DocumentRig rig;
    std::vector<Result<void, StorageError>> results;
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.keep(results)));
    // These are waiting when the new day starts and must not land in the old log.
    REQUIRE(rig.document->add_entry(sample_error(2, 11), Origin::USER, rig.keep(results)));
    REQUIRE(rig.document->add_entry(sample_error(3, 12), Origin::USER, rig.keep(results)));

    // The first has committed and the second is on its way to the store when the new day starts. The owner of the
    // new-day start calls set_archived when the write that archived the log has committed.
    rig.gateway().run_next();
    REQUIRE(rig.storage().memory().start_new_log(rig.log_id(), sample_id<LogId>(2), sample_header()));
    rig.document->set_archived();
    rig.gateway().run_all();

    CHECK(rig.document->archived());
    CHECK(rig.document->log().entries().size() == 1); // only the one that committed before the archive
    REQUIRE(results.size() == 3);
    CHECK(results[0].has_value());
    CHECK_FALSE(results[1].has_value()); // the store refused it: archiving is the boundary for new entries
    REQUIRE_FALSE(results[2].has_value());
    CHECK(results[2].error().m_Kind == StorageErrorKind::INVALID_ARGUMENT); // refused at the front, never sent
    CHECK(rig.observer.changes().size() == 1);
    CHECK(rig.gateway().posted() == 2);
}

TEST_CASE("edit.no_lost_edit: two edits submitted back to back both stay", "[viewmodel][document][edit]")
{
    DocumentRig rig;
    std::vector<Result<void, StorageError>> results;
    REQUIRE(rig.document->add_entry(sample_interaction_entry(1, 10, {}), Origin::USER, rig.keep(results)));
    rig.gateway().run_all();

    // Both are submitted before either has committed: each used to start from the entry as it was at submission, so the
    // second overwrote the first.
    const auto note = [](Entry& entry) -> std::optional<std::string>
    {
        std::get<InteractionEntry>(entry.m_Body).m_AttachedNote = "Asked about the schedule";
        return std::nullopt;
    };
    const auto people = [](Entry& entry) -> std::optional<std::string>
    {
        std::get<InteractionEntry>(entry.m_Body).m_People = {sample_id<PersonId>(7)};
        return std::nullopt;
    };
    REQUIRE(rig.document->edit_entry(entry_id(1), LogDocument::Mutator(note), rig.keep(results)));
    REQUIRE(rig.document->edit_entry(entry_id(1), LogDocument::Mutator(people), rig.keep(results)));
    rig.gateway().run_all();

    const Entry* stored = rig.document->log().find(entry_id(1));
    REQUIRE(stored != nullptr);
    const auto& body = std::get<InteractionEntry>(stored->m_Body);
    CHECK(body.m_AttachedNote == std::optional<std::string>("Asked about the schedule"));
    REQUIRE(body.m_People.size() == 1);
    CHECK(mirror_matches_store(rig));
}

TEST_CASE("edit.mutator_refusal: a refusal at the front of the queue reports and blocks nothing",
          "[viewmodel][document][edit]")
{
    DocumentRig rig;
    std::vector<Result<void, StorageError>> results;
    REQUIRE(rig.document->add_entry(sample_interaction_entry(1, 10, {}), Origin::USER, rig.keep(results)));
    rig.gateway().run_all();

    int calls = 0;
    // Agrees when it is submitted and refuses when it is reached (the entry has changed in between).
    const auto picky = [&calls](Entry& entry) -> std::optional<std::string>
    {
        ++calls;
        auto& body = std::get<InteractionEntry>(entry.m_Body);
        if (body.m_AttachedNote.has_value())
        {
            return "That entry has a note already.";
        }
        body.m_AttachedNote = "second";
        return std::nullopt;
    };
    std::vector<Result<void, StorageError>> first;
    std::vector<Result<void, StorageError>> second;
    REQUIRE(rig.document->edit_entry(entry_id(1), LogDocument::Mutator(picky), rig.keep(first)));
    REQUIRE(rig.document->edit_entry(entry_id(1), LogDocument::Mutator(picky), rig.keep(second)));
    rig.gateway().run_all();
    REQUIRE(first.size() == 1);
    CHECK(first[0].has_value());
    REQUIRE(second.size() == 1);
    CHECK_FALSE(second[0].has_value());
    CHECK(std::get<InteractionEntry>(rig.document->log().find(entry_id(1))->m_Body).m_AttachedNote ==
          std::optional<std::string>("second"));
}
