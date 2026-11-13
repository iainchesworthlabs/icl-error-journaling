// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which boards the app has and which one it reopens on (Stage 14, package I-5; docs/plan/stage-14.md, 4.1).
//
// `interactionsEnabled` is the optional second board, off until the person turns it on in Settings. `lastBoard` is the
// board in use when the app was left, "errors" or "interactions", so that the app reopens on it. Like HelpState this is
// a setting of the application and not a part of a log: it lives in its own small file beside the data, outside the
// store, the journals and the setup file. A file that cannot be read or written is treated as empty (second board off,
// Errors), which is how a v1 install looks, so the person is never told.
//
// Turning the board off keeps everything: the entries, the types and the people are not this class's, and it touches
// none of them. If the person was on the Interactions board, the last board becomes "errors" at once.

#include <QObject>
#include <QString>

namespace ej::viewmodel
{

class BoardState final : public QObject
{
    Q_OBJECT
    /// The second board is on: the Interactions tab is shown and its Settings rows are there.
    Q_PROPERTY(bool interactionsEnabled READ interactions_enabled WRITE set_interactions_enabled NOTIFY
                   interactionsEnabledChanged)
    /// "errors" or "interactions". Always "errors" while the second board is off.
    Q_PROPERTY(QString lastBoard READ last_board WRITE set_last_board NOTIFY lastBoardChanged)

public:
    static inline const QString ERRORS = QStringLiteral("errors");
    static inline const QString INTERACTIONS = QStringLiteral("interactions");

    /// `file` is the path of the settings file (created on the first write); empty keeps nothing.
    explicit BoardState(QString file, QObject* parent = nullptr);

    bool interactions_enabled() const;
    QString last_board() const;

    /// Turns the second board on or off. Off while on the Interactions board goes back to Errors.
    void set_interactions_enabled(bool enabled);
    /// Remembers the board in use. "interactions" is ignored while the second board is off, and any other word is
    /// "errors".
    void set_last_board(const QString& board);

signals:
    void interactionsEnabledChanged();
    void lastBoardChanged();

private:
    void load();
    void save() const;

    QString m_File;
    bool m_Enabled = false;
    QString m_Last = ERRORS;
};

} // namespace ej::viewmodel
