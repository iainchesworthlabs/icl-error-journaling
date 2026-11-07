// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/fake_platform.hpp"

#include <algorithm>
#include <utility>

namespace ej::test
{

namespace bridge = error_journaling::bridge;

std::string_view haptic_name(bridge::haptic_kind kind)
{
    switch (kind)
    {
    case bridge::haptic_kind::impact:
        return "impact";
    case bridge::haptic_kind::success:
        return "success";
    }
    return "unknown";
}

std::string_view share_outcome_name(bridge::share_outcome outcome)
{
    switch (outcome)
    {
    case bridge::share_outcome::shared:
        return "shared";
    case bridge::share_outcome::cancelled:
        return "cancelled";
    case bridge::share_outcome::unavailable:
        return "unavailable";
    case bridge::share_outcome::failed:
        return "failed";
    }
    return "unknown";
}

void FakePlatform::present_share_sheet(std::string_view text, bridge::share_callback done)
{
    m_Texts.emplace_back(text);
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::SHARE, "opened");
    }
    if (m_Hold)
    {
        m_Held = std::move(done);
        return;
    }
    finish(std::move(done), m_Next);
}

void FakePlatform::fire_haptic(haptic_kind kind)
{
    m_Haptics.push_back(kind);
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::HAPTIC, std::string(haptic_name(kind)));
    }
}

void FakePlatform::answer_with(share_outcome outcome, std::string activity_type)
{
    m_Next = share_result{outcome, std::move(activity_type)};
}

bool FakePlatform::complete_share_sheet(share_outcome outcome, std::string activity_type)
{
    if (!m_Held)
    {
        return false;
    }
    bridge::share_callback done = std::move(*m_Held);
    m_Held.reset();
    finish(std::move(done), share_result{outcome, std::move(activity_type)});
    return true;
}

std::size_t FakePlatform::haptic_count(haptic_kind kind) const
{
    return static_cast<std::size_t>(std::ranges::count(m_Haptics, kind));
}

void FakePlatform::finish(bridge::share_callback done, const share_result& result)
{
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::SHARE, "outcome " + std::string(share_outcome_name(result.outcome)));
    }
    done(result);
}

} // namespace ej::test
