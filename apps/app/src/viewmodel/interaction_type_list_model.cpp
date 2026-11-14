// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/interaction_type_list_model.hpp"

#include <utility>
#include <vector>

namespace ej::viewmodel
{

QVariant InteractionTypeListModel::data(const QModelIndex& index, int role) const
{
    const InteractionTypeRow* row = row_at(index);
    if (row == nullptr)
    {
        return {};
    }
    switch (role)
    {
    case TYPE_ID:
        return QString::fromStdString(row->m_Id.format());
    case NAME:
    case Qt::DisplayRole:
        return row->m_Name;
    case LINE:
        return row->m_Line;
    case ASKS:
        return row->m_Asks;
    case BOARD_POSITION:
        return row->m_BoardPosition;
    case ON_BOARD:
        return row->m_BoardPosition != 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> InteractionTypeListModel::roleNames() const
{
    return {{TYPE_ID, "typeId"}, {NAME, "name"},
            {LINE, "line"},      {ASKS, "asks"},
            {BOARD_POSITION, "boardPosition"}, {ON_BOARD, "onBoard"}};
}

void InteractionTypeListModel::refresh(const Configuration& configuration)
{
    std::vector<InteractionTypeRow> rows;
    for (const InteractionType& type : configuration.m_InteractionTypes)
    {
        if (type.m_Retired)
        {
            continue;
        }
        rows.push_back({type.m_Id, QString::fromStdString(type.m_Name),
                        type.m_LinePattern ? pattern_to_field(QString::fromStdString(*type.m_LinePattern)) : QString(),
                        QString::fromUtf8(interaction_asks_key(type.m_Asks).data(),
                                          static_cast<qsizetype>(interaction_asks_key(type.m_Asks).size())),
                        type.m_BoardPosition.value_or(0)});
    }
    replace_rows(std::move(rows));
}

QString InteractionTypeListModel::pattern_to_field(const QString& pattern)
{
    QString out;
    for (qsizetype i = 0; i < pattern.size();)
    {
        if (pattern.mid(i, 2) == QLatin1String("{{") || pattern.mid(i, 2) == QLatin1String("}}"))
        {
            out += pattern.mid(i, 2);
            i += 2;
        }
        else if (pattern.mid(i, 8) == QLatin1String("{person}"))
        {
            out += QStringLiteral("[person]");
            i += 8;
        }
        else if (pattern.mid(i, 7) == QLatin1String("{value}"))
        {
            out += QStringLiteral("[answer]");
            i += 7;
        }
        else
        {
            out += pattern[i];
            ++i;
        }
    }
    return out;
}

QString InteractionTypeListModel::pattern_from_field(const QString& field)
{
    QString out = field;
    out.replace(QStringLiteral("[person]"), QStringLiteral("{person}"));
    out.replace(QStringLiteral("[answer]"), QStringLiteral("{value}"));
    return out;
}

} // namespace ej::viewmodel
