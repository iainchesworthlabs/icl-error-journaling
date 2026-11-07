// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/configuration.hpp"
#include "error_journaling/export_format.hpp"
#include "error_journaling/uuid.hpp"
#include "viewmodel/history_model.hpp"

#include <QPointer>
#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace ej::viewmodel
{

namespace
{

QString text_of(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString id_text(LogId id)
{
    return text_of(id.format());
}

std::optional<LogId> parse_log_id(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    const auto uuid = Uuid::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!uuid)
    {
        return std::nullopt;
    }
    return LogId(*uuid);
}

QString month_text(const std::chrono::year_month_day& date)
{
    const unsigned month = static_cast<unsigned>(date.month());
    return QString::fromUtf8(export_format::MONTH_NAMES[month - 1].data(), 3);
}

/// "7 Nov 2026", the section heading; empty for a date that does not exist.
QString section_text(const std::chrono::year_month_day& date)
{
    if (!date.ok())
    {
        return {};
    }
    return QStringLiteral("%1 %2 %3")
        .arg(static_cast<unsigned>(date.day()))
        .arg(month_text(date))
        .arg(static_cast<int>(date.year()));
}

/// "Sat 7 Nov", the date of a row; empty for a date that does not exist.
QString date_text(const std::chrono::year_month_day& date)
{
    static constexpr std::array<std::string_view, 7> WEEKDAYS = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (!date.ok())
    {
        return {};
    }
    const unsigned weekday = std::chrono::weekday{std::chrono::sys_days{date}}.c_encoding();
    return QStringLiteral("%1 %2 %3")
        .arg(QString::fromUtf8(WEEKDAYS[weekday].data(), 3))
        .arg(static_cast<unsigned>(date.day()))
        .arg(month_text(date));
}

std::size_t live_entries(const Log& log)
{
    return log.sorted_entries(false).size();
}

} // namespace

HistoryModel::HistoryModel(const Environment& environment, StorageGateway& gateway, DocumentRegistry& registry,
                           SettingsModel& settings, OpenLogSource& open_log, Clipboard& clipboard, ShareService& share,
                           QObject* parent)
    : QAbstractListModel(parent), m_Environment(environment), m_Gateway(gateway), m_Registry(registry),
      m_Settings(settings), m_OpenLog(open_log), m_Review(environment, settings, clipboard)
{
    m_Settings.add_observer(static_cast<ConfigurationObserver&>(*this));
    m_OpenLog.add_observer(static_cast<OpenLogObserver&>(*this));
    watch_open_log(m_OpenLog.open_log());
    connect(&share, &ShareService::shareRecorded, this, &HistoryModel::share_recorded);
}

HistoryModel::~HistoryModel()
{
    close_review();
    watch_open_log(nullptr);
    m_OpenLog.remove_observer(static_cast<OpenLogObserver&>(*this));
    m_Settings.remove_observer(static_cast<ConfigurationObserver&>(*this));
}

// ------------------------------------------------------------------------------------------------------------ rows

int HistoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_Rows.size());
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(m_Rows.size()))
    {
        return {};
    }
    const Row& row = m_Rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case Qt::DisplayRole:
    case DATE_TEXT:
        return row.m_Date;
    case LOG_ID:
        return row.m_IdText;
    case SECTION:
        return row.m_Section;
    case UNIT_TEXT:
        return row.m_Unit;
    case LOCATION_TEXT:
        return row.m_Location;
    case ENTRY_COUNT:
        return row.m_Count;
    case SHARED:
        return row.m_Shared.has_value();
    case SHARED_AT_TEXT:
        return row.m_SharedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {{LOG_ID, "logId"},       {DATE_TEXT, "dateText"},         {SECTION, "section"},
            {UNIT_TEXT, "unitText"}, {LOCATION_TEXT, "locationText"}, {ENTRY_COUNT, "entryCount"},
            {SHARED, "shared"},      {SHARED_AT_TEXT, "sharedAtText"}};
}

void HistoryModel::render(Row& row) const
{
    const Configuration& configuration = m_Settings.configuration();

    row.m_Date = date_text(row.m_Header.m_LocalDate);
    row.m_Section = section_text(row.m_Header.m_LocalDate);

    row.m_Unit =
        row.m_Header.m_UnitNumber ? text_of(row.m_Header.m_UnitNumber->format(configuration.m_UnitPrefix)) : QString{};

    row.m_Location.clear();
    if (row.m_Header.m_Location)
    {
        const LocationValue& location = *row.m_Header.m_Location;
        if (location.m_Id)
        {
            // A retired location still has its name: a past log must keep reading as it did.
            const auto it = std::ranges::find(configuration.m_Locations, *location.m_Id, &Location::m_Id);
            if (it != configuration.m_Locations.end())
            {
                row.m_Location = text_of(it->m_Name);
            }
        }
        else
        {
            row.m_Location = text_of(location.m_Text);
        }
    }

    row.m_SharedAt.clear();
    if (row.m_Shared)
    {
        // The time is a UTC instant; it is shown where the device is now, in the user's time format.
        row.m_SharedAt = m_Settings.formatTime(static_cast<qint64>(row.m_Shared->time_since_epoch().count()),
                                               text_of(m_Environment.m_Zone.current()));
    }
}

QList<int> HistoryModel::changed_roles(const Row& before, const Row& after)
{
    QList<int> roles;
    if (before.m_Unit != after.m_Unit)
    {
        roles.push_back(UNIT_TEXT);
    }
    if (before.m_Location != after.m_Location)
    {
        roles.push_back(LOCATION_TEXT);
    }
    if (before.m_Count != after.m_Count)
    {
        roles.push_back(ENTRY_COUNT);
    }
    if (before.m_Shared != after.m_Shared)
    {
        roles.push_back(SHARED);
    }
    if (before.m_SharedAt != after.m_SharedAt)
    {
        roles.push_back(SHARED_AT_TEXT);
    }
    return roles;
}

std::optional<std::size_t> HistoryModel::find_row(LogId id) const
{
    const auto it = std::ranges::find(m_Rows, id, &Row::m_Id);
    if (it == m_Rows.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - m_Rows.begin());
}

void HistoryModel::set_loading(bool loading)
{
    if (m_Loading != loading)
    {
        m_Loading = loading;
        emit loadingChanged();
    }
}

// --------------------------------------------------------------------------------------------------------- refresh

void HistoryModel::refresh()
{
    if (m_Loading)
    {
        return;
    }
    set_loading(true);
    m_Gateway.run([](StorageContext& context) { return context.m_Logs.list_logs(); },
                  [self = QPointer<HistoryModel>(this)](Result<std::vector<LogSummary>, StorageError> result)
                  {
                      if (self)
                      {
                          self->list_arrived(result);
                      }
                  });
}

void HistoryModel::list_arrived(const Result<std::vector<LogSummary>, StorageError>& result)
{
    if (!result)
    {
        set_loading(false);
        emit refreshFailed(tr("The list of logs could not be read."));
        m_RefreshAgain = false;
        return;
    }

    std::vector<Row> rows;
    rows.reserve(result->size());
    for (const LogSummary& summary : *result)
    {
        if (!summary.m_Archived)
        {
            continue; // the open log is the Log and Review tabs
        }
        Row row;
        row.m_Id = summary.m_Id;
        row.m_IdText = id_text(summary.m_Id);
        row.m_Header = summary.m_Header;
        row.m_Created = summary.m_CreatedUtc;
        row.m_Shared = summary.m_LastSharedUtc;
        row.m_Count = static_cast<int>(summary.m_LiveEntryCount);
        rows.push_back(std::move(row));
    }
    // Newest first: the local date, then the time the log was made. The store lists them so already; this keeps the
    // sections whole whatever order a store answers in.
    std::ranges::stable_sort(rows,
                             [](const Row& a, const Row& b)
                             {
                                 if (a.m_Header.m_LocalDate != b.m_Header.m_LocalDate)
                                 {
                                     return a.m_Header.m_LocalDate > b.m_Header.m_LocalDate;
                                 }
                                 return a.m_Created > b.m_Created;
                             });
    for (Row& row : rows)
    {
        render(row);
    }

    beginResetModel();
    m_Rows = std::move(rows);
    endResetModel();
    set_loading(false);
    if (std::exchange(m_RefreshAgain, false))
    {
        refresh();
    }
}

// -------------------------------------------------------------------------------------------------- following a log

void HistoryModel::show_document(const LogDocument& document)
{
    const auto at = find_row(document.id());
    if (!at || !document.ready())
    {
        return;
    }
    Row after = m_Rows[*at];
    after.m_Header = document.log().initial_header();
    after.m_Shared = document.last_shared();
    after.m_Count = static_cast<int>(live_entries(document.log()));
    render(after);

    const QList<int> roles = changed_roles(m_Rows[*at], after);
    if (roles.isEmpty())
    {
        return;
    }
    m_Rows[*at] = std::move(after);
    const QModelIndex index = this->index(static_cast<int>(*at));
    emit dataChanged(index, index, roles);
}

void HistoryModel::on_log_changed(const LogDocument& document, const LogChange& change)
{
    if (&document == m_Watched)
    {
        // The open log was replaced by the next day's: the day that has just been archived is a row now.
        if (change.m_Kind == LogChange::Kind::REPLACED)
        {
            if (m_Loading)
            {
                m_RefreshAgain = true; // the read under way may have been made before the archive
            }
            else
            {
                refresh();
            }
        }
        return;
    }
    if (change.m_Kind == LogChange::Kind::LOADED || change.m_Kind == LogChange::Kind::REPLACED)
    {
        return;
    }
    show_document(document);
}

void HistoryModel::share_recorded(const QString& logId)
{
    const auto id = parse_log_id(logId);
    if (!id || !find_row(*id))
    {
        return;
    }
    // The service lets a past log go before it reports, so the document may have to be read again; the store has the
    // time by now. A log that is open for review is live and answers at once.
    m_Registry.acquire(*id,
                       [self = QPointer<HistoryModel>(this)](Result<std::shared_ptr<LogDocument>, StorageError> loaded)
                       {
                           if (self && loaded)
                           {
                               self->show_document(**loaded);
                           }
                       });
}

void HistoryModel::watch_open_log(LogDocument* document)
{
    if (document == m_Watched)
    {
        return;
    }
    if (m_Watched != nullptr)
    {
        m_Watched->remove_observer(static_cast<LogObserver&>(*this));
    }
    m_Watched = document;
    if (m_Watched != nullptr)
    {
        m_Watched->add_observer(static_cast<LogObserver&>(*this));
    }
}

void HistoryModel::on_open_log_changed(LogDocument* document)
{
    watch_open_log(document);
}

void HistoryModel::on_configuration_changed(const ConfigurationSource&)
{
    if (m_Rows.empty())
    {
        return;
    }
    QList<int> roles;
    for (Row& row : m_Rows)
    {
        Row after = row;
        render(after);
        for (const int role : changed_roles(row, after))
        {
            if (!roles.contains(role))
            {
                roles.push_back(role);
            }
        }
        row = std::move(after);
    }
    if (!roles.isEmpty())
    {
        emit dataChanged(index(0), index(static_cast<int>(m_Rows.size()) - 1), roles);
    }
}

// ------------------------------------------------------------------------------------------------------- open, close

void HistoryModel::openLog(const QString& logId)
{
    close_review();
    const std::uint64_t serial = ++m_OpenSerial;

    const auto id = parse_log_id(logId);
    if (!id)
    {
        emit logOpenFailed(tr("That log could not be found."));
        return;
    }
    m_Registry.acquire(
        *id,
        [self = QPointer<HistoryModel>(this), serial](Result<std::shared_ptr<LogDocument>, StorageError> loaded)
        {
            if (self)
            {
                self->opened(serial, std::move(loaded));
            }
        });
}

void HistoryModel::opened(std::uint64_t serial, Result<std::shared_ptr<LogDocument>, StorageError> result)
{
    if (serial != m_OpenSerial)
    {
        return; // closed, or another log was asked for, while this one loaded
    }
    if (!result)
    {
        emit logOpenFailed(result.error().m_Kind == StorageErrorKind::NOT_FOUND ? tr("That log could not be found.")
                                                                                : tr("That log could not be opened."));
        return;
    }
    std::shared_ptr<LogDocument> document = std::move(*result);
    if (!document->archived())
    {
        emit logOpenFailed(tr("Only a past log can be reviewed here."));
        return;
    }
    m_Opened = std::move(document);
    m_Opened->add_observer(static_cast<LogObserver&>(*this));
    m_Review.bind(m_Opened);
    emit reviewOpened(m_Opened.get());
    emit reviewModelChanged();
}

void HistoryModel::close_review()
{
    ++m_OpenSerial; // an open that is still loading is dropped
    if (!m_Opened)
    {
        return;
    }
    emit reviewClosing(m_Opened.get());
    m_Opened->remove_observer(static_cast<LogObserver&>(*this));
    m_Review.unbind();
    m_Opened.reset();
    emit reviewModelChanged();
}

void HistoryModel::closeLog()
{
    close_review();
    m_Tokens.clear();
}

void HistoryModel::leave()
{
    closeLog();
}

// ---------------------------------------------------------------------------------------------------------- delete

QString HistoryModel::requestDelete(const QString& logId)
{
    const auto id = parse_log_id(logId);
    // Only a listed row is an archived log: the open log is never listed.
    if (!id || !find_row(*id) || std::ranges::find(m_Deleting, *id) != m_Deleting.end())
    {
        return {};
    }
    Token token;
    token.m_Text = id_text(LogId(m_Environment.m_Ids.next()));
    token.m_Log = *id;
    m_Tokens.push_back(token);
    return token.m_Text;
}

bool HistoryModel::confirmDelete(const QString& tokenText)
{
    const auto it = std::ranges::find(m_Tokens, tokenText, &Token::m_Text);
    if (it == m_Tokens.end())
    {
        return false;
    }
    const LogId id = it->m_Log;
    // Used up, and so is every other token for the same log.
    std::erase_if(m_Tokens, [id](const Token& token) { return token.m_Log == id; });
    if (!find_row(id) || std::ranges::find(m_Deleting, id) != m_Deleting.end())
    {
        return false;
    }

    // Nothing may be written to the log from here on.
    if (m_Opened && m_Opened->id() == id)
    {
        close_review();
    }
    m_Deleting.push_back(id);

    // The one place in this layer that makes a DeleteConfirmation (DS-05).
    m_Gateway.run([id](StorageContext& context)
                  { return context.m_Logs.hard_delete_log(id, HistoryDeleteConfirmation::confirmed(id)); },
                  [self = QPointer<HistoryModel>(this), id](Result<void, StorageError> result)
                  {
                      if (self)
                      {
                          self->deleted(id, result);
                      }
                  });
    return true;
}

void HistoryModel::deleted(LogId id, const Result<void, StorageError>& result)
{
    std::erase(m_Deleting, id);
    if (result)
    {
        if (const auto at = find_row(id))
        {
            const int row = static_cast<int>(*at);
            beginRemoveRows(QModelIndex(), row, row);
            m_Rows.erase(m_Rows.begin() + row);
            endRemoveRows();
        }
    }
    emit deleteFinished(id_text(id), result.has_value());
}

} // namespace ej::viewmodel
