// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/environment.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace ej::test
{

/// Ids that count: the first is `Uuid::from_random(0, first)`, the next one more. The same number gives the same id as
/// `ej::test::test_id<Id>(number)` (golden_scenarios.hpp), so a test can name an id it expects before it is made.
/// Start above the numbers of the golden scenarios (1 to 99) to keep them apart; the default does.
class SequentialIds final : public viewmodel::IdGenerator
{
public:
    explicit SequentialIds(std::uint64_t first = 1000) : m_First(first), m_Next(first) {}

    [[nodiscard]] Uuid next() override
    {
        return Uuid::from_random(0, m_Next++);
    }

    /// The number the next call will use.
    [[nodiscard]] std::uint64_t peek() const
    {
        return m_Next;
    }

    /// How many ids have been made.
    [[nodiscard]] std::uint64_t made() const
    {
        return m_Next - m_First;
    }

private:
    std::uint64_t m_First;
    std::uint64_t m_Next;
};

/// A device zone a test sets.
class FakeDeviceZone final : public viewmodel::DeviceZone
{
public:
    explicit FakeDeviceZone(std::string zone = "Australia/Melbourne") : m_Zone(std::move(zone)) {}

    [[nodiscard]] std::string current() const override
    {
        return m_Zone;
    }
    void set(std::string zone)
    {
        m_Zone = std::move(zone);
    }

private:
    std::string m_Zone;
};

} // namespace ej::test
