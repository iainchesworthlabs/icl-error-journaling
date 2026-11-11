// SPDX-License-Identifier: GPL-3.0-or-later
// Android window limits. Android has no call that sets a window's minimum size at run time: the system reads it from the
// manifest (the <layout> element's minWidth and minHeight, which multi-window and free-form windows honour), and a phone
// has no resizable window. So this does nothing, like the desktop one, and the minimum for Android is decided with the
// window work (docs/plan/stage-9.md, package 9.7), where a manifest value can be tested on a tablet and a foldable.
#include "error_journaling/window_limits.hpp"

namespace ej::bridge
{

namespace
{

class AndroidWindowLimits final : public WindowLimits
{
public:
    void set_minimum_size(double /*width*/, double /*height*/) override {}
};

} // namespace

std::unique_ptr<WindowLimits> make_window_limits()
{
    return std::make_unique<AndroidWindowLimits>();
}

} // namespace ej::bridge
