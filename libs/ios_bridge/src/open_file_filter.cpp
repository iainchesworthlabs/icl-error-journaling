// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/open_file_filter.hpp"

#include <QEvent>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QUrl>
#include <utility>

namespace ej::bridge
{

OpenFileFilter::OpenFileFilter(std::function<void(std::string_view)> log, QObject* parent)
    : QObject(parent), m_Log(std::move(log))
{
}

void OpenFileFilter::ready()
{
    m_Ready = true;
    deliver();
}

void OpenFileFilter::handled()
{
    m_Busy = false;
    deliver();
}

bool OpenFileFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::FileOpen)
    {
        return QObject::eventFilter(watched, event);
    }
    if (m_Log)
    {
        m_Log("open: file event received");
    }
    const auto* open = static_cast<QFileOpenEvent*>(event);
    const QUrl url = open->url();
    if (url.isEmpty() || !url.isLocalFile())
    {
        return true;
    }
    const QString path = url.toLocalFile();
    if (path.isEmpty() || QFileInfo(path).isDir())
    {
        return true;
    }
    m_Pending.append(path);
    deliver();
    return true;
}

void OpenFileFilter::deliver()
{
    if (!m_Ready || m_Busy || m_Pending.isEmpty())
    {
        return;
    }
    m_Busy = true;
    emit fileOpened(m_Pending.takeFirst());
}

} // namespace ej::bridge
