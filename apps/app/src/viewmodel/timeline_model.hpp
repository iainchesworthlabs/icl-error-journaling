// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/result.hpp"
#include "viewmodel/clipboard.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/open_log_source.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <Qt>
#include <memory>
#include <optional>
#include <vector>

namespace ej::viewmodel
{

/// The entries of one log in time order, for the "Last" bar and the review screen (REV-01 to REV-04;
/// docs/plan/stage-3.md, "TimelineModel"). The same class serves the open log and a past log opened from History:
/// `follow` binds it to whatever log is open, `bind` to one document.
///
/// Every string it shows comes from the export renderer, with the time format of the configuration in force and the
/// entry's own zone, so the review screen reads exactly like the export. The sentences around the lines (the title) are
/// the only text written here. A row holds its strings, rendered when the row is made, so `data()` formats nothing.
///
/// The model changes only when the document tells it that a commit has succeeded, so a failed write changes nothing
/// and emits nothing. Edits and deletes are `LogDocument` commands, the same for the open log and an archived one.
///
/// GUI thread only. Not copyable.
class TimelineModel : public QAbstractListModel,
                      private LogObserver,
                      private ConfigurationObserver,
                      private OpenLogObserver
{
    Q_OBJECT
    Q_PROPERTY(int entryCount READ entry_count NOTIFY summaryChanged)     // live entries: "7 today"
    Q_PROPERTY(QString lastLine READ last_line NOTIFY summaryChanged)     // the full line of the latest entry
    Q_PROPERTY(QString headerText READ header_text NOTIFY summaryChanged) // render_header: the start values
    Q_PROPERTY(QString titleText READ title_text NOTIFY summaryChanged)   // "Sat 7 Nov · 7 entries"
    Q_PROPERTY(bool ready READ ready NOTIFY summaryChanged)

public:
    /// QML role names: entryId, timeText, lineText, fullLine, kind. `Qt::DisplayRole` is the full line.
    enum Role
    {
        ENTRY_ID = Qt::UserRole + 1,
        TIME_TEXT,
        LINE_TEXT,
        FULL_LINE,
        KIND
    };
    Q_ENUM(Role)

    /// None of the arguments is owned; all must outlive the model. The model listens to `configuration` for its whole
    /// life.
    TimelineModel(const Environment& environment, ConfigurationSource& configuration, Clipboard& clipboard,
                  QObject* parent = nullptr);
    ~TimelineModel() override;

    // Which log is shown.

    /// Shows `document` (an archived log from History, or any other) and holds it for as long as it is bound. One
    /// `modelReset`. Stops following the open log.
    void bind(std::shared_ptr<LogDocument> document);

    /// Shows the open log and follows it: when `source` reports another document (a new day took its place) the model
    /// resets once and shows that. The document is not held, so the source must report null before it lets the
    /// document go. `source` must outlive the model or the model must be unbound first.
    void follow(OpenLogSource& source);

    /// Shows nothing. One `modelReset`.
    void unbind();

    /// The document shown, or null.
    [[nodiscard]] const LogDocument* document() const
    {
        return m_Document;
    }

    // The summary.

    [[nodiscard]] int entry_count() const;
    [[nodiscard]] QString last_line() const;
    [[nodiscard]] QString header_text() const;
    [[nodiscard]] QString title_text() const;
    [[nodiscard]] bool ready() const;

    // QAbstractListModel. Qt's names, so these overrides keep them.

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // REV-04: Copy. Neither marks the log as shared (Stage 3, decision 10) and neither writes anything.

    /// Puts the full line of `row` on the clipboard. False for a row that is not there or cannot be rendered.
    Q_INVOKABLE bool copyLine(int row);

    /// Puts the whole export on the clipboard: `export_text`, the function ShareService sends. False when the log
    /// cannot be rendered (nothing is copied then).
    Q_INVOKABLE bool copyAll();

    // REV-03: delete.

    /// A soft delete (origin USER). False when the entry is not a live entry of the bound log or the document
    /// refuses it (its queue is full); `writeFailed` is emitted for a refusal and for a failed commit. The row leaves
    /// the model after the commit.
    Q_INVOKABLE bool deleteEntry(const QString& entryId);

    // REV-02: edit. `edit` has the keys of `entryForEdit`; a key that is left out keeps the entry's value.

    /// What the edit sheet shows: `entryId`, `kind` ("error", "note" or "header_change"), `localDate` ("2026-11-07"),
    /// `localSeconds` (seconds since local midnight of that date), `zone`, `timeText`; for an error `errorTypeId`,
    /// `errorTypeName` and `attachedNote`; for a note `noteText` (the free text, or the template's value) and
    /// `templateId` (empty for free text); for a header change `fieldKey` and `newValue` (the stored text). Empty for
    /// an entry that is not in the bound log. A deleted entry is described too.
    Q_INVOKABLE QVariantMap entryForEdit(const QString& entryId) const;

    /// The plain reason `edit` cannot be saved, or an empty string when it can. Nothing is written.
    Q_INVOKABLE QString validateEdit(const QString& entryId, const QVariantMap& edit) const;

    /// The line the entry would have with `edit` ("In the shared log"), made by the renderer on a scratch copy of the
    /// log, so it equals the line `saveEntry` then saves. Empty when `validateEdit` refuses the edit. Nothing is
    /// written.
    Q_INVOKABLE QString previewLine(const QString& entryId, const QVariantMap& edit) const;

    /// Saves `edit`. The time is resolved in the entry's own zone with `TimeZoneProvider::to_utc`, so an instant in a
    /// spring-forward gap resolves to the instant after the gap and one in a repeated hour to the earlier occurrence,
    /// as Stage 1 defines (use `localToUtc` first to refuse the gap). The id and the deleted flag stay, the edit time
    /// is the clock's, and the row moves if the time moved. An edit that changes nothing returns true and writes
    /// nothing. False when `validateEdit` refuses it or the document does; `writeFailed` follows a failed commit.
    Q_INVOKABLE bool saveEntry(const QString& entryId, const QVariantMap& edit);

    /// Converts a local date and time in the entry's own zone to UTC, for the time editor: QML does no time
    /// arithmetic. `localDate` is "YYYY-MM-DD" and `localSeconds` is seconds since local midnight, 0 to 86399. The
    /// result has `ok`, `utcMillis` (milliseconds since the epoch), `repeated` and `message`. A time in the hour the
    /// clocks skip is refused (`ok` false, `message` says so). A time in the repeated hour resolves to the earlier
    /// occurrence and sets `repeated`. The same resolution `saveEntry` uses.
    Q_INVOKABLE QVariantMap localToUtc(const QString& entryId, const QString& localDate, int localSeconds) const;

    /// A time of day in the user's time format ("14:05", "2:05 pm"): the edit sheet and the time tumbler never format
    /// a time themselves (PRF-07). Empty for a value outside 0 to 86399.
    Q_INVOKABLE QString formatLocalSeconds(int localSeconds) const;

    /// The row of an entry, or -1.
    Q_INVOKABLE int rowOf(const QString& entryId) const;

signals:
    /// The count, the last line, the header text, the title or `ready` changed.
    void summaryChanged();

    /// An edit or a delete could not be written. `operation` is the invokable ("saveEntry", "deleteEntry"); `message`
    /// is plain and holds no entry text.
    void writeFailed(const QString& operation, const QString& message);

private:
    struct Row
    {
        EntryId m_Id;
        QString m_IdText;
        QString m_Time;
        QString m_Line;
        QString m_Full; // empty when the entry could not be rendered
        QString m_Kind;
    };

    struct Summary
    {
        int m_Count = 0;
        QString m_Last;
        QString m_Header;
        QString m_Title;
        bool m_Ready = false;

        friend bool operator==(const Summary&, const Summary&) = default;
    };

    /// An edit worked out: the entry it would make and what changed in it.
    struct Edit
    {
        Entry m_Entry;
        bool m_Unchanged = false;
        bool m_TimeChanged = false;
        bool m_ValueChanged = false; // the new value of a header change
    };

    // LogObserver, ConfigurationObserver, OpenLogObserver
    void on_log_changed(const LogDocument& document, const LogChange& change) override;
    void on_configuration_changed(const ConfigurationSource& source) override;
    void on_open_log_changed(LogDocument* document) override;

    void switch_document(LogDocument* document);
    void stop_following();

    // Rows
    [[nodiscard]] Row make_row(const Log& log, const Entry& entry, const ExportContext& context) const;
    [[nodiscard]] std::vector<Row> make_rows(const ExportContext& context) const;
    [[nodiscard]] int find_row(EntryId id) const;
    void reset_rows();
    void insert_row(EntryId id);
    void remove_row(EntryId id);
    void edit_row(EntryId id);
    void refresh_all_rows();
    void refresh_wording();
    [[nodiscard]] Summary make_summary(const ExportContext& context) const;
    void update_summary();

    // Edits
    [[nodiscard]] Result<Edit, QString> work_out_edit(EntryId id, const QVariantMap& edit) const;
    [[nodiscard]] std::optional<EntryId> parse_id(const QString& text) const;

    Environment m_Environment;
    ConfigurationSource& m_Configuration;
    Clipboard& m_Clipboard;

    OpenLogSource* m_Source = nullptr;     // set while following
    std::shared_ptr<LogDocument> m_Pinned; // set while bound
    LogDocument* m_Document = nullptr;
    std::vector<Row> m_Rows;
    Summary m_Summary;
};

} // namespace ej::viewmodel
