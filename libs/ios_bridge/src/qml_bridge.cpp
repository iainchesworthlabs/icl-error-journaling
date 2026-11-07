// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/qml_bridge.hpp"

#include <QPointer>

#include <utility>

namespace error_journaling::bridge
{

qml_bridge::qml_bridge(std::unique_ptr<platform_services> services, QObject* parent)
    : QObject(parent)
    , m_Services(std::move(services))
{
}

qml_bridge::~qml_bridge() = default;

bool qml_bridge::share_available() const noexcept
{
    return implementation_name() != "desktop-stub";
}

void qml_bridge::shareSpikeText()
{
    // The guard keeps a late completion from reaching a bridge that has gone.
    QPointer<qml_bridge> self(this);
    m_Services->present_share_sheet(spike_share_text, [self](const share_result& result) {
        if (self)
        {
            emit self->shareFinished(outcome_name(result.outcome), QString::fromStdString(result.activity_type));
        }
    });
}

void qml_bridge::prepareHaptics()
{
    m_Services->prepare_haptics();
}

void qml_bridge::hapticImpact()
{
    m_Services->fire_haptic(haptic_kind::impact);
}

void qml_bridge::hapticSuccess()
{
    m_Services->fire_haptic(haptic_kind::success);
}

QString qml_bridge::outcome_name(share_outcome outcome)
{
    switch (outcome)
    {
    case share_outcome::shared:
        return QStringLiteral("shared");
    case share_outcome::cancelled:
        return QStringLiteral("cancelled");
    case share_outcome::unavailable:
        return QStringLiteral("unavailable");
    case share_outcome::failed:
        break;
    }
    return QStringLiteral("failed");
}

}  // namespace error_journaling::bridge
