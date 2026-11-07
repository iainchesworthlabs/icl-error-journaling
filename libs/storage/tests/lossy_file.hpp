// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/journal_file.hpp"

#include <functional>
#include <optional>
#include <random>
#include <string>

namespace ej::test
{

/// A journal file that models a disk losing power (section 4.2 of the Stage 2 plan). Appended bytes sit in a buffer
/// until `sync` makes them durable; `after_power_loss` is what is on the disk if the power goes now: everything
/// durable, and of the rest a random prefix, sometimes cut in the middle of a line and sometimes followed by zeros, as
/// a file system that extended the file but had not written the data would leave it. It models the journal only; SQLite
/// does its own file I/O.
class LossyFile final : public JournalFile
{
public:
    enum class Op
    {
        APPEND,
        SYNC,
        TRUNCATE
    };

    /// Called before each operation; an error it returns is the operation's result and changes nothing.
    std::function<std::optional<StorageError>(Op)> m_Fault;

    LossyFile() = default;
    explicit LossyFile(std::string durable) : m_Durable(std::move(durable)) {}

    [[nodiscard]] std::uint64_t size() const override { return m_Durable.size() + m_Pending.size(); }

    Result<std::string, StorageError> read_all() override { return m_Durable + m_Pending; }

    Result<void, StorageError> append(std::string_view bytes) override
    {
        if (const auto fault = fault_of(Op::APPEND))
        {
            return fail(*fault);
        }
        m_Pending.append(bytes);
        return {};
    }

    Result<void, StorageError> sync() override
    {
        if (const auto fault = fault_of(Op::SYNC))
        {
            return fail(*fault);
        }
        m_Durable += m_Pending;
        m_Pending.clear();
        ++m_Syncs;
        return {};
    }

    Result<void, StorageError> truncate(std::uint64_t length) override
    {
        if (const auto fault = fault_of(Op::TRUNCATE))
        {
            return fail(*fault);
        }
        if (length <= m_Durable.size())
        {
            m_Durable.resize(static_cast<std::size_t>(length));
            m_Pending.clear();
        }
        else
        {
            m_Pending.resize(static_cast<std::size_t>(length) - m_Durable.size());
        }
        return {};
    }

    /// What is on the disk after a power loss now.
    [[nodiscard]] std::string after_power_loss(std::mt19937_64& rng) const
    {
        std::string image = m_Durable;
        const std::size_t kept = m_Pending.empty() ? 0 : static_cast<std::size_t>(rng() % (m_Pending.size() + 1));
        image.append(m_Pending, 0, kept);
        if (rng() % 3 == 0)
        {
            image.append(static_cast<std::size_t>(rng() % 5000),
                         '\0');  // the file was extended, the data was not written
        }
        return image;
    }

    [[nodiscard]] const std::string& durable() const { return m_Durable; }
    [[nodiscard]] const std::string& pending() const { return m_Pending; }
    [[nodiscard]] int syncs() const { return m_Syncs; }

private:
    [[nodiscard]] std::optional<StorageError> fault_of(Op op) const { return m_Fault ? m_Fault(op) : std::nullopt; }

    std::string m_Durable;
    std::string m_Pending;
    int m_Syncs = 0;
};

}  // namespace ej::test
