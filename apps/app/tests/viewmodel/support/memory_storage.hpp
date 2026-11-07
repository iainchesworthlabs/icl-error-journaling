// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/memory_log_repository.hpp"
#include "support/event_log.hpp"
#include "support/fake_config_store.hpp"
#include "support/recording_repository.hpp"
#include "viewmodel/storage_gateway.hpp"

namespace ej::test
{

/// The store of the unit tests: `MemoryLogRepository` seen through a `RecordingRepository`, a `FakeConfigStore`, and
/// the `StorageContext` that holds both. Hand `context()` to a `ManualStorageGateway` (or a `ThreadedStorageGateway`).
/// Services under test go through the gateway; the test reads `memory()` (what is in the store) and `repository()`
/// (what was asked of it).
class MemoryStorage
{
public:
    /// `clock` stamps the logs the repository makes; it must outlive the storage. `log` may be null.
    explicit MemoryStorage(const Clock& clock, EventLog* log = nullptr) : m_Memory(clock), m_Repository(m_Memory, log)
    {
    }

    MemoryStorage(const MemoryStorage&) = delete;
    MemoryStorage& operator=(const MemoryStorage&) = delete;

    [[nodiscard]] MemoryLogRepository& memory()
    {
        return m_Memory;
    }
    [[nodiscard]] RecordingRepository& repository()
    {
        return m_Repository;
    }
    [[nodiscard]] FakeConfigStore& config()
    {
        return m_Config;
    }
    [[nodiscard]] viewmodel::StorageContext& context()
    {
        return m_Context;
    }

private:
    MemoryLogRepository m_Memory;
    RecordingRepository m_Repository;
    FakeConfigStore m_Config;
    viewmodel::StorageContext m_Context{m_Repository, m_Config};
};

} // namespace ej::test
