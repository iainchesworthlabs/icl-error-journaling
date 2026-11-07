// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "error_journaling/entry.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/result.hpp"

namespace ej
{

/// One day's log: the initial header and its entries, in insertion order. A value type; copies are independent.
/// Entries are never physically removed here; Stage 2 decides what leaves the database.
class Log
{
public:
    enum class Error
    {
        UNKNOWN_ENTRY,
        DUPLICATE_ID
    };

    explicit Log(HeaderValues initial) : m_Initial(std::move(initial)) {}

    [[nodiscard]] const HeaderValues& initial_header() const { return m_Initial; }
    [[nodiscard]] std::span<const Entry> entries() const { return m_Entries; }
    [[nodiscard]] const Entry* find(EntryId id) const;

    /// Entries ordered by captured UTC time, ties by insertion order. Deleted entries are left out unless asked for.
    /// The pointers stay valid until the log is next changed.
    [[nodiscard]] std::vector<const Entry*> sorted_entries(bool include_deleted = false) const;

    Result<void, Error> add(Entry entry);

    /// Soft delete and restore.
    Result<void, Error> set_deleted(EntryId id, bool deleted);

    /// A review-screen edit: the entry takes the new content (time, kind and body), keeps its id and its deleted flag,
    /// keeps its old captured zone when the new one is empty, and records `edited_at` as its edit time. The log reads
    /// no clock, so the caller passes the time.
    Result<void, Error> replace(EntryId id, Entry entry, UtcMillis edited_at);

    /// A correction (HDR-07): swaps the body only and records nothing, as if the entry had always read this way.
    Result<void, Error> replace_body(EntryId id, EntryBody body);

    /// A correction of the header the log started with.
    void set_initial_header(HeaderValues header) { m_Initial = std::move(header); }

    /// Live (not deleted) error entries of this type; notes and header changes never count.
    [[nodiscard]] std::size_t count_for(ErrorTypeId id) const;

private:
    [[nodiscard]] Entry* find_mutable(EntryId id);

    HeaderValues m_Initial;
    std::vector<Entry> m_Entries;
};

}  // namespace ej
