// SPDX-License-Identifier: GPL-3.0-or-later
//
// The filter of the review screen (docs/plan/feedback-log-filter.md): which rows are shown for a kind and for words,
// that a shown row still maps to its row in the whole log, and that Copy all, the count and the shared text never
// change.
#include "error_journaling/configuration.hpp"
#include "error_journaling/header_field.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/stored_log.hpp"
#include "fake_time_zone_provider.hpp"
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/timeline_filter_model.hpp"
#include "viewmodel/timeline_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <chrono>
#include <memory>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::TimelineFilterModel;
using viewmodel::TimelineModel;

namespace
{

QString id_text(std::uint64_t number)
{
    return QString::fromStdString(sample_id<EntryId>(number).format());
}

/// Entries 1 to 6, in time order: an error (Wrong Response), a note (Played Morning Medley), an observation with no
/// person, an error (Wrong Song), a note with an accent (Café opened) and a change of the software version.
struct Fixture
{
    Fixture()
    {
        m_Config.set(sample_configuration());
        m_Document = make_document(1, {sample_error(1, 0, "Wrong Response"), sample_note(2, 1, "Played Morning Medley"),
                                       sample_interaction_entry(3, 2), sample_error(4, 3, "Wrong Song"),
                                       sample_note(5, 4, "Café opened"),
                                       sample_change(6, 5, HeaderField::SOFTWARE_VERSION, "1.5.0")});
        m_Model = std::make_unique<TimelineModel>(m_World.environment(), m_Config, m_Clipboard);
        m_Model->bind(m_Document);
    }

    [[nodiscard]] std::shared_ptr<LogDocument> make_document(std::uint64_t number, const std::vector<Entry>& entries)
    {
        const LogId id = sample_id<LogId>(number);
        REQUIRE(m_Storage.memory().create_log(id, sample_header()));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        return std::make_shared<LogDocument>(m_World.environment(), m_Gateway, std::move(*loaded));
    }

    /// Another log to show, loaded from memory (the store holds one open log at a time, and this one is not in it).
    [[nodiscard]] std::shared_ptr<LogDocument> make_extra(std::uint64_t number, const std::vector<Entry>& entries)
    {
        Log log(sample_header());
        for (const Entry& entry : entries)
        {
            REQUIRE(log.add(entry));
        }
        const LogId id = sample_id<LogId>(number);
        auto document = std::make_shared<LogDocument>(m_World.environment(), m_Gateway, id);
        document->load(StoredLog{LogMeta{id, UtcMillis{}, std::nullopt, false, std::nullopt}, std::move(log)});
        return document;
    }

    [[nodiscard]] TimelineModel& model()
    {
        return *m_Model;
    }
    [[nodiscard]] TimelineFilterModel& filter()
    {
        return *m_Model->filtered();
    }

    /// The numbers of the entries the filter shows, in the order shown.
    [[nodiscard]] std::vector<int> shown()
    {
        std::vector<int> numbers;
        for (int row = 0; row < filter().rowCount(); ++row)
        {
            const QString text = filter().data(filter().index(row, 0), TimelineModel::ENTRY_ID).toString();
            for (int number = 1; number <= 9; ++number)
            {
                if (text == id_text(static_cast<std::uint64_t>(number)))
                {
                    numbers.push_back(number);
                }
            }
        }
        return numbers;
    }

    void commit()
    {
        m_Gateway.run_all();
    }

    TestEnvironment m_World;
    EventLog m_Events;
    MemoryStorage m_Storage{m_World.m_Clock, &m_Events};
    ManualStorageGateway m_Gateway{m_Storage.context()};
    FakeConfigurationSource m_Config;
    FakeClipboard m_Clipboard{&m_Events};
    std::shared_ptr<LogDocument> m_Document;
    std::unique_ptr<TimelineModel> m_Model;
};

using Numbers = std::vector<int>;

} // namespace

TEST_CASE("filter.kinds: no kind on shows every row, one kind shows its rows, two show either",
          "[viewmodel][review][filter]")
{
    Fixture f;
    CHECK(f.shown() == Numbers{1, 2, 3, 4, 5, 6});
    CHECK_FALSE(f.filter().active());

    f.filter().toggleKind("error");
    CHECK(f.filter().active());
    CHECK(f.shown() == Numbers{1, 4});

    f.filter().toggleKind("note");
    CHECK(f.shown() == Numbers{1, 2, 4, 5});

    f.filter().toggleKind("error");
    CHECK(f.shown() == Numbers{2, 5});

    f.filter().toggleKind("note");
    CHECK(f.shown() == Numbers{1, 2, 3, 4, 5, 6});
    CHECK_FALSE(f.filter().active());
}

TEST_CASE("filter.kinds: observations and changes are kinds of their own", "[viewmodel][review][filter]")
{
    Fixture f;
    f.filter().toggleKind("interaction");
    CHECK(f.shown() == Numbers{3});
    f.filter().toggleKind("interaction");
    f.filter().toggleKind("header_change");
    CHECK(f.shown() == Numbers{6});
    CHECK(f.filter().kindOn("header_change"));
    CHECK_FALSE(f.filter().kindOn("note"));
}

TEST_CASE("filter.kinds: a name that is not a kind is ignored and signals nothing", "[viewmodel][review][filter]")
{
    Fixture f;
    QSignalSpy changed(&f.filter(), &TimelineFilterModel::filterChanged);
    f.filter().toggleKind("T|");
    f.filter().toggleKind("");
    CHECK(changed.count() == 0);
    CHECK_FALSE(f.filter().active());
}

TEST_CASE("filter.text: every word has to appear, in any order, ignoring case, accents and spacing",
          "[viewmodel][review][filter]")
{
    Fixture f;

    f.filter().set_text("wrong");
    CHECK(f.shown() == Numbers{1, 4});

    f.filter().set_text("WRONG   song");
    CHECK(f.shown() == Numbers{4});

    f.filter().set_text("song wrong");
    CHECK(f.shown() == Numbers{4});

    f.filter().set_text("  morning   medley ");
    CHECK(f.shown() == Numbers{2});

    // An accent in the entry is found without typing it, and typed it still finds it.
    f.filter().set_text("cafe");
    CHECK(f.shown() == Numbers{5});
    f.filter().set_text("CAFÉ");
    CHECK(f.shown() == Numbers{5});

    f.filter().set_text("no such words");
    CHECK(f.shown().empty());
    CHECK(f.filter().shown_count() == 0);
    CHECK(f.filter().total_count() == 6);
}

TEST_CASE("filter.text: the time of an entry is not searched, and blank text is no filter",
          "[viewmodel][review][filter]")
{
    Fixture f;
    const QString time = f.model().data(f.model().index(0), TimelineModel::TIME_TEXT).toString();
    REQUIRE_FALSE(time.isEmpty());
    f.filter().set_text(time);
    CHECK(f.shown().empty());

    f.filter().set_text("   ");
    CHECK(f.shown() == Numbers{1, 2, 3, 4, 5, 6});
    CHECK_FALSE(f.filter().active());
}

TEST_CASE("filter.text: the same text signals nothing", "[viewmodel][review][filter]")
{
    Fixture f;
    f.filter().set_text("song");
    QSignalSpy changed(&f.filter(), &TimelineFilterModel::filterChanged);
    f.filter().set_text("song");
    CHECK(changed.count() == 0);
    f.filter().set_text("songs");
    CHECK(changed.count() == 1);
}

TEST_CASE("filter.combined: kinds and words are both required, and clear puts every row back",
          "[viewmodel][review][filter]")
{
    Fixture f;
    f.filter().toggleKind("error");
    f.filter().set_text("song");
    CHECK(f.shown() == Numbers{4});

    // "Played Morning Medley" has no "song" but a note with it would not pass the error kind.
    f.filter().set_text("medley");
    CHECK(f.shown().empty());

    QSignalSpy changed(&f.filter(), &TimelineFilterModel::filterChanged);
    f.filter().clear();
    CHECK(changed.count() == 1);
    CHECK(f.shown() == Numbers{1, 2, 3, 4, 5, 6});
    CHECK_FALSE(f.filter().active());
    CHECK(f.filter().text().isEmpty());

    // Nothing to clear: nothing is signalled.
    f.filter().clear();
    CHECK(changed.count() == 1);
}

TEST_CASE("filter.rows: a shown row is the row of the whole log it came from", "[viewmodel][review][filter]")
{
    Fixture f;
    f.filter().toggleKind("error");
    f.filter().set_text("song");
    REQUIRE(f.filter().rowCount() == 1);
    CHECK(f.filter().sourceRow(0) == 3);
    CHECK(f.filter().sourceRow(1) == -1);
    CHECK(f.filter().sourceRow(-1) == -1);

    // Copy line takes the row of the whole log, as the screen passes it.
    REQUIRE(f.model().copyLine(f.filter().sourceRow(0)));
    CHECK(f.m_Clipboard.text().contains("Wrong Song"));

    // A delete is by the id of the entry, and it leaves the shown rows.
    REQUIRE(f.model().deleteEntry(id_text(4)));
    f.commit();
    CHECK(f.shown().empty());
    CHECK(f.filter().total_count() == 5);
}

TEST_CASE("filter.whole_log: Copy all, the count and the title are the whole log's while a filter is on",
          "[viewmodel][review][filter]")
{
    Fixture f;
    REQUIRE(f.model().copyAll());
    const QString whole = QString::fromStdString(f.m_Clipboard.text());
    const int count = f.model().entry_count();
    const QString title = f.model().title_text();
    REQUIRE(count == 6);

    f.filter().toggleKind("error");
    f.filter().set_text("song");
    REQUIRE(f.filter().shown_count() == 1);

    REQUIRE(f.model().copyAll());
    CHECK(QString::fromStdString(f.m_Clipboard.text()) == whole);
    CHECK(f.model().entry_count() == count);
    CHECK(f.model().title_text() == title);
    CHECK(f.model().last_line() == f.model().data(f.model().index(5), TimelineModel::FULL_LINE).toString());
}

TEST_CASE("filter.live: an entry that is added shows when it matches, and one that is edited out leaves",
          "[viewmodel][review][filter]")
{
    Fixture f;
    f.filter().toggleKind("error");
    REQUIRE(f.shown() == Numbers{1, 4});

    REQUIRE(f.m_Document->add_entry(sample_error(7, 6, "Wrong Song"), LogChange::Origin::USER, {}));
    f.commit();
    CHECK(f.shown() == Numbers{1, 4, 7});

    REQUIRE(f.m_Document->add_entry(sample_note(8, 7, "A note"), LogChange::Origin::USER, {}));
    f.commit();
    CHECK(f.shown() == Numbers{1, 4, 7});
    CHECK(f.filter().total_count() == 8);
}

TEST_CASE("filter.log: showing another log, or the same one again, clears the filter", "[viewmodel][review][filter]")
{
    Fixture f;
    f.filter().toggleKind("note");
    f.filter().set_text("song");
    REQUIRE(f.filter().active());

    f.model().bind(f.make_extra(2, {sample_error(1, 0, "Wrong Response")}));
    CHECK_FALSE(f.filter().active());
    CHECK(f.filter().text().isEmpty());
    CHECK(f.filter().kinds().isEmpty());
    CHECK(f.shown() == Numbers{1});

    f.filter().toggleKind("error");
    f.model().unbind();
    CHECK_FALSE(f.filter().active());
    CHECK(f.filter().rowCount() == 0);
}

TEST_CASE("filter.observations: the log says whether it holds an observation", "[viewmodel][review][filter]")
{
    Fixture f;
    CHECK(f.filter().has_observations());

    f.model().bind(f.make_extra(2, {sample_error(1, 0, "Wrong Response")}));
    CHECK_FALSE(f.filter().has_observations());
}

TEST_CASE("filter.fold: case, accents and spacing fold away", "[viewmodel][review][filter]")
{
    CHECK(TimelineFilterModel::fold("  Café   Noël ") == QStringLiteral("cafe noel"));
    CHECK(TimelineFilterModel::fold("WRONG Response") == TimelineFilterModel::fold("wrong response"));
    CHECK(TimelineFilterModel::fold("").isEmpty());
}

TEST_CASE("filter.size: typing in a log of 5,000 entries is a short step", "[viewmodel][review][filter]")
{
    Fixture f;
    std::vector<Entry> entries;
    entries.reserve(5000);
    for (std::uint64_t number = 1; number <= 5000; ++number)
    {
        entries.push_back(number % 2 == 0
                              ? sample_error(number, static_cast<std::int64_t>(number % 1000), "Wrong Song")
                              : sample_note(number, static_cast<std::int64_t>(number % 1000), "Played Morning Medley"));
    }
    f.model().bind(f.make_extra(3, entries));
    REQUIRE(f.filter().total_count() == 5000);

    const auto start = std::chrono::steady_clock::now();
    f.filter().set_text("w");
    f.filter().set_text("wr");
    f.filter().set_text("wro");
    f.filter().set_text("wron");
    f.filter().set_text("wrong");
    const auto taken = std::chrono::steady_clock::now() - start;

    CHECK(f.filter().shown_count() == 2500);
    // Five keys over 5,000 rows: far from a pause. The bound is loose so that a loaded CI machine or a sanitizer build
    // does not fail it; a filter that folded the whole log on every row of every key would still not pass.
    CHECK(taken < std::chrono::seconds(5));
}
