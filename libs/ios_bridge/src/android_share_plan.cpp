// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/android_share_plan.hpp"

namespace ej::bridge
{

AndroidShareKind plan_android_share(const ShareRequest& request) noexcept
{
    if (!request.m_File.empty())
    {
        return AndroidShareKind::FILE;
    }
    return request.m_Text.size() > android_inline_text_limit ? AndroidShareKind::TEXT_FILE : AndroidShareKind::TEXT;
}

ShareOutcome android_share_outcome(AndroidShareEnd end) noexcept
{
    switch (end)
    {
    case AndroidShareEnd::CHOSEN:
        return ShareOutcome::SHARED;
    case AndroidShareEnd::DISMISSED:
        return ShareOutcome::CANCELLED;
    case AndroidShareEnd::ERROR:
        break;
    }
    return ShareOutcome::FAILED;
}

AndroidShareEnd android_share_end_from_code(int code) noexcept
{
    switch (code)
    {
    case 0:
        return AndroidShareEnd::CHOSEN;
    case 1:
        return AndroidShareEnd::DISMISSED;
    default:
        return AndroidShareEnd::ERROR;
    }
}

} // namespace ej::bridge
