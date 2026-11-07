// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/template_list_model.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

QVariant TemplateListModel::data(const QModelIndex& index, int role) const
{
    const TemplateRow* row = row_at(index);
    if (row == nullptr)
    {
        return {};
    }
    switch (role)
    {
    case TEMPLATE_ID:
        return QString::fromStdString(row->m_Id.format());
    case NAME:
    case Qt::DisplayRole:
        return row->m_Name;
    case VALUE_PROMPT:
        return row->m_ValuePrompt;
    case OUTPUT_PATTERN:
        return row->m_OutputPattern;
    default:
        return {};
    }
}

QHash<int, QByteArray> TemplateListModel::roleNames() const
{
    return {
        {TEMPLATE_ID, "templateId"}, {NAME, "name"}, {VALUE_PROMPT, "valuePrompt"}, {OUTPUT_PATTERN, "outputPattern"}};
}

void TemplateListModel::refresh(const Configuration& configuration)
{
    std::vector<const NoteTemplate*> ordered;
    for (const NoteTemplate& note_template : configuration.m_Templates)
    {
        ordered.push_back(&note_template);
    }
    std::ranges::stable_sort(ordered, {}, &NoteTemplate::m_SortOrder);

    std::vector<TemplateRow> rows;
    rows.reserve(ordered.size());
    for (const NoteTemplate* note_template : ordered)
    {
        rows.push_back({note_template->m_Id, QString::fromStdString(note_template->m_Name),
                        QString::fromStdString(note_template->m_ValuePrompt),
                        QString::fromStdString(note_template->m_OutputPattern)});
    }
    replace_rows(std::move(rows));
}

} // namespace ej::viewmodel
