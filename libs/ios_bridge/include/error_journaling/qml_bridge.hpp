// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptic_policy.hpp"
#include "error_journaling/platform_services.hpp"
#include "error_journaling/share_presenter.hpp"
#include "error_journaling/timing_stats.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <chrono>
#include <cstdint>
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
    /// The text size multiplier from the system's content size category (Dynamic Type): 1.0 by default, about 0.82
    /// to 3.1. `Theme` multiplies its font sizes by it. 1.0 where there is no such setting.
    Q_PROPERTY(double textScale READ text_scale NOTIFY textScaleChanged)
    /// Reduce Motion is on: replace springs and slides with a short fade.
    Q_PROPERTY(bool reduceMotion READ reduce_motion NOTIFY reduceMotionChanged)
    /// VoiceOver is running. For tests and diagnostics.
    Q_PROPERTY(bool voiceOverRunning READ voice_over_running NOTIFY voiceOverRunningChanged)
    /// The device can turn speech into text on the device itself (Dictation::available). False where it cannot, and QML
    /// then shows no microphone.
    Q_PROPERTY(bool dictationAvailable READ dictation_available CONSTANT)
    /// Listening now: from `startDictation` until `dictationEnded`.
    Q_PROPERTY(bool dictating READ dictating NOTIFY dictatingChanged)
    /// The exit-gate figures as one line (launch time; last, median and worst of the recent taps), to read off the
    /// phone.
    Q_PROPERTY(QString timingSummary READ timing_summary NOTIFY timingChanged)

public:
    explicit QmlBridge(PlatformServices services, HapticPolicyConfig haptics = {}, QObject* parent = nullptr);
    ~QmlBridge() override;

    [[nodiscard]] bool share_available() const noexcept;
    [[nodiscard]] bool sharing() const noexcept;
    [[nodiscard]] bool dictation_available() const noexcept;
    [[nodiscard]] bool dictating() const noexcept;
    [[nodiscard]] double text_scale() const noexcept;
    [[nodiscard]] bool reduce_motion() const noexcept;
    [[nodiscard]] bool voice_over_running() const noexcept;
    [[nodiscard]] QString timing_summary() const;

    /// The board became usable `milliseconds` after the process was created (M1). Called by main through the launch
    /// tracker.
    void set_launch_ms(double milliseconds);

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
    /// The shell's board accepts taps (the Log tab is shown and the session has loaded): the end of the launch timing.
    /// Emits `boardEnabled`, which the application connects to the launch tracker. Safe to call again.
    Q_INVOKABLE void reportBoardEnabled();
    /// Starts dictation: asks for the microphone and speech recognition if they have not been asked for, then listens.
    /// Does nothing while listening, or where `dictationAvailable` is false. It ends when `stopDictation` is called, when
    /// nothing is heard for `DICTATION_SILENCE_LIMIT`, when the application goes to the background, or on an error, and
    /// always with one `dictationEnded`. The recogniser may end a run by itself after a pause in the speech (Android
    /// does); the bridge then starts another so that one tap covers a whole note, and `dictationSegment` marks each start.
    Q_INVOKABLE void startDictation();
    /// Ends dictation (the second tap on the microphone). Does nothing when not listening.
    Q_INVOKABLE void stopDictation();
    /// How long dictation waits in silence before it stops by itself.
    static constexpr std::chrono::milliseconds DICTATION_SILENCE_LIMIT{60'000};
    /// For tests: the silence limit of this bridge.
    void set_dictation_silence_limit(std::chrono::milliseconds limit);

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
    void dictatingChanged();
    /// A new stretch of speech begins: the text that follows is separate from what came before it.
    void dictationSegment();
    /// The words of the current stretch so far, each call replacing the one before it.
    void dictationText(const QString& text);
    /// Dictation ended. `reason` is "finished", "denied", "unavailable" or "failed".
    void dictationEnded(const QString& reason);
    /// One signal for each setting that changed, and none when the value is the same.
    void textScaleChanged();
    void reduceMotionChanged();
    void voiceOverRunningChanged();
    /// The timing summary changed. Emitted on a queued call, never inside the press handler.
    void timingChanged();
    /// The shell reported that the board accepts taps.
    void boardEnabled();

private:
    void begin_share(ShareRequest request, bool temporary_file);
    void refresh_sharing();
    void queue_timing_changed();
    void refresh_accessibility();
    void deliver(const ShareResult& result);
    void begin_dictation_segment();
    void end_dictation(const QString& reason);

    PlatformServices m_Services;
    HapticPolicy m_HapticPolicy;
    TimingStats m_Timing;
    bool m_TimingNotifyQueued = false;
    QTimer m_KeepAlive;
    SharePresenter m_Presenter;
    bool m_Sharing = false;
    AccessibilityState m_Accessibility;
    bool m_Dictating = false;
    bool m_DictationStopping = false;
    std::uint64_t m_DictationRun = 0; ///< Counts runs, so that a late callback of an earlier one is ignored.
    std::chrono::milliseconds m_DictationSilenceLimit = DICTATION_SILENCE_LIMIT;
    std::chrono::steady_clock::time_point m_DictationSegmentStart;
    QTimer m_DictationSilence;
};

} // namespace ej::bridge
