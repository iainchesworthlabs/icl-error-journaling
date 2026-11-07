// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace ej::bridge
{

/// The two figures of the Stage 5 exit gate, kept so that they can be read off the phone (there is no Mac to run
/// Instruments on): the cold start to a usable board (M1) and, for the last taps, the time from the touch to the haptic
/// call returning (M2). Timings only: no content. Main thread only.
///
/// A tap's figure is the age of the touch when the press handler ran (when the timestamp could be trusted) plus the
/// time the haptic call took, on the monotonic clock. It stops at the call returning and does not include the actuator,
/// which the feel test covers.
class TimingStats
{
public:
    /// How many taps the summary covers.
    static constexpr std::size_t WINDOW = 50;

    void set_launch(double milliseconds);
    void record_tap(double milliseconds);

    [[nodiscard]] std::optional<double> launch_ms() const noexcept
    {
        return m_Launch;
    }
    [[nodiscard]] std::optional<double> last_tap_ms() const noexcept
    {
        return m_Taps.empty() ? std::nullopt : std::optional<double>(m_Taps.back());
    }
    /// The taps in the window (at most WINDOW).
    [[nodiscard]] std::size_t tap_count() const noexcept
    {
        return m_Taps.size();
    }
    [[nodiscard]] std::optional<double> median_tap_ms() const;
    [[nodiscard]] std::optional<double> worst_tap_ms() const;

    /// For the screen and the diagnostics text, for example
    /// `launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms`. "no taps yet" before the first.
    [[nodiscard]] std::string summary() const;

private:
    std::optional<double> m_Launch;
    std::deque<double> m_Taps;
};

} // namespace ej::bridge
