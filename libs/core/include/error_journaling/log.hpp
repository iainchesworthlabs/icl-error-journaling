// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <utility>
#include <vector>

#include "error_journaling/entry.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/result.hpp"

namespace ej
{

/// One day's log: the initial header and its entries, in insertion order. A value type; copies are independent.
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

    Result<void, Error> add(Entry entry);

private:
    HeaderValues m_Initial;
    std::vector<Entry> m_Entries;
};

}  // namespace ej
