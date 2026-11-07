// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptic_policy.hpp"
#include "error_journaling/platform_services.hpp"
#include "error_journaling/share_presenter.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

namespace ej::bridge
{

/// What QML sees of the bridge. Created in main.cpp and handed to the root item as an initial property. It owns the
/// platform services and lives on the GUI thread. QML-facing names are camelCase; everything else follows the project
/// naming rules.
class QmlBridge : public QObject
{
    Q_OBJECT
    /// False on the desktop stub, so the interface can hide the share control there.
    Q_PROPERTY(bool shareAvailable READ share_available CONSTANT)
    /// True from a share request until its sheet has ended. QML disables the Share button while it is true.
    Q_PROPERTY(bool sharing READ sharing NOTIFY sharingChanged)

public:
    explicit QmlBridge(PlatformServices services, HapticPolicyConfig haptics = {}, QObject* parent = nullptr);
    ~QmlBridge() override;

    [[nodiscard]] bool share_available() const noexcept;
    [[nodiscard]] bool sharing() const noexcept;

    /// Shares `text` (UTF-8 on the way to the sheet). Always ends with shareFinished, delivered on a queued call, so a
    /// caller sees one order on every platform. A request while a sheet is up ends "failed" without reaching the sheet.
    Q_INVOKABLE void shareText(const QString& text);

    /// Shares the file at `path` and removes it when the sheet has ended, whatever the outcome (the configuration
    /// export). Ends with shareFinished like shareText.
    Q_INVOKABLE void shareTemporaryFile(const QString& path);

    /// Wakes the haptic engine now, whatever the board and application state. The board calls setBoardVisible and the
    /// window setApplicationActive instead and lets the policy decide; this stays for a caller that knows better.
    Q_INVOKABLE void prepareHaptics();
    /// The board became visible or hidden. Prepares the haptics when it becomes visible while the application is
    /// active.
    Q_INVOKABLE void setBoardVisible(bool visible);
    /// The application became active or inactive. Prepares the haptics when it becomes active while the board is
    /// visible.
    Q_INVOKABLE void setApplicationActive(bool active);
    /// Fires the impact haptic. Call it from onPressed (touch-down), never from onClicked or onReleased: the entry is
    /// recorded on the press too, and a finger sliding off must not fire a haptic without an entry. `pressTimestamp` is
    /// the touch's event timestamp in milliseconds (`point.pressTimestamp` of the pointer handler), used only for the
    /// timing marks: the time from the touch to this call is the TOUCH_AGE mark. 0 when unknown.
    Q_INVOKABLE void hapticImpact(double pressTimestamp = 0);
    /// Call when a note has been saved.
    Q_INVOKABLE void hapticSuccess();

    /// "shared", "cancelled", "unavailable" or "failed".
    [[nodiscard]] static QString outcome_name(ShareOutcome outcome);

signals:
    /// `activityType` is empty when no activity was chosen.
    void shareFinished(const QString& outcome, const QString& activityType);
    void sharingChanged();

private:
    void begin_share(ShareRequest request, bool temporary_file);
    void refresh_sharing();
    void deliver(const ShareResult& result);

    PlatformServices m_Services;
    HapticPolicy m_HapticPolicy;
    QTimer m_KeepAlive;
    SharePresenter m_Presenter;
    bool m_Sharing = false;
};

} // namespace ej::bridge
