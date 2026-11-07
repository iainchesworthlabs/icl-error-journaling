// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/document_registry.hpp"

#include "error_journaling/log_repository.hpp"

#include <algorithm>
#include <utility>

namespace ej::viewmodel
{

DocumentRegistry::DocumentRegistry(const Environment& environment, StorageGateway& gateway)
    : m_Environment(environment), m_Gateway(gateway)
{
}

DocumentRegistry::~DocumentRegistry() = default;

void DocumentRegistry::purge()
{
    std::erase_if(m_Documents, [](const std::weak_ptr<LogDocument>& document) { return document.expired(); });
}

std::shared_ptr<LogDocument> DocumentRegistry::find(LogId id) const
{
    // By the id the document has now: `LogDocument::replace` can give a document another log.
    for (const std::weak_ptr<LogDocument>& weak : m_Documents)
    {
        if (auto document = weak.lock(); document && document->id() == id)
        {
            return document;
        }
    }
    return nullptr;
}

std::shared_ptr<LogDocument> DocumentRegistry::make_document(StoredLog stored)
{
    purge();
    const LogId id = stored.m_Meta.m_Id;
    if (auto existing = find(id))
    {
        return existing;
    }
    auto document = std::make_shared<LogDocument>(m_Environment, m_Gateway, std::move(stored));
    m_Documents.push_back(document);
    return document;
}

void DocumentRegistry::acquire(LogId id, Loaded done)
{
    if (auto document = find(id))
    {
        done(std::move(document));
        return;
    }

    auto [waiting, first] = m_Loading.try_emplace(id);
    waiting->second.push_back(std::move(done));
    if (!first)
    {
        return; // the load in flight answers this call too
    }

    m_Gateway.run([id](StorageContext& context) { return context.m_Logs.load_log(id); },
                  [this, alive = std::weak_ptr<void>(m_Alive), id](Result<StoredLog, StorageError> result)
                  {
                      if (!alive.expired())
                      {
                          loaded(id, std::move(result));
                      }
                  });
}

void DocumentRegistry::loaded(LogId id, Result<StoredLog, StorageError> result)
{
    const auto node = m_Loading.extract(id);
    if (node.empty())
    {
        return;
    }
    std::vector<Loaded> callbacks = std::move(node.mapped());
    const std::weak_ptr<void> alive = m_Alive;

    std::shared_ptr<LogDocument> document;
    if (result)
    {
        // A document may have been made while the load was in flight (the open log): it wins, it is not older.
        document = find(id);
        if (!document)
        {
            purge();
            document = std::make_shared<LogDocument>(m_Environment, m_Gateway, std::move(*result));
            m_Documents.push_back(document);
        }
    }

    for (Loaded& callback : callbacks)
    {
        if (alive.expired())
        {
            return;
        }
        if (document)
        {
            callback(document);
        }
        else
        {
            callback(fail(result.error()));
        }
    }
}

std::size_t DocumentRegistry::live_count() const
{
    return static_cast<std::size_t>(std::ranges::count_if(m_Documents, [](const std::weak_ptr<LogDocument>& document)
                                                          { return !document.expired(); }));
}

std::size_t DocumentRegistry::loading_count() const
{
    return m_Loading.size();
}

} // namespace ej::viewmodel
