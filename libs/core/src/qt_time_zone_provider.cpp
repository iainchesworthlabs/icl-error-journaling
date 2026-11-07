// SPDX-License-Identifier: GPL-3.0-or-later
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

#include "error_journaling/time_zone_provider.hpp"

namespace ej
{
namespace
{

QTimeZone zone_for(std::string_view name)
{
    const QByteArray id(name.data(), static_cast<qsizetype>(name.size()));
    if (name.empty() || !QTimeZone::isTimeZoneIdAvailable(id))
    {
        return {};
    }
    return QTimeZone(id);
}

class QtTimeZoneProvider final : public TimeZoneProvider
{
public:
    [[nodiscard]] Result<LocalTime, Error> to_local(const Timestamp& timestamp) const override
    {
        const QTimeZone zone = zone_for(timestamp.m_Zone);
        if (!zone.isValid())
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const QDateTime local = QDateTime::fromMSecsSinceEpoch(timestamp.m_Utc.time_since_epoch().count(), zone);
        const QDate date = local.date();
        const QTime time = local.time();
        return LocalTime{
            std::chrono::year_month_day{std::chrono::year{date.year()},
                std::chrono::month{static_cast<unsigned>(date.month())},
                std::chrono::day{static_cast<unsigned>(date.day())}},
            std::chrono::seconds{time.hour() * 3600 + time.minute() * 60 + time.second()}};
    }

    [[nodiscard]] Result<UtcMillis, Error> to_utc(const LocalTime& local, std::string_view zone_name) const override
    {
        const QTimeZone zone = zone_for(zone_name);
        if (!zone.isValid())
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const int seconds = static_cast<int>(local.m_SinceMidnight.count());
        const QDate date(static_cast<int>(local.m_Date.year()), static_cast<int>(unsigned{local.m_Date.month()}),
            static_cast<int>(unsigned{local.m_Date.day()}));
        const QTime time(seconds / 3600, (seconds / 60) % 60, seconds % 60);
        // RelativeToBefore reads the time with the offset in force before the transition: the first of two
        // repeated times, and in a gap the instant after it.
        const QDateTime resolved(date, time, zone, QDateTime::TransitionResolution::RelativeToBefore);
        if (!resolved.isValid())
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        return UtcMillis{std::chrono::milliseconds{resolved.toMSecsSinceEpoch()}};
    }
};

}  // namespace

std::unique_ptr<TimeZoneProvider> make_qt_time_zone_provider()
{
    return std::make_unique<QtTimeZoneProvider>();
}

}  // namespace ej
