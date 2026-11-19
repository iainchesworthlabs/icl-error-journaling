// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/sqlite_log_repository.hpp"
#include "recovery_fixture.hpp"

#include <filesystem>
#include <map>
#include <string>

/// A store written by the build before the rename of Interactions to Observations (journal records at version 2 with
/// `"kind":"interaction"`, database at schema version 4 with the `interaction_*` tables). The files under
/// `fixtures/legacy-interactions/store` were written by that build from stand-in data (Sam Example, UNIT, North Site,
/// Wrong Response), never by hand, and are never rewritten. `reference_digests` builds the same data with this build,
/// so "nothing was lost" is a comparison with an independent writer, not with the reader under test.
///
/// The store: an archived and shared log 1 (entry 1 an error, 2 an interaction naming two people, 3 an interaction with
/// an answer and nobody, 4 a note, 5 an interaction edited to two people, an answer and a note, 6 an interaction that
/// was deleted) and the open log 2 (entry 7 an error, 8 an interaction with a note).
namespace ej::test
{

inline std::filesystem::path legacy_store_fixture()
{
    return std::filesystem::path(EJ_LEGACY_STORE_DIR);
}

inline void copy_legacy_store(const std::filesystem::path& to)
{
    std::filesystem::create_directories(to);
    std::filesystem::copy(legacy_store_fixture(), to,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
}

inline PersonId legacy_person(std::uint64_t n)
{
    return sample_id<PersonId>(n);
}

/// The operations the old build was given, run through this build into a new store at `root`. The same calls, in the
/// same order, with the same clock, so the journals differ only in the words of the format.
inline void build_reference_store(const std::filesystem::path& root)
{
    OpenStore store(root);
    HeaderValues header = sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 11 / 5;
    store.clock().set(sample_time(1));
    REQUIRE(store.repo().create_log(log_id(1), header).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(1, 10)).has_value());
    REQUIRE(store.repo()
                .add_entry(log_id(1), sample_interaction_entry(2, 20, {legacy_person(7), legacy_person(3)}))
                .has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(3, 30, {}, std::string("task 4"))).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_note(4, 40, "Began a task for Sam Example")).has_value());
    REQUIRE(store.repo()
                .add_entry(log_id(1), sample_interaction_entry(5, 50, {legacy_person(3)}, std::nullopt,
                                                               std::string("attached note")))
                .has_value());
    REQUIRE(store.repo().add_entry(log_id(1), sample_interaction_entry(6, 60, {legacy_person(9)})).has_value());
    REQUIRE(store.repo()
                .edit_entry(log_id(1),
                            sample_interaction_entry(5, 50, {legacy_person(3), legacy_person(7)}, std::string("answer"),
                                                     std::string("edited note")),
                            sample_time(70))
                .has_value());
    REQUIRE(store.repo().set_entry_deleted(log_id(1), entry_id(6), true).has_value());
    REQUIRE(store.repo().mark_shared(log_id(1), sample_time(80)).has_value());
    header.m_LocalDate = std::chrono::year{2026} / 11 / 6;
    store.clock().set(sample_time(2));
    REQUIRE(store.repo().start_new_log(log_id(1), log_id(2), header).has_value());
    REQUIRE(store.repo().add_entry(log_id(2), sample_error(7, 15)).has_value());
    REQUIRE(store.repo()
                .add_entry(log_id(2), sample_interaction_entry(8, 25, {legacy_person(3)}, std::nullopt,
                                                               std::string("open log note")))
                .has_value());
}

/// What each log of the reference store holds.
inline std::map<LogId, StateDigest> reference_digests()
{
    TempDir dir;
    build_reference_store(dir.path());
    ScriptedClock clock(sample_time(3000));
    NullJournal journal;
    auto repo = SqliteLogRepository::open(dir.path() / "ej.db", clock, journal);
    REQUIRE(repo.has_value());
    std::map<LogId, StateDigest> digests;
    for (const LogId log : {log_id(1), log_id(2)})
    {
        const auto stored = (*repo)->load_log(log);
        REQUIRE(stored.has_value());
        digests.emplace(log, digest_of(*stored));
    }
    return digests;
}

/// Opens the database of `location` and checks that every log is what the old build wrote: the same digest as the
/// reference store, six entries in log 1 (one deleted) and two in log 2, the interactions read as observations.
inline void check_legacy_content(const StorageLocation& location, const std::map<LogId, StateDigest>& expected)
{
    ScriptedClock clock(sample_time(3000));
    NullJournal journal;
    auto repo = SqliteLogRepository::open(location.database_path(), clock, journal);
    const std::string why = repo ? std::string() : describe(repo.error());
    INFO(why);
    REQUIRE(repo.has_value());
    for (const auto& [log, digest] : expected)
    {
        const auto stored = (*repo)->load_log(log);
        INFO("log " << log.format());
        REQUIRE(stored.has_value());
        CHECK(digest_of(*stored) == digest);
    }
    const auto first = (*repo)->load_log(log_id(1));
    REQUIRE(first.has_value());
    CHECK(first->m_Log.entries().size() == 6);
    CHECK(first->m_Meta.m_Archived);
    const Entry* edited = first->m_Log.find(entry_id(5));
    REQUIRE(edited != nullptr);
    CHECK(edited->m_EditedUtc.has_value());
    const auto& body = std::get<InteractionEntry>(edited->m_Body);
    CHECK(body.m_People == std::vector<PersonId>{legacy_person(3), legacy_person(7)});
    CHECK(body.m_Value == "answer");
    CHECK(body.m_AttachedNote == "edited note");
    CHECK(first->m_Log.find(entry_id(6))->m_Deleted);
    CHECK(std::get<InteractionEntry>(first->m_Log.find(entry_id(2))->m_Body).m_People ==
          std::vector<PersonId>{legacy_person(7), legacy_person(3)});
    const auto second = (*repo)->load_log(log_id(2));
    REQUIRE(second.has_value());
    CHECK(second->m_Log.entries().size() == 2);
    CHECK_FALSE(second->m_Meta.m_Archived);
}

} // namespace ej::test
