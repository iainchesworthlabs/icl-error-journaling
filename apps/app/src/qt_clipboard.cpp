// SPDX-License-Identifier: GPL-3.0-or-later
#include "qt_clipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QString>

namespace ej::app
{

void QtClipboard::set_text(std::string_view text)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
    {
        return;
    }
    clipboard->setText(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
}

} // namespace ej::app
