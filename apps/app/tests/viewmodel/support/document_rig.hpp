// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/log_document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace ej::test
{

/// A log in the in-memory store, a `LogDocument` over it and the manual gateway, for the tests of 3.2 and of the
/// packages that stack on it. The log is created in the store directly (not through the recording repository), so the
/// repository's records show only what the document wrote.
///
///     DocumentRig rig;
///     rig.document->add_entry(sample_error(1, 10), LogChange::Origin::USER, rig.count(done));
///     rig.gateway.run_all();                      // the commit, then the completion
class DocumentRig
{
public:
    explicit DocumentRig(bool open_document = true)
    {
        REQUIRE(m_Storage.memory().create_log(m_LogId, sample_header()));
        if (open_document)
        {
            document = std::make_shared<viewmodel::LogDocument>(m_World.environment(), m_Gateway, stored());
            document->add_observer(observer);
        }
    }

    DocumentRig(const DocumentRig&) = delete;
    DocumentRig& operator=(const DocumentRig&) = delete;

    ~DocumentRig()
    {
        if (document)
        {
            document->remove_observer(observer);
        }
    }

    /// The log as the store has it now.
    [[nodiscard]] StoredLog stored()
    {
        auto loaded = m_Storage.memory().load_log(m_LogId);
        REQUIRE(loaded.has_value());
        return std::move(*loaded);
    }

    /// A `Done` that counts its calls and keeps the last result.
    [[nodiscard]] viewmodel::LogDocument::Done count(int& calls)
    {
        return [&calls](const Result<void, StorageError>&) { ++calls; };
    }
    [[nodiscard]] viewmodel::LogDocument::Done keep(std::vector<Result<void, StorageError>>& results)
    {
        return [&results](const Result<void, StorageError>& result) { results.push_back(result); };
    }

    [[nodiscard]] LogId log_id() const
    {
        return m_LogId;
    }
    [[nodiscard]] TestEnvironment& world()
    {
        return m_World;
    }
    [[nodiscard]] EventLog& events()
    {
        return m_Events;
    }
    [[nodiscard]] MemoryStorage& storage()
    {
        return m_Storage;
    }
    [[nodiscard]] ManualStorageGateway& gateway()
    {
        return m_Gateway;
    }

    std::shared_ptr<viewmodel::LogDocument> document;
    FakeLogObserver observer{&m_Events};

private:
    LogId m_LogId = sample_id<LogId>(1);
    TestEnvironment m_World;
    EventLog m_Events;
    MemoryStorage m_Storage{m_World.m_Clock, &m_Events};
    ManualStorageGateway m_Gateway{m_Storage.context()};
};

} // namespace ej::test
