// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 9: until package 9.6 has an Android implementation of this capability it is the desktop stub (a copy of
// ../desktop/background_tasks.cpp). check_platform_layout.py wants one file per capability in every platform directory.
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
