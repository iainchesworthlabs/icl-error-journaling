// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace ej
{

/// How long a change to the note draft may wait before it is written (section 2.8 of the Stage 2 plan). Text typed in
/// the last 300 ms before a kill can be lost; nothing older can (decision 3).
inline constexpr std::chrono::milliseconds DRAFT_DEBOUNCE{300};

/// A single-shot timer on a monotonic clock. Stage 3 wraps a `QTimer` (which never reads the wall clock) in it; the
/// tests use one they advance by hand, so no test sleeps. The wall clock has no part in the timing: a clock that steps
/// neither fires the timer early nor stalls it.
class DraftTimer
{
public:
    using Callback = std::function<void()>;

    DraftTimer() = default;
    DraftTimer(const DraftTimer&) = delete;
    DraftTimer& operator=(const DraftTimer&) = delete;
    virtual ~DraftTimer() = default;

    /// Arms the timer: `on_expired` runs once, after `delay` of monotonic time, on the thread that owns the store.
    /// Replaces anything armed before.
    virtual void start(std::chrono::milliseconds delay, Callback on_expired) = 0;

    /// Disarms the timer. Nothing runs afterwards until the next `start`.
    virtual void stop() = 0;
};

struct DraftStoreOptions
{
    /// Called when a write the timer started fails (a disk that is full, a locked file). The text stays pending and the
    /// write is tried again after the same delay. A failure of `flush` is returned to its caller instead.
    std::function<void(const StorageError&)> m_OnWriteError;
    std::chrono::milliseconds m_Delay = DRAFT_DEBOUNCE;
};

/// The note field's draft (NOTE-03). `set` is called on every change of the field; it keeps the text and arms a timer,
/// and when the timer expires the latest text is written to the single `draft` row in a transaction of its own (WAL,
/// synchronous FULL). Changes that arrive while the timer is armed only replace the text, so one write covers them, and
/// no text waits longer than the delay to be written: typing without a pause never starves the write. `flush` writes at
/// once; Stage 5 calls it when the app goes inactive. An empty string clears the draft.
///
/// Committing a note clears the draft in the same transaction as the entry (`LogRepository::add_entry`), so a kill
/// cannot leave both. The caller then calls `committed`, which drops what is pending and stops the timer, so a write
/// that was about to happen cannot bring the draft back.
///
/// The draft is not journalled: a corrupt database loses it (NFR-08 and decision 3). Use from one thread, the one that
/// owns the repository and the timer.
class DraftStore
{
public:
    using Options = DraftStoreOptions;

    /// The repository, the clock (for the time stored with the draft) and the timer must outlive the store.
    DraftStore(LogRepository& repository, const Clock& clock, DraftTimer& timer, Options options = {});

    /// Disarms the timer and writes nothing: the owner flushes first when the text must not be lost.
    ~DraftStore();

    DraftStore(const DraftStore&) = delete;
    DraftStore& operator=(const DraftStore&) = delete;

    /// Reads the stored draft, for the note field to show at launch. Also tells the store what the row holds, so that
    /// setting the same text again writes nothing.
    [[nodiscard]] Result<std::optional<std::string>, StorageError> load();

    /// A change of the field. Refused (nothing kept) when the text is not well-formed UTF-8 or is longer than a note
    /// may be. Otherwise it only keeps the text and arms the timer if it is not armed already, and cannot fail.
    [[nodiscard]] Result<void, StorageError> set(std::string_view text);

    /// Writes what is pending now and disarms the timer. Nothing pending: nothing to do. On failure the text stays
    /// pending and the timer is armed again.
    [[nodiscard]] Result<void, StorageError> flush();

    /// A note was committed and the repository cleared the draft with it: drops what is pending and disarms the timer.
    void committed();

    [[nodiscard]] bool has_pending() const
    {
        return m_Pending.has_value();
    }

    /// The text in the field as far as the store knows: what is pending, else what was last written.
    [[nodiscard]] std::optional<std::string> current() const;

private:
    void arm();
    void on_timer();
    [[nodiscard]] Result<void, StorageError> write_pending();

    LogRepository& m_Repository;
    const Clock& m_Clock;
    DraftTimer& m_Timer;
    Options m_Options;
    std::optional<std::string> m_Pending; // set, not written yet
    std::optional<std::string> m_Written; // what the row holds as far as is known; nothing for no row
    bool m_Armed = false;
};

} // namespace ej
