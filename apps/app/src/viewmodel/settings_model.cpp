// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/settings_model.hpp"

#include "error_journaling/entry.hpp"
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/note_template.hpp"
#include "error_journaling/time_format.hpp"
#include "error_journaling/unit_number.hpp"
#include "error_journaling/uuid.hpp"
#include "viewmodel/board_layout_model.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/error_type_list_model.hpp"
#include "viewmodel/import_messages.hpp"
#include "viewmodel/location_list_model.hpp"
#include "viewmodel/template_list_model.hpp"

#include <QFileInfo>
#include <QPointer>
#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <ranges>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

namespace
{

// The limits of the core's reader (`import_configuration`), which also guards the file when it is read back at launch.
constexpr int MAX_NAME = 200;
constexpr int MAX_PREFIX = 20;
constexpr int MAX_PATTERN = 500;
/// How the "Line in the log" field shows the answer slot, which the file still stores as {value}.
const QString ANSWER_TOKEN = QStringLiteral("[answer]");
constexpr int MAX_SORT_ORDER = 1'000'000;

const QString OP_FACILITATOR = QStringLiteral("facilitator");
const QString OP_PREFIX = QStringLiteral("unit prefix");
const QString OP_TIME_FORMAT = QStringLiteral("time format");
const QString OP_TOTALS = QStringLiteral("totals");
const QString OP_ERROR_PREFIX = QStringLiteral("error prefix");
const QString OP_LOCATION = QStringLiteral("location");
const QString OP_ENVIRONMENT = QStringLiteral("unit environment");
const QString OP_ERROR_TYPE = QStringLiteral("error type");
const QString OP_BOARD = QStringLiteral("board");
const QString OP_TEMPLATE = QStringLiteral("template");
const QString OP_IMPORT = QStringLiteral("import");

/// The text trimmed and checked against `max_length` (counted as the core's reader counts: UTF-16 units).
std::expected<std::string, QString> checked_text(const QString& raw, int max_length, const QString& when_empty)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
    {
        return std::unexpected(when_empty);
    }
    if (text.size() > max_length)
    {
        return std::unexpected(SettingsModel::tr("Use %1 characters or fewer.").arg(max_length));
    }
    return text.toStdString();
}

std::expected<std::string, QString> checked_name(const QString& raw)
{
    return checked_text(raw, MAX_NAME, SettingsModel::tr("Enter a name."));
}

template <typename Id>
std::optional<Id> parse_id(const QString& text)
{
    const auto parsed = Uuid::parse(text.toStdString());
    if (!parsed || parsed->is_nil())
    {
        return std::nullopt;
    }
    return Id{*parsed};
}

/// An environment name as the view model takes it: spaces dropped, upper case lowered, and then judged by the core's own
/// rule for the names of a setup file.
std::expected<std::string, QString> checked_environment(const QString& raw)
{
    const QString text = raw.trimmed().toLower();
    if (text.isEmpty())
    {
        return std::unexpected(SettingsModel::tr("Enter a name, for example prod."));
    }
    if (static_cast<std::size_t>(text.size()) > UnitNumber::MAX_ENVIRONMENT_LENGTH)
    {
        return std::unexpected(SettingsModel::tr("Use %1 characters or fewer.").arg(UnitNumber::MAX_ENVIRONMENT_LENGTH));
    }
    const std::string name = text.toStdString();
    if (!UnitNumber::is_valid_environment(name))
    {
        return std::unexpected(SettingsModel::tr("Use lower-case letters, digits and dashes, for example prod. It cannot "
                                                 "start or end with a dash."));
    }
    return name;
}

QString environment_taken(const std::string& name)
{
    return SettingsModel::tr("\xE2\x80\x9C%1\xE2\x80\x9D is already in the list.").arg(QString::fromStdString(name));
}

QString no_such_item()
{
    return SettingsModel::tr("That item is not in the list any more.");
}

QString list_full()
{
    return SettingsModel::tr("The list is full (%1 at most).").arg(CONFIG_MAX_ENTRIES_PER_LIST);
}

/// True when another live item has the name, ignoring case: the core's rule (`import_configuration` refuses the file
/// otherwise). Retired items do not count.
template <typename Item, typename Id>
bool name_in_use(const std::vector<Item>& items, const std::string& name, const std::optional<Id>& except)
{
    const QString folded = QString::fromStdString(name).toCaseFolded();
    return std::ranges::any_of(items,
                               [&](const Item& item)
                               {
                                   if (except && item.m_Id == *except)
                                   {
                                       return false;
                                   }
                                   if constexpr (requires { item.m_Retired; })
                                   {
                                       if (item.m_Retired)
                                       {
                                           return false;
                                       }
                                   }
                                   return QString::fromStdString(item.m_Name).toCaseFolded() == folded;
                               });
}

/// What a list holds, for the refusal of a name that is taken.
enum class NameKind
{
    ERROR_TYPE,
    LOCATION,
    NOTE_TEMPLATE
};

QString name_taken(const std::string& name, NameKind kind)
{
    const QString shown = QString::fromStdString(name);
    // The quotes are typographic (U+201C, U+201D), as in the designs.
    switch (kind)
    {
    case NameKind::ERROR_TYPE:
        return SettingsModel::tr("There is already an error type called \xE2\x80\x9C%1\xE2\x80\x9D. Names must be "
                                 "different.")
            .arg(shown);
    case NameKind::LOCATION:
        return SettingsModel::tr("There is already a location called \xE2\x80\x9C%1\xE2\x80\x9D. Names must be "
                                 "different.")
            .arg(shown);
    case NameKind::NOTE_TEMPLATE:
        break;
    }
    return SettingsModel::tr("There is already a note template called \xE2\x80\x9C%1\xE2\x80\x9D. Names must be "
                             "different.")
        .arg(shown);
}

/// A new sort order after every existing one.
template <typename Item>
int next_sort_order(const std::vector<Item>& items)
{
    int highest = 0;
    for (const Item& item : items)
    {
        highest = std::max(highest, item.m_SortOrder);
    }
    return highest < MAX_SORT_ORDER ? highest + 1 : MAX_SORT_ORDER;
}

QString pattern_message(PatternError error)
{
    // The words are the editor's ("Line in the log", "Insert answer", the [answer] token); the stored pattern's {value}
    // is named only where the person has typed a bracket of their own.
    switch (error)
    {
    case PatternError::MISSING_PLACEHOLDER:
        return SettingsModel::tr("The line needs the answer in it. Tap Insert answer to put it where it goes, for "
                                 "example \xE2\x80\x9CPlayed [answer]\xE2\x80\x9D.");
    case PatternError::UNBALANCED_BRACE:
        return SettingsModel::tr("The line has a curly bracket on its own. Remove it, or type two in a row to show one "
                                 "in the log. Tap Insert answer for the answer.");
    case PatternError::UNKNOWN_PLACEHOLDER:
        return SettingsModel::tr("Words between curly brackets, such as {name}, are not known. Remove them, and tap "
                                 "Insert answer for the answer.");
    }
    return SettingsModel::tr("The line cannot be used.");
}

std::expected<std::string, QString> checked_pattern(const QString& raw)
{
    if (raw.trimmed().isEmpty())
    {
        return std::unexpected(pattern_message(PatternError::MISSING_PLACEHOLDER));
    }
    if (raw.size() > MAX_PATTERN)
    {
        return std::unexpected(SettingsModel::tr("The line can have up to %1 characters.").arg(MAX_PATTERN));
    }
    std::string pattern = raw.toStdString();
    if (const auto valid = validate_pattern(pattern); !valid)
    {
        return std::unexpected(pattern_message(valid.error()));
    }
    return pattern;
}

/// What the person is told when a storage call failed.
QString plain_message(const StorageError& error)
{
    switch (error.m_Kind)
    {
    case StorageErrorKind::NOT_FOUND:
        return SettingsModel::tr("That file could not be found.");
    case StorageErrorKind::PERMISSION:
        return SettingsModel::tr("The app is not allowed to use that file.");
    case StorageErrorKind::LIMIT_EXCEEDED:
        return SettingsModel::tr("That file is too large to be a settings file (the limit is 1 MiB).");
    case StorageErrorKind::DISK_FULL:
        return SettingsModel::tr("There is no space left on this phone.");
    case StorageErrorKind::LOCKED:
        return SettingsModel::tr("The file is locked. Unlock the phone and try again.");
    default:
        return SettingsModel::tr("The settings file could not be read or written.");
    }
}

template <typename Item>
int count_live(const std::vector<Item>& items)
{
    return static_cast<int>(std::ranges::count_if(items, [](const Item& item) { return !item.m_Retired; }));
}

ConfigEditError refused(const QString& message)
{
    return ConfigEditError{ConfigEditError::Code::REFUSED, message.toStdString()};
}

QString message_of(const std::expected<void, ConfigEditError>& result)
{
    return result ? QString() : QString::fromStdString(result.error().m_Message);
}

bool is_complete(const Configuration& configuration)
{
    return !configuration.m_Facilitator.empty() && !configuration.m_UnitPrefix.empty();
}

/// Writes `layout` back: the types in it get their position, the other live types are off the board. A retired type
/// keeps whatever value it had.
void write_layout(Configuration& configuration, const BoardLayout& layout)
{
    for (ErrorType& type : configuration.m_ErrorTypes)
    {
        if (!type.m_Retired)
        {
            type.m_BoardPosition.reset();
        }
    }
    for (std::size_t position = 0; position < layout.size(); ++position)
    {
        if (layout[position])
        {
            configuration.m_ErrorTypes[*layout[position]].m_BoardPosition = static_cast<int>(position) + 1;
        }
    }
}

bool valid_position(int position)
{
    return position >= 1 && position <= CONFIG_BOARD_POSITIONS;
}

/// The indexes of `locations`, in sort order (ties in list order).
std::vector<std::size_t> location_order(const std::vector<Location>& locations)
{
    std::vector<std::size_t> order(locations.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return locations[i].m_SortOrder; });
    return order;
}

} // namespace

/// What a `loadImport` job found, made on the storage thread and read on the GUI thread.
struct SettingsModel::ImportOutcome
{
    std::optional<Configuration> m_Configuration;
    std::optional<StorageError> m_ReadError;
    std::vector<ConfigError> m_Errors;
};

SettingsModel::SettingsModel(StorageGateway& gateway, const Environment& environment, QObject* parent)
    : QObject(parent), m_Gateway(gateway), m_Ids(environment.m_Ids), m_Clock(environment.m_Clock),
      m_Zone(environment.m_Zone), m_Zones(environment.m_Zones), m_ShownTimeFormat(Configuration{}.m_TimeFormat),
      m_ErrorTypes(new ErrorTypeListModel(this)), m_Locations(new LocationListModel(this)),
      m_Templates(new TemplateListModel(this)), m_BoardLayout(new BoardLayoutModel(this))
{
    m_ErrorTypes->refresh(m_Setup);
    m_Locations->refresh(m_Setup);
    m_Templates->refresh(m_Setup);
    m_BoardLayout->refresh(m_Setup);
}

SettingsModel::~SettingsModel() = default;

// ---------------------------------------------------------------------------------------------------------- reading

QString SettingsModel::facilitator() const
{
    return QString::fromStdString(view().m_Facilitator);
}

QString SettingsModel::unit_prefix() const
{
    return QString::fromStdString(view().m_UnitPrefix);
}

QStringList SettingsModel::unit_environments() const
{
    QStringList names;
    for (const std::string& name : view().m_UnitEnvironments)
    {
        names.push_back(QString::fromStdString(name));
    }
    return names;
}

QString SettingsModel::time_style() const
{
    return view().m_TimeFormat.m_Style == TimeStyle::H12 ? QStringLiteral("12h") : QStringLiteral("24h");
}

bool SettingsModel::time_seconds() const
{
    return view().m_TimeFormat.m_Seconds;
}

QString SettingsModel::time_meridiem() const
{
    return view().m_TimeFormat.m_Meridiem == MeridiemCase::LOWER ? QStringLiteral("lower") : QStringLiteral("upper");
}

QString SettingsModel::time_format_key() const
{
    return time_style() + (time_seconds() ? QStringLiteral("-seconds-") : QStringLiteral("-")) + time_meridiem();
}

QString SettingsModel::formatTime(qint64 utcMs, const QString& zone) const
{
    const Timestamp when{UtcMillis{std::chrono::milliseconds{utcMs}}, zone.toStdString()};
    const auto local = m_Zones.to_local(when);
    if (!local)
    {
        return {};
    }
    return QString::fromStdString(format_time(*local, view().m_TimeFormat));
}

bool SettingsModel::include_totals() const
{
    return view().m_IncludeTotals;
}

QString SettingsModel::error_prefix() const
{
    return QString::fromStdString(view().m_ErrorPrefix);
}

QAbstractItemModel* SettingsModel::error_types() const
{
    return m_ErrorTypes;
}

QAbstractItemModel* SettingsModel::locations() const
{
    return m_Locations;
}

QAbstractItemModel* SettingsModel::templates() const
{
    return m_Templates;
}

QAbstractItemModel* SettingsModel::board_layout() const
{
    return m_BoardLayout;
}

QString SettingsModel::unit_prefix_hint() const
{
    return tr("Added in front of the number you type each day.");
}

int SettingsModel::first_empty_position() const
{
    const BoardLayout layout = viewmodel::board_layout(view());
    const auto empty = std::ranges::find(layout, std::optional<std::size_t>{});
    return empty == layout.end() ? 0 : static_cast<int>(empty - layout.begin()) + 1;
}

int SettingsModel::next_colour_slot() const
{
    return tile_colour_slot(view().m_ErrorTypes.size());
}

QString SettingsModel::colourName(int slot) const
{
    return tile_colour_name(slot);
}

// ---------------------------------------------------------------------------------------------------------- loading

void SettingsModel::load()
{
    if (m_LoadStarted)
    {
        return;
    }
    m_LoadStarted = true;
    m_Gateway.run([](StorageContext& context) { return context.m_Config.load(); },
                  [self = QPointer<SettingsModel>(this)](Result<std::optional<std::string>, StorageError> result)
                  {
                      if (SettingsModel* model = self.data(); model != nullptr)
                      {
                          model->finish_load(std::move(result));
                      }
                  });
}

void SettingsModel::finish_load(Result<std::optional<std::string>, StorageError> result)
{
    m_Loading = false;
    if (!result)
    {
        m_LoadProblem = tr("The saved settings could not be read. Set up the app again, or import a file; the saved "
                           "file has not been changed.");
        emit writeFailed(QStringLiteral("load"), plain_message(result.error()));
        publish(false);
        return;
    }
    if (!*result)
    {
        publish(false);
        return;
    }
    auto imported = import_configuration(**result);
    if (!imported)
    {
        // Setup starts again. The file stays as it is until the first save, which copies it aside first (the store).
        m_LoadProblem =
            tr("The saved settings could not be used, so the app is asking for setup again. The saved file is kept. %1")
                .arg(QString::fromStdString(imported.error().front().m_Message));
        publish(false);
        return;
    }
    m_Configuration = std::move(*imported);
    m_Configured = true;
    publish(true);
}

// --------------------------------------------------------------------------------------------------------- changing

std::expected<void, ConfigEditError> SettingsModel::change(const QString& operation, const Edit& edit,
                                                           bool needs_configured)
{
    using Code = ConfigEditError::Code;
    if (m_Loading)
    {
        return fail(ConfigEditError{Code::NOT_READY, tr("The settings are still loading.").toStdString()});
    }
    if (needs_configured && !m_Configured)
    {
        return fail(ConfigEditError{Code::NOT_READY, tr("Set up the app first.").toStdString()});
    }
    if (m_Saving)
    {
        return fail(ConfigEditError{Code::QUEUE_FULL,
                                    tr("The last change is still being saved. Try again in a moment.").toStdString()});
    }
    Configuration candidate = view();
    const EditResult edited = edit(candidate);
    if (!edited)
    {
        return fail(refused(edited.error()));
    }
    if (!*edited)
    {
        return {};
    }
    return commit(std::move(candidate), operation);
}

QString SettingsModel::change_message(const QString& operation, const Edit& edit)
{
    return message_of(change(operation, edit, false));
}

std::expected<void, ConfigEditError> SettingsModel::commit(Configuration candidate, const QString& operation)
{
    if (!is_complete(candidate))
    {
        if (m_Configured)
        {
            return fail(refused(tr("The facilitator and the unit prefix cannot be empty.")));
        }
        // A set-up in progress: kept in memory until it has what the importer needs.
        m_Setup = std::move(candidate);
        publish(false);
        return {};
    }

    std::string text = export_configuration(candidate);
    // The checks of the edits mirror the core's rules; this is the guarantee that what is saved can be read back.
    if (const auto readable = import_configuration(text); !readable)
    {
        return fail(refused(
            tr("That change cannot be saved: %1").arg(QString::fromStdString(readable.error().front().m_Message))));
    }

    set_saving(true);
    m_Gateway.run([text = std::move(text)](StorageContext& context) { return context.m_Config.save(text); },
                  [self = QPointer<SettingsModel>(this), saved = std::move(candidate),
                   operation](Result<void, StorageError> result) mutable
                  {
                      if (SettingsModel* model = self.data(); model != nullptr)
                      {
                          model->finish_save(std::move(saved), operation, result);
                      }
                  });
    return {};
}

void SettingsModel::finish_save(Configuration saved, const QString& operation, const Result<void, StorageError>& result)
{
    set_saving(false);
    const auto added = std::exchange(m_PendingAdded, std::nullopt);
    if (!result)
    {
        emit writeFailed(operation, plain_message(result.error()));
        return;
    }
    m_Configuration = std::move(saved);
    m_Configured = true;
    m_Setup = Configuration{};
    if (operation == OP_IMPORT)
    {
        m_PreparedImport.reset();
    }
    publish(true);
    if (added)
    {
        emit errorTypeAdded(added->first, added->second);
    }
}

void SettingsModel::set_saving(bool saving)
{
    if (m_Saving != saving)
    {
        m_Saving = saving;
        emit savingChanged();
    }
}

void SettingsModel::publish(bool in_force_changed)
{
    const Configuration& shown = view();
    m_ErrorTypes->refresh(shown);
    m_Locations->refresh(shown);
    m_Templates->refresh(shown);
    m_BoardLayout->refresh(shown);
    if (in_force_changed)
    {
        m_Observers.notify([this](ConfigurationObserver& observer) { observer.on_configuration_changed(*this); });
    }
    emit configurationChanged();
    if (!(m_ShownTimeFormat == shown.m_TimeFormat))
    {
        m_ShownTimeFormat = shown.m_TimeFormat;
        emit timeFormatChanged();
    }
}

// -------------------------------------------------------------------------------------------------------- the lists

QString SettingsModel::setFacilitator(const QString& name)
{
    const auto text = checked_name(name);
    if (!text)
    {
        return text.error();
    }
    return change_message(OP_FACILITATOR,
                          [value = *text](Configuration& configuration) -> EditResult
                          {
                              const bool changed = configuration.m_Facilitator != value;
                              configuration.m_Facilitator = value;
                              return changed;
                          });
}

QString SettingsModel::setUnitPrefix(const QString& prefix)
{
    const auto text = checked_text(prefix, MAX_PREFIX, tr("Enter a prefix, for example UNIT."));
    if (!text)
    {
        return text.error();
    }
    if (text->contains('-') || text->contains('#'))
    {
        return tr("Use only the letters of the prefix, for example UNIT. The app adds the dash and the number itself.");
    }
    return change_message(OP_PREFIX,
                          [value = *text](Configuration& configuration) -> EditResult
                          {
                              const bool changed = configuration.m_UnitPrefix != value;
                              configuration.m_UnitPrefix = value;
                              return changed;
                          });
}

QString SettingsModel::setTimeFormat(const QString& style, bool seconds, const QString& meridiem)
{
    TimeFormat format;
    if (style == QLatin1StringView("24h"))
    {
        format.m_Style = TimeStyle::H24;
    }
    else if (style == QLatin1StringView("12h"))
    {
        format.m_Style = TimeStyle::H12;
    }
    else
    {
        return tr("Choose 24-hour or 12-hour time.");
    }
    if (meridiem == QLatin1StringView("upper"))
    {
        format.m_Meridiem = MeridiemCase::UPPER;
    }
    else if (meridiem == QLatin1StringView("lower"))
    {
        format.m_Meridiem = MeridiemCase::LOWER;
    }
    else
    {
        return tr("Choose AM and PM, or am and pm.");
    }
    format.m_Seconds = seconds;
    return change_message(OP_TIME_FORMAT,
                          [format](Configuration& configuration) -> EditResult
                          {
                              const bool changed = !(configuration.m_TimeFormat == format);
                              configuration.m_TimeFormat = format;
                              return changed;
                          });
}

QString SettingsModel::setIncludeTotals(bool include)
{
    return change_message(OP_TOTALS,
                          [include](Configuration& configuration) -> EditResult
                          {
                              const bool changed = configuration.m_IncludeTotals != include;
                              configuration.m_IncludeTotals = include;
                              return changed;
                          });
}

QString SettingsModel::setErrorPrefix(const QString& prefix)
{
    const std::string text = trim_error_prefix(prefix.toStdString());
    switch (error_prefix_problem(text))
    {
    case ErrorPrefixProblem::NONE:
        break;
    case ErrorPrefixProblem::TOO_LONG:
        return tr("Use at most %1 characters.").arg(export_format::MAX_ERROR_PREFIX_CHARACTERS);
    case ErrorPrefixProblem::CONTROL_CHARACTER:
        return tr("Write the prefix on one line, with no tab or line break.");
    }
    return change_message(OP_ERROR_PREFIX,
                          [value = text](Configuration& configuration) -> EditResult
                          {
                              const bool changed = configuration.m_ErrorPrefix != value;
                              configuration.m_ErrorPrefix = value;
                              return changed;
                          });
}

QString SettingsModel::resetErrorPrefix()
{
    return setErrorPrefix(QString::fromUtf8(export_format::DEFAULT_ERROR_PREFIX.data(),
                                            static_cast<qsizetype>(export_format::DEFAULT_ERROR_PREFIX.size())));
}

QString SettingsModel::addLocation(const QString& name)
{
    const auto text = checked_name(name);
    if (!text)
    {
        return text.error();
    }
    return change_message(OP_LOCATION,
                          [value = *text, id = m_Ids.next_as<LocationId>()](Configuration& configuration) -> EditResult
                          {
                              if (configuration.m_Locations.size() >= CONFIG_MAX_ENTRIES_PER_LIST)
                              {
                                  return std::unexpected(list_full());
                              }
                              if (name_in_use(configuration.m_Locations, value, std::optional<LocationId>{}))
                              {
                                  return std::unexpected(name_taken(value, NameKind::LOCATION));
                              }
                              Location location;
                              location.m_Id = id;
                              location.m_Name = value;
                              location.m_SortOrder = next_sort_order(configuration.m_Locations);
                              configuration.m_Locations.push_back(std::move(location));
                              return true;
                          });
}

QString SettingsModel::renameLocation(const QString& id, const QString& name)
{
    const auto text = checked_name(name);
    if (!text)
    {
        return text.error();
    }
    const auto location_id = parse_id<LocationId>(id);
    if (!location_id)
    {
        return no_such_item();
    }
    return change_message(OP_LOCATION,
                          [value = *text, target = *location_id](Configuration& configuration) -> EditResult
                          {
                              const auto found = std::ranges::find(configuration.m_Locations, target, &Location::m_Id);
                              if (found == configuration.m_Locations.end() || found->m_Retired)
                              {
                                  return std::unexpected(no_such_item());
                              }
                              if (name_in_use(configuration.m_Locations, value, std::optional<LocationId>(target)))
                              {
                                  return std::unexpected(name_taken(value, NameKind::LOCATION));
                              }
                              const bool changed = found->m_Name != value;
                              found->m_Name = value;
                              return changed;
                          });
}

QString SettingsModel::moveLocation(int from, int to)
{
    return change_message(OP_LOCATION,
                          [from, to](Configuration& configuration) -> EditResult
                          {
                              std::vector<std::size_t> order = location_order(configuration.m_Locations);
                              // Where the live locations stand in the full order, which keeps the retired ones where
                              // they are.
                              std::vector<std::size_t> live;
                              for (std::size_t place = 0; place < order.size(); ++place)
                              {
                                  if (!configuration.m_Locations[order[place]].m_Retired)
                                  {
                                      live.push_back(place);
                                  }
                              }
                              const auto count = static_cast<int>(live.size());
                              if (from < 0 || from >= count || to < 0 || to >= count)
                              {
                                  return std::unexpected(SettingsModel::tr("That place in the list does not exist."));
                              }
                              if (from == to)
                              {
                                  return false;
                              }
                              const std::size_t a = live[static_cast<std::size_t>(from)];
                              const std::size_t b = live[static_cast<std::size_t>(to)];
                              const auto at = [&order](std::size_t place)
                              { return order.begin() + static_cast<std::ptrdiff_t>(place); };
                              if (a < b)
                              {
                                  std::rotate(at(a), at(a + 1), at(b + 1));
                              }
                              else
                              {
                                  std::rotate(at(b), at(a), at(a + 1));
                              }
                              std::vector<Location> moved;
                              moved.reserve(order.size());
                              int sort_order = 1;
                              for (const std::size_t index : order)
                              {
                                  moved.push_back(configuration.m_Locations[index]);
                                  moved.back().m_SortOrder = sort_order++;
                              }
                              configuration.m_Locations = std::move(moved);
                              return true;
                          });
}

QString SettingsModel::removeLocation(const QString& id)
{
    const auto location_id = parse_id<LocationId>(id);
    if (!location_id)
    {
        return no_such_item();
    }
    return change_message(OP_LOCATION,
                          [target = *location_id](Configuration& configuration) -> EditResult
                          {
                              const auto found = std::ranges::find(configuration.m_Locations, target, &Location::m_Id);
                              if (found == configuration.m_Locations.end() || found->m_Retired)
                              {
                                  return std::unexpected(no_such_item());
                              }
                              found->m_Retired = true;
                              return true;
                          });
}

QString SettingsModel::addUnitEnvironment(const QString& name)
{
    const auto text = checked_environment(name);
    if (!text)
    {
        return text.error();
    }
    return change_message(OP_ENVIRONMENT,
                          [value = *text](Configuration& configuration) -> EditResult
                          {
                              auto& list = configuration.m_UnitEnvironments;
                              if (list.size() >= CONFIG_MAX_ENTRIES_PER_LIST)
                              {
                                  return std::unexpected(list_full());
                              }
                              if (std::ranges::find(list, value) != list.end())
                              {
                                  return std::unexpected(environment_taken(value));
                              }
                              list.push_back(value);
                              return true;
                          });
}

QString SettingsModel::renameUnitEnvironment(const QString& from, const QString& to)
{
    const auto text = checked_environment(to);
    if (!text)
    {
        return text.error();
    }
    return change_message(OP_ENVIRONMENT,
                          [old_name = from.toStdString(), value = *text](Configuration& configuration) -> EditResult
                          {
                              auto& list = configuration.m_UnitEnvironments;
                              const auto found = std::ranges::find(list, old_name);
                              if (found == list.end())
                              {
                                  return std::unexpected(no_such_item());
                              }
                              if (value != old_name && std::ranges::find(list, value) != list.end())
                              {
                                  return std::unexpected(environment_taken(value));
                              }
                              const bool changed = *found != value;
                              *found = value;
                              return changed;
                          });
}

QString SettingsModel::moveUnitEnvironment(int from, int to)
{
    return change_message(OP_ENVIRONMENT,
                          [from, to](Configuration& configuration) -> EditResult
                          {
                              auto& list = configuration.m_UnitEnvironments;
                              const auto count = static_cast<int>(list.size());
                              if (from < 0 || from >= count || to < 0 || to >= count)
                              {
                                  return std::unexpected(SettingsModel::tr("That place in the list does not exist."));
                              }
                              if (from == to)
                              {
                                  return false;
                              }
                              const auto at = [&list](int place) { return list.begin() + place; };
                              if (from < to)
                              {
                                  std::rotate(at(from), at(from + 1), at(to + 1));
                              }
                              else
                              {
                                  std::rotate(at(to), at(from), at(from + 1));
                              }
                              return true;
                          });
}

QString SettingsModel::removeUnitEnvironment(const QString& name)
{
    return change_message(OP_ENVIRONMENT,
                          [target = name.toStdString()](Configuration& configuration) -> EditResult
                          {
                              auto& list = configuration.m_UnitEnvironments;
                              const auto found = std::ranges::find(list, target);
                              if (found == list.end())
                              {
                                  return std::unexpected(no_such_item());
                              }
                              if (list.size() == 1)
                              {
                                  return std::unexpected(SettingsModel::tr("Keep at least one environment."));
                              }
                              list.erase(found);
                              return true;
                          });
}

// ----------------------------------------------------------------------------------------------------- error types

std::expected<SettingsModel::Added, ConfigEditError> SettingsModel::add_type(const QString& name, bool needs_configured,
                                                                             Placement placement, int position)
{
    const auto text = checked_name(name);
    if (!text)
    {
        return fail(refused(text.error()));
    }
    if (placement == Placement::AT_POSITION && !valid_position(position))
    {
        return fail(refused(tr("That position is not on the board.")));
    }
    const auto id = m_Ids.next_as<ErrorTypeId>();
    // Where the edit put the type, read after `change` has run it (it runs before `change` returns).
    int placed = 0;
    const auto result = change(
        OP_ERROR_TYPE,
        [value = *text, id, placement, position, &placed](Configuration& configuration) -> EditResult
        {
            if (configuration.m_ErrorTypes.size() >= CONFIG_MAX_ENTRIES_PER_LIST)
            {
                return std::unexpected(list_full());
            }
            if (name_in_use(configuration.m_ErrorTypes, value, std::optional<ErrorTypeId>{}))
            {
                return std::unexpected(name_taken(value, NameKind::ERROR_TYPE));
            }
            BoardLayout layout = viewmodel::board_layout(configuration);
            ErrorType type;
            type.m_Id = id;
            type.m_Name = value;
            configuration.m_ErrorTypes.push_back(std::move(type));
            int target = 0;
            if (placement == Placement::AT_POSITION)
            {
                target = position;
            }
            else if (placement == Placement::FIRST_EMPTY)
            {
                const auto empty = std::ranges::find(layout, std::optional<std::size_t>{});
                target = empty == layout.end() ? 0 : static_cast<int>(empty - layout.begin()) + 1;
            }
            if (target > 0)
            {
                // The type that held the position, if any, is left out of the layout and goes off the board. One
                // change: the type and its place are saved together, or neither is.
                layout[static_cast<std::size_t>(target - 1)] = configuration.m_ErrorTypes.size() - 1;
                write_layout(configuration, layout);
            }
            placed = target;
            return true;
        },
        needs_configured);
    if (!result)
    {
        return fail(result.error());
    }
    return Added{id, placed};
}

QString SettingsModel::add_and_announce(const QString& name, Placement placement, int position)
{
    const auto added = add_type(name, false, placement, position);
    if (!added)
    {
        return QString::fromStdString(added.error().m_Message);
    }
    const auto found = std::ranges::find(view().m_ErrorTypes, added->m_Id, &ErrorType::m_Id);
    const QString shown = found != view().m_ErrorTypes.end() ? QString::fromStdString(found->m_Name) : name.trimmed();
    if (m_Saving)
    {
        // In force when the save has finished; `finish_save` tells.
        m_PendingAdded = std::pair{shown, added->m_Position};
    }
    else
    {
        // The set-up in progress, which is in force at once.
        emit errorTypeAdded(shown, added->m_Position);
    }
    return {};
}

Result<ErrorTypeId, ConfigEditError> SettingsModel::add_error_type(std::string name)
{
    const auto added = add_type(QString::fromStdString(name), true, Placement::OFF_BOARD);
    if (!added)
    {
        return fail(added.error());
    }
    return added->m_Id;
}

QString SettingsModel::addErrorType(const QString& name)
{
    return add_and_announce(name, Placement::FIRST_EMPTY, 0);
}

QString SettingsModel::addErrorTypeAt(const QString& name, int position)
{
    return add_and_announce(name, Placement::AT_POSITION, position);
}

QString SettingsModel::renameErrorType(const QString& id, const QString& name)
{
    const auto text = checked_name(name);
    if (!text)
    {
        return text.error();
    }
    const auto type_id = parse_id<ErrorTypeId>(id);
    if (!type_id)
    {
        return no_such_item();
    }
    return change_message(OP_ERROR_TYPE,
                          [value = *text, target = *type_id](Configuration& configuration) -> EditResult
                          {
                              const auto found =
                                  std::ranges::find(configuration.m_ErrorTypes, target, &ErrorType::m_Id);
                              if (found == configuration.m_ErrorTypes.end() || found->m_Retired)
                              {
                                  return std::unexpected(no_such_item());
                              }
                              if (name_in_use(configuration.m_ErrorTypes, value, std::optional<ErrorTypeId>(target)))
                              {
                                  return std::unexpected(name_taken(value, NameKind::ERROR_TYPE));
                              }
                              const bool changed = found->m_Name != value;
                              found->m_Name = value;
                              return changed;
                          });
}

QString SettingsModel::retireErrorType(const QString& id)
{
    const auto type_id = parse_id<ErrorTypeId>(id);
    if (!type_id)
    {
        return no_such_item();
    }
    return change_message(OP_ERROR_TYPE,
                          [target = *type_id](Configuration& configuration) -> EditResult
                          {
                              const auto found =
                                  std::ranges::find(configuration.m_ErrorTypes, target, &ErrorType::m_Id);
                              if (found == configuration.m_ErrorTypes.end() || found->m_Retired)
                              {
                                  return std::unexpected(no_such_item());
                              }
                              // The position value stays, as in the file format; the layout leaves a retired type out.
                              found->m_Retired = true;
                              return true;
                          });
}

// ------------------------------------------------------------------------------------------------------ the board

QString SettingsModel::moveBoardTile(int fromPosition, int toPosition)
{
    if (!valid_position(fromPosition) || !valid_position(toPosition))
    {
        return tr("That position is not on the board.");
    }
    return change_message(OP_BOARD,
                          [fromPosition, toPosition](Configuration& configuration) -> EditResult
                          {
                              BoardLayout layout = viewmodel::board_layout(configuration);
                              const auto from = static_cast<std::ptrdiff_t>(fromPosition - 1);
                              const auto to = static_cast<std::ptrdiff_t>(toPosition - 1);
                              if (!layout[static_cast<std::size_t>(from)])
                              {
                                  return std::unexpected(SettingsModel::tr("There is no tile there to move."));
                              }
                              if (from == to)
                              {
                                  return false;
                              }
                              // A move of the tile within the row of positions, the others shifting by one; empty
                              // positions move too.
                              if (from < to)
                              {
                                  std::rotate(layout.begin() + from, layout.begin() + from + 1,
                                              layout.begin() + to + 1);
                              }
                              else
                              {
                                  std::rotate(layout.begin() + to, layout.begin() + from, layout.begin() + from + 1);
                              }
                              write_layout(configuration, layout);
                              return true;
                          });
}

QString SettingsModel::assignBoardTile(int position, const QString& errorTypeId)
{
    if (!valid_position(position))
    {
        return tr("That position is not on the board.");
    }
    const auto type_id = parse_id<ErrorTypeId>(errorTypeId);
    if (!type_id)
    {
        return no_such_item();
    }
    return change_message(OP_BOARD,
                          [position, target = *type_id](Configuration& configuration) -> EditResult
                          {
                              const auto found =
                                  std::ranges::find(configuration.m_ErrorTypes, target, &ErrorType::m_Id);
                              if (found == configuration.m_ErrorTypes.end() || found->m_Retired)
                              {
                                  return std::unexpected(no_such_item());
                              }
                              const auto index = static_cast<std::size_t>(found - configuration.m_ErrorTypes.begin());
                              BoardLayout layout = viewmodel::board_layout(configuration);
                              auto& tile = layout[static_cast<std::size_t>(position - 1)];
                              if (tile == index)
                              {
                                  return false;
                              }
                              const auto current = std::ranges::find(layout, std::optional<std::size_t>(index));
                              if (current != layout.end())
                              {
                                  // On the board elsewhere: the two swap places (the tile's own type may be none).
                                  *current = tile;
                              }
                              // Otherwise the tile's type is dropped from the layout, which sends it off the board.
                              tile = index;
                              write_layout(configuration, layout);
                              return true;
                          });
}

// ------------------------------------------------------------------------------------------------------ templates

QString SettingsModel::validatePattern(const QString& pattern) const
{
    const auto checked = checked_pattern(pattern);
    return checked ? QString() : checked.error();
}

QString SettingsModel::previewTemplateLine(const QString& pattern, const QString& value, int localSeconds) const
{
    if (localSeconds >= 24 * 60 * 60)
    {
        return {};
    }
    const auto checked = checked_pattern(pattern);
    if (!checked)
    {
        return tr("Fix the line to see an example.");
    }

    // The instant: now, or today's date at the seconds the caller names, in the zone the device is in.
    Timestamp when{m_Clock.now(), m_Zone.current()};
    if (localSeconds >= 0)
    {
        auto local = m_Zones.to_local(when);
        if (!local)
        {
            return {};
        }
        local->m_SinceMidnight = std::chrono::seconds{localSeconds};
        const auto utc = m_Zones.to_utc(*local, when.m_Zone);
        if (!utc)
        {
            return {};
        }
        when.m_Utc = *utc;
    }

    // One note on a scratch log, rendered by the code that renders every line of the review screen.
    NoteTemplate scratch_template;
    scratch_template.m_Id = NoteTemplateId(Uuid::from_random(~std::uint64_t{0}, ~std::uint64_t{0}));
    scratch_template.m_OutputPattern = *checked;
    Entry entry;
    entry.m_Id = EntryId(Uuid::from_random(~std::uint64_t{0}, ~std::uint64_t{0}));
    entry.m_Captured = when;
    const QString shown = value.trimmed().isEmpty() ? tr("Morning Medley") : value;
    entry.m_Body = NoteEntry{scratch_template.m_Id, shown.toStdString()};
    Log scratch{HeaderValues{}};
    if (!scratch.add(entry))
    {
        return {};
    }

    ExportContext context;
    context.m_TimeFormat = view().m_TimeFormat;
    context.m_Zones = &m_Zones;
    context.m_Template = [&scratch_template](NoteTemplateId id) -> const NoteTemplate*
    { return id == scratch_template.m_Id ? &scratch_template : nullptr; };
    const auto line = render_entry_line(scratch, entry.m_Id, context);
    // As the Log screen shows the note: without the marker the export keeps.
    return line ? QString::fromStdString(export_format::without_note_marker(*line)) : QString();
}

QString SettingsModel::patternToField(const QString& pattern) const
{
    const QString slot = QStringLiteral("{value}");
    QString shown;
    shown.reserve(pattern.size());
    qsizetype at = 0;
    while (at < pattern.size())
    {
        // "{{" and "}}" are literal braces: copy both, so "{{value}}" stays the text {value} and not a slot.
        if (pattern.mid(at, 2) == QStringLiteral("{{") || pattern.mid(at, 2) == QStringLiteral("}}"))
        {
            shown += pattern.mid(at, 2);
            at += 2;
        }
        else if (pattern.mid(at, slot.size()) == slot)
        {
            shown += ANSWER_TOKEN;
            at += slot.size();
        }
        else
        {
            shown += pattern.at(at);
            ++at;
        }
    }
    return shown;
}

QString SettingsModel::patternFromField(const QString& field) const
{
    QString pattern = field;
    pattern.replace(ANSWER_TOKEN, QStringLiteral("{value}"));
    return pattern;
}

QString SettingsModel::defaultTemplateQuestion(const QString& name) const
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QString() : trimmed + QLatin1Char('?');
}

QString SettingsModel::defaultTemplateLine(const QString& name) const
{
    QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }
    // A brace in the name is doubled, so that it is a literal brace and not the start of a slot.
    trimmed.replace(QLatin1Char('{'), QStringLiteral("{{")).replace(QLatin1Char('}'), QStringLiteral("}}"));
    return trimmed + QStringLiteral(": ") + ANSWER_TOKEN;
}

QString SettingsModel::addTemplate(const QString& name, const QString& prompt, const QString& pattern)
{
    const auto template_name = checked_name(name);
    if (!template_name)
    {
        return template_name.error();
    }
    const auto template_prompt =
        checked_text(prompt, MAX_NAME, tr("Enter the question to ask, for example \"Which song?\"."));
    if (!template_prompt)
    {
        return template_prompt.error();
    }
    const auto template_pattern = checked_pattern(pattern);
    if (!template_pattern)
    {
        return template_pattern.error();
    }
    return change_message(OP_TEMPLATE,
                          [name = *template_name, prompt = *template_prompt, pattern = *template_pattern,
                           id = m_Ids.next_as<NoteTemplateId>()](Configuration& configuration) -> EditResult
                          {
                              if (configuration.m_Templates.size() >= CONFIG_MAX_ENTRIES_PER_LIST)
                              {
                                  return std::unexpected(list_full());
                              }
                              if (name_in_use(configuration.m_Templates, name, std::optional<NoteTemplateId>{}))
                              {
                                  return std::unexpected(name_taken(name, NameKind::NOTE_TEMPLATE));
                              }
                              NoteTemplate note_template;
                              note_template.m_Id = id;
                              note_template.m_Name = name;
                              note_template.m_ValuePrompt = prompt;
                              note_template.m_OutputPattern = pattern;
                              note_template.m_SortOrder = next_sort_order(configuration.m_Templates);
                              configuration.m_Templates.push_back(std::move(note_template));
                              return true;
                          });
}

QString SettingsModel::renameTemplate(const QString& id, const QString& name)
{
    const auto template_name = checked_name(name);
    if (!template_name)
    {
        return template_name.error();
    }
    const auto template_id = parse_id<NoteTemplateId>(id);
    if (!template_id)
    {
        return no_such_item();
    }
    return change_message(OP_TEMPLATE,
                          [value = *template_name, target = *template_id](Configuration& configuration) -> EditResult
                          {
                              const auto found =
                                  std::ranges::find(configuration.m_Templates, target, &NoteTemplate::m_Id);
                              if (found == configuration.m_Templates.end())
                              {
                                  return std::unexpected(no_such_item());
                              }
                              if (name_in_use(configuration.m_Templates, value, std::optional<NoteTemplateId>(target)))
                              {
                                  return std::unexpected(name_taken(value, NameKind::NOTE_TEMPLATE));
                              }
                              const bool changed = found->m_Name != value;
                              found->m_Name = value;
                              return changed;
                          });
}

QString SettingsModel::updateTemplate(const QString& id, const QString& name, const QString& prompt,
                                      const QString& pattern)
{
    const auto template_name = checked_name(name);
    if (!template_name)
    {
        return template_name.error();
    }
    const auto template_prompt =
        checked_text(prompt, MAX_NAME, tr("Enter the question to ask, for example \"Which song?\"."));
    if (!template_prompt)
    {
        return template_prompt.error();
    }
    const auto template_pattern = checked_pattern(pattern);
    if (!template_pattern)
    {
        return template_pattern.error();
    }
    const auto template_id = parse_id<NoteTemplateId>(id);
    if (!template_id)
    {
        return no_such_item();
    }
    return change_message(OP_TEMPLATE,
                          [name = *template_name, prompt = *template_prompt, pattern = *template_pattern,
                           target = *template_id](Configuration& configuration) -> EditResult
                          {
                              const auto found =
                                  std::ranges::find(configuration.m_Templates, target, &NoteTemplate::m_Id);
                              if (found == configuration.m_Templates.end())
                              {
                                  return std::unexpected(no_such_item());
                              }
                              if (name_in_use(configuration.m_Templates, name, std::optional<NoteTemplateId>(target)))
                              {
                                  return std::unexpected(name_taken(name, NameKind::NOTE_TEMPLATE));
                              }
                              const bool changed = found->m_Name != name || found->m_ValuePrompt != prompt ||
                                                   found->m_OutputPattern != pattern;
                              found->m_Name = name;
                              found->m_ValuePrompt = prompt;
                              found->m_OutputPattern = pattern;
                              return changed;
                          });
}

// ------------------------------------------------------------------------------------------------ import and export

void SettingsModel::loadImport(const QString& path)
{
    m_PreparedImport.reset();
    const std::uint64_t token = ++m_ImportToken;
    m_Gateway.run(
        [file = path.toStdString()](StorageContext& context) -> ImportOutcome
        {
            ImportOutcome outcome;
            const auto text = context.m_Config.read_file(file, CONFIG_MAX_BYTES);
            if (!text)
            {
                outcome.m_ReadError = text.error();
                return outcome;
            }
            auto imported = import_configuration(*text);
            if (imported)
            {
                outcome.m_Configuration = std::move(*imported);
            }
            else
            {
                outcome.m_Errors = std::move(imported.error());
            }
            return outcome;
        },
        [self = QPointer<SettingsModel>(this), token](ImportOutcome outcome)
        {
            // A later `loadImport` or a `discardImport` has overtaken this one.
            SettingsModel* model = self.data();
            if (model != nullptr && model->m_ImportToken == token)
            {
                model->finish_import(std::move(outcome));
            }
        });
}

void SettingsModel::finish_import(ImportOutcome outcome)
{
    if (outcome.m_Configuration)
    {
        const Configuration& imported = *outcome.m_Configuration;
        const int error_types = count_live(imported.m_ErrorTypes);
        const int locations = count_live(imported.m_Locations);
        const int templates = static_cast<int>(imported.m_Templates.size());
        m_PreparedImport = std::move(outcome.m_Configuration);
        emit importPrepared(error_types, locations, templates);
        return;
    }
    QStringList messages;
    if (outcome.m_ReadError)
    {
        messages.push_back(plain_message(*outcome.m_ReadError));
    }
    for (const ConfigError& error : outcome.m_Errors)
    {
        messages.push_back(plain_import_message(error));
    }
    emit importFailed(messages);
}

void SettingsModel::applyImport()
{
    if (!m_PreparedImport)
    {
        emit writeFailed(OP_IMPORT, tr("There is no file ready to import."));
        return;
    }
    if (m_Loading || m_Saving)
    {
        emit writeFailed(OP_IMPORT, m_Loading ? tr("The settings are still loading.")
                                              : tr("The last change is still being saved. Try again in a moment."));
        return;
    }
    if (const auto result = commit(*m_PreparedImport, OP_IMPORT); !result)
    {
        emit writeFailed(OP_IMPORT, QString::fromStdString(result.error().m_Message));
    }
}

void SettingsModel::discardImport()
{
    m_PreparedImport.reset();
    ++m_ImportToken;
}

void SettingsModel::exportTo(const QString& path)
{
    if (!m_Configured)
    {
        const QString message = tr("There is nothing to export yet. Set up the app first.");
        emit exportResult(false, tr("Export failed"), QString(), message);
        emit exportFinished(false, message);
        return;
    }
    const QString file_name = QFileInfo(path).fileName();
    m_Gateway.run([file = path.toStdString(), text = export_configuration(m_Configuration)](StorageContext& context)
                  { return context.m_Config.write_file(file, text); },
                  [self = QPointer<SettingsModel>(this), file_name](Result<void, StorageError> result)
                  {
                      SettingsModel* model = self.data();
                      if (model == nullptr)
                      {
                          return;
                      }
                      if (result)
                      {
                          const QString message = tr("%1 is ready to send.").arg(file_name);
                          emit model->exportResult(true, tr("Exported to Files"), file_name, message);
                          emit model->exportFinished(true, message);
                      }
                      else
                      {
                          const QString message = plain_message(result.error());
                          emit model->exportResult(false, tr("Export failed"), file_name, message);
                          emit model->exportFinished(false, message);
                      }
                  });
}

} // namespace ej::viewmodel
