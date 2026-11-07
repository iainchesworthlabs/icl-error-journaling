// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/clock.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "raw_db.hpp"
#include "storage_samples.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <tuple>

namespace
{

struct SqliteFixture
{
    ej::test::TempDir m_Dir;
    ej::ScriptedClock m_Clock{ej::test::sample_time(0)};
    ej::NullJournal m_Journal;
    std::unique_ptr<ej::SqliteLogRepository> m_Repository;

    SqliteFixture()
    {
        auto opened = ej::SqliteLogRepository::open(m_Dir.path() / "ej.db", m_Clock, m_Journal);
        const std::string why = opened ? std::string() : ej::describe(opened.error());
        INFO(why);
        REQUIRE(opened.has_value());
        m_Repository = std::move(*opened);
    }

    ej::LogRepository& repo() { return *m_Repository; }
    ej::ScriptedClock& clock() { return m_Clock; }

    /// Adds `count` error entries (ids 1 to `count`) to a log in one transaction, behind the repository's back: the
    /// repository commits and syncs each write, which makes thousands of them slow.
    void fill_entries(ej::LogId log, std::size_t count)
    {
        ej::test::RawDb raw(m_Dir.path() / "ej.db");
        REQUIRE(raw.is_open());
        REQUIRE(raw.exec(QStringLiteral("BEGIN")));
        const QString log_text = QString::fromStdString(log.format());
        for (std::size_t i = 1; i <= count; ++i)
        {
            const QString id = QString::fromStdString(ej::test::sample_id<ej::EntryId>(i).format());
            REQUIRE(raw.exec(QStringLiteral("INSERT INTO entry (id, day_log_id, seq, kind, captured_utc, captured_tz, "
                                            "edited_utc, deleted) VALUES (?, ?, ?, 'error', ?, 'Australia/Melbourne', "
                                            "NULL, 0)"),
                             {id, log_text, static_cast<qulonglong>(i + 1), static_cast<qlonglong>(i % 1000) * 60000}));
            REQUIRE(raw.exec(QStringLiteral("INSERT INTO error_entry (entry_id, error_type_id, error_type_label) "
                                            "VALUES (?, ?, 'Wrong Response')"),
                             {id, QString::fromStdString(ej::test::sample_id<ej::ErrorTypeId>(1).format())}));
        }
        REQUIRE(raw.exec(QStringLiteral("UPDATE day_log SET applied_seq = ? WHERE id = ?"),
                         {static_cast<qulonglong>(count + 1), log_text}));
        REQUIRE(raw.exec(QStringLiteral("COMMIT")));
    }
};

using Fixtures = std::tuple<SqliteFixture>;

}  // namespace

// The cases themselves; they run against every fixture in `Fixtures`.
#include "contract_cases.hpp"
