// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace ej::bridge
{

/// A short grace period from the system to finish work when the application is going to the background
/// (`beginBackgroundTaskWithName:expirationHandler:`). Every call is made on the main thread, and the expiry handler
/// runs on it.
class BackgroundTasks
{
public:
    using Token = std::uint64_t;

    BackgroundTasks() = default;
    BackgroundTasks(const BackgroundTasks&) = delete;
    BackgroundTasks& operator=(const BackgroundTasks&) = delete;
    virtual ~BackgroundTasks() = default;

    /// Starts a task. `on_expiry` is called if the system's time runs out before `end`; the caller then ends the task.
    /// A token of 0 means the system gave none (the platform has no such thing, or refused), and `end(0)` does nothing.
    virtual Token begin(std::string_view name, std::function<void()> on_expiry) = 0;

    /// Ends a task. Once per token.
    virtual void end(Token token) = 0;
};

/// ios/background_tasks.mm or desktop/background_tasks.cpp (hands out tokens and does nothing else).
[[nodiscard]] std::unique_ptr<BackgroundTasks> make_background_tasks();

} // namespace ej::bridge
