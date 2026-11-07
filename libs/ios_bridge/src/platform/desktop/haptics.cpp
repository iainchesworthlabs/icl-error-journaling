// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/haptics.hpp"

namespace ej::bridge
{

namespace
{

class DesktopHaptics final : public Haptics
{
public:
    void prepare() override {}

    void fire(HapticKind /*kind*/) override {}
};

}  // namespace

std::unique_ptr<Haptics> make_haptics()
{
    return std::make_unique<DesktopHaptics>();
}

}  // namespace ej::bridge
