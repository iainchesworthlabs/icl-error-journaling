// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/feedback_sink.hpp"

namespace ej::viewmodel
{

namespace bridge = ej::bridge;

void PlatformFeedbackSink::prepare_haptics()
{
    m_Haptics.prepare();
}

void PlatformFeedbackSink::acknowledge_touch()
{
    m_Haptics.fire(bridge::HapticKind::IMPACT);
}

void PlatformFeedbackSink::entry_recorded(EntryId)
{
    // Confirmed by the count, the toast and the "Last" bar. No haptic: the impact already answered the touch.
}

void PlatformFeedbackSink::note_saved(EntryId)
{
    m_Haptics.fire(bridge::HapticKind::SUCCESS);
}

} // namespace ej::viewmodel
