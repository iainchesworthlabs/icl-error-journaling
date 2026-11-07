// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig/services.hpp"

#include <QAbstractItemModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <chrono>

namespace ej::ui
{

namespace
{

QVariant role_of(const QAbstractItemModel& model, int row, const char* role)
{
    const QHash<int, QByteArray> names = model.roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
    {
        if (it.value() == role)
        {
            return model.data(model.index(row, 0), it.key());
        }
    }
    return {};
}

int row_with_label(const QAbstractItemModel& model, const QString& label)
{
    for (int row = 0; row < model.rowCount(); ++row)
    {
        if (role_of(model, row, "label").toString() == label)
        {
            return row;
        }
    }
    return -1;
}

} // namespace

void use(UiRig& rig, test::Rig& services)
{
    rig.on_advance([&services](std::chrono::milliseconds by) { services.advance(by); });
    rig.on_settle([&services] { services.settle(); });
}

QVariantMap screen_properties(test::Rig& services, const QVariantMap& more)
{
    QVariantMap properties{
        {QStringLiteral("session"), QVariant::fromValue<QObject*>(&services.session())},
        {QStringLiteral("undo"), QVariant::fromValue<QObject*>(&services.undo())},
        {QStringLiteral("board"), QVariant::fromValue<QObject*>(&services.board())},
        {QStringLiteral("picker"), QVariant::fromValue<QObject*>(&services.picker())},
        {QStringLiteral("timeline"), QVariant::fromValue<QObject*>(&services.timeline())},
        {QStringLiteral("history"), QVariant::fromValue<QObject*>(&services.history())},
        {QStringLiteral("day"), QVariant::fromValue<QObject*>(&services.day())},
        {QStringLiteral("settings"), QVariant::fromValue<QObject*>(&services.settings())},
        {QStringLiteral("share"), QVariant::fromValue<QObject*>(&services.share())},
    };
    for (auto it = more.begin(); it != more.end(); ++it)
    {
        properties.insert(it.key(), it.value());
    }
    return properties;
}

QString type_id(test::Rig& services, const QString& label)
{
    const int row = row_with_label(services.board(), label);
    return row < 0 ? QString() : role_of(services.board(), row, "typeId").toString();
}

int board_count(test::Rig& services, const QString& label)
{
    const int row = row_with_label(services.board(), label);
    return row < 0 ? -1 : role_of(services.board(), row, "count").toInt();
}

} // namespace ej::ui
