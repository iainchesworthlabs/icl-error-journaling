// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/board_model.hpp"

#include "viewmodel/log_document.hpp"

#include <QCoreApplication>
#include <QList>
#include <algorithm>
#include <utility>
#include <variant>

namespace ej::viewmodel
{

QString tile_colour_name(int slot)
{
    switch (slot)
    {
    case 1:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Sky");
    case 2:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Lavender");
    case 3:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Apricot");
    case 4:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Aqua");
    case 5:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Orchid");
    case 6:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Butter");
    case 7:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Steel");
    case 8:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Periwinkle");
    case 9:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Sand");
    case 10:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Oat");
    case 11:
        return QCoreApplication::translate("ej::viewmodel::TileColours", "Blush");
    default:
        return {};
    }
}

BoardLayout board_layout(const Configuration& configuration)
{
    BoardLayout layout;
    for (std::size_t i = 0; i < configuration.m_ErrorTypes.size(); ++i)
    {
        const ErrorType& type = configuration.m_ErrorTypes[i];
        if (type.m_Retired || !type.m_BoardPosition || *type.m_BoardPosition < 1 ||
            *type.m_BoardPosition > CONFIG_BOARD_POSITIONS)
        {
            continue;
        }
        auto& slot = layout[static_cast<std::size_t>(*type.m_BoardPosition - 1)];
        if (!slot)
        {
            slot = i;
        }
    }
    return layout;
}

std::unordered_map<ErrorTypeId, int> live_error_counts(const Log& log)
{
    std::unordered_map<ErrorTypeId, int> counts;
    for (const Entry& entry : log.entries())
    {
        if (entry.m_Deleted)
        {
            continue;
        }
        if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
        {
            ++counts[error->m_ErrorType];
        }
    }
    return counts;
}

// ------------------------------------------------------------------------------------------------ OpenLogFollower

OpenLogFollower::OpenLogFollower(OpenLogSource& source, Changed changed)
    : m_Source(source), m_Changed(std::move(changed))
{
    attach(m_Source.open_log());
    m_Source.add_observer(*this);
}

OpenLogFollower::~OpenLogFollower()
{
    m_Source.remove_observer(*this);
    detach();
}

void OpenLogFollower::attach(LogDocument* document)
{
    m_Document = document;
    if (m_Document != nullptr)
    {
        m_Document->add_observer(*this);
    }
}

void OpenLogFollower::detach()
{
    if (m_Document != nullptr)
    {
        m_Document->remove_observer(*this);
        m_Document = nullptr;
    }
}

void OpenLogFollower::on_open_log_changed(LogDocument* document)
{
    if (document != m_Document)
    {
        detach();
        attach(document);
    }
    // The same object told again (the document was replaced inside) is still a new log to the model.
    m_Changed(LogChange{LogChange::Kind::REPLACED, LogChange::Origin::USER, std::nullopt});
}

void OpenLogFollower::on_log_changed(const LogDocument& document, const LogChange& change)
{
    if (&document == m_Document)
    {
        m_Changed(change);
    }
}

// ------------------------------------------------------------------------------------------------------ BoardModel

BoardModel::BoardModel(ConfigurationSource& configuration, OpenLogSource& open_log, QObject* parent)
    : QAbstractListModel(parent), m_Configuration(configuration),
      m_Log(open_log, [this](const LogChange& change) { on_open_log_event(change); })
{
    Built built = build();
    m_Tiles = std::move(built.m_Tiles);
    m_Total = built.m_Total;
    m_Configuration.add_observer(*this);
}

BoardModel::~BoardModel()
{
    m_Configuration.remove_observer(*this);
}

int BoardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : BOARD_ROWS;
}

QVariant BoardModel::data(const QModelIndex& index, int role) const
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
        // The tile reads "Other..." in the designs, as it opens a list; the accessible name keeps the plain word.
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
            return tr("Position %1, empty. Add error type").arg(index.row() + 1);
        }
        return tile.m_Count > 0 ? tr("%1, %2 today").arg(label).arg(tile.m_Count) : label;
    default:
        return {};
    }
}

QHash<int, QByteArray> BoardModel::roleNames() const
{
    return {{TYPE_ID, "typeId"},         {LABEL, "label"},
            {COLOUR_SLOT, "colourSlot"}, {COUNT, "count"},
            {HAS_COUNT, "hasCount"},     {IS_OTHER, "isOther"},
            {IS_EMPTY, "isEmpty"},       {ACCESSIBLE_NAME, "accessibleName"}};
}

BoardModel::Built BoardModel::build() const
{
    Built built;
    built.m_Tiles.back().m_IsOther = true;

    const Configuration& configuration = m_Configuration.configuration();
    std::unordered_map<ErrorTypeId, std::size_t> row_of;
    const BoardLayout layout = board_layout(configuration);
    for (std::size_t position = 0; position < layout.size(); ++position)
    {
        if (!layout[position])
        {
            continue;
        }
        const ErrorType& type = configuration.m_ErrorTypes[*layout[position]];
        Tile& tile = built.m_Tiles[position];
        tile.m_TypeId = QString::fromStdString(type.m_Id.format());
        tile.m_Label = QString::fromStdString(type.m_Name);
        tile.m_ColourSlot = tile_colour_slot(*layout[position]);
        row_of.emplace(type.m_Id, position);
    }

    // Counts come from the mirror of the open document and from nothing else. A document that is not ready has no log.
    const LogDocument* document = m_Log.document();
    if (document != nullptr && document->ready())
    {
        for (const auto& [type, count] : live_error_counts(document->log()))
        {
            const auto found = row_of.find(type);
            const std::size_t row = found != row_of.end() ? found->second : static_cast<std::size_t>(OTHER_ROW);
            built.m_Tiles[row].m_Count += count;
            built.m_Total += count;
        }
    }
    return built;
}

bool BoardModel::board_empty() const
{
    return std::ranges::all_of(m_Tiles, [](const Tile& tile) { return tile.m_IsOther || tile.m_TypeId.isEmpty(); });
}

void BoardModel::refresh_everything()
{
    const bool was_empty = board_empty();
    Built built = build();
    m_Tiles = std::move(built.m_Tiles);
    m_Total = built.m_Total;
    emit dataChanged(index(0), index(BOARD_ROWS - 1)); // no roles listed: every role
    emit countsChanged();
    if (board_empty() != was_empty)
    {
        emit boardEmptyChanged();
    }
}

void BoardModel::refresh_counts()
{
    Built built = build();

    int first = BOARD_ROWS;
    int last = -1;
    bool only_counts = true;
    for (int row = 0; row < BOARD_ROWS; ++row)
    {
        const Tile& before = m_Tiles[static_cast<std::size_t>(row)];
        const Tile& after = built.m_Tiles[static_cast<std::size_t>(row)];
        if (before == after)
        {
            continue;
        }
        first = std::min(first, row);
        last = row;
        Tile same_count = after;
        same_count.m_Count = before.m_Count;
        only_counts = only_counts && same_count == before;
    }

    const bool total_changed = built.m_Total != m_Total;
    m_Tiles = std::move(built.m_Tiles);
    m_Total = built.m_Total;
    if (last >= 0)
    {
        QList<int> roles;
        if (only_counts)
        {
            roles = {COUNT, HAS_COUNT, ACCESSIBLE_NAME};
        }
        emit dataChanged(index(first), index(last), roles);
    }
    if (last >= 0 || total_changed)
    {
        emit countsChanged();
    }
}

void BoardModel::on_configuration_changed(const ConfigurationSource&)
{
    refresh_everything();
}

void BoardModel::on_open_log_event(const LogChange& change)
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
        break; // no tile shows these
    }
}

} // namespace ej::viewmodel
