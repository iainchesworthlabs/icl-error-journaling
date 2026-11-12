// SPDX-License-Identifier: GPL-3.0-or-later
// Android haptics: View.performHapticFeedback on the activity's window, through EjHaptics.java (it needs the Android UI
// thread). No VIBRATE permission is needed, and the system's "Touch feedback" setting decides whether it is felt, which
// is the Android counterpart of the iOS system haptics switch. Which constant a kind uses is decided in
// src/android_haptics_plan.cpp, which a test covers on the desktop.
#include "error_journaling/android_haptics_plan.hpp"
#include "error_journaling/haptics.hpp"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace ej::bridge
{

namespace
{

class AndroidHaptics final : public Haptics
{
public:
    // Android has no generator to warm up.
    void prepare() override {}

    void fire(HapticKind kind) override
    {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
        {
            return;
        }
        const int constant = android_haptic_constant(kind, QNativeInterface::QAndroidApplication::sdkVersion());
        QJniObject::callStaticMethod<void>("com/iainchesworthlabs/fieldtest/EjHaptics", "fire",
                                           "(Landroid/app/Activity;I)V", activity.object(), static_cast<jint>(constant));
        // A failed haptic is not worth more than a log line from the JNI layer.
        QJniEnvironment().checkAndClearExceptions();
    }
};

} // namespace

std::unique_ptr<Haptics> make_haptics()
{
    return std::make_unique<AndroidHaptics>();
}

} // namespace ej::bridge
