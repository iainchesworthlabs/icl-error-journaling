// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/clock.hpp"
#include "error_journaling/memory_log_repository.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <tuple>

namespace
{

struct MemoryFixture
{
    ej::ScriptedClock m_Clock{ej::test::sample_time(0)};
    ej::MemoryLogRepository m_Repository{m_Clock};

    ej::LogRepository& repo() { return m_Repository; }
    ej::ScriptedClock& clock() { return m_Clock; }

    void fill_entries(ej::LogId log, std::size_t count)
    {
        for (std::size_t i = 1; i <= count; ++i)
        {
            REQUIRE(m_Repository.add_entry(log, ej::test::sample_error(i, static_cast<std::int64_t>(i % 1000)))
                        .has_value());
        }
    }
};

using Fixtures = std::tuple<MemoryFixture>;

}  // namespace

// The cases themselves; they run against every fixture in `Fixtures`.
#include "contract_cases.hpp"
