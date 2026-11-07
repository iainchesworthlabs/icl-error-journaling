// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/timing_stats.hpp"

#include <algorithm>
#include <vector>

namespace ej::bridge
{

namespace
{

std::string whole(double ms)
{
    return std::to_string(static_cast<long long>(ms + 0.5));
}

} // namespace

void TimingStats::set_launch(double milliseconds)
{
    m_Launch = milliseconds;
}

void TimingStats::record_tap(double milliseconds)
{
    m_Taps.push_back(milliseconds);
    if (m_Taps.size() > WINDOW)
    {
        m_Taps.pop_front();
    }
}

std::optional<double> TimingStats::median_tap_ms() const
{
    if (m_Taps.empty())
    {
        return std::nullopt;
    }
    std::vector<double> sorted(m_Taps.begin(), m_Taps.end());
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
}

std::optional<double> TimingStats::worst_tap_ms() const
{
    if (m_Taps.empty())
    {
        return std::nullopt;
    }
    return *std::max_element(m_Taps.begin(), m_Taps.end());
}

std::string TimingStats::summary() const
{
    std::string text = m_Launch ? "launch " + whole(*m_Launch) + " ms" : std::string("launch not yet usable");
    if (m_Taps.empty())
    {
        return text + "; no taps yet";
    }
    return text + "; tap " + whole(m_Taps.back()) + " ms (last of " + std::to_string(m_Taps.size()) + "), median " +
           whole(*median_tap_ms()) + " ms, worst " + whole(*worst_tap_ms()) + " ms";
}

} // namespace ej::bridge
