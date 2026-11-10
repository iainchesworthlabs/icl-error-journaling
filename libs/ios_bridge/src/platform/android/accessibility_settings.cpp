// SPDX-License-Identifier: GPL-3.0-or-later
// Android accessibility settings (docs/plan/stage-9.md, "Accessibility settings"): the text size, Remove animations and
// TalkBack, read through EjAccessibility.java, and a notice when one changes. What a number means is decided in
// src/android_system_plan.cpp, which a test covers on the desktop.
#include "error_journaling/accessibility_settings.hpp"
#include "error_journaling/android_system_plan.hpp"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QtCore/qcoreapplication_platform.h>

#include <functional>
#include <memory>
#include <utility>

namespace ej::bridge
{

namespace
{

constexpr const char* java_class = "com/iainchesworthlabs/errorjournaling/EjAccessibility";

// What the change notice calls. It outlives the object that owns it in the closure of a queued call, so that nothing is
// called after the object is gone. Main thread only.
struct State
{
    std::function<void()> m_Handler;
};

std::weak_ptr<State>& current()
{
    static std::weak_ptr<State> state;
    return state;
}

// EjAccessibility.nativeChanged: called on the Android UI thread, which is not Qt's main thread.
void JNICALL native_changed(JNIEnv*, jclass)
{
    if (QCoreApplication::instance() == nullptr)
    {
        return;
    }
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        []
        {
            if (const auto state = current().lock(); state && state->m_Handler)
            {
                state->m_Handler();
            }
        },
        Qt::QueuedConnection);
}

// Registers the native, then asks Java to watch the settings. Once; a failure is tried again at the next call, because the
// activity may not exist yet at start-up.
bool start_java()
{
    static bool started = false;
    if (started)
    {
        return true;
    }
    started = []
    {
        const JNINativeMethod methods[] = {{"nativeChanged", "()V", reinterpret_cast<void*>(&native_changed)}};
        QJniEnvironment env;
        if (!env.registerNativeMethods(java_class, methods, 1))
        {
            return false;
        }
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
        {
            return false;
        }
        QJniObject::callStaticMethod<void>(java_class, "start", "(Landroid/app/Activity;)V", activity.object());
        return !env.checkAndClearExceptions();
    }();
    return started;
}

class AndroidAccessibilitySettings final : public AccessibilitySettings
{
public:
    AndroidAccessibilitySettings() : m_State(std::make_shared<State>())
    {
        current() = m_State;
        start_java();
    }

    ~AndroidAccessibilitySettings() override
    {
        m_State->m_Handler = nullptr;
    }

    [[nodiscard]] AccessibilityState read() const override
    {
        // The default size, no Reduce Motion and no screen reader if the system cannot be asked.
        AccessibilityState state;
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
        {
            return state;
        }
        QJniEnvironment env;
        const jdouble font_scale = QJniObject::callStaticMethod<jdouble>(java_class, "fontScale",
                                                                         "(Landroid/app/Activity;)D", activity.object());
        const jdouble animator_scale = QJniObject::callStaticMethod<jdouble>(
            java_class, "animatorScale", "(Landroid/app/Activity;)D", activity.object());
        const jboolean touch_exploration = QJniObject::callStaticMethod<jboolean>(
            java_class, "touchExploration", "(Landroid/app/Activity;)Z", activity.object());
        if (env.checkAndClearExceptions())
        {
            return state;
        }
        state.m_TextScale = android_text_scale(font_scale);
        state.m_ReduceMotion = android_reduce_motion(animator_scale);
        state.m_VoiceOverRunning = touch_exploration != JNI_FALSE; // TalkBack is what VoiceOver is on iOS
        return state;
    }

    void set_change_handler(std::function<void()> changed) override
    {
        m_State->m_Handler = std::move(changed);
    }

private:
    std::shared_ptr<State> m_State;
};

} // namespace

std::unique_ptr<AccessibilitySettings> make_accessibility_settings()
{
    return std::make_unique<AndroidAccessibilitySettings>();
}

} // namespace ej::bridge
