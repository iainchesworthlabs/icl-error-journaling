// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/qml_bridge.hpp"

#include <QMetaObject>
#include <QPointer>
#include <filesystem>
#include <utility>

namespace ej::bridge
{

QmlBridge::QmlBridge(PlatformServices services, QObject* parent)
    : QObject(parent), m_Services(std::move(services)), m_Presenter(*m_Services.m_Share)
{
}

QmlBridge::~QmlBridge() = default;

bool QmlBridge::share_available() const noexcept
{
    return m_Services.m_Share->available();
}

bool QmlBridge::sharing() const noexcept
{
    return m_Sharing;
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

void QmlBridge::hapticImpact()
{
    m_Services.m_Haptics->fire(HapticKind::IMPACT);
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
