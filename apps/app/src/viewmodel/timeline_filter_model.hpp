// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>

namespace ej::viewmodel
{

/// The rows of a `TimelineModel` that match what the person is looking for (the Stage 4 review screen, "Filter the
/// entries of a log"; docs/plan/feedback-log-filter.md). It only chooses which rows the screen shows. It never touches
/// the log, the export, Copy all or Share: those read the document, not the rows, so a filter cannot make a partial log
/// leave the phone.
///
/// Two controls, combined with AND:
///  - kinds: any of "error", "note", "interaction" (an Observation) and "header_change". None on shows every kind.
///    More than one on shows an entry of any of them.
///  - text: every word typed has to appear in the line as the screen shows it (the time is not part of it), ignoring
///    case, accents and extra spaces. Nothing typed matches every row.
///
/// The proxy has the roles of its source. A row of the proxy is a row of the source through `sourceRow`, which is what
/// Copy line needs. Nothing is stored: the filter lives as long as the model that owns it, and the timeline clears it
/// when it shows another log.
///
/// GUI thread only. Not copyable.
class TimelineFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList kinds READ kinds NOTIFY filterChanged)
    Q_PROPERTY(QString text READ text WRITE set_text NOTIFY filterChanged)
    Q_PROPERTY(bool active READ active NOTIFY filterChanged)
    Q_PROPERTY(int shownCount READ shown_count NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ total_count NOTIFY countsChanged)
    Q_PROPERTY(bool hasObservations READ has_observations NOTIFY countsChanged)

public:
    explicit TimelineFilterModel(QObject* parent = nullptr);

    /// The kinds that are on, in the order they were turned on.
    [[nodiscard]] QStringList kinds() const
    {
        return m_Kinds;
    }

    [[nodiscard]] QString text() const
    {
        return m_Text;
    }

    /// Sets what is typed. The same text again changes nothing and signals nothing.
    void set_text(const QString& text);

    /// True when a kind is on or a word is typed.
    [[nodiscard]] bool active() const;

    /// The rows shown now, and the rows of the log.
    [[nodiscard]] int shown_count() const;
    [[nodiscard]] int total_count() const;

    /// True when the log holds at least one Observation. The screen shows the Observations chip when this is true or
    /// the second board is on.
    [[nodiscard]] bool has_observations() const;

    /// Turns `kind` on when it is off and off when it is on. A name that is not a kind is ignored.
    Q_INVOKABLE void toggleKind(const QString& kind);

    /// True when `kind` is on.
    Q_INVOKABLE bool kindOn(const QString& kind) const;

    /// Turns every kind off and empties the text. Nothing changes, and nothing is signalled, when no filter is on.
    Q_INVOKABLE void clear();

    /// The row of the source (the whole log) that `row` of this model shows, or -1.
    Q_INVOKABLE int sourceRow(int row) const;

    /// `text` as the matcher reads it: decomposed, with its accents dropped, case-folded and with its runs of white
    /// space made one space. Two texts that differ only in case, accents or spacing fold to the same string.
    [[nodiscard]] static QString fold(const QString& text);

signals:
    /// A kind or the text changed.
    void filterChanged();

    /// The rows shown, the rows of the log or `hasObservations` may have changed.
    void countsChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    [[nodiscard]] static bool is_kind(const QString& kind);

    QStringList m_Kinds;
    QString m_Text;
    QStringList m_Words; // `m_Text` folded and split
};

} // namespace ej::viewmodel
