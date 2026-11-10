// SPDX-License-Identifier: GPL-3.0-or-later
// Android has no grace period to ask for: an application that has stopped keeps running until the system needs its memory,
// so this is the desktop implementation (a copy of ../desktop/background_tasks.cpp, which check_platform_layout.py wants in
// every platform directory): it hands out tokens and does nothing else. The flush on going inactive is the portable
// LifecycleCoordinator's, and the recovery of Stage 2 is the backstop for a process the system ends
// (docs/plan/stage-9.md, "Lifecycle").
#include "error_journaling/background_tasks.hpp"

namespace ej::bridge
{

namespace
{

class DesktopBackgroundTasks final : public BackgroundTasks
{
public:
    Token begin(std::string_view /*name*/, std::function<void()> /*on_expiry*/) override
    {
        return ++m_Last;
    }

    void end(Token /*token*/) override {}

private:
    Token m_Last = 0;
};

} // namespace

std::unique_ptr<BackgroundTasks> make_background_tasks()
{
    return std::make_unique<DesktopBackgroundTasks>();
}

} // namespace ej::bridge
