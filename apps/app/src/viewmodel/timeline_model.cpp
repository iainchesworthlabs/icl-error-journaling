// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/timeline_model.hpp"

#include "error_journaling/configuration.hpp"
#include "error_journaling/export_format.hpp"
#include "error_journaling/header_field.hpp"
#include "error_journaling/header_history.hpp"
#include "error_journaling/iso_date.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/stored_log.hpp"
#include "error_journaling/time_format.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/person_names.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QPointer>
#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ej::viewmodel
{

namespace
{

constexpr int SECONDS_PER_DAY = 24 * 60 * 60;

/// Steps tried to find out whether a local time happens twice. The repeat is as long as the clock change, and
/// 30 minutes, one hour and two hours cover the changes the zones in use make.
constexpr std::array<std::chrono::minutes, 3> REPEAT_STEPS = {std::chrono::minutes{30}, std::chrono::minutes{60},
                                                              std::chrono::minutes{120}};

QString text_of(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string utf8_of(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString translate(const char* source)
{
    return QCoreApplication::translate("ej::viewmodel::TimelineModel", source);
}

QString kind_text(const Entry& entry)
{
    if (std::holds_alternative<ErrorEntry>(entry.m_Body))
    {
        return QStringLiteral("error");
    }
    if (std::holds_alternative<NoteEntry>(entry.m_Body))
    {
        return QStringLiteral("note");
    }
    if (std::holds_alternative<InteractionEntry>(entry.m_Body))
    {
        return QStringLiteral("interaction");
    }
    return QStringLiteral("header_change");
}

/// The marker an error line starts with, without the space after it ("T|"); empty for a note, for a change of the
/// header, and for an error line when the error prefix is empty (PRF-13).
std::string_view line_marker(const Entry& entry, std::string_view error_prefix)
{
    if (std::holds_alternative<ErrorEntry>(entry.m_Body))
    {
        return error_prefix;
    }
    // A note has no marker on the screen (the export keeps it).
    return {};
}

/// The line of `entry` as the screens show it: the export line, without the marker of a note.
std::string line_of_screen(const Entry& entry, const std::string& export_line)
{
    return std::holds_alternative<NoteEntry>(entry.m_Body) ? export_format::without_note_marker(export_line)
                                                           : export_line;
}

bool is_header_change(const Entry* entry)
{
    return entry != nullptr && std::holds_alternative<HeaderChangeEntry>(entry->m_Body);
}

bool same_body(const EntryBody& a, const EntryBody& b)
{
    if (a.index() != b.index())
    {
        return false;
    }
    if (const auto* x = std::get_if<ErrorEntry>(&a))
    {
        const auto& y = std::get<ErrorEntry>(b);
        return x->m_ErrorType == y.m_ErrorType && x->m_AttachedNote == y.m_AttachedNote &&
               x->m_ErrorTypeLabel == y.m_ErrorTypeLabel;
    }
    if (const auto* x = std::get_if<NoteEntry>(&a))
    {
        const auto& y = std::get<NoteEntry>(b);
        return x->m_Template == y.m_Template && x->m_Value == y.m_Value;
    }
    if (const auto* x = std::get_if<InteractionEntry>(&a))
    {
        const auto& y = std::get<InteractionEntry>(b);
        return x->m_Type == y.m_Type && x->m_TypeLabel == y.m_TypeLabel && x->m_People == y.m_People &&
               x->m_Value == y.m_Value && x->m_AttachedNote == y.m_AttachedNote;
    }
    const auto& x = std::get<HeaderChangeEntry>(a);
    const auto& y = std::get<HeaderChangeEntry>(b);
    return x.m_Field == y.m_Field && x.m_NewValue == y.m_NewValue;
}

QString header_refusal(const HeaderError& error)
{
    switch (error.m_Kind)
    {
    case HeaderError::Kind::INVALID_UNIT_NUMBER:
        return translate("That is not a valid unit number.");
    case HeaderError::Kind::INVALID_SOFTWARE_VERSION:
        return translate("That is not a valid software version.");
    case HeaderError::Kind::INVALID_EXTRA_VERSIONS:
        return translate("Those are not valid versions.");
    case HeaderError::Kind::EMPTY_LOCATION:
        return translate("The location cannot be empty.");
    case HeaderError::Kind::UNCHANGED:
        return translate("That is the value already in force.");
    case HeaderError::Kind::UNKNOWN_ENTRY:
        return translate("That entry is not in this log.");
    case HeaderError::Kind::NOT_A_HEADER_CHANGE:
        return translate("That entry is not a header change.");
    case HeaderError::Kind::DUPLICATE_ID:
        return translate("An entry with that id exists already.");
    }
    return translate("That value is refused.");
}

/// A local time worked out in a zone.
struct Resolution
{
    UtcMillis m_Utc;
    bool m_Skipped = false;  // the clocks skip this local time: `m_Utc` is the instant after the gap
    bool m_Repeated = false; // the clocks show this local time twice: `m_Utc` is the earlier one
};

bool same_local(const LocalTime& a, const LocalTime& b)
{
    return a.m_Date == b.m_Date && a.m_SinceMidnight == b.m_SinceMidnight;
}

/// The instant of `local` in `zone`, with whether the local time is skipped or repeated there. The provider resolves
/// both (Stage 1): a repeated time to the earlier instant, a skipped one to the instant after the gap. The skipped
/// case is found by reading the answer back, and the repeated one by looking for the same local time again a little
/// later.
Result<Resolution, TimeZoneProvider::Error> resolve_local(const TimeZoneProvider& zones, std::string_view zone,
                                                          const LocalTime& local)
{
    const auto utc = zones.to_utc(local, zone);
    if (!utc)
    {
        return fail(utc.error());
    }
    const auto back = zones.to_local(Timestamp{*utc, std::string(zone)});
    if (!back)
    {
        return fail(back.error());
    }
    Resolution resolution;
    resolution.m_Utc = *utc;
    if (!same_local(*back, local))
    {
        resolution.m_Skipped = true;
        return resolution;
    }
    for (const auto step : REPEAT_STEPS)
    {
        const auto later = zones.to_local(Timestamp{*utc + step, std::string(zone)});
        if (later && same_local(*later, local))
        {
            resolution.m_Repeated = true;
            break;
        }
    }
    return resolution;
}

std::optional<std::chrono::year_month_day> parse_date(const QString& text)
{
    return parse_iso_date(utf8_of(text));
}

} // namespace

TimelineModel::TimelineModel(const Environment& environment, ConfigurationSource& configuration, Clipboard& clipboard,
                             QObject* parent)
    : QAbstractListModel(parent), m_Environment(environment), m_Configuration(configuration), m_Clipboard(clipboard)
{
    m_Configuration.add_observer(static_cast<ConfigurationObserver&>(*this));
}

TimelineModel::~TimelineModel()
{
    if (m_Document != nullptr)
    {
        m_Document->remove_observer(static_cast<LogObserver&>(*this));
    }
    if (m_Source != nullptr)
    {
        m_Source->remove_observer(static_cast<OpenLogObserver&>(*this));
    }
    m_Configuration.remove_observer(static_cast<ConfigurationObserver&>(*this));
}

// ------------------------------------------------------------------------------------------- which log is shown

void TimelineModel::stop_following()
{
    if (m_Source != nullptr)
    {
        m_Source->remove_observer(static_cast<OpenLogObserver&>(*this));
        m_Source = nullptr;
    }
}

void TimelineModel::switch_document(LogDocument* document)
{
    beginResetModel();
    if (m_Document != nullptr)
    {
        m_Document->remove_observer(static_cast<LogObserver&>(*this));
    }
    m_Document = document;
    if (m_Document != nullptr)
    {
        m_Document->add_observer(static_cast<LogObserver&>(*this));
    }
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    m_Rows = make_rows(context);
    endResetModel();
    update_summary();
}

void TimelineModel::bind(std::shared_ptr<LogDocument> document)
{
    stop_following();
    m_Pinned = std::move(document);
    switch_document(m_Pinned.get());
}

void TimelineModel::follow(OpenLogSource& source)
{
    stop_following();
    m_Pinned.reset();
    m_Source = &source;
    m_Source->add_observer(static_cast<OpenLogObserver&>(*this));
    switch_document(source.open_log());
}

void TimelineModel::unbind()
{
    stop_following();
    m_Pinned.reset();
    switch_document(nullptr);
}

void TimelineModel::on_open_log_changed(LogDocument* document)
{
    if (document != m_Document)
    {
        switch_document(document);
    }
}

// ----------------------------------------------------------------------------------------------------- the summary

int TimelineModel::entry_count() const
{
    return m_Summary.m_Count;
}

QString TimelineModel::last_line() const
{
    return m_Summary.m_Last;
}

QString TimelineModel::header_text() const
{
    return m_Summary.m_Header;
}

QString TimelineModel::title_text() const
{
    return m_Summary.m_Title;
}

bool TimelineModel::ready() const
{
    return m_Summary.m_Ready;
}

TimelineModel::Summary TimelineModel::make_summary(const ExportContext& context) const
{
    Summary summary;
    summary.m_Ready = m_Document != nullptr && m_Document->ready();
    summary.m_Count = static_cast<int>(m_Rows.size());
    if (!m_Rows.empty())
    {
        summary.m_Last = m_Rows.back().m_Shown;
    }
    if (!summary.m_Ready)
    {
        return summary;
    }
    const Log& log = m_Document->log();
    if (const auto header = render_header(log, context))
    {
        summary.m_Header = text_of(*header);
    }

    static constexpr std::array<std::string_view, 7> WEEKDAYS = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const std::chrono::year_month_day date = log.initial_header().m_LocalDate;
    if (date.ok())
    {
        const unsigned weekday = std::chrono::weekday{std::chrono::sys_days{date}}.c_encoding();
        const unsigned month = static_cast<unsigned>(date.month());
        const QString day = QStringLiteral("%1 %2 %3")
                                .arg(QString::fromUtf8(WEEKDAYS[weekday].data(), 3))
                                .arg(static_cast<unsigned>(date.day()))
                                .arg(QString::fromUtf8(export_format::MONTH_NAMES[month - 1].data(), 3));
        // The separator is a middle dot (U+00B7).
        summary.m_Title = summary.m_Count == 1 ? tr("%1 \xC2\xB7 1 entry").arg(day)
                                               : tr("%1 \xC2\xB7 %2 entries").arg(day).arg(summary.m_Count);
    }
    return summary;
}

void TimelineModel::update_summary()
{
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    Summary summary = make_summary(context);
    if (summary == m_Summary)
    {
        return;
    }
    m_Summary = std::move(summary);
    emit summaryChanged();
}

// ------------------------------------------------------------------------------------------------------------ rows

int TimelineModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_Rows.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_Rows.size()))
    {
        return {};
    }
    const Row& row = m_Rows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case ENTRY_ID:
        return row.m_IdText;
    case TIME_TEXT:
        return row.m_Time;
    case LINE_TEXT:
        return row.m_Line;
    case Qt::DisplayRole:
    case FULL_LINE:
        return row.m_Shown;
    case KIND:
        return row.m_Kind;
    case PREFIX_TEXT:
        return row.m_Prefix;
    case REST_TEXT:
        return row.m_Rest;
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{ENTRY_ID, "entryId"}, {TIME_TEXT, "timeText"},     {LINE_TEXT, "lineText"}, {FULL_LINE, "fullLine"},
            {KIND, "kind"},        {PREFIX_TEXT, "prefixText"}, {REST_TEXT, "restText"}};
}

/// Splits `row.m_Line` into the marker and the rest.
void TimelineModel::split_marker(const Entry& entry, Row& row, std::string_view error_prefix)
{
    const std::string_view marker = line_marker(entry, error_prefix);
    const std::string line = row.m_Line.toStdString();
    if (!marker.empty() && line.starts_with(marker))
    {
        row.m_Prefix = text_of(std::string(marker));
        std::string_view rest = std::string_view(line).substr(marker.size());
        if (rest.starts_with(' '))
        {
            rest.remove_prefix(1);
        }
        row.m_Rest = text_of(std::string(rest));
        return;
    }
    row.m_Rest = row.m_Line;
}

TimelineModel::Row TimelineModel::make_row(const Log& log, const Entry& entry, const ExportContext& context) const
{
    Row row;
    row.m_Id = entry.m_Id;
    row.m_IdText = text_of(entry.m_Id.format());
    row.m_Kind = kind_text(entry);

    // The time is the renderer's own time formatting; the row is only used when the line begins with it, so the two
    // columns of the design cannot drift from the export.
    const auto local = m_Environment.m_Zones.to_local(entry.m_Captured);
    const auto line = render_entry_line(log, entry.m_Id, context);
    if (local && line)
    {
        const std::string time = format_time(*local, context.m_TimeFormat);
        const std::string prefix = time + " ";
        if (line->starts_with(prefix))
        {
            row.m_Time = text_of(time);
            // A note shows without its marker; the export wording stays in `m_Full`, which Copy line gives.
            const std::string shown = line_of_screen(entry, *line);
            row.m_Line = text_of(shown.substr(std::min(prefix.size(), shown.size())));
            row.m_Full = text_of(*line);
            row.m_Shown = text_of(shown);
            split_marker(entry, row, context.m_ErrorPrefix);
            return row;
        }
    }
    row.m_Line = tr("This entry cannot be shown.");
    row.m_Rest = row.m_Line;
    return row;
}

std::vector<TimelineModel::Row> TimelineModel::make_rows(const ExportContext& context) const
{
    std::vector<Row> rows;
    if (m_Document == nullptr || !m_Document->ready())
    {
        return rows;
    }
    const Log& log = m_Document->log();
    const std::vector<const Entry*> order = log.sorted_entries(false);
    rows.reserve(order.size());
    for (const Entry* entry : order)
    {
        rows.push_back(make_row(log, *entry, context));
    }
    return rows;
}

int TimelineModel::find_row(EntryId id) const
{
    const auto it = std::ranges::find(m_Rows, id, &Row::m_Id);
    return it == m_Rows.end() ? -1 : static_cast<int>(it - m_Rows.begin());
}

void TimelineModel::reset_rows()
{
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    beginResetModel();
    m_Rows = make_rows(context);
    endResetModel();
    update_summary();
}

void TimelineModel::insert_row(EntryId id)
{
    if (find_row(id) >= 0)
    {
        return;
    }
    const Log& log = m_Document->log();
    const std::vector<const Entry*> order = log.sorted_entries(false);
    const auto it = std::ranges::find(order, id, [](const Entry* entry) { return entry->m_Id; });
    if (it == order.end() || order.size() != m_Rows.size() + 1)
    {
        reset_rows(); // the rows and the log disagree: start again rather than show a wrong list
        return;
    }
    const int position = static_cast<int>(it - order.begin());
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    Row row = make_row(log, **it, context);
    beginInsertRows({}, position, position);
    m_Rows.insert(m_Rows.begin() + position, std::move(row));
    endInsertRows();
}

void TimelineModel::remove_row(EntryId id)
{
    const int position = find_row(id);
    if (position < 0)
    {
        return;
    }
    beginRemoveRows({}, position, position);
    m_Rows.erase(m_Rows.begin() + position);
    endRemoveRows();
}

void TimelineModel::edit_row(EntryId id)
{
    const Log& log = m_Document->log();
    const Entry* entry = log.find(id);
    if (entry == nullptr || entry->m_Deleted)
    {
        remove_row(id);
        return;
    }
    const std::vector<const Entry*> order = log.sorted_entries(false);
    const auto it = std::ranges::find(order, id, [](const Entry* candidate) { return candidate->m_Id; });
    const int from = find_row(id);
    if (from < 0 || it == order.end() || order.size() != m_Rows.size())
    {
        reset_rows();
        return;
    }
    const int to = static_cast<int>(it - order.begin());
    if (to != from)
    {
        // Moving down, the destination is the row after the last one moved.
        const int destination = to > from ? to + 1 : to;
        beginMoveRows({}, from, from, {}, destination);
        const auto first = m_Rows.begin();
        if (to > from)
        {
            std::rotate(first + from, first + from + 1, first + to + 1);
        }
        else
        {
            std::rotate(first + to, first + from, first + from + 1);
        }
        endMoveRows();
    }
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    m_Rows[static_cast<std::size_t>(to)] = make_row(log, *entry, context);
    const QModelIndex changed = index(to);
    emit dataChanged(changed, changed);
}

void TimelineModel::refresh_all_rows()
{
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    std::vector<Row> rows = make_rows(context);
    if (rows.size() != m_Rows.size())
    {
        reset_rows();
        return;
    }
    m_Rows = std::move(rows);
    if (!m_Rows.empty())
    {
        emit dataChanged(index(0), index(static_cast<int>(m_Rows.size()) - 1));
    }
}

void TimelineModel::refresh_wording()
{
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    std::vector<Row> rows = make_rows(context);
    if (rows.size() != m_Rows.size())
    {
        reset_rows();
        return;
    }
    const bool changed =
        !std::ranges::equal(rows, m_Rows, [](const Row& a, const Row& b)
                            { return a.m_Time == b.m_Time && a.m_Line == b.m_Line && a.m_Full == b.m_Full; });
    m_Rows = std::move(rows);
    if (changed)
    {
        emit dataChanged(index(0), index(static_cast<int>(m_Rows.size()) - 1));
    }
    update_summary();
}

void TimelineModel::on_configuration_changed(const ConfigurationSource&)
{
    refresh_wording();
}

void TimelineModel::on_log_changed(const LogDocument& document, const LogChange& change)
{
    if (&document != m_Document)
    {
        return;
    }
    const Log& log = m_Document->log();
    // A header change entry that appears, goes, moves or is edited changes the "old" side of the change lines after it.
    const bool header_entry = change.m_Entry && is_header_change(log.find(*change.m_Entry));

    switch (change.m_Kind)
    {
    case LogChange::Kind::LOADED:
    case LogChange::Kind::REPLACED:
        reset_rows();
        return;
    case LogChange::Kind::ENTRY_ADDED:
    case LogChange::Kind::ENTRY_RESTORED:
        if (change.m_Entry)
        {
            insert_row(*change.m_Entry);
        }
        break;
    case LogChange::Kind::ENTRY_DELETED:
        if (change.m_Entry)
        {
            remove_row(*change.m_Entry);
        }
        break;
    case LogChange::Kind::ENTRY_EDITED:
        if (change.m_Entry)
        {
            edit_row(*change.m_Entry);
        }
        break;
    case LogChange::Kind::HEADER_CHANGED:
        if (change.m_Entry)
        {
            insert_row(*change.m_Entry);
        }
        refresh_all_rows();
        update_summary();
        return;
    case LogChange::Kind::HEADER_CORRECTED:
        refresh_all_rows();
        update_summary();
        return;
    case LogChange::Kind::SHARED:
        return;
    }
    if (header_entry)
    {
        refresh_all_rows();
    }
    update_summary();
}

// ------------------------------------------------------------------------------------------------------------ copy

bool TimelineModel::copyLine(int row)
{
    if (row < 0 || row >= static_cast<int>(m_Rows.size()))
    {
        return false;
    }
    const Row& selected = m_Rows[static_cast<std::size_t>(row)];
    if (selected.m_Full.isEmpty())
    {
        return false;
    }
    std::string text = utf8_of(selected.m_Full);
    // The row shows the full name of a person; the line that leaves the device is written at the level of the
    // preference `name_export` (Stage 14, 3.6). The wording of every other kind of line is the same in both.
    if (m_Document != nullptr && m_Document->ready())
    {
        const ExportContext sharing = ExportContextBuilder(m_Configuration, m_Environment).build_for_sharing();
        if (const auto line = render_entry_line(m_Document->log(), selected.m_Id, sharing))
        {
            text = *line;
        }
    }
    m_Clipboard.set_text(text);
    return true;
}

bool TimelineModel::copyAll()
{
    if (m_Document == nullptr || !m_Document->ready())
    {
        return false;
    }
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build_for_sharing();
    const auto text = export_text(m_Document->log(), context);
    if (!text)
    {
        return false;
    }
    m_Clipboard.set_text(*text);
    return true;
}

// ---------------------------------------------------------------------------------------------------------- delete

std::optional<EntryId> TimelineModel::parse_id(const QString& text) const
{
    const auto uuid = Uuid::parse(utf8_of(text));
    if (!uuid)
    {
        return std::nullopt;
    }
    return EntryId(*uuid);
}

bool TimelineModel::deleteEntry(const QString& entryId)
{
    const auto id = parse_id(entryId);
    if (!id || m_Document == nullptr || !m_Document->ready())
    {
        return false;
    }
    const Entry* entry = m_Document->log().find(*id);
    if (entry == nullptr || entry->m_Deleted)
    {
        return false;
    }
    const QString failure = tr("The entry could not be deleted.");
    auto done = [guard = QPointer<TimelineModel>(this), failure](const Result<void, StorageError>& result)
    {
        if (guard && !result)
        {
            emit guard->writeFailed(QStringLiteral("deleteEntry"), failure);
        }
    };
    const auto accepted = m_Document->set_deleted(*id, true, LogChange::Origin::USER, std::move(done));
    if (!accepted)
    {
        emit writeFailed(QStringLiteral("deleteEntry"), text_of(accepted.error().m_Message));
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------------------ edit

QVariantList TimelineModel::interactionTypesFor(const QString& entryId) const
{
    QVariantList list;
    const auto id = parse_id(entryId);
    const Entry* entry = id ? m_Document->log().find(*id) : nullptr;
    const auto* interaction = entry != nullptr ? std::get_if<InteractionEntry>(&entry->m_Body) : nullptr;
    if (interaction == nullptr)
    {
        return list;
    }
    bool own_listed = false;
    for (const InteractionType& type : m_Configuration.configuration().m_InteractionTypes)
    {
        const bool own = type.m_Id == interaction->m_Type;
        if (type.m_Retired && !own)
        {
            continue;
        }
        own_listed = own_listed || own;
        QVariantMap item;
        item.insert(QStringLiteral("typeId"), text_of(type.m_Id.format()));
        item.insert(QStringLiteral("name"), text_of(type.m_Name));
        list.append(item);
    }
    if (!own_listed)
    {
        QVariantMap item;
        item.insert(QStringLiteral("typeId"), text_of(interaction->m_Type.format()));
        item.insert(QStringLiteral("name"), text_of(interaction->m_TypeLabel));
        list.append(item);
    }
    return list;
}

QVariantMap TimelineModel::entryForEdit(const QString& entryId) const
{
    const auto id = parse_id(entryId);
    if (!id || m_Document == nullptr || !m_Document->ready())
    {
        return {};
    }
    const Entry* entry = m_Document->log().find(*id);
    if (entry == nullptr)
    {
        return {};
    }
    const auto local = m_Environment.m_Zones.to_local(entry->m_Captured);
    if (!local)
    {
        return {};
    }

    QVariantMap map;
    map.insert(QStringLiteral("entryId"), text_of(entry->m_Id.format()));
    map.insert(QStringLiteral("kind"), kind_text(*entry));
    map.insert(QStringLiteral("localDate"), text_of(format_iso_date(local->m_Date).value_or(std::string{})));
    map.insert(QStringLiteral("localSeconds"), static_cast<int>(local->m_SinceMidnight.count()));
    map.insert(QStringLiteral("zone"), text_of(entry->m_Captured.m_Zone));
    map.insert(QStringLiteral("timeText"), text_of(format_time(*local, m_Configuration.configuration().m_TimeFormat)));

    map.insert(QStringLiteral("footnote"), footnote_for(*entry, map.value(QStringLiteral("timeText")).toString()));

    if (const auto* error = std::get_if<ErrorEntry>(&entry->m_Body))
    {
        const auto& types = m_Configuration.configuration().m_ErrorTypes;
        const auto type = std::ranges::find(types, error->m_ErrorType, &ErrorType::m_Id);
        const int slot =
            type == types.end() ? NO_TILE_COLOUR : tile_colour_slot(static_cast<std::size_t>(type - types.begin()));
        map.insert(QStringLiteral("colourSlot"), slot);
        map.insert(QStringLiteral("colourName"), tile_colour_name(slot));
        map.insert(QStringLiteral("errorTypeId"), text_of(error->m_ErrorType.format()));
        map.insert(QStringLiteral("errorTypeName"), text_of(error->m_ErrorTypeLabel));
        map.insert(QStringLiteral("attachedNote"), text_of(error->m_AttachedNote.value_or(std::string{})));
    }
    else if (const auto* note = std::get_if<NoteEntry>(&entry->m_Body))
    {
        map.insert(QStringLiteral("noteText"), text_of(note->m_Value));
        map.insert(QStringLiteral("templateId"), note->m_Template ? text_of(note->m_Template->format()) : QString());
    }
    else if (const auto* interaction = std::get_if<InteractionEntry>(&entry->m_Body))
    {
        map.insert(QStringLiteral("interactionTypeId"), text_of(interaction->m_Type.format()));
        map.insert(QStringLiteral("interactionTypeName"), text_of(interaction->m_TypeLabel));
        QStringList people;
        for (const PersonId& person : interaction->m_People)
        {
            people.append(text_of(person.format()));
        }
        map.insert(QStringLiteral("personIds"), people);
        // The location in force when the entry was made, for "Everyone here" in the picker; empty for none.
        const HeaderValues header = HeaderHistory(m_Document->log()).at(entry->m_Captured.m_Utc);
        map.insert(QStringLiteral("locationId"), header.m_Location && header.m_Location->m_Id
                                                     ? text_of(header.m_Location->m_Id->format())
                                                     : QString());
        // The names of the people as the screen shows them (the full name; "someone" when forgotten or unknown).
        QVariantMap names;
        for (const PersonId& person : interaction->m_People)
        {
            const auto name =
                m_Environment.m_People != nullptr ? m_Environment.m_People->name_of(person) : std::nullopt;
            names.insert(text_of(person.format()), name ? text_of(*name) : tr("someone"));
        }
        map.insert(QStringLiteral("personNames"), names);
        const auto& types = m_Configuration.configuration().m_InteractionTypes;
        const auto type = std::ranges::find(types, interaction->m_Type, &InteractionType::m_Id);
        const InteractionAsks asks = type == types.end() ? InteractionAsks::NOTHING : type->m_Asks;
        map.insert(QStringLiteral("asksPerson"), asks == InteractionAsks::PERSON || !interaction->m_People.empty());
        // The answer can be edited when the type asks for one, or when the entry already has one.
        map.insert(QStringLiteral("asksText"), asks == InteractionAsks::TEXT || interaction->m_Value.has_value());
        map.insert(QStringLiteral("value"), text_of(interaction->m_Value.value_or(std::string{})));
        map.insert(QStringLiteral("attachedNote"), text_of(interaction->m_AttachedNote.value_or(std::string{})));
    }
    else
    {
        const auto& change = std::get<HeaderChangeEntry>(entry->m_Body);
        map.insert(QStringLiteral("fieldKey"),
                   QString::fromUtf8(header_field_key(change.m_Field).data(),
                                     static_cast<qsizetype>(header_field_key(change.m_Field).size())));
        map.insert(QStringLiteral("newValue"), text_of(change.m_NewValue));
    }
    return map;
}

QString TimelineModel::footnote_for(const Entry& entry, const QString& time_text) const
{
    // The unit and the version that were in force when the entry was made.
    const HeaderValues header = HeaderHistory(m_Document->log()).at(entry.m_Captured.m_Utc);
    QString unit;
    if (header.m_UnitNumber)
    {
        unit = m_Configuration.configured()
                   ? text_of(header.m_UnitNumber->format(m_Configuration.configuration().m_UnitPrefix))
                   : text_of(header.m_UnitNumber->stored());
    }
    const QString version = header.m_SoftwareVersion ? text_of(header.m_SoftwareVersion->display()) : QString();

    if (!unit.isEmpty() && !version.isEmpty())
    {
        return tr("Logged at %1 on %2, version %3.").arg(time_text, unit, version);
    }
    if (!unit.isEmpty())
    {
        return tr("Logged at %1 on %2.").arg(time_text, unit);
    }
    if (!version.isEmpty())
    {
        return tr("Logged at %1, version %2.").arg(time_text, version);
    }
    return tr("Logged at %1.").arg(time_text);
}

Result<TimelineModel::Edit, QString> TimelineModel::work_out_edit(EntryId id, const QVariantMap& edit) const
{
    if (m_Document == nullptr || !m_Document->ready())
    {
        return fail(tr("The log is not loaded yet."));
    }
    const Entry* current = m_Document->log().find(id);
    if (current == nullptr)
    {
        return fail(tr("That entry is not in this log."));
    }
    if (edit.contains(QStringLiteral("kind")) && edit.value(QStringLiteral("kind")).toString() != kind_text(*current))
    {
        return fail(tr("The kind of an entry cannot be changed."));
    }
    if (edit.contains(QStringLiteral("zone")) &&
        edit.value(QStringLiteral("zone")).toString() != text_of(current->m_Captured.m_Zone))
    {
        return fail(tr("The time zone of an entry cannot be changed."));
    }

    Edit result;
    result.m_Entry = *current;
    Entry& entry = result.m_Entry;

    // The time. Whole seconds are what the sheet shows, so a sheet that was not touched keeps the instant to the
    // millisecond (and a second occurrence of a repeated time stays the second one).
    const auto local = m_Environment.m_Zones.to_local(current->m_Captured);
    if (!local)
    {
        return fail(tr("The time zone of this entry is not known."));
    }
    LocalTime wanted = *local;
    if (edit.contains(QStringLiteral("localDate")))
    {
        const auto date = parse_date(edit.value(QStringLiteral("localDate")).toString());
        if (!date)
        {
            return fail(tr("That date is not valid."));
        }
        wanted.m_Date = *date;
    }
    if (edit.contains(QStringLiteral("localSeconds")))
    {
        bool ok = false;
        const int seconds = edit.value(QStringLiteral("localSeconds")).toInt(&ok);
        if (!ok || seconds < 0 || seconds >= SECONDS_PER_DAY)
        {
            return fail(tr("That time of day is not valid."));
        }
        wanted.m_SinceMidnight = std::chrono::seconds{seconds};
    }
    if (!same_local(wanted, *local))
    {
        const auto utc = m_Environment.m_Zones.to_utc(wanted, current->m_Captured.m_Zone);
        if (!utc)
        {
            return fail(tr("The time zone of this entry is not known."));
        }
        entry.m_Captured.m_Utc = *utc;
        result.m_TimeChanged = true;
    }

    // The content.
    const Configuration& configuration = m_Configuration.configuration();
    if (auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
    {
        if (edit.contains(QStringLiteral("errorTypeId")))
        {
            const auto uuid = Uuid::parse(utf8_of(edit.value(QStringLiteral("errorTypeId")).toString()));
            if (!uuid)
            {
                return fail(tr("That error type is not known."));
            }
            // The same type keeps the snapshot it has, even if the configuration has no such type any more (the
            // sheet of an old entry sends it back unchanged); a new type takes a new name snapshot.
            if (ErrorTypeId(*uuid) != error->m_ErrorType)
            {
                const auto type = std::ranges::find(configuration.m_ErrorTypes, ErrorTypeId(*uuid), &ErrorType::m_Id);
                if (type == configuration.m_ErrorTypes.end())
                {
                    return fail(tr("That error type is not known."));
                }
                if (!error->retype(type->m_Id, type->m_Name))
                {
                    return fail(tr("That error type has no name."));
                }
            }
        }
        if (edit.contains(QStringLiteral("attachedNote")))
        {
            const QString note = edit.value(QStringLiteral("attachedNote")).toString();
            if (note.trimmed().isEmpty())
            {
                error->m_AttachedNote.reset();
            }
            else
            {
                error->m_AttachedNote = utf8_of(note);
            }
        }
    }
    else if (auto* note = std::get_if<NoteEntry>(&entry.m_Body))
    {
        if (edit.contains(QStringLiteral("templateId")))
        {
            const QString text = edit.value(QStringLiteral("templateId")).toString();
            if (text.isEmpty())
            {
                note->m_Template.reset();
            }
            else
            {
                const auto uuid = Uuid::parse(utf8_of(text));
                const NoteTemplateId wanted_template = uuid ? NoteTemplateId(*uuid) : NoteTemplateId{};
                const bool known = uuid && std::ranges::any_of(configuration.m_Templates, [&](const NoteTemplate& t)
                                                               { return t.m_Id == wanted_template; });
                if (!known)
                {
                    return fail(tr("That note template is not known."));
                }
                note->m_Template = wanted_template;
            }
        }
        if (edit.contains(QStringLiteral("noteText")))
        {
            note->m_Value = utf8_of(edit.value(QStringLiteral("noteText")).toString());
        }
        if (text_of(note->m_Value).trimmed().isEmpty())
        {
            return fail(tr("A note cannot be empty."));
        }
    }
    else if (auto* interaction = std::get_if<InteractionEntry>(&entry.m_Body))
    {
        if (edit.contains(QStringLiteral("interactionTypeId")))
        {
            const auto uuid = Uuid::parse(utf8_of(edit.value(QStringLiteral("interactionTypeId")).toString()));
            if (!uuid)
            {
                return fail(tr("That interaction type is not known."));
            }
            // The same type keeps the name it has, even when the configuration lost it; a new type takes a new one.
            if (InteractionTypeId(*uuid) != interaction->m_Type)
            {
                const auto type = std::ranges::find(configuration.m_InteractionTypes, InteractionTypeId(*uuid),
                                                    &InteractionType::m_Id);
                if (type == configuration.m_InteractionTypes.end() || type->m_Retired || type->m_Name.empty())
                {
                    return fail(tr("That interaction type is not known."));
                }
                interaction->m_Type = type->m_Id;
                interaction->m_TypeLabel = type->m_Name;
            }
        }
        if (edit.contains(QStringLiteral("personIds")))
        {
            std::vector<PersonId> people;
            for (const QVariant& item : edit.value(QStringLiteral("personIds")).toList())
            {
                const auto uuid = Uuid::parse(utf8_of(item.toString()));
                if (!uuid)
                {
                    return fail(tr("That person is not known."));
                }
                if (!add_person_pick(people, PersonId(*uuid)))
                {
                    return fail(tr("An interaction can name at most %1 people.").arg(MAX_PEOPLE_PER_INTERACTION));
                }
            }
            interaction->m_People = std::move(people);
        }
        if (edit.contains(QStringLiteral("value")))
        {
            const QString value = edit.value(QStringLiteral("value")).toString();
            if (value.trimmed().isEmpty())
            {
                interaction->m_Value.reset();
            }
            else
            {
                interaction->m_Value = utf8_of(value);
            }
        }
        if (edit.contains(QStringLiteral("attachedNote")))
        {
            const QString typed_note = edit.value(QStringLiteral("attachedNote")).toString();
            if (typed_note.trimmed().isEmpty())
            {
                interaction->m_AttachedNote.reset();
            }
            else
            {
                interaction->m_AttachedNote = utf8_of(typed_note);
            }
        }
    }
    else if (auto* change = std::get_if<HeaderChangeEntry>(&entry.m_Body))
    {
        if (edit.contains(QStringLiteral("newValue")))
        {
            const std::string value = utf8_of(edit.value(QStringLiteral("newValue")).toString());
            if (value != change->m_NewValue)
            {
                // The rules of the field, on a scratch copy; the stored text is the canonical one.
                Log scratch = m_Document->log();
                if (auto corrected = correct_header_change(scratch, id, value); !corrected)
                {
                    return fail(header_refusal(corrected.error()));
                }
                const auto& canonical = std::get<HeaderChangeEntry>(scratch.find(id)->m_Body);
                result.m_ValueChanged = canonical.m_NewValue != change->m_NewValue;
                change->m_NewValue = canonical.m_NewValue;
            }
        }
    }

    if (const auto valid = validate_entry(entry); !valid)
    {
        return fail(valid.error().m_Message.empty() ? tr("That change is not allowed.")
                                                    : text_of(valid.error().m_Message));
    }
    result.m_Unchanged = !result.m_TimeChanged && same_body(entry.m_Body, current->m_Body);
    return result;
}

QString TimelineModel::validateEdit(const QString& entryId, const QVariantMap& edit) const
{
    const auto id = parse_id(entryId);
    if (!id)
    {
        return tr("That entry is not in this log.");
    }
    const auto worked = work_out_edit(*id, edit);
    return worked ? QString() : worked.error();
}

QString TimelineModel::previewLine(const QString& entryId, const QVariantMap& edit) const
{
    const auto id = parse_id(entryId);
    if (!id)
    {
        return {};
    }
    const auto worked = work_out_edit(*id, edit);
    if (!worked)
    {
        return {};
    }
    // The renderer, on a copy of the log that holds the edit: what is previewed is what is saved.
    Log scratch = m_Document->log();
    if (!worked->m_Unchanged && !scratch.replace(*id, worked->m_Entry, m_Environment.m_Clock.now()))
    {
        return {};
    }
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
    const auto line = render_entry_line(scratch, *id, context);
    if (!line)
    {
        return {};
    }
    const Entry* saved = scratch.find(*id);
    return text_of(saved != nullptr ? line_of_screen(*saved, *line) : *line);
}

bool TimelineModel::saveEntry(const QString& entryId, const QVariantMap& edit)
{
    const auto id = parse_id(entryId);
    if (!id)
    {
        return false;
    }
    const auto worked = work_out_edit(*id, edit);
    if (!worked)
    {
        return false;
    }
    if (worked->m_Unchanged)
    {
        return true;
    }
    const QString failure = tr("The change could not be saved.");
    auto done = [guard = QPointer<TimelineModel>(this), failure](const Result<void, StorageError>& result)
    {
        if (guard && !result)
        {
            emit guard->writeFailed(QStringLiteral("saveEntry"), failure);
        }
    };
    // A new value for a header change, nothing else: the correction the document has for it. Anything else is an edit.
    Result<void, DocumentError> accepted;
    if (worked->m_ValueChanged && !worked->m_TimeChanged)
    {
        const auto& change = std::get<HeaderChangeEntry>(worked->m_Entry.m_Body);
        accepted = m_Document->correct_header_change(*id, change.m_NewValue, std::move(done));
    }
    else
    {
        accepted = m_Document->edit_entry(*id, worked->m_Entry, std::move(done));
    }
    if (!accepted)
    {
        emit writeFailed(QStringLiteral("saveEntry"), text_of(accepted.error().m_Message));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------- local times

QVariantMap TimelineModel::localToUtc(const QString& entryId, const QString& localDate, int localSeconds) const
{
    const auto refuse = [](const QString& message)
    {
        QVariantMap map;
        map.insert(QStringLiteral("ok"), false);
        map.insert(QStringLiteral("utcMillis"), qlonglong{0});
        map.insert(QStringLiteral("repeated"), false);
        map.insert(QStringLiteral("message"), message);
        return map;
    };

    const auto id = parse_id(entryId);
    const Entry* entry = id && m_Document != nullptr && m_Document->ready() ? m_Document->log().find(*id) : nullptr;
    if (entry == nullptr)
    {
        return refuse(tr("That entry is not in this log."));
    }
    const auto date = parse_date(localDate);
    if (!date)
    {
        return refuse(tr("That date is not valid."));
    }
    if (localSeconds < 0 || localSeconds >= SECONDS_PER_DAY)
    {
        return refuse(tr("That time of day is not valid."));
    }

    const auto resolved = resolve_local(m_Environment.m_Zones, entry->m_Captured.m_Zone,
                                        LocalTime{*date, std::chrono::seconds{localSeconds}});
    if (!resolved)
    {
        return refuse(tr("The time zone of this entry is not known."));
    }
    if (resolved->m_Skipped)
    {
        return refuse(tr("That time does not exist on that day, because the clocks go forward. Choose another time."));
    }

    QVariantMap map;
    map.insert(QStringLiteral("ok"), true);
    map.insert(QStringLiteral("utcMillis"), static_cast<qlonglong>(resolved->m_Utc.time_since_epoch().count()));
    map.insert(QStringLiteral("repeated"), resolved->m_Repeated);
    map.insert(QStringLiteral("message"),
               resolved->m_Repeated ? tr("That time happens twice on that day, because the clocks go back. The first "
                                         "one is used.")
                                    : QString());
    return map;
}

QString TimelineModel::formatLocalSeconds(int localSeconds) const
{
    if (localSeconds < 0 || localSeconds >= SECONDS_PER_DAY)
    {
        return {};
    }
    const LocalTime time{std::chrono::year{1970} / 1 / 1, std::chrono::seconds{localSeconds}};
    return text_of(format_time(time, m_Configuration.configuration().m_TimeFormat));
}

int TimelineModel::nowLocalSeconds(const QString& entryId) const
{
    std::string zone = m_Environment.m_Zone.current();
    if (const auto id = parse_id(entryId); id && m_Document != nullptr && m_Document->ready())
    {
        if (const Entry* entry = m_Document->log().find(*id); entry != nullptr)
        {
            zone = entry->m_Captured.m_Zone;
        }
    }
    const auto local = m_Environment.m_Zones.to_local(Timestamp{m_Environment.m_Clock.now(), zone});
    return local ? static_cast<int>(local->m_SinceMidnight.count()) : -1;
}

int TimelineModel::rowOf(const QString& entryId) const
{
    const auto id = parse_id(entryId);
    return id ? find_row(*id) : -1;
}

} // namespace ej::viewmodel
