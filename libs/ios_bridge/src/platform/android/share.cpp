// SPDX-License-Identifier: GPL-3.0-or-later
// The Android share sheet. The chooser is started in Java (EjShare.java, in apps/app/packaging/android/); this file
// passes the request over and brings the outcome back to the main thread. The decisions (inline text or a file, how the
// chooser's end maps to a ShareOutcome) are in src/android_share_plan.cpp, which a test covers on the desktop.
#include "error_journaling/android_share_plan.hpp"
#include "error_journaling/share.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QString>
#include <QtCore/qnativeinterface.h>

#include <map>
#include <string>
#include <utility>

namespace ej::bridge
{

namespace
{

constexpr const char* java_class = "com/iainchesworthlabs/fieldtest/EjShare";

// The sheets in flight, by the number given to Java. Main thread only.
std::map<jlong, ShareCallback>& pending()
{
    static std::map<jlong, ShareCallback> sheets;
    return sheets;
}

jlong g_NextId = 1;

void finish_on_main_thread(jlong id, jint code, const QString& detail)
{
    auto& sheets = pending();
    const auto it = sheets.find(id);
    if (it == sheets.end())
    {
        return;
    }
    ShareCallback done = std::move(it->second);
    sheets.erase(it);

    const AndroidShareEnd end = android_share_end_from_code(code);
    ShareResult result;
    result.m_Outcome = android_share_outcome(end);
    if (end == AndroidShareEnd::CHOSEN)
    {
        result.m_ActivityType = detail.toStdString(); // the package of the app the person chose
    }
    else if (end == AndroidShareEnd::ERROR)
    {
        result.m_Detail = detail.toStdString(); // an exception class, never text or a path
    }
    if (done)
    {
        done(result);
    }
}

// EjShare.nativeFinished: called on the Android UI thread, which is not Qt's main thread.
void JNICALL native_finished(JNIEnv*, jclass, jlong id, jint code, jstring detail)
{
    const QString text = detail != nullptr ? QJniObject(detail).toString() : QString();
    QMetaObject::invokeMethod(QCoreApplication::instance(), [id, code, text] { finish_on_main_thread(id, code, text); },
                              Qt::QueuedConnection);
}

bool register_natives()
{
    static const bool registered = []
    {
        const JNINativeMethod methods[] = {
            {"nativeFinished", "(JILjava/lang/String;)V", reinterpret_cast<void*>(&native_finished)}};
        QJniEnvironment env;
        return env.registerNativeMethods(java_class, methods, 1);
    }();
    return registered;
}

// A long text goes out as a file in the same directory as the configuration export (ConfigExporter), which the launch
// purge empties and EjFileProvider serves. Returns the path, or an empty string.
QString write_text_file(const std::string& text)
{
    const QString directory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("ej-share"));
    if (!QDir().mkpath(directory))
    {
        return {};
    }
    QFile file(QDir(directory).filePath(QStringLiteral("field-test-log.txt")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
    {
        return {};
    }
    return file.fileName();
}

class AndroidShare final : public Share
{
public:
    [[nodiscard]] bool available() const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool retains_shared_files() const noexcept override
    {
        return true;
    }

    void present(ShareRequest request, ShareCallback done) override
    {
        QString path;
        QString text;
        switch (plan_android_share(request))
        {
        case AndroidShareKind::TEXT:
            text = QString::fromStdString(request.m_Text);
            break;
        case AndroidShareKind::TEXT_FILE:
            path = write_text_file(request.m_Text);
            break;
        case AndroidShareKind::FILE:
            path = QString::fromStdU16String(request.m_File.u16string());
            break;
        }
        const bool needs_file = request.m_Text.size() > android_inline_text_limit || !request.m_File.empty();
        if ((needs_file && path.isEmpty()) || !register_natives())
        {
            fail_later(std::move(done), "preparing the share");
            return;
        }

        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        const jlong id = g_NextId++;
        pending()[id] = std::move(done);
        QJniEnvironment env;
        QJniObject::callStaticMethod<void>(
            java_class, "present", "(Landroid/app/Activity;JLjava/lang/String;Ljava/lang/String;)V", activity.object(), id,
            text.isNull() ? nullptr : QJniObject::fromString(text).object<jstring>(),
            path.isEmpty() ? nullptr : QJniObject::fromString(path).object<jstring>());
        if (env.checkAndClearExceptions())
        {
            finish_on_main_thread(id, 2, QStringLiteral("JNI"));
        }
    }

private:
    // The interface says `done` is called after present has returned, as on iOS.
    static void fail_later(ShareCallback done, const char* detail)
    {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [done = std::move(done), detail]
            {
                if (done)
                {
                    done(ShareResult{ShareOutcome::FAILED, {}, detail});
                }
            },
            Qt::QueuedConnection);
    }
};

} // namespace

std::unique_ptr<Share> make_share()
{
    return std::make_unique<AndroidShare>();
}

} // namespace ej::bridge
