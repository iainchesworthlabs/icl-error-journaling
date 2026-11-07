// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/error_picker_model.hpp"

#include "viewmodel/log_document.hpp"

#include <QList>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <utility>

namespace ej::viewmodel
{

ErrorPickerModel::ErrorPickerModel(ConfigurationSource& configuration, OpenLogSource& open_log,
                                   ConfigurationEditor& editor, QObject* parent)
    : QAbstractListModel(parent), m_Configuration(configuration), m_Editor(editor),
      m_Log(open_log, [this](const LogChange& change) { on_open_log_event(change); })
{
    m_Rows = build();
    m_Configuration.add_observer(*this);
}

ErrorPickerModel::~ErrorPickerModel()
{
    m_Configuration.remove_observer(*this);
}

int ErrorPickerModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_Rows.size());
}

QVariant ErrorPickerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= m_Rows.size())
    {
        return {};
    }
    const Row& row = m_Rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case TYPE_ID:
        return row.m_TypeId;
    case LABEL:
    case Qt::DisplayRole:
        return row.m_Label;
    case SECTION:
        return static_cast<int>(row.m_Section);
    case COUNT:
        return row.m_Count;
    case COLOUR_SLOT:
        return row.m_ColourSlot;
    default:
        return {};
    }
}

QHash<int, QByteArray> ErrorPickerModel::roleNames() const
{
    return {{TYPE_ID, "typeId"}, {LABEL, "label"}, {SECTION, "section"}, {COUNT, "count"}, {COLOUR_SLOT, "colourSlot"}};
}

void ErrorPickerModel::set_filter(const QString& filter)
{
    if (filter == m_Filter)
    {
        return;
    }
    m_Filter = filter;
    beginResetModel();
    m_Rows = build();
    endResetModel();
    emit filterChanged();
}

QString ErrorPickerModel::addType(const QString& name)
{
    auto added = m_Editor.add_error_type(name.trimmed().toStdString());
    if (!added)
    {
        set_add_type_error(QString::fromStdString(added.error().m_Message));
        return {};
    }
    set_add_type_error({});
    return QString::fromStdString(added->format());
}

void ErrorPickerModel::set_add_type_error(QString message)
{
    if (message == m_AddTypeError)
    {
        return;
    }
    m_AddTypeError = std::move(message);
    emit addTypeErrorChanged();
}

std::vector<ErrorPickerModel::Row> ErrorPickerModel::build() const
{
    const Configuration& configuration = m_Configuration.configuration();
    const BoardLayout layout = board_layout(configuration);
    const QString needle = m_Filter.trimmed().toCaseFolded();

    std::unordered_map<ErrorTypeId, int> counts;
    const LogDocument* document = m_Log.document();
    if (document != nullptr && document->ready())
    {
        counts = live_error_counts(document->log());
    }

    std::vector<Row> off_board;
    std::vector<Row> on_board;
    for (std::size_t i = 0; i < configuration.m_ErrorTypes.size(); ++i)
    {
        const ErrorType& type = configuration.m_ErrorTypes[i];
        if (type.m_Retired)
        {
            continue;
        }
        Row row;
        row.m_Label = QString::fromStdString(type.m_Name);
        if (!needle.isEmpty() && !row.m_Label.toCaseFolded().contains(needle))
        {
            continue;
        }
        row.m_TypeId = QString::fromStdString(type.m_Id.format());
        row.m_ColourSlot = tile_colour_slot(i);
        const auto found = counts.find(type.m_Id);
        row.m_Count = found != counts.end() ? found->second : 0;
        const bool on_the_board = std::ranges::any_of(layout, [i](const auto& place) { return place == i; });
        row.m_Section = on_the_board ? ON_BOARD : OFF_BOARD;
        (on_the_board ? on_board : off_board).push_back(std::move(row));
    }
    off_board.insert(off_board.end(), std::make_move_iterator(on_board.begin()),
                     std::make_move_iterator(on_board.end()));
    return off_board;
}

void ErrorPickerModel::refresh()
{
    std::vector<Row> next = build();

    bool same_rows = next.size() == m_Rows.size();
    for (std::size_t i = 0; same_rows && i < next.size(); ++i)
    {
        same_rows = next[i].m_TypeId == m_Rows[i].m_TypeId && next[i].m_Label == m_Rows[i].m_Label &&
                    next[i].m_Section == m_Rows[i].m_Section && next[i].m_ColourSlot == m_Rows[i].m_ColourSlot;
    }

    if (!same_rows)
    {
        beginResetModel();
        m_Rows = std::move(next);
        endResetModel();
        return;
    }

    int first = -1;
    int last = -1;
    for (std::size_t i = 0; i < next.size(); ++i)
    {
        if (next[i].m_Count != m_Rows[i].m_Count)
        {
            first = first < 0 ? static_cast<int>(i) : first;
            last = static_cast<int>(i);
        }
    }
    m_Rows = std::move(next);
    if (first >= 0)
    {
        emit dataChanged(index(first), index(last), QList<int>{COUNT});
    }
}

void ErrorPickerModel::on_configuration_changed(const ConfigurationSource&)
{
    refresh();
}

void ErrorPickerModel::on_open_log_event(const LogChange& change)
{
    using Kind = LogChange::Kind;
    switch (change.m_Kind)
    {
    case Kind::LOADED:
    case Kind::REPLACED:
    case Kind::ENTRY_ADDED:
    case Kind::ENTRY_EDITED:
    case Kind::ENTRY_DELETED:
    case Kind::ENTRY_RESTORED:
        refresh();
        break;
    case Kind::HEADER_CORRECTED:
    case Kind::HEADER_CHANGED:
    case Kind::SHARED:
        break;
    }
}

} // namespace ej::viewmodel
