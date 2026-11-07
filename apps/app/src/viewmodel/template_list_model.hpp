// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "error_journaling/ids.hpp"
#include "viewmodel/row_list_model.hpp"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>

namespace ej::viewmodel
{

/// One row of `TemplateListModel`.
struct TemplateRow
{
    NoteTemplateId m_Id;
    QString m_Name;
    QString m_ValuePrompt;
    QString m_OutputPattern;

    friend bool operator==(const TemplateRow&, const TemplateRow&) = default;
};

/// The note templates of the settings screen (PRF-06), in their sort order. A template is never removed in v1: a note
/// entry holds the template's id, so a removed template could not be rendered.
///
/// `SettingsModel` owns one and calls `refresh` after every change; it is read-only to QML.
class TemplateListModel final : public RowListModel<TemplateRow, NoteTemplateId>
{
    Q_OBJECT

public:
    enum Role
    {
        TEMPLATE_ID = Qt::UserRole + 1,
        NAME,
        VALUE_PROMPT,
        OUTPUT_PATTERN
    };
    // roleNames(): templateId, name, valuePrompt, outputPattern

    explicit TemplateListModel(QObject* parent = nullptr) : RowListModel(parent) {}

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Rebuilds the rows from `configuration` and tells the views the least that is true.
    void refresh(const Configuration& configuration);

protected:
    [[nodiscard]] NoteTemplateId key_of(const TemplateRow& row) const override
    {
        return row.m_Id;
    }
};

} // namespace ej::viewmodel
