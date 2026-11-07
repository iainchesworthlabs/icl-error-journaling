// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/ios_bridge.hpp"

#include <utility>

namespace error_journaling::bridge
{

namespace
{

class desktop_stub final : public platform_services
{
public:
    void present_share_sheet(std::string_view /*text*/, share_callback done) override
    {
        if (done)
        {
            done(share_result{share_outcome::unavailable, {}});
        }
    }

    void prepare_haptics() override {}

    void fire_haptic(haptic_kind /*kind*/) override {}
};

}  // namespace

std::string_view implementation_name() noexcept
{
    return "desktop-stub";
}

std::unique_ptr<platform_services> make_platform_services()
{
    return std::make_unique<desktop_stub>();
}

}  // namespace error_journaling::bridge
