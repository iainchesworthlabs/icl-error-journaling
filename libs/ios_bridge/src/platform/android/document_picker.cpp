// SPDX-License-Identifier: GPL-3.0-or-later
// The Android document picker and "Open with" (docs/plan/stage-9.md, "File picker and open-with"). EjDocuments.java shows
// the picker, receives a file from another app and copies it into a private directory; this file passes the request over
// and brings the result back to the main thread. A file another app sends is posted as a QFileOpenEvent, the event Qt's
// iOS plugin posts for AirDrop, so OpenFileFilter and the importer take it from there unchanged. The decisions are in
// src/android_import_plan.cpp, which a test covers on the desktop.
#include "error_journaling/android_import_plan.hpp"
#include "error_journaling/document_picker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileOpenEvent>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtCore/qcoreapplication_platform.h>

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace ej::bridge
{

namespace
{

constexpr const char* java_class = "com/iainchesworthlabs/fieldtest/EjDocuments";

using PickCallback = std::function<void(const PickResult&)>;

// The pickers in flight, by the number given to Java. Main thread only.
std::map<jlong, PickCallback>& pending()
{
    static std::map<jlong, PickCallback> pickers;
    return pickers;
}

jlong g_NextId = 1;

void finish_on_main_thread(jlong id, jint code, const QString& detail)
{
    auto& pickers = pending();
    const auto it = pickers.find(id);
    if (it == pickers.end())
    {
        return;
    }
    PickCallback done = std::move(it->second);
    pickers.erase(it);

    PickResult result;
    result.m_Outcome = android_pick_outcome_from_code(code);
    if (result.m_Outcome == PickOutcome::PICKED)
    {
        result.m_Path = std::filesystem::path(detail.toStdU16String()); // the private copy
    }
    if (done)
    {
        done(result);
    }
}

// EjDocuments.nativePicked: called on the Android UI thread or a copy thread, neither of which is Qt's main thread.
void JNICALL native_picked(JNIEnv*, jclass, jlong id, jint code, jstring detail)
{
    const QString text = detail != nullptr ? QJniObject(detail).toString() : QString();
    QMetaObject::invokeMethod(QCoreApplication::instance(), [id, code, text] { finish_on_main_thread(id, code, text); },
                              Qt::QueuedConnection);
}

// EjDocuments.nativeOpened: a file another app sent ("Open with", "Share to"), already copied. Posting is thread-safe.
void JNICALL native_opened(JNIEnv*, jclass, jstring path)
{
    if (path == nullptr || QCoreApplication::instance() == nullptr)
    {
        return;
    }
    QCoreApplication::postEvent(QCoreApplication::instance(), new QFileOpenEvent(QJniObject(path).toString()));
}

// Registers the natives, then tells Java where the copies go, which also starts the copy of a file that arrived before
// this (the application was started by "Open with"). Once; a failure is tried again at the next call, because the activity
// may not exist yet at start-up.
bool configure_java()
{
    static bool configured = false;
    if (configured)
    {
        return true;
    }
    configured = []
    {
        const JNINativeMethod methods[] = {
            {"nativePicked", "(JILjava/lang/String;)V", reinterpret_cast<void*>(&native_picked)},
            {"nativeOpened", "(Ljava/lang/String;)V", reinterpret_cast<void*>(&native_opened)}};
        QJniEnvironment env;
        if (!env.registerNativeMethods(java_class, methods, 2))
        {
            return false;
        }
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid())
        {
            return false;
        }
        const QString directory =
            QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                .filePath(QString::fromUtf8(android_import_directory_name.data(),
                                            static_cast<qsizetype>(android_import_directory_name.size())));
        QJniObject::callStaticMethod<void>(java_class, "configure", "(Landroid/app/Activity;Ljava/lang/String;J)V",
                                           activity.object(), QJniObject::fromString(directory).object<jstring>(),
                                           static_cast<jlong>(android_import_copy_cap_bytes));
        return !env.checkAndClearExceptions();
    }();
    return configured;
}

QString mime_types_csv()
{
    QStringList types;
    for (const std::string_view type : android_picker_mime_types)
    {
        types.append(QString::fromUtf8(type.data(), static_cast<qsizetype>(type.size())));
    }
    return types.join(QLatin1Char(','));
}

class AndroidDocumentPicker final : public DocumentPicker
{
public:
    AndroidDocumentPicker()
    {
        // At start-up, so that a file the application was opened with is copied and queued before the first screen.
        configure_java();
    }

    void pick_configuration(PickCallback done) override
    {
        if (!configure_java())
        {
            fail_later(std::move(done));
            return;
        }
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const jlong id = g_NextId++;
        pending()[id] = std::move(done);
        QJniEnvironment env;
        QJniObject::callStaticMethod<void>(java_class, "pick", "(Landroid/app/Activity;JLjava/lang/String;)V",
                                           activity.object(), id, QJniObject::fromString(mime_types_csv()).object<jstring>());
        if (env.checkAndClearExceptions())
        {
            finish_on_main_thread(id, 2, QStringLiteral("JNI"));
        }
    }

private:
    // The interface says `done` is called after pick_configuration has returned.
    static void fail_later(PickCallback done)
    {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [done = std::move(done)]
            {
                if (done)
                {
                    done(PickResult{PickOutcome::FAILED, {}});
                }
            },
            Qt::QueuedConnection);
    }
};

} // namespace

std::unique_ptr<DocumentPicker> make_document_picker()
{
    return std::make_unique<AndroidDocumentPicker>();
}

} // namespace ej::bridge
