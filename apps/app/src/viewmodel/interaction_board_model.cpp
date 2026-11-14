// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/interaction_board_model.hpp"

#include "error_journaling/interaction.hpp"
#include "viewmodel/interaction_types.hpp"
#include "viewmodel/log_document.hpp"

#include <QVariantMap>
#include <algorithm>
#include <unordered_map>
#include <utility>

namespace ej::viewmodel
{

namespace
{

const InteractionType* find_type(const std::vector<InteractionType>& types, const QString& id)
{
    const auto it = std::ranges::find_if(types, [&id](const InteractionType& type)
                                         { return QString::fromStdString(type.m_Id.format()) == id; });
    return it == types.end() ? nullptr : &*it;
}

} // namespace

InteractionBoardModel::InteractionBoardModel(ConfigurationSource& configuration, OpenLogSource& open_log,
                                             QObject* parent)
    : QAbstractListModel(parent), m_Configuration(configuration),
      m_Log(open_log, [this](const LogChange& change) { on_open_log_event(change); })
{
    m_Tiles = build();
    m_ShownOtherTypes = other_types();
    m_Configuration.add_observer(*this);
}

InteractionBoardModel::~InteractionBoardModel()
{
    m_Configuration.remove_observer(*this);
}

int InteractionBoardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : BOARD_ROWS;
}

QVariant InteractionBoardModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= BOARD_ROWS)
    {
        return {};
    }
    const Tile& tile = m_Tiles[static_cast<std::size_t>(index.row())];
    const bool empty = !tile.m_IsOther && tile.m_TypeId.isEmpty();
    const QString label = tile.m_IsOther ? tr("Other") : tile.m_Label;
    switch (role)
    {
    case TYPE_ID:
        return tile.m_TypeId;
    case LABEL:
    case Qt::DisplayRole:
        return tile.m_IsOther ? tr("Other\xE2\x80\xA6") : label;
    case COLOUR_SLOT:
        return tile.m_ColourSlot;
    case COUNT:
        return tile.m_Count;
    case HAS_COUNT:
        return tile.m_Count > 0;
    case IS_OTHER:
        return tile.m_IsOther;
    case IS_EMPTY:
        return empty;
    case ACCESSIBLE_NAME:
        if (empty)
        {
            return tr("Position %1, empty. Add interaction type").arg(index.row() + 1);
        }
        return tile.m_Count > 0 ? tr("%1, %2 today").arg(label).arg(tile.m_Count) : label;
    default:
        return {};
    }
}

QHash<int, QByteArray> InteractionBoardModel::roleNames() const
{
    return {{TYPE_ID, "typeId"},         {LABEL, "label"},
            {COLOUR_SLOT, "colourSlot"}, {COUNT, "count"},
            {HAS_COUNT, "hasCount"},     {IS_OTHER, "isOther"},
            {IS_EMPTY, "isEmpty"},       {ACCESSIBLE_NAME, "accessibleName"}};
}

InteractionBoardModel::Tiles InteractionBoardModel::build() const
{
    Tiles tiles;
    tiles.back().m_IsOther = true;

    const auto types = interaction_types_in_force(m_Configuration.configuration());
    std::unordered_map<InteractionTypeId, std::size_t> row_of;
    const InteractionLayout layout = interaction_layout(types);
    for (std::size_t position = 0; position < layout.size(); ++position)
    {
        if (!layout[position])
        {
            continue;
        }
        const InteractionType& type = types[*layout[position]];
        Tile& tile = tiles[position];
        tile.m_TypeId = QString::fromStdString(type.m_Id.format());
        tile.m_Label = QString::fromStdString(type.m_Name);
        tile.m_ColourSlot = tile_colour_slot(*layout[position]);
        row_of.emplace(type.m_Id, position);
    }

    const LogDocument* document = m_Log.document();
    if (document != nullptr && document->ready())
    {
        for (const Entry& entry : document->log().entries())
        {
            const auto* interaction = std::get_if<InteractionEntry>(&entry.m_Body);
            if (entry.m_Deleted || interaction == nullptr)
            {
                continue;
            }
            const auto found = row_of.find(interaction->m_Type);
            const std::size_t row = found != row_of.end() ? found->second : static_cast<std::size_t>(OTHER_ROW);
            ++tiles[row].m_Count;
        }
    }
    return tiles;
}

bool InteractionBoardModel::board_empty() const
{
    return std::ranges::all_of(m_Tiles, [](const Tile& tile) { return tile.m_IsOther || tile.m_TypeId.isEmpty(); });
}

QVariantList InteractionBoardModel::other_types() const
{
    QVariantList rows;
    const auto types = interaction_types_in_force(m_Configuration.configuration());
    const InteractionLayout layout = interaction_layout(types);
    for (std::size_t i = 0; i < types.size(); ++i)
    {
        const bool on_board = std::ranges::any_of(layout, [i](const auto& slot) { return slot && *slot == i; });
        if (types[i].m_Retired || on_board)
        {
            continue;
        }
        QVariantMap row;
        row.insert(QStringLiteral("typeId"), QString::fromStdString(types[i].m_Id.format()));
        row.insert(QStringLiteral("label"), QString::fromStdString(types[i].m_Name));
        rows.append(row);
    }
    return rows;
}

QString InteractionBoardModel::asksFor(const QString& typeId) const
{
    const auto types = interaction_types_in_force(m_Configuration.configuration());
    const InteractionType* type = find_type(types, typeId);
    if (type == nullptr)
    {
        return QStringLiteral("nothing");
    }
    const auto key = interaction_asks_key(interaction_asks_for(type->m_LinePattern));
    return QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()));
}

bool InteractionBoardModel::hasValueSlot(const QString& typeId) const
{
    const auto types = interaction_types_in_force(m_Configuration.configuration());
    const InteractionType* type = find_type(types, typeId);
    return type != nullptr && type->m_LinePattern && type->m_LinePattern->find("{value}") != std::string::npos;
}

QString InteractionBoardModel::typeName(const QString& typeId) const
{
    const auto types = interaction_types_in_force(m_Configuration.configuration());
    const InteractionType* type = find_type(types, typeId);
    return type == nullptr ? QString() : QString::fromStdString(type->m_Name);
}

QString InteractionBoardModel::previewLine(const QString& typeId, const QString& value) const
{
    const auto types = interaction_types_in_force(m_Configuration.configuration());
    const InteractionType* type = find_type(types, typeId);
    if (type == nullptr)
    {
        return {};
    }
    const std::optional<std::string> answer =
        value.trimmed().isEmpty() ? std::nullopt : std::optional<std::string>(value.toStdString());
    return QString::fromStdString(render_interaction_text(type->m_LinePattern, type->m_Name, "", answer));
}

void InteractionBoardModel::refresh_everything()
{
    const bool was_empty = board_empty();
    m_Tiles = build();
    emit dataChanged(index(0), index(BOARD_ROWS - 1));
    if (board_empty() != was_empty)
    {
        emit boardEmptyChanged();
    }
    if (QVariantList other = other_types(); other != m_ShownOtherTypes)
    {
        m_ShownOtherTypes = std::move(other);
        emit otherTypesChanged();
    }
}

void InteractionBoardModel::refresh_counts()
{
    Tiles built = build();
    int first = BOARD_ROWS;
    int last = -1;
    for (int row = 0; row < BOARD_ROWS; ++row)
    {
        if (m_Tiles[static_cast<std::size_t>(row)] != built[static_cast<std::size_t>(row)])
        {
            first = std::min(first, row);
            last = row;
        }
    }
    m_Tiles = std::move(built);
    if (last >= 0)
    {
        emit dataChanged(index(first), index(last), {COUNT, HAS_COUNT, ACCESSIBLE_NAME});
    }
}

void InteractionBoardModel::on_configuration_changed(const ConfigurationSource&)
{
    refresh_everything();
}

void InteractionBoardModel::on_open_log_event(const LogChange& change)
{
    using Kind = LogChange::Kind;
    switch (change.m_Kind)
    {
    case Kind::LOADED:
    case Kind::REPLACED:
        refresh_everything();
        break;
    case Kind::ENTRY_ADDED:
    case Kind::ENTRY_EDITED:
    case Kind::ENTRY_DELETED:
    case Kind::ENTRY_RESTORED:
        refresh_counts();
        break;
    case Kind::HEADER_CORRECTED:
    case Kind::HEADER_CHANGED:
    case Kind::SHARED:
        break;
    }
}

} // namespace ej::viewmodel
