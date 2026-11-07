// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QModelIndex>
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

/// The base of the list models that mirror a list of the configuration (`ErrorTypeListModel`, `LocationListModel`,
/// `TemplateListModel`). A subclass builds the whole new list of rows and calls `replace_rows`; this class works out
/// the smallest signal that tells a view what happened, so a list keeps its scroll position and a row being dragged is
/// not rebuilt (standard 7.6: one signal per batch):
///
///   - the same rows, some changed: one `dataChanged` over the smallest range;
///   - rows added at the end: one `rowsInserted`;
///   - rows taken away: one `rowsRemoved` per run of neighbours;
///   - one row moved: one `rowsMoved`;
///   - anything else: one `modelReset`.
///
/// `Row` is compared with `==` and `Key` identifies a row across changes. Not a QObject itself (a template cannot be),
/// so a subclass carries the `Q_OBJECT`.
template <typename Row, typename Key>
class RowListModel : public QAbstractListModel
{
public:
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override // NOLINT(readability-identifier-naming)
    {
        return parent.isValid() ? 0 : static_cast<int>(m_Rows.size());
    }

protected:
    explicit RowListModel(QObject* parent) : QAbstractListModel(parent) {}

    [[nodiscard]] virtual Key key_of(const Row& row) const = 0;

    /// The row an index names, or null for an index that is not in range.
    [[nodiscard]] const Row* row_at(const QModelIndex& index) const
    {
        if (!index.isValid() || index.parent().isValid() || index.row() < 0 ||
            static_cast<std::size_t>(index.row()) >= m_Rows.size())
        {
            return nullptr;
        }
        return &m_Rows[static_cast<std::size_t>(index.row())];
    }

    void replace_rows(std::vector<Row> next)
    {
        const std::vector<Key> before = keys_of(m_Rows);
        const std::vector<Key> after = keys_of(next);
        if (before != after && !insert_at_end(before, after, next) && !remove_rows(before, after) &&
            !move_row(before, after))
        {
            beginResetModel();
            m_Rows = std::move(next);
            endResetModel();
            return;
        }
        // The keys now agree; tell the view about rows whose content differs.
        const std::size_t count = std::min(m_Rows.size(), next.size());
        std::size_t first = count;
        std::size_t last = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!(m_Rows[i] == next[i]))
            {
                first = std::min(first, i);
                last = i;
            }
        }
        m_Rows = std::move(next);
        if (first < count)
        {
            emit dataChanged(index(static_cast<int>(first)), index(static_cast<int>(last)));
        }
    }

    std::vector<Row> m_Rows;

private:
    [[nodiscard]] std::vector<Key> keys_of(const std::vector<Row>& rows) const
    {
        std::vector<Key> keys;
        keys.reserve(rows.size());
        for (const Row& row : rows)
        {
            keys.push_back(key_of(row));
        }
        return keys;
    }

    bool insert_at_end(const std::vector<Key>& before, const std::vector<Key>& after, const std::vector<Row>& next)
    {
        if (after.size() <= before.size() || !std::equal(before.begin(), before.end(), after.begin()))
        {
            return false;
        }
        beginInsertRows({}, static_cast<int>(before.size()), static_cast<int>(after.size()) - 1);
        m_Rows.insert(m_Rows.end(), next.begin() + static_cast<std::ptrdiff_t>(before.size()), next.end());
        endInsertRows();
        return true;
    }

    /// `after` must be `before` with some rows left out, in order.
    bool remove_rows(const std::vector<Key>& before, const std::vector<Key>& after)
    {
        if (after.size() >= before.size())
        {
            return false;
        }
        std::vector<bool> removed(before.size(), false);
        std::size_t kept = 0;
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            if (kept < after.size() && before[i] == after[kept])
            {
                ++kept;
            }
            else
            {
                removed[i] = true;
            }
        }
        if (kept != after.size())
        {
            return false;
        }
        // From the back, so the indexes of the runs still to be removed do not move.
        std::size_t end = before.size();
        while (end > 0)
        {
            if (!removed[end - 1])
            {
                --end;
                continue;
            }
            std::size_t begin = end - 1;
            while (begin > 0 && removed[begin - 1])
            {
                --begin;
            }
            beginRemoveRows({}, static_cast<int>(begin), static_cast<int>(end) - 1);
            m_Rows.erase(m_Rows.begin() + static_cast<std::ptrdiff_t>(begin),
                         m_Rows.begin() + static_cast<std::ptrdiff_t>(end));
            endRemoveRows();
            end = begin;
        }
        return true;
    }

    /// `after` must be `before` with one row taken out and put back somewhere else.
    bool move_row(const std::vector<Key>& before, const std::vector<Key>& after)
    {
        if (before.size() != after.size())
        {
            return false;
        }
        const auto [first_before, first_after] = std::ranges::mismatch(before, after);
        if (first_before == before.end())
        {
            return false;
        }
        const auto i = static_cast<std::ptrdiff_t>(first_before - before.begin());
        auto j = static_cast<std::ptrdiff_t>(before.size()) - 1;
        while (before[static_cast<std::size_t>(j)] == after[static_cast<std::size_t>(j)])
        {
            --j;
        }
        const auto at = [](const std::vector<Key>& keys, std::ptrdiff_t offset) { return keys.begin() + offset; };
        // The row at i moved down to j.
        if (before[static_cast<std::size_t>(i)] == after[static_cast<std::size_t>(j)] &&
            std::equal(at(before, i + 1), at(before, j + 1), at(after, i)))
        {
            if (!beginMoveRows({}, static_cast<int>(i), static_cast<int>(i), {}, static_cast<int>(j) + 1))
            {
                return false;
            }
            std::rotate(m_Rows.begin() + i, m_Rows.begin() + i + 1, m_Rows.begin() + j + 1);
            endMoveRows();
            return true;
        }
        // The row at j moved up to i.
        if (before[static_cast<std::size_t>(j)] == after[static_cast<std::size_t>(i)] &&
            std::equal(at(before, i), at(before, j), at(after, i + 1)))
        {
            if (!beginMoveRows({}, static_cast<int>(j), static_cast<int>(j), {}, static_cast<int>(i)))
            {
                return false;
            }
            std::rotate(m_Rows.begin() + i, m_Rows.begin() + j, m_Rows.begin() + j + 1);
            endMoveRows();
            return true;
        }
        return false;
    }
};

} // namespace ej::viewmodel
