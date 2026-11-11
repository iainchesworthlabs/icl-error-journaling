// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/fake_platform.hpp"

#include <algorithm>
#include <utility>

namespace ej::test
{

namespace bridge = ej::bridge;

std::string_view haptic_name(bridge::HapticKind kind)
{
    switch (kind)
    {
    case bridge::HapticKind::IMPACT:
        return "impact";
    case bridge::HapticKind::SUCCESS:
        return "success";
    }
    return "unknown";
}

std::string_view share_outcome_name(bridge::ShareOutcome outcome)
{
    switch (outcome)
    {
    case bridge::ShareOutcome::SHARED:
        return "shared";
    case bridge::ShareOutcome::CANCELLED:
        return "cancelled";
    case bridge::ShareOutcome::UNAVAILABLE:
        return "unavailable";
    case bridge::ShareOutcome::FAILED:
        return "failed";
    }
    return "unknown";
}

void FakePlatform::present(bridge::ShareRequest request, bridge::ShareCallback done)
{
    m_Texts.push_back(std::move(request.m_Text));
    m_Anchors.push_back(request.m_Anchor);
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

void FakePlatform::fire(haptic_kind kind)
{
    m_Haptics.push_back(kind);
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::HAPTIC, std::string(haptic_name(kind)));
    }
}

void FakePlatform::answer_with(share_outcome outcome, std::string activity_type)
{
    m_Next = share_result{outcome, std::move(activity_type), {}};
}

bool FakePlatform::complete_share_sheet(share_outcome outcome, std::string activity_type)
{
    if (!m_Held)
    {
        return false;
    }
    bridge::ShareCallback done = std::move(*m_Held);
    m_Held.reset();
    finish(std::move(done), share_result{outcome, std::move(activity_type), {}});
    return true;
}

std::size_t FakePlatform::haptic_count(haptic_kind kind) const
{
    return static_cast<std::size_t>(std::ranges::count(m_Haptics, kind));
}

void FakePlatform::finish(bridge::ShareCallback done, const share_result& result)
{
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::SHARE, "outcome " + std::string(share_outcome_name(result.m_Outcome)));
    }
    done(result);
}

} // namespace ej::test
