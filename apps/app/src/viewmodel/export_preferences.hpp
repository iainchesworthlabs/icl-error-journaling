// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How text that leaves the device is written (Stage 14, 3.6): the level at which a person's name is printed (the
// preference `name_export`) and the label of an interaction line (`interaction_marker`).
//
// Like BoardState this is a setting of the application and not of a log or the setup file, so it lives in its own small
// file beside the data. A file that cannot be read or written is treated as empty: names as initials and the label
// "(Interaction)". The default is initials, so a log pasted into a chat does not carry a full name until the owner
// turns it on. The Review screen and the board always show the full name; only Share, Copy line and Copy all use this.

#include "error_journaling/person.hpp"

#include <QObject>
#include <QString>
#include <string>

namespace ej::viewmodel
{

class ExportPreferences final : public QObject
{
    Q_OBJECT
    /// "full", "initials" or "hidden". Any other word is ignored when set and reads as "initials" when stored.
    Q_PROPERTY(QString nameExport READ name_export WRITE set_name_export NOTIFY nameExportChanged)
    /// The label after the time on an interaction line. Empty drops it.
    Q_PROPERTY(
        QString interactionMarker READ interaction_marker WRITE set_interaction_marker NOTIFY interactionMarkerChanged)

public:
    /// The longest label, in characters.
    static constexpr int MAX_MARKER_CHARACTERS = 24;

    /// `file` is the path of the settings file (created on the first write); empty keeps nothing.
    explicit ExportPreferences(QString file, QObject* parent = nullptr);

    [[nodiscard]] QString name_export() const;
    [[nodiscard]] QString interaction_marker() const;
    [[nodiscard]] NameLevel name_level() const
    {
        return m_Level;
    }
    [[nodiscard]] const std::string& marker() const
    {
        return m_Marker;
    }

    void set_name_export(const QString& level);
    /// Trims the text, cuts it at the first line break or control character and at MAX_MARKER_CHARACTERS.
    void set_interaction_marker(const QString& marker);

signals:
    void nameExportChanged();
    void interactionMarkerChanged();

private:
    void load();
    void save() const;

    QString m_File;
    NameLevel m_Level = NameLevel::INITIALS;
    std::string m_Marker;
};

} // namespace ej::viewmodel
