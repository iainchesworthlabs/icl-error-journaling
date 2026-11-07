// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"

#include <optional>

namespace ej::viewmodel
{

class LogDocument; // work package 3.2

/// What happened to a log. Models and the undo controller read it to change as little as possible.
struct LogChange
{
    enum class Kind
    {
        LOADED,   // the document has just been loaded; one notification however many entries it has
        REPLACED, // a new log took the place of the open one (a new day); one notification
        ENTRY_ADDED,
        ENTRY_EDITED,
        ENTRY_DELETED,
        ENTRY_RESTORED,
        HEADER_CORRECTED,
        HEADER_CHANGED,
        SHARED
    };

    /// `UNDO` for a change that undid an action: it opens no toast and fires no haptic.
    enum class Origin
    {
        USER,
        UNDO
    };

    Kind m_Kind = Kind::LOADED;
    Origin m_Origin = Origin::USER;
    std::optional<EntryId> m_Entry; // the entry an ENTRY_* change is about
};

/// Told, on the GUI thread, after a log's mirror has changed, and only after a commit: a failed write notifies nobody.
/// This is a plain C++ interface, not a Qt signal, because only C++ code listens (docs/plan/stage-3.md, "Scope and
/// conventions").
class LogObserver
{
public:
    LogObserver() = default;
    LogObserver(const LogObserver&) = delete;
    LogObserver& operator=(const LogObserver&) = delete;
    virtual ~LogObserver() = default;

    virtual void on_log_changed(const LogDocument& document, const LogChange& change) = 0;
};

} // namespace ej::viewmodel
