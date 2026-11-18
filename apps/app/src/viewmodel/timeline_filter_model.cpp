// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/timeline_filter_model.hpp"

#include "viewmodel/timeline_model.hpp"

#include <QChar>
#include <QModelIndex>
#include <QStringView>
#include <array>

namespace ej::viewmodel
{

namespace
{

constexpr std::array<QLatin1StringView, 4> KINDS{QLatin1StringView("error"), QLatin1StringView("note"),
                                                 QLatin1StringView("interaction"), QLatin1StringView("header_change")};

} // namespace

TimelineFilterModel::TimelineFilterModel(QObject* parent) : QSortFilterProxyModel(parent)
{
    // The count of the rows follows the source: a row added, taken out or edited, and a new log.
    connect(this, &QAbstractItemModel::rowsInserted, this, &TimelineFilterModel::countsChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TimelineFilterModel::countsChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TimelineFilterModel::countsChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &TimelineFilterModel::countsChanged);
    connect(this, &QAbstractItemModel::dataChanged, this, &TimelineFilterModel::countsChanged);
}

bool TimelineFilterModel::is_kind(const QString& kind)
{
    for (const QLatin1StringView known : KINDS)
    {
        if (kind == known)
        {
            return true;
        }
    }
    return false;
}

QString TimelineFilterModel::fold(const QString& text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString plain;
    plain.reserve(decomposed.size());
    for (const QChar character : decomposed)
    {
        if (character.category() != QChar::Mark_NonSpacing)
        {
            plain.append(character);
        }
    }
    return plain.toCaseFolded().simplified();
}

void TimelineFilterModel::set_text(const QString& text)
{
    if (text == m_Text)
    {
        return;
    }
    beginFilterChange();
    m_Text = text;
    const QString folded = fold(text);
    m_Words = folded.isEmpty() ? QStringList{} : folded.split(QLatin1Char(' '));
    endFilterChange(Direction::Rows);
    emit filterChanged();
}

bool TimelineFilterModel::active() const
{
    return !m_Kinds.isEmpty() || !m_Words.isEmpty();
}

int TimelineFilterModel::shown_count() const
{
    return rowCount();
}

int TimelineFilterModel::total_count() const
{
    return sourceModel() != nullptr ? sourceModel()->rowCount() : 0;
}

bool TimelineFilterModel::has_observations() const
{
    const QAbstractItemModel* source = sourceModel();
    if (source == nullptr)
    {
        return false;
    }
    for (int row = 0; row < source->rowCount(); ++row)
    {
        if (source->data(source->index(row, 0), TimelineModel::KIND).toString() == QLatin1StringView("interaction"))
        {
            return true;
        }
    }
    return false;
}

void TimelineFilterModel::toggleKind(const QString& kind)
{
    if (!is_kind(kind))
    {
        return;
    }
    beginFilterChange();
    if (!m_Kinds.removeOne(kind))
    {
        m_Kinds.append(kind);
    }
    endFilterChange(Direction::Rows);
    emit filterChanged();
}

bool TimelineFilterModel::kindOn(const QString& kind) const
{
    return m_Kinds.contains(kind);
}

void TimelineFilterModel::clear()
{
    if (!active() && m_Text.isEmpty())
    {
        return;
    }
    beginFilterChange();
    m_Kinds.clear();
    m_Text.clear();
    m_Words.clear();
    endFilterChange(Direction::Rows);
    emit filterChanged();
}

int TimelineFilterModel::sourceRow(int row) const
{
    const QModelIndex source = mapToSource(index(row, 0));
    return source.isValid() ? source.row() : -1;
}

bool TimelineFilterModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    if (!active())
    {
        return true;
    }
    const QAbstractItemModel* source = sourceModel();
    const QModelIndex index = source->index(source_row, 0, source_parent);
    if (!m_Kinds.isEmpty() && !m_Kinds.contains(source->data(index, TimelineModel::KIND).toString()))
    {
        return false;
    }
    if (m_Words.isEmpty())
    {
        return true;
    }
    const QString line = fold(source->data(index, TimelineModel::LINE_TEXT).toString());
    for (const QString& word : m_Words)
    {
        if (!line.contains(word))
        {
            return false;
        }
    }
    return true;
}

} // namespace ej::viewmodel
