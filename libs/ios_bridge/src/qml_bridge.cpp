// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/qml_bridge.hpp"

#include <QMetaObject>
#include <QPointer>
#include <chrono>
#include <filesystem>
#include <utility>

namespace ej::bridge
{

namespace
{

/// A touch older than this when the press handler runs is not a touch age but a timestamp on another time base.
constexpr double MAX_TOUCH_AGE_MS = 10000.0;
/// A dictation run shorter than this that ends by itself did not listen.
constexpr std::chrono::milliseconds MIN_DICTATION_SEGMENT{400};
/// How long a stopped run may take to deliver its last words before it is closed.
constexpr std::chrono::milliseconds DICTATION_STOP_GRACE{1500};

} // namespace

QmlBridge::QmlBridge(PlatformServices services, HapticPolicyConfig haptics, QObject* parent)
    : QObject(parent), m_Services(std::move(services)),
      m_HapticPolicy(
          *m_Services.m_Haptics,
          []
          {
              return std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch());
          },
          haptics),
      m_Presenter(*m_Services.m_Share)
{
    // The settings as they are now, and a re-read on every notice. The handler is guarded, so a notice after the bridge
    // is gone does nothing (the settings object is destroyed with the services, and an iOS one removes its observers).
    m_Accessibility = m_Services.m_Accessibility->read();
    m_Services.m_Accessibility->set_change_handler(
        [self = QPointer<QmlBridge>(this)]
        {
            if (self)
            {
                self->refresh_accessibility();
            }
        });
    // Silence for DICTATION_SILENCE_LIMIT ends dictation.
    m_DictationSilence.setSingleShot(true);
    connect(&m_DictationSilence, &QTimer::timeout, this,
            [this]
            {
                if (m_DictationStopping)
                {
                    end_dictation(QStringLiteral("finished"));
                }
                else
                {
                    stopDictation();
                }
            });
    // The keep-alive is off unless the device pass asks for it (HAPTIC_KEEPALIVE); then one timer ticks the policy.
    if (m_HapticPolicy.keep_alive_enabled())
    {
        m_KeepAlive.setInterval(m_HapticPolicy.interval());
        connect(&m_KeepAlive, &QTimer::timeout, this, [this] { m_HapticPolicy.tick(); });
        m_KeepAlive.start();
    }
}

QmlBridge::~QmlBridge() = default;

bool QmlBridge::dictation_available() const noexcept
{
    return m_Services.m_Dictation != nullptr && m_Services.m_Dictation->available();
}

bool QmlBridge::dictating() const noexcept
{
    return m_Dictating;
}

void QmlBridge::set_dictation_silence_limit(std::chrono::milliseconds limit)
{
    m_DictationSilenceLimit = limit;
}

void QmlBridge::startDictation()
{
    if (m_Dictating || !dictation_available())
    {
        return;
    }
    m_Dictating = true;
    m_DictationStopping = false;
    emit dictatingChanged();
    m_DictationSilence.start(m_DictationSilenceLimit);
    begin_dictation_segment();
}

void QmlBridge::begin_dictation_segment()
{
    const std::uint64_t run = ++m_DictationRun;
    emit dictationSegment();
    m_DictationSegmentStart = std::chrono::steady_clock::now();
    DictationHandlers handlers;
    handlers.m_Text = [self = QPointer<QmlBridge>(this), run](const std::string& text)
    {
        if (self && self->m_Dictating && self->m_DictationRun == run)
        {
            // Speech was heard: the silence starts again.
            if (!self->m_DictationStopping)
            {
                self->m_DictationSilence.start(self->m_DictationSilenceLimit);
            }
            emit self->dictationText(QString::fromStdString(text));
        }
    };
    handlers.m_Ended = [self = QPointer<QmlBridge>(this), run](DictationEnd end)
    {
        if (!self || !self->m_Dictating || self->m_DictationRun != run)
        {
            return;
        }
        switch (end)
        {
        case DictationEnd::FINISHED:
        case DictationEnd::NO_SPEECH:
            // A run that ends at once, again and again, is a recogniser that cannot listen: that is a failure, not a
            // pause, and it must not spin.
            if (!self->m_DictationStopping &&
                std::chrono::steady_clock::now() - self->m_DictationSegmentStart < MIN_DICTATION_SEGMENT)
            {
                self->end_dictation(QStringLiteral("failed"));
                return;
            }
            if (!self->m_DictationStopping && self->m_DictationSilence.isActive())
            {
                // The recogniser stopped by itself (a pause in the speech). One tap covers the whole note, so listen
                // again until the person stops it or the silence limit is reached.
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, run]
                    {
                        if (self && self->m_Dictating && self->m_DictationRun == run && !self->m_DictationStopping)
                        {
                            self->begin_dictation_segment();
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }
            self->end_dictation(QStringLiteral("finished"));
            return;
        case DictationEnd::DENIED:
            self->end_dictation(QStringLiteral("denied"));
            return;
        case DictationEnd::UNAVAILABLE:
            self->end_dictation(QStringLiteral("unavailable"));
            return;
        case DictationEnd::FAILED:
            self->end_dictation(QStringLiteral("failed"));
            return;
        }
    };
    m_Services.m_Dictation->start(std::move(handlers));
}

void QmlBridge::stopDictation()
{
    if (!m_Dictating || m_DictationStopping)
    {
        return;
    }
    m_DictationStopping = true;
    // The recogniser sends the last words after it is told to stop, so the run stays open until it ends (FINISHED) or
    // DICTATION_STOP_GRACE has passed, whichever is first.
    m_DictationSilence.start(DICTATION_STOP_GRACE);
    m_Services.m_Dictation->stop();
}

void QmlBridge::end_dictation(const QString& reason)
{
    if (!m_Dictating)
    {
        return;
    }
    m_Dictating = false;
    m_DictationStopping = false;
    ++m_DictationRun;
    m_DictationSilence.stop();
    emit dictatingChanged();
    emit dictationEnded(reason);
}

bool QmlBridge::share_available() const noexcept
{
    return m_Services.m_Share->available();
}

bool QmlBridge::sharing() const noexcept
{
    return m_Sharing;
}

double QmlBridge::text_scale() const noexcept
{
    return m_Accessibility.m_TextScale;
}

bool QmlBridge::reduce_motion() const noexcept
{
    return m_Accessibility.m_ReduceMotion;
}

bool QmlBridge::voice_over_running() const noexcept
{
    return m_Accessibility.m_VoiceOverRunning;
}

QString QmlBridge::timing_summary() const
{
    return QString::fromStdString(m_Timing.summary());
}

void QmlBridge::set_launch_ms(double milliseconds)
{
    m_Timing.set_launch(milliseconds);
    queue_timing_changed();
}

void QmlBridge::queue_timing_changed()
{
    if (m_TimingNotifyQueued)
    {
        return;
    }
    m_TimingNotifyQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]
        {
            m_TimingNotifyQueued = false;
            emit timingChanged();
        },
        Qt::QueuedConnection);
}

void QmlBridge::refresh_accessibility()
{
    const AccessibilityState now = m_Services.m_Accessibility->read();
    const AccessibilityState before = m_Accessibility;
    m_Accessibility = now;
    if (!qFuzzyCompare(now.m_TextScale, before.m_TextScale))
    {
        emit textScaleChanged();
    }
    if (now.m_ReduceMotion != before.m_ReduceMotion)
    {
        emit reduceMotionChanged();
    }
    if (now.m_VoiceOverRunning != before.m_VoiceOverRunning)
    {
        emit voiceOverRunningChanged();
    }
}

void QmlBridge::shareText(const QString& text)
{
    ShareRequest request;
    request.m_Text = text.toUtf8().toStdString();
    begin_share(std::move(request), false);
}

void QmlBridge::shareTemporaryFile(const QString& path)
{
    ShareRequest request;
    request.m_File = std::filesystem::path(path.toStdU16String());
    begin_share(std::move(request), true);
}

void QmlBridge::begin_share(ShareRequest request, bool temporary_file)
{
    // The guard keeps a late completion from reaching a bridge that has gone.
    QPointer<QmlBridge> self(this);
    auto done = [self](const ShareResult& result)
    {
        // Always a queued call: the stub completes before present returns and a phone after, and a caller sees one
        // order.
        if (self)
        {
            QMetaObject::invokeMethod(
                self.data(),
                [self, result]
                {
                    if (self)
                    {
                        self->deliver(result);
                    }
                },
                Qt::QueuedConnection);
        }
    };

    if (temporary_file)
    {
        m_Presenter.present_temporary_file(request.m_File, std::move(done));
    }
    else
    {
        m_Presenter.present(std::move(request), std::move(done));
    }
    refresh_sharing();
}

void QmlBridge::refresh_sharing()
{
    // `sharing` follows the presenter, so a refused request (answered FAILED while another sheet is up) leaves it
    // alone.
    if (m_Sharing != m_Presenter.in_flight())
    {
        m_Sharing = m_Presenter.in_flight();
        emit sharingChanged();
    }
}

void QmlBridge::deliver(const ShareResult& result)
{
    refresh_sharing();
    emit shareFinished(outcome_name(result.m_Outcome), QString::fromStdString(result.m_ActivityType));
}

void QmlBridge::prepareHaptics()
{
    m_Services.m_Haptics->prepare();
}

void QmlBridge::setBoardVisible(bool visible)
{
    m_HapticPolicy.set_board_visible(visible);
}

void QmlBridge::reportBoardEnabled()
{
    emit boardEnabled();
}

void QmlBridge::setApplicationActive(bool active)
{
    m_HapticPolicy.set_application_active(active);
    // Nothing listens from the background.
    if (!active)
    {
        stopDictation();
    }
}

void QmlBridge::hapticImpact(double pressTimestamp)
{
    PerfMarks& marks = *m_Services.m_PerfMarks;
    double age = 0;
    if (pressTimestamp > 0)
    {
        // How old the touch already is. A timestamp on another time base (assumption A5 not holding) gives a nonsense
        // age, which is dropped here rather than recorded.
        const double candidate = marks.uptime_ms() - pressTimestamp;
        if (candidate >= 0 && candidate < MAX_TOUCH_AGE_MS)
        {
            age = candidate;
            marks.event(Mark::TOUCH_AGE, age);
        }
    }
    // Nothing but the fire between the marks: the engine prepares itself afterwards (haptics.mm), so no prepare sits in
    // this path. The call is timed on the monotonic uptime clock.
    const double before = marks.uptime_ms();
    marks.begin(Mark::TAP_TO_HAPTIC);
    m_Services.m_Haptics->fire(HapticKind::IMPACT);
    marks.end(Mark::TAP_TO_HAPTIC);
    const double after = marks.uptime_ms();
    // The figure of the gate (M2): touch to the call returning. The actuator is not in it. Recorded after the call, and
    // shown on a queued call, so neither adds to the path being measured.
    m_Timing.record_tap(age + (after - before));
    queue_timing_changed();
}

void QmlBridge::hapticSuccess()
{
    m_Services.m_Haptics->fire(HapticKind::SUCCESS);
}

QString QmlBridge::outcome_name(ShareOutcome outcome)
{
    switch (outcome)
    {
    case ShareOutcome::SHARED:
        return QStringLiteral("shared");
    case ShareOutcome::CANCELLED:
        return QStringLiteral("cancelled");
    case ShareOutcome::UNAVAILABLE:
        return QStringLiteral("unavailable");
    case ShareOutcome::FAILED:
        break;
    }
    return QStringLiteral("failed");
}

} // namespace ej::bridge
