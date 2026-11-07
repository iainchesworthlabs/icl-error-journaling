// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

#include "error_journaling/result.hpp"
#include "error_journaling/timestamp.hpp"

namespace ej
{

/// Converts between UTC instants and local wall-clock time in an IANA zone. The core never reads the platform tz
/// database directly; production code uses `make_qt_time_zone_provider()`, tests use a fake with fixed offsets.
class TimeZoneProvider
{
public:
    enum class Error
    {
        UNKNOWN_ZONE
    };

    virtual ~TimeZoneProvider() = default;

    /// The local date and time of `timestamp.m_Utc` in `timestamp.m_Zone`, truncated to whole seconds.
    [[nodiscard]] virtual Result<LocalTime, Error> to_local(const Timestamp& timestamp) const = 0;

    /// The instant a local time refers to, for edited times. In a repeated hour the earlier instant is returned. A
    /// local time inside a spring-forward gap resolves to the instant after the gap; it is never an error.
    [[nodiscard]] virtual Result<UtcMillis, Error> to_utc(const LocalTime& local, std::string_view zone) const = 0;
};

/// Implemented in the one file that includes `QTimeZone`.
[[nodiscard]] std::unique_ptr<TimeZoneProvider> make_qt_time_zone_provider();

}  // namespace ej
