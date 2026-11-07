// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ios_bridge.hpp"

#include <QObject>
#include <QString>

#include <memory>

namespace error_journaling::bridge
{

/// What QML sees of the bridge. Created in main.cpp and handed to the root item as an initial property. It owns the
/// platform services and lives on the GUI thread.
class qml_bridge : public QObject
{
    Q_OBJECT
    /// False on the desktop stub, so the interface can hide the share control there.
    Q_PROPERTY(bool shareAvailable READ share_available CONSTANT)

public:
    explicit qml_bridge(std::unique_ptr<platform_services> services, QObject* parent = nullptr);
    ~qml_bridge() override;

    [[nodiscard]] bool share_available() const noexcept;

    /// Presents the share sheet with the fixed spike text. Ends with share_finished.
    Q_INVOKABLE void shareSpikeText();
    /// Call when a tile appears, so the first press has no start-up delay.
    Q_INVOKABLE void prepareHaptics();
    /// Call from onPressed (touch-down), not from onClicked or onReleased.
    Q_INVOKABLE void hapticImpact();
    /// Call when a note has been saved.
    Q_INVOKABLE void hapticSuccess();

    /// "shared", "cancelled", "unavailable" or "failed".
    [[nodiscard]] static QString outcome_name(share_outcome outcome);

signals:
    /// `activityType` is empty when no activity was chosen.
    void shareFinished(const QString& outcome, const QString& activityType);

private:
    std::unique_ptr<platform_services> m_Services;
};

}  // namespace error_journaling::bridge
