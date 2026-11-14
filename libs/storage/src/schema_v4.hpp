// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// The tables that schema version 4 adds or rebuilds (Stage 14, section 3.3). One text for a new database
/// (`sqlite_log_repository.cpp`) and for the migration step (`migrator.cpp`), so the two cannot drift: a test compares
/// the schema of a migrated database with that of a new one.
namespace ej::schema
{

/// `entry` with `interaction` among its kinds. SQLite cannot change a CHECK in place, so the migration builds this
/// table under another name, copies the rows, drops the old table and renames the new one.
inline constexpr const char* ENTRY_TABLE =
    "CREATE TABLE entry ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " day_log_id TEXT NOT NULL REFERENCES day_log(id) ON DELETE RESTRICT,"
    " seq INTEGER NOT NULL,"
    " kind TEXT NOT NULL CHECK (kind IN ('error', 'note', 'header_change', 'interaction')),"
    " captured_utc INTEGER NOT NULL,"
    " captured_tz TEXT NOT NULL,"
    " edited_utc INTEGER,"
    " deleted INTEGER NOT NULL DEFAULT 0 CHECK (deleted IN (0, 1)),"
    " UNIQUE (day_log_id, seq))";

/// The text that opens `ENTRY_TABLE`, and the one the rebuild gives the copy.
inline constexpr const char* ENTRY_TABLE_PREFIX = "CREATE TABLE entry (";
inline constexpr const char* ENTRY_COPY_PREFIX = "CREATE TABLE entry_new (";

inline constexpr const char* ENTRY_BY_TIME = "CREATE INDEX entry_by_time ON entry(day_log_id, captured_utc, seq)";

/// An interaction entry holds the id of its type and a snapshot of the type's label, never a person's name. The people
/// of an entry are rows of `interaction_entry_person`, in the order they were picked (`position`), by id only: no row
/// of an entry table can hold a name, and a person who is forgotten leaves the entry's rows as they were.
inline constexpr const char* INTERACTION_TABLES[] = {
    "CREATE TABLE interaction_entry ("
    " entry_id TEXT PRIMARY KEY NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " interaction_type_id TEXT NOT NULL,"
    " interaction_type_label TEXT NOT NULL,"
    " value_text TEXT,"
    " attached_note TEXT)",
    // No reference to `person`: the person table is a cache of people.jsonl, and an entry keeps the id of a person who
    // was forgotten or whose journal was lost.
    "CREATE TABLE interaction_entry_person ("
    " entry_id TEXT NOT NULL REFERENCES entry(id) ON DELETE RESTRICT,"
    " person_id TEXT NOT NULL,"
    " position INTEGER NOT NULL CHECK (position >= 0 AND position < 20),"
    " PRIMARY KEY (entry_id, position),"
    " UNIQUE (entry_id, person_id))",
    "CREATE TABLE interaction_type ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " name TEXT NOT NULL,"
    " retired INTEGER NOT NULL DEFAULT 0 CHECK (retired IN (0, 1)),"
    " board_position INTEGER CHECK (board_position IS NULL OR (board_position >= 1 AND board_position <= 11)),"
    " line_pattern TEXT,"
    " asks TEXT NOT NULL DEFAULT 'nothing' CHECK (asks IN ('nothing', 'person', 'text')))",
    // The cache of people.jsonl, the journal of record for people (rebuilt from it at every launch). A forgotten person
    // keeps the row, with an empty name, so that the id stays known.
    "CREATE TABLE person ("
    " id TEXT PRIMARY KEY NOT NULL,"
    " display_name TEXT NOT NULL,"
    " retired INTEGER NOT NULL DEFAULT 0 CHECK (retired IN (0, 1)),"
    " location_id TEXT,"
    " last_used_utc INTEGER,"
    " created_utc INTEGER NOT NULL,"
    " forgotten INTEGER NOT NULL DEFAULT 0 CHECK (forgotten IN (0, 1)))",
};

} // namespace ej::schema
