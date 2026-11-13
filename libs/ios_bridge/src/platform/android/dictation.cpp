// SPDX-License-Identifier: GPL-3.0-or-later
// Android dictation: the on-device SpeechRecognizer (Android 13 and later), through EjDictation.java. The microphone
// permission is asked for through Qt's QMicrophonePermission at the first tap, never at launch. Where the phone has no
// on-device recogniser for the language, `available()` is false and the microphone is not shown: there is no fallback to
// the recogniser that may use a server.
//
// Not run on a device: CI builds the package and looks inside it.
#include "error_journaling/dictation.hpp"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QPermission>
#include <QString>
#include <QtCore/qcoreapplication_platform.h>
#include <string>
#include <utility>

namespace ej::bridge
{

namespace
{

constexpr const char* JAVA_CLASS = "com/iainchesworthlabs/fieldtest/EjDictation";

class AndroidDictation;
AndroidDictation* g_Instance = nullptr;

class AndroidDictation final : public Dictation
{
public:
    AndroidDictation()
    {
        g_Instance = this;
    }

    ~AndroidDictation() override
    {
        g_Instance = nullptr;
    }

    [[nodiscard]] bool available() const noexcept override
    {
        if (!m_Checked)
        {
            const QJniObject activity = QNativeInterface::QAndroidApplication::context();
            if (!activity.isValid())
            {
                return false;
            }
            m_Available = QJniObject::callStaticMethod<jboolean>(JAVA_CLASS, "isAvailable", "(Landroid/content/Context;)Z",
                                                                activity.object()) != JNI_FALSE;
            QJniEnvironment().checkAndClearExceptions();
            m_Checked = true;
        }
        return m_Available;
    }

    void start(DictationHandlers handlers) override
    {
        m_Handlers = std::move(handlers);
        const jlong id = ++m_Id;
        if (!available() || !register_natives())
        {
            finish_later(id, DictationEnd::UNAVAILABLE);
            return;
        }
        QCoreApplication* app = QCoreApplication::instance();
        app->requestPermission(QMicrophonePermission{}, app,
                               [this, id](const QPermission& permission)
                               {
                                   if (id != m_Id)
                                   {
                                       return;
                                   }
                                   if (permission.status() != Qt::PermissionStatus::Granted)
                                   {
                                       finish(id, DictationEnd::DENIED);
                                       return;
                                   }
                                   const QJniObject activity = QNativeInterface::QAndroidApplication::context();
                                   QJniObject::callStaticMethod<void>(JAVA_CLASS, "start", "(Landroid/app/Activity;J)V",
                                                                      activity.object(), id);
                                   if (QJniEnvironment().checkAndClearExceptions())
                                   {
                                       finish(id, DictationEnd::FAILED);
                                   }
                               });
    }

    void stop() override
    {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (activity.isValid())
        {
            QJniObject::callStaticMethod<void>(JAVA_CLASS, "stop", "(Landroid/app/Activity;)V", activity.object());
            QJniEnvironment().checkAndClearExceptions();
        }
    }

    // Called on the main thread, from the queued natives.
    void on_text(jlong id, const std::string& text)
    {
        if (id == m_Id && m_Handlers.m_Text)
        {
            m_Handlers.m_Text(text);
        }
    }

    void on_ended(jlong id, int code)
    {
        static constexpr DictationEnd ends[] = {DictationEnd::FINISHED, DictationEnd::NO_SPEECH, DictationEnd::DENIED,
                                                DictationEnd::UNAVAILABLE, DictationEnd::FAILED};
        finish(id, code >= 0 && code < 5 ? ends[code] : DictationEnd::FAILED);
    }

private:
    void finish(jlong id, DictationEnd end)
    {
        if (id != m_Id)
        {
            return;
        }
        ++m_Id;
        DictationHandlers handlers = std::move(m_Handlers);
        m_Handlers = {};
        if (handlers.m_Ended)
        {
            handlers.m_Ended(end);
        }
    }

    // The callback is never made from inside start().
    void finish_later(jlong id, DictationEnd end)
    {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [this, id, end] { finish(id, end); }, Qt::QueuedConnection);
    }

    static void JNICALL native_text(JNIEnv*, jclass, jlong id, jstring text)
    {
        const std::string utf8 = text != nullptr ? QJniObject(text).toString().toStdString() : std::string();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [id, utf8]
            {
                if (g_Instance != nullptr)
                {
                    g_Instance->on_text(id, utf8);
                }
            },
            Qt::QueuedConnection);
    }

    static void JNICALL native_ended(JNIEnv*, jclass, jlong id, jint code)
    {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [id, code]
            {
                if (g_Instance != nullptr)
                {
                    g_Instance->on_ended(id, code);
                }
            },
            Qt::QueuedConnection);
    }

    static bool register_natives()
    {
        static const bool registered = []
        {
            const JNINativeMethod methods[] = {
                {"nativeText", "(JLjava/lang/String;)V", reinterpret_cast<void*>(&native_text)},
                {"nativeEnded", "(JI)V", reinterpret_cast<void*>(&native_ended)}};
            QJniEnvironment env;
            return env.registerNativeMethods(JAVA_CLASS, methods, 2);
        }();
        return registered;
    }

    mutable bool m_Checked = false;
    mutable bool m_Available = false;
    jlong m_Id = 0;
    DictationHandlers m_Handlers;
};

} // namespace

std::unique_ptr<Dictation> make_dictation()
{
    return std::make_unique<AndroidDictation>();
}

} // namespace ej::bridge
