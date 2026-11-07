// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/document_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::DocumentRegistry;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using Origin = LogChange::Origin;

namespace
{

using Delivery = Result<std::shared_ptr<LogDocument>, StorageError>;

/// A store with an archived log (id 1, two entries) and the open log (id 2).
struct History
{
    explicit History(DocumentRig& rig) : m_Rig(rig)
    {
        REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(1, 10)));
        REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(2, 11)));
        REQUIRE(rig.storage().memory().start_new_log(rig.log_id(), open_id(), sample_header()));
    }

    static LogId open_id()
    {
        return sample_id<LogId>(2);
    }
    [[nodiscard]] LogId archived_id() const
    {
        return m_Rig.log_id();
    }

    DocumentRig& m_Rig;
};

viewmodel::DocumentRegistry::Loaded keep(std::vector<Delivery>& into)
{
    return [&into](Delivery delivery) { into.push_back(std::move(delivery)); };
}

} // namespace

TEST_CASE("The registry loads a past log through the gateway and returns the same document for the same id",
          "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    std::vector<Delivery> first;
    std::vector<Delivery> second;

    registry.acquire(history.archived_id(), keep(first));
    registry.acquire(history.archived_id(), keep(second)); // while the first load is in flight

    CHECK(first.empty());
    CHECK(registry.loading_count() == 1);
    CHECK(rig.gateway().pending() == 1); // one load answers both

    rig.gateway().run_all();

    REQUIRE(first.size() == 1);
    REQUIRE(second.size() == 1);
    REQUIRE(first[0].has_value());
    REQUIRE(second[0].has_value());
    CHECK(first[0]->get() == second[0]->get());
    CHECK(first[0].value()->id() == history.archived_id());
    CHECK(first[0].value()->log().entries().size() == 2);
    CHECK(first[0].value()->archived());
    CHECK(registry.loading_count() == 0);
    CHECK(rig.storage().repository().call_count(RepositoryOp::LOAD_LOG) == 1);

    // Later calls find the live document and ask the store for nothing; the answer comes before acquire returns.
    std::vector<Delivery> third;
    registry.acquire(history.archived_id(), keep(third));
    REQUIRE(third.size() == 1);
    CHECK(third[0]->get() == first[0]->get());
    CHECK(rig.storage().repository().call_count(RepositoryOp::LOAD_LOG) == 1);
    CHECK(registry.find(history.archived_id()) == *first[0]);
}

TEST_CASE("The registry reloads a log after the last owner lets go", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    std::vector<Delivery> loaded;

    registry.acquire(history.archived_id(), keep(loaded));
    rig.gateway().run_all();
    REQUIRE(loaded.size() == 1);
    std::shared_ptr<LogDocument> held = loaded[0].value();

    SECTION("while someone holds it, it is the same document")
    {
        loaded.clear(); // the delivery's own pointer
        CHECK(registry.live_count() == 1);
        CHECK(registry.find(history.archived_id()) == held);
    }

    SECTION("when the last owner lets go, the next acquire reads the store again")
    {
        // A change committed through the document is in the store, so the reload has it.
        REQUIRE(held->set_deleted(sample_id<EntryId>(1), true, Origin::USER, {}));
        rig.gateway().run_all();
        loaded.clear();
        held.reset();

        CHECK(registry.live_count() == 0);
        CHECK(registry.find(history.archived_id()) == nullptr);

        registry.acquire(history.archived_id(), keep(loaded));
        rig.gateway().run_all();

        REQUIRE(loaded.size() == 1);
        REQUIRE(loaded[0].has_value());
        CHECK(loaded[0].value()->log().find(sample_id<EntryId>(1))->m_Deleted);
        CHECK(rig.storage().repository().call_count(RepositoryOp::LOAD_LOG) == 2);
    }
}

TEST_CASE("The registry refuses an id the store does not have", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    std::vector<Delivery> first;
    std::vector<Delivery> second;

    registry.acquire(sample_id<LogId>(99), keep(first));
    registry.acquire(sample_id<LogId>(99), keep(second));
    rig.gateway().run_all();

    REQUIRE(first.size() == 1);
    REQUIRE(second.size() == 1);
    REQUIRE_FALSE(first[0].has_value());
    CHECK(first[0].error().m_Kind == StorageErrorKind::NOT_FOUND);
    CHECK(second[0].error().m_Kind == StorageErrorKind::NOT_FOUND);
    CHECK(registry.live_count() == 0);
    CHECK(registry.loading_count() == 0);
    CHECK(registry.find(sample_id<LogId>(99)) == nullptr);

    // The failed load is not remembered: the next call asks the store again.
    registry.acquire(sample_id<LogId>(99), keep(first));
    CHECK(rig.gateway().pending() == 1);
}

TEST_CASE("The open log is the registry's document too, with no load", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    auto open = rig.storage().memory().load_log(History::open_id());
    REQUIRE(open.has_value());

    const std::shared_ptr<LogDocument> document = registry.make_document(std::move(*open));

    CHECK(document->id() == History::open_id());
    CHECK(document->ready());
    CHECK(registry.find(History::open_id()) == document);

    std::vector<Delivery> delivered;
    registry.acquire(History::open_id(), keep(delivered));
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0]->get() == document.get());
    CHECK(rig.gateway().posted() == 0);

    // A second document for a log that is alive is not made; the first is returned.
    auto again = rig.storage().memory().load_log(History::open_id());
    REQUIRE(again.has_value());
    CHECK(registry.make_document(std::move(*again)) == document);
    CHECK(registry.live_count() == 1);
}

TEST_CASE("A document made while its log is loading wins, and everyone gets that one", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    std::vector<Delivery> delivered;

    registry.acquire(History::open_id(), keep(delivered));
    auto open = rig.storage().memory().load_log(History::open_id());
    REQUIRE(open.has_value());
    const std::shared_ptr<LogDocument> document = registry.make_document(std::move(*open));
    rig.gateway().run_all();

    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0]->get() == document.get());
    CHECK(registry.live_count() == 1);
}

TEST_CASE("Two views of one log are one document, so edits do not overwrite each other", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    std::vector<Delivery> history_view;
    std::vector<Delivery> share_view;
    registry.acquire(history.archived_id(), keep(history_view));
    registry.acquire(history.archived_id(), keep(share_view));
    rig.gateway().run_all();
    const auto history_document = history_view[0].value();
    const auto share_document = share_view[0].value();
    FakeLogObserver history_observer;
    history_document->add_observer(history_observer);

    // Review edits entry 1; Share records the share time. Each sees the other's change.
    REQUIRE(history_document->edit_entry(sample_id<EntryId>(1), sample_error(1, 10, "Wrong Response", "asked"), {}));
    REQUIRE(share_document->mark_shared(rig.world().m_Clock.now(), {}));
    rig.gateway().run_all();

    CHECK(history_document->last_shared().has_value());
    CHECK(share_document->log().find(sample_id<EntryId>(1))->m_EditedUtc.has_value());
    CHECK(history_observer.changes().size() == 2);

    const auto stored = rig.storage().memory().load_log(history.archived_id());
    REQUIRE(stored.has_value());
    CHECK(stored->m_Meta.m_LastSharedUtc.has_value());
    CHECK(stored->m_Log.find(sample_id<EntryId>(1))->m_EditedUtc.has_value());
    history_document->remove_observer(history_observer);
}

TEST_CASE("A load that finishes after the registry has gone does not call back", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    std::vector<Delivery> delivered;
    {
        DocumentRegistry registry(rig.world().environment(), rig.gateway());
        registry.acquire(history.archived_id(), keep(delivered));
    }

    rig.gateway().run_all();

    CHECK(delivered.empty());
}

TEST_CASE("A document whose log was replaced is found under its new id only", "[viewmodel][registry]")
{
    DocumentRig rig(false);
    const History history(rig);
    DocumentRegistry registry(rig.world().environment(), rig.gateway());
    auto open = rig.storage().memory().load_log(history.archived_id());
    REQUIRE(open.has_value());
    const std::shared_ptr<LogDocument> document = registry.make_document(std::move(*open));
    auto next = rig.storage().memory().load_log(History::open_id());
    REQUIRE(next.has_value());

    document->replace(std::move(*next));

    CHECK(registry.find(History::open_id()) == document);
    CHECK(registry.find(history.archived_id()) == nullptr);
}
