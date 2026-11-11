// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

namespace ej::bridge
{

/// How small the system may make the application's window. iPadOS lets a Stage Manager window be made smaller than any
/// phone; the application's layout is tested down to a minimum (docs/plan/stage-8.md, "Multitasking"), and Qt does not
/// pass a window's minimum size on to iOS, so the platform is told here. Every call is made on the main thread.
class WindowLimits
{
public:
    WindowLimits() = default;
    WindowLimits(const WindowLimits&) = delete;
    WindowLimits& operator=(const WindowLimits&) = delete;
    virtual ~WindowLimits() = default;

    /// Asks the system not to make the application's windows smaller than `width` by `height` points. It applies to the
    /// window scenes that are connected when it is called, so the application calls it again when it becomes active. A
    /// phone has no resizable window and ignores it. Safe to call as often as is wanted.
    virtual void set_minimum_size(double width, double height) = 0;
};

/// ios/window_limits.mm or desktop/window_limits.cpp (does nothing: the window's own `minimumWidth` and `minimumHeight`
/// are what a desktop honours).
[[nodiscard]] std::unique_ptr<WindowLimits> make_window_limits();

} // namespace ej::bridge
