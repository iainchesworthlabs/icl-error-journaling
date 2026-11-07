// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace ej
{

/// Why a storage call failed. The kinds a caller acts on differently have their own value; the message is for logs.
enum class StorageErrorKind
{
    NOT_FOUND,        // no such log or entry
    ALREADY_EXISTS,   // the id is already in use
    OPEN_LOG_EXISTS,  // a second open log was asked for
    NOT_OPEN_LOG,     // the call needs the open log and this log is not it
    LOG_NOT_ARCHIVED, // a hard delete of a log that is not archived (DS-05)
    INVALID_ARGUMENT, // the call was refused before anything was written
    LIMIT_EXCEEDED,   // a size limit (note length, entries per log)
    DISK_FULL,
    LOCKED,           // the file is locked or protected (iOS before first unlock); never corruption
    PERMISSION,       // the file may not be read or written; never corruption
    CORRUPT,          // the database reports corruption, or is not a database
    NOT_OUR_DATABASE, // a SQLite file with another application id
    NEWER_SCHEMA,     // written by a newer build; left untouched
    IO,               // any other file system failure
    JOURNAL,          // the journal could not be written, read or locked
    DATABASE,         // any other database failure
    INTERNAL          // a broken invariant in this program
};

/// The error of every storage call. The message holds counts, kinds and paths, never entry text, so it is safe in CI
/// output (PRD NFR-10).
struct StorageError
{
    StorageErrorKind m_Kind = StorageErrorKind::INTERNAL;
    std::string m_Message;
    int m_NativeCode = 0; // the database's or the operating system's own code, 0 when there is none

    friend bool operator==(const StorageError&, const StorageError&) = default;
};

[[nodiscard]] std::string_view storage_error_kind_name(StorageErrorKind kind);

/// "locked: the database is locked (code 5)": one plain line for logs.
[[nodiscard]] std::string describe(const StorageError& error);

[[nodiscard]] inline StorageError make_error(StorageErrorKind kind, std::string message, int native_code = 0)
{
    return StorageError{kind, std::move(message), native_code};
}

} // namespace ej
