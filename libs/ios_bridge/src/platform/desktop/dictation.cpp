// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/dictation.hpp"

namespace ej::bridge
{

namespace
{

class DesktopDictation final : public Dictation
{
public:
    [[nodiscard]] bool available() const noexcept override
    {
        return false;
    }

    void start(DictationHandlers handlers) override
    {
        if (handlers.m_Ended)
        {
            handlers.m_Ended(DictationEnd::UNAVAILABLE);
        }
    }

    void stop() override {}
};

}  // namespace

std::unique_ptr<Dictation> make_dictation()
{
    return std::make_unique<DesktopDictation>();
}

}  // namespace ej::bridge
