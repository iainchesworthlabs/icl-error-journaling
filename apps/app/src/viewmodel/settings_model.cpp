// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/settings_model.hpp"

#include "error_journaling/note_template.hpp"
#include "error_journaling/time_format.hpp"
#include "error_journaling/uuid.hpp"
#include "viewmodel/board_layout_model.hpp"
#include "viewmodel/error_type_list_model.hpp"
#include "viewmodel/location_list_model.hpp"
#include "viewmodel/template_list_model.hpp"

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
constexpr int MAX_SORT_ORDER = 1'000'000;

const QString OP_FACILITATOR = QStringLiteral("facilitator");
const QString OP_PREFIX = QStringLiteral("unit prefix");
const QString OP_TIME_FORMAT = QStringLiteral("time format");
const QString OP_TOTALS = QStringLiteral("totals");
const QString OP_LOCATION = QStringLiteral("location");
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

QString name_taken(const std::string& name)
{
    return SettingsModel::tr("There is already one called \"%1\".").arg(QString::fromStdString(name));
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
    switch (error)
    {
    case PatternError::MISSING_PLACEHOLDER:
        return SettingsModel::tr("Put {value} where the typed text should go, for example \"Played {value}\".");
    case PatternError::UNBALANCED_BRACE:
        return SettingsModel::tr("The pattern has a stray brace. Write {{ or }} for a literal brace, and close every "
                                 "{value}.");
    case PatternError::UNKNOWN_PLACEHOLDER:
        return SettingsModel::tr("Only {value} can go between braces. Other words, such as {name}, are not known.");
    }
    return SettingsModel::tr("The pattern cannot be used.");
}

std::expected<std::string, QString> checked_pattern(const QString& raw)
{
    if (raw.trimmed().isEmpty())
    {
        return std::unexpected(pattern_message(PatternError::MISSING_PLACEHOLDER));
    }
    if (raw.size() > MAX_PATTERN)
    {
        return std::unexpected(SettingsModel::tr("Use %1 characters or fewer.").arg(MAX_PATTERN));
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

/// The board as an array of master-list indexes, by position 1 to 11 (index 0 to 10).
using Layout = std::array<std::optional<std::size_t>, CONFIG_BOARD_POSITIONS>;

Layout layout_of(const Configuration& configuration)
{
    Layout layout;
    for (std::size_t i = 0; i < configuration.m_ErrorTypes.size(); ++i)
    {
        const ErrorType& type = configuration.m_ErrorTypes[i];
        if (type.m_Retired || !type.m_BoardPosition || *type.m_BoardPosition < 1 ||
            *type.m_BoardPosition > CONFIG_BOARD_POSITIONS)
        {
            continue;
        }
        auto& slot = layout[static_cast<std::size_t>(*type.m_BoardPosition - 1)];
        if (!slot)
        {
            slot = i;
        }
    }
    return layout;
}

/// Writes `layout` back: the types in it get their position, the other live types are off the board. A retired type
/// keeps whatever value it had.
void write_layout(Configuration& configuration, const Layout& layout)
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
    : QObject(parent), m_Gateway(gateway), m_Ids(environment.m_Ids), m_Zones(environment.m_Zones),
      m_ShownTimeFormat(Configuration{}.m_TimeFormat), m_ErrorTypes(new ErrorTypeListModel(this)),
      m_Locations(new LocationListModel(this)), m_Templates(new TemplateListModel(this)),
      m_BoardLayout(new BoardLayoutModel(this))
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
                      if (self)
                      {
                          self->finish_load(std::move(result));
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
                      if (self)
                      {
                          self->finish_save(std::move(saved), operation, result);
                      }
                  });
    return {};
}

void SettingsModel::finish_save(Configuration saved, const QString& operation, const Result<void, StorageError>& result)
{
    set_saving(false);
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
                                  return std::unexpected(name_taken(value));
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
                                  return std::unexpected(name_taken(value));
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

// ----------------------------------------------------------------------------------------------------- error types

std::expected<ErrorTypeId, ConfigEditError> SettingsModel::add_type(const QString& name, bool needs_configured)
{
    const auto text = checked_name(name);
    if (!text)
    {
        return fail(refused(text.error()));
    }
    const auto id = m_Ids.next_as<ErrorTypeId>();
    const auto result = change(
        OP_ERROR_TYPE,
        [value = *text, id](Configuration& configuration) -> EditResult
        {
            if (configuration.m_ErrorTypes.size() >= CONFIG_MAX_ENTRIES_PER_LIST)
            {
                return std::unexpected(list_full());
            }
            if (name_in_use(configuration.m_ErrorTypes, value, std::optional<ErrorTypeId>{}))
            {
                return std::unexpected(name_taken(value));
            }
            ErrorType type;
            type.m_Id = id;
            type.m_Name = value;
            configuration.m_ErrorTypes.push_back(std::move(type));
            return true;
        },
        needs_configured);
    if (!result)
    {
        return fail(result.error());
    }
    return id;
}

Result<ErrorTypeId, ConfigEditError> SettingsModel::add_error_type(std::string name)
{
    return add_type(QString::fromStdString(name), true);
}

QString SettingsModel::addErrorType(const QString& name)
{
    const auto added = add_type(name, false);
    return added ? QString() : QString::fromStdString(added.error().m_Message);
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
                                  return std::unexpected(name_taken(value));
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
                              Layout layout = layout_of(configuration);
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
                              Layout layout = layout_of(configuration);
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
                                  return std::unexpected(name_taken(name));
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
                                  return std::unexpected(name_taken(value));
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
                                  return std::unexpected(name_taken(name));
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
            if (self && self->m_ImportToken == token)
            {
                self->finish_import(std::move(outcome));
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
        messages.push_back(QString::fromStdString(error.m_Message));
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
        emit exportFinished(false, tr("There is nothing to export yet. Set up the app first."));
        return;
    }
    m_Gateway.run([file = path.toStdString(), text = export_configuration(m_Configuration)](StorageContext& context)
                  { return context.m_Config.write_file(file, text); },
                  [self = QPointer<SettingsModel>(this)](Result<void, StorageError> result)
                  {
                      if (!self)
                      {
                          return;
                      }
                      if (result)
                      {
                          emit self->exportFinished(true, tr("The settings were exported."));
                      }
                      else
                      {
                          emit self->exportFinished(false, plain_message(result.error()));
                      }
                  });
}

} // namespace ej::viewmodel
