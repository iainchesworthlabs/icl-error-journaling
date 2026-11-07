// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ej::test
{

/// What kind of thing an `EventLog` entry records.
enum class EventKind
{
    COMMIT,        // a repository call that changed the store succeeded (RecordingRepository)
    HAPTIC,        // a haptic fired: "impact", "success" (FakePlatform)
    CONFIRMATION,  // a FeedbackSink confirmation: "entry_recorded <id>", "note_saved <id>" (FakeFeedbackSink)
    TOAST,         // the toast opened, changed or closed (a test connects its signal to the log)
    SHARE,         // the share sheet: "opened", "outcome shared" (FakePlatform)
    CLIPBOARD,     // text put on the clipboard (FakeClipboard)
    WRITE_FAILED,  // a service reported a failed write
    LOG_CHANGE,    // a LogObserver was told (FakeLogObserver)
    CONFIGURATION, // a ConfigurationObserver was told (FakeConfigurationObserver)
    MARK           // anything else a test wants to put in the sequence
};

[[nodiscard]] std::string_view event_kind_name(EventKind kind);

struct Event
{
    EventKind m_Kind = EventKind::MARK;
    std::string m_Detail;

    friend bool operator==(const Event&, const Event&) = default;
};

/// The order in which things happened, for the tests of the order of feedback: the touch-down haptic, the commit, the
/// confirmation, the toast (docs/plan/stage-3.md, "The exit-gate test"). The fakes write into one log and the test
/// reads it. Safe to write from the storage thread, because `RecordingRepository` does.
class EventLog
{
public:
    void record(EventKind kind, std::string detail = {});

    /// A copy of every event so far, in order.
    [[nodiscard]] std::vector<Event> events() const;
    [[nodiscard]] std::size_t size() const;

    [[nodiscard]] std::size_t count(EventKind kind) const;
    [[nodiscard]] std::size_t count(EventKind kind, std::string_view detail) const;

    /// The details of the events of one kind, in order.
    [[nodiscard]] std::vector<std::string> details(EventKind kind) const;

    /// The position of the first / last event of this kind and detail.
    [[nodiscard]] std::optional<std::size_t> first_index(EventKind kind, std::string_view detail) const;
    [[nodiscard]] std::optional<std::size_t> last_index(EventKind kind, std::string_view detail) const;

    /// True when both events exist and the first one of `before` comes before the first one of `after`.
    [[nodiscard]] bool occurred_before(const Event& before, const Event& after) const;

    /// One line per event, "0 HAPTIC impact", for the message of a failed check.
    [[nodiscard]] std::string format() const;

    void clear();

private:
    mutable std::mutex m_Mutex;
    std::vector<Event> m_Events;
};

} // namespace ej::test
