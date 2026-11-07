// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/feedback_sink.hpp"

namespace ej::viewmodel
{

namespace bridge = error_journaling::bridge;

void PlatformFeedbackSink::prepare_haptics()
{
    m_Platform.prepare_haptics();
}

void PlatformFeedbackSink::acknowledge_touch()
{
    m_Platform.fire_haptic(bridge::haptic_kind::impact);
}

void PlatformFeedbackSink::entry_recorded(EntryId)
{
    // Confirmed by the count, the toast and the "Last" bar. No haptic: the impact already answered the touch.
}

void PlatformFeedbackSink::note_saved(EntryId)
{
    m_Platform.fire_haptic(bridge::haptic_kind::success);
}

} // namespace ej::viewmodel
