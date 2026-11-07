// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/share.hpp"

namespace ej::bridge
{

namespace
{

class DesktopShare final : public Share
{
public:
    [[nodiscard]] bool available() const noexcept override
    {
        return false;
    }

    void present(ShareRequest /*request*/, ShareCallback done) override
    {
        if (done)
        {
            done(ShareResult{ShareOutcome::UNAVAILABLE, {}, {}});
        }
    }
};

}  // namespace

std::unique_ptr<Share> make_share()
{
    return std::make_unique<DesktopShare>();
}

}  // namespace ej::bridge
