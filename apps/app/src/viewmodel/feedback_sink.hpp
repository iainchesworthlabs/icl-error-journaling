// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptics.hpp"
#include "error_journaling/ids.hpp"

namespace ej::viewmodel
{

/// Everything the tester feels that is not drawn by a model: the haptics. The calls fall into two groups, and the
/// group says when a call may be made (Stage 3, "Threading and the order of feedback"; Stage 3 decision 2).
///
/// Touch acknowledgement, before any commit. The impact haptic answers the finger, not the store: it fires on
/// touch-down, ahead of the write it belongs to, and is made whether or not that write later succeeds. It is not a
/// confirmation that an entry is saved. Whoever handles the press calls it: in the product QML does, through the
/// `qml_bridge` (Stage 5, 5.2 and 5.7), and the headless tests do through their rig. No service calls it from a write.
///   - `prepare_haptics`, `acknowledge_touch`
///
/// Confirmation, only after a successful commit. The services call these from the completion of a write that
/// succeeded, and from nowhere else; on a failed write none of them is called.
///   - `entry_recorded`, `note_saved`
///
/// The count, the toast and the "Last" bar are models, not feedback calls, and they follow the same rule.
class FeedbackSink
{
public:
    FeedbackSink() = default;
    FeedbackSink(const FeedbackSink&) = delete;
    FeedbackSink& operator=(const FeedbackSink&) = delete;
    virtual ~FeedbackSink() = default;

    /// Wakes the haptic engine so that the next touch has no start-up delay. Call it ahead of the touch, when the board
    /// appears.
    virtual void prepare_haptics() = 0;

    /// The impact haptic, fired on touch-down. Touch acknowledgement, not confirmation.
    virtual void acknowledge_touch() = 0;

    /// An error entry has been committed.
    virtual void entry_recorded(EntryId entry) = 0;

    /// A note entry has been committed: the success haptic.
    virtual void note_saved(EntryId entry) = 0;
};

/// Does nothing. For a rig that needs no feedback.
class NullFeedbackSink final : public FeedbackSink
{
public:
    void prepare_haptics() override {}
    void acknowledge_touch() override {}
    void entry_recorded(EntryId) override {}
    void note_saved(EntryId) override {}
};

/// The production sink: the bridge's `Haptics` fires the haptics. The impact haptic is the touch acknowledgement and
/// the success haptic is for a saved note only; a recorded error entry is confirmed by the count, the toast and the
/// "Last" bar, and makes no haptic of its own.
class PlatformFeedbackSink final : public FeedbackSink
{
public:
    explicit PlatformFeedbackSink(ej::bridge::Haptics& haptics) : m_Haptics(haptics) {}

    void prepare_haptics() override;
    void acknowledge_touch() override;
    void entry_recorded(EntryId entry) override;
    void note_saved(EntryId entry) override;

private:
    ej::bridge::Haptics& m_Haptics;
};

} // namespace ej::viewmodel
