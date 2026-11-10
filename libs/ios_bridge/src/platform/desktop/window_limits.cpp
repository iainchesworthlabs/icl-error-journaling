// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/window_limits.hpp"

namespace ej::bridge
{

namespace
{

class DesktopWindowLimits final : public WindowLimits
{
public:
    void set_minimum_size(double /*width*/, double /*height*/) override {}
};

} // namespace

std::unique_ptr<WindowLimits> make_window_limits()
{
    return std::make_unique<DesktopWindowLimits>();
}

} // namespace ej::bridge
