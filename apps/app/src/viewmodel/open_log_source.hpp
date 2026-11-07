// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace ej::viewmodel
{

class LogDocument; // work package 3.2

/// Told when the open log is a different document: it has just loaded, or a new log has replaced it (a new day).
class OpenLogObserver
{
public:
    OpenLogObserver() = default;
    OpenLogObserver(const OpenLogObserver&) = delete;
    OpenLogObserver& operator=(const OpenLogObserver&) = delete;
    virtual ~OpenLogObserver() = default;

    /// `document` is the open log now, or null when there is none (loading, or the configuration is missing).
    virtual void on_open_log_changed(LogDocument* document) = 0;
};

/// Gives the models the open log and tells them when another takes its place, so that `BoardModel`, `TimelineModel`,
/// `UndoController` and `DayWatcher` follow a new log without depending on `LogSession`, which implements it (3.4).
class OpenLogSource
{
public:
    OpenLogSource() = default;
    OpenLogSource(const OpenLogSource&) = delete;
    OpenLogSource& operator=(const OpenLogSource&) = delete;
    virtual ~OpenLogSource() = default;

    /// The open log, or null while it is loading or when no log can be started yet.
    [[nodiscard]] virtual LogDocument* open_log() const = 0;

    /// Observers are told in the order they were added. An observer that is added is not told about the current
    /// document: it reads `open_log()` itself.
    virtual void add_observer(OpenLogObserver& observer) = 0;
    virtual void remove_observer(OpenLogObserver& observer) = 0;
};

} // namespace ej::viewmodel
