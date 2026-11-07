// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/stored_log.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/move_only_function.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace ej::viewmodel
{

/// Hands out one `LogDocument` per log (docs/plan/stage-3.md, "LogDocument and the registry"), so that History, Review
/// and Share work on the same document and cannot overwrite each other's edits.
///
/// The registry holds documents weakly. The open log is held strongly by whoever made it with `make_document`
/// (`LogSession`); a past log is loaded on demand through the gateway (`load_log`) and lives as long as someone holds
/// the pointer `acquire` gave. When the last holder lets go the document is gone, and the next `acquire` loads the log
/// again from the store, which has every committed change.
///
/// GUI thread only. A load that is in flight when the registry is destroyed does not call back.
class DocumentRegistry
{
public:
    /// What `acquire` delivers: the document, or the store's error. An id the store does not have is `NOT_FOUND`.
    using Loaded = MoveOnlyFunction<void(Result<std::shared_ptr<LogDocument>, StorageError>)>;

    DocumentRegistry(const Environment& environment, StorageGateway& gateway);
    ~DocumentRegistry();

    DocumentRegistry(const DocumentRegistry&) = delete;
    DocumentRegistry& operator=(const DocumentRegistry&) = delete;

    /// Makes the document of a log the caller has loaded (the open log, read at start-up) and registers it. The caller
    /// holds the pointer. If a document of that id is alive already, that one is returned and `stored` is dropped: it
    /// has been loaded from the same store and holds every change since.
    [[nodiscard]] std::shared_ptr<LogDocument> make_document(StoredLog stored);

    /// The live document of `id`, or null. Starts no load.
    [[nodiscard]] std::shared_ptr<LogDocument> find(LogId id) const;

    /// Gives `done` the document of `id`. A live document is delivered at once, before `acquire` returns (the open log
    /// is in memory already); otherwise the log is loaded through the gateway and `done` runs on the GUI thread after
    /// it. Calls for an id that is loading share the one load and are answered in the order they were made.
    void acquire(LogId id, Loaded done);

    /// Documents alive and loads in flight, for tests and diagnostics.
    [[nodiscard]] std::size_t live_count() const;
    [[nodiscard]] std::size_t loading_count() const;

private:
    void purge();
    void loaded(LogId id, Result<StoredLog, StorageError> result);

    Environment m_Environment;
    StorageGateway& m_Gateway;
    std::vector<std::weak_ptr<LogDocument>> m_Documents;
    std::map<LogId, std::vector<Loaded>> m_Loading;

    std::shared_ptr<int> m_Alive = std::make_shared<int>(0);
};

} // namespace ej::viewmodel
