// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/configuration.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/time_format.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/observer_list.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace ej::viewmodel
{

class BoardLayoutModel;
class ErrorTypeListModel;
class LocationListModel;
class TemplateListModel;

/// The preferences and the three master lists (PRF-01 to PRF-10), and their import and export. It holds the
/// `Configuration` of Stage 1 as its mirror, saves it through the `ConfigStore` on the storage thread, and is the
/// `ConfigurationSource` the other models read.
///
/// **Saving.** Every change makes a changed copy of the configuration, checks it against the core's rules, and queues
/// one save of the whole file (the JSON the person imports and exports, replaced as a unit). The mirror, the list
/// models and the observers change only when the save has succeeded, so nothing the screen shows is missing from the
/// file. A failed save changes nothing and emits `writeFailed`. One save is in flight at a time: a change that arrives
/// while the last is being saved is refused with a message, so no change is built on one that may still fail.
///
/// **First launch.** Until a valid configuration is in force, `configured` is false and the invokables work on a set-up
/// in progress that is held in memory and shown by the properties and the list models, but is not saved and is not what
/// `configuration()` returns. It is saved, and `configured` becomes true, by the change that gives it a facilitator and
/// a unit prefix (a configuration without those is not one the importer accepts), or by an import.
///
/// **Effects.** A new facilitator applies to logs created after it; the open log keeps the one it started with (the log
/// holds its own header). A new unit prefix or time format applies to every log, because neither is stored in a log.
/// Retiring an error type or a location removes it from the pickers and keeps it on past entries.
///
/// The invokables that change something return "" when the change was accepted, or the plain message of the refusal for
/// the screen to show inline. GUI thread only.
class SettingsModel : public QObject, public ConfigurationSource, public ConfigurationEditor
{
    Q_OBJECT
    // True from construction until `load()` has heard from the store, so the first frame does not offer setup to a
    // person who has a saved configuration.
    Q_PROPERTY(bool loading READ loading NOTIFY configurationChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configurationChanged) // PRF-09: first launch shows setup
    // Empty, or why the saved file was not used at launch ("update the app", a damaged file). The file is kept.
    Q_PROPERTY(QString loadProblem READ load_problem NOTIFY configurationChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(QString facilitator READ facilitator NOTIFY configurationChanged) // PRF-01
    Q_PROPERTY(QString unitPrefix READ unit_prefix NOTIFY configurationChanged)  // PRF-02
    // The choices of the environment picker on Today's details, in order, without "None" (PRF-11). Never empty.
    Q_PROPERTY(QStringList unitEnvironments READ unit_environments NOTIFY configurationChanged)
    Q_PROPERTY(QString timeStyle READ time_style NOTIFY timeFormatChanged)       // PRF-07: "24h" or "12h"
    Q_PROPERTY(bool timeSeconds READ time_seconds NOTIFY timeFormatChanged)
    Q_PROPERTY(QString timeMeridiem READ time_meridiem NOTIFY timeFormatChanged) // "upper" or "lower"
    // One value that changes whenever the time format does ("12h-seconds-lower"). A QML binding that calls `formatTime`
    // names it, so the binding is evaluated again when the format changes: `text: (settings.timeFormatKey,
    // settings.formatTime(ms, zone))`.
    Q_PROPERTY(QString timeFormatKey READ time_format_key NOTIFY timeFormatChanged)
    Q_PROPERTY(bool includeTotals READ include_totals NOTIFY configurationChanged) // PRF-10
    Q_PROPERTY(QString errorPrefix READ error_prefix NOTIFY configurationChanged)  // PRF-13
    Q_PROPERTY(QAbstractItemModel* errorTypes READ error_types CONSTANT)           // PRF-04
    Q_PROPERTY(QAbstractItemModel* locations READ locations CONSTANT)              // PRF-03
    Q_PROPERTY(QAbstractItemModel* templates READ templates CONSTANT)              // PRF-06
    Q_PROPERTY(QAbstractItemModel* boardLayout READ board_layout CONSTANT)         // PRF-05
    // The line under the unit prefix field: "Added in front of the number you type each day." (FirstLaunchManual.)
    Q_PROPERTY(QString unitPrefixHint READ unit_prefix_hint CONSTANT)
    // The first board position (1 to 11) with no type on it, or 0 when the board is full: where `addErrorType` puts a
    // new type.
    Q_PROPERTY(int firstEmptyPosition READ first_empty_position NOTIFY configurationChanged)
    // The colour slot (1 to 11) the next new error type gets: its colour follows from its place in the master list.
    Q_PROPERTY(int nextColourSlot READ next_colour_slot NOTIFY configurationChanged)

public:
    /// `gateway` and what `environment` refers to (the ids and the zones) must outlive the model. Nothing is read until
    /// `load()`.
    SettingsModel(StorageGateway& gateway, const Environment& environment, QObject* parent = nullptr);
    ~SettingsModel() override;

    /// Reads the saved configuration through the gateway. Once; later calls do nothing. The properties tell the screen
    /// when it has finished (`loading`), and the observers are told if it found a valid configuration.
    void load();

    [[nodiscard]] bool loading() const
    {
        return m_Loading;
    }
    [[nodiscard]] bool saving() const
    {
        return m_Saving;
    }
    [[nodiscard]] QString load_problem() const
    {
        return m_LoadProblem;
    }
    [[nodiscard]] QString facilitator() const;
    [[nodiscard]] QString unit_prefix() const;
    [[nodiscard]] QStringList unit_environments() const;
    [[nodiscard]] QString time_style() const;
    [[nodiscard]] bool time_seconds() const;
    [[nodiscard]] QString time_meridiem() const;
    [[nodiscard]] QString time_format_key() const;
    [[nodiscard]] bool include_totals() const;
    [[nodiscard]] QString error_prefix() const;
    [[nodiscard]] QAbstractItemModel* error_types() const;
    [[nodiscard]] QAbstractItemModel* locations() const;
    [[nodiscard]] QAbstractItemModel* templates() const;
    [[nodiscard]] QAbstractItemModel* board_layout() const;
    [[nodiscard]] QString unit_prefix_hint() const;
    [[nodiscard]] int first_empty_position() const;
    [[nodiscard]] int next_colour_slot() const;

    // ConfigurationSource
    [[nodiscard]] bool configured() const override
    {
        return m_Configured;
    }
    [[nodiscard]] const Configuration& configuration() const override
    {
        return m_Configuration;
    }
    void add_observer(ConfigurationObserver& observer) override
    {
        m_Observers.add(observer);
    }
    void remove_observer(ConfigurationObserver& observer) override
    {
        m_Observers.remove(observer);
    }

    // ConfigurationEditor: the Other sheet adds a type to the master list. Needs a configuration in force.
    [[nodiscard]] Result<ErrorTypeId, ConfigEditError> add_error_type(std::string name) override;

    Q_INVOKABLE QString setFacilitator(const QString& name);
    Q_INVOKABLE QString setUnitPrefix(const QString& prefix);
    /// `style` is "24h" or "12h"; `meridiem` is "upper" (AM, PM) or "lower" (am, pm) and only matters for "12h".
    Q_INVOKABLE QString setTimeFormat(const QString& style, bool seconds, const QString& meridiem);
    Q_INVOKABLE QString setIncludeTotals(bool include);
    /// The text before the error name on an error line (PRF-13): at most 12 characters on one line, which may be empty;
    /// spaces at either end are dropped. Returns "" on success, or the refusal in plain words.
    Q_INVOKABLE QString setErrorPrefix(const QString& prefix);
    /// Puts the default, "T|", back.
    Q_INVOKABLE QString resetErrorPrefix();

    /// `from` and `to` are rows of the `locations` model.
    Q_INVOKABLE QString addLocation(const QString& name);
    Q_INVOKABLE QString renameLocation(const QString& id, const QString& name);
    Q_INVOKABLE QString moveLocation(int from, int to);
    /// Retires the location: it leaves the list and the pickers, and past logs still name it.
    Q_INVOKABLE QString removeLocation(const QString& id);

    // The environments of a unit, which Today's details offers (PRF-11). `unitEnvironments` lists them in the order the
    // picker shows them. A name is lower-case letters, digits and dashes, at most 20 characters, not starting or ending
    // with a dash; upper case is lowered; none is listed twice; at least one stays. Each call returns "" on success or the
    // refusal in plain words. `from` and `to` are rows of `unitEnvironments`.
    Q_INVOKABLE QString addUnitEnvironment(const QString& name);
    Q_INVOKABLE QString renameUnitEnvironment(const QString& from, const QString& to);
    Q_INVOKABLE QString moveUnitEnvironment(int from, int to);
    /// A log whose unit has this environment keeps it; the picker stops offering it.
    Q_INVOKABLE QString removeUnitEnvironment(const QString& name);

    /// A new type goes to the end of the master list and onto the first empty board position, in the same change (one
    /// save); when the board is full it is not on the board. `errorTypeAdded` says where it went once it is in force.
    Q_INVOKABLE QString addErrorType(const QString& name);
    /// A new type that goes onto the board at `position` (1 to 11), in one change: the board setup's "New error type".
    /// A type that was on that position goes off the board, as `assignBoardTile` does with a type that was not on it.
    Q_INVOKABLE QString addErrorTypeAt(const QString& name, int position);
    Q_INVOKABLE QString renameErrorType(const QString& id, const QString& name);
    Q_INVOKABLE QString retireErrorType(const QString& id);

    /// Positions are the board positions 1 to 11 (the `position` role of `boardLayout`). A drag takes the tile at
    /// `fromPosition` out and puts it at `toPosition`, the tiles between shifting by one.
    Q_INVOKABLE QString moveBoardTile(int fromPosition, int toPosition);
    /// Puts the type on the tile. A type that is on the board elsewhere swaps places with the tile's type; one that is
    /// not takes the tile and sends the tile's type off the board.
    Q_INVOKABLE QString assignBoardTile(int position, const QString& errorTypeId);

    Q_INVOKABLE QString addTemplate(const QString& name, const QString& prompt, const QString& pattern);
    Q_INVOKABLE QString renameTemplate(const QString& id, const QString& name);
    Q_INVOKABLE QString updateTemplate(const QString& id, const QString& name, const QString& prompt,
                                       const QString& pattern);
    /// A time of day for the screen, so that QML does no time arithmetic: `utcMs` (milliseconds since 1970 UTC) as the
    /// wall clock of the IANA zone `zone`, in the configured time format ("10:05", "10:05:09 am", "10:05 PM"), by the
    /// core's `format_time`. The zone's rules apply on that date (daylight saving). Empty for a zone that is not known.
    Q_INVOKABLE QString formatTime(qint64 utcMs, const QString& zone) const;
    /// "" for a pattern the renderer accepts, or the message to show while the person types.
    Q_INVOKABLE QString validatePattern(const QString& pattern) const;

    /// The log line a note made from `pattern` and `value` would have, made by the renderer that makes the review
    /// screen's lines: "10:30 Played Morning Medley". The time is `localSeconds` (seconds since local
    /// midnight, 0 to 86399) on today's date in the device zone, in the user's time format; a negative value means
    /// now. A blank `value` is shown as "Morning Medley", the designs' sample. For a pattern that is not valid it
    /// returns "Fix the line to see an example." (check `validatePattern` to style it differently), and for a
    /// `localSeconds` above 86399 an empty string. Nothing is written.
    Q_INVOKABLE QString previewTemplateLine(const QString& pattern, const QString& value, int localSeconds = -1) const;

    /// What the "Line in the log" field shows for a stored pattern: each {value} is written "[answer]", so nobody has
    /// to type curly brackets. A doubled brace ("{{", "}}", a literal brace in the log) is left as it is. The stored
    /// pattern, and the file format, still say {value}.
    Q_INVOKABLE QString patternToField(const QString& pattern) const;
    /// The stored pattern for what the field shows: each "[answer]" is {value} again. Text the person typed as
    /// {value} stays as it is.
    Q_INVOKABLE QString patternFromField(const QString& field) const;
    /// The question a new template starts with, made from its name: "Played song" gives "Played song?". Empty for a
    /// blank name.
    Q_INVOKABLE QString defaultTemplateQuestion(const QString& name) const;
    /// The line a new template starts with, as the field shows it: "Played song: [answer]". Empty for a blank name.
    Q_INVOKABLE QString defaultTemplateLine(const QString& name) const;

    /// The name of a tile colour slot: "Periwinkle" for 8. Empty for 0 and for a slot above 11. The `colourSlot` of the
    /// error type rows, the board tiles and `TimelineModel::entryForEdit` is the argument.
    Q_INVOKABLE QString colourName(int slot) const;

    /// Reads and checks the file at `path` (at most 1 MiB) on the storage thread; nothing changes. Ends with
    /// `importPrepared` or `importFailed`.
    Q_INVOKABLE void loadImport(const QString& path);
    /// Replaces the configuration with the file `loadImport` prepared, in one save.
    Q_INVOKABLE void applyImport();
    Q_INVOKABLE void discardImport();
    Q_INVOKABLE void exportTo(const QString& path);

signals:
    /// Anything the properties show changed, or `load()` finished.
    void configurationChanged();
    void savingChanged();
    /// The time format differs from what it was: tells a binding on `formatTime` to run again. Not emitted for a change
    /// of anything else.
    void timeFormatChanged();
    /// A file was read and is valid: the counts of what it holds (types and locations not retired, templates), for the
    /// confirmation. Nothing has changed yet.
    void importPrepared(int errorTypes, int locations, int templates);
    /// The file could not be used: the core's plain-English messages, in file order. Nothing changed.
    void importFailed(const QStringList& messages);
    void exportFinished(bool ok, const QString& message);
    /// The same end of an export, in the words of the banner: `title` ("Exported to Files", or "Export failed"),
    /// `fileName` (the last part of the path, "field-test-setup.ejconfig"; empty when nothing was written) and
    /// `message` ("field-test-setup.ejconfig is ready to send.", or the reason). Emitted just before
    /// `exportFinished`, whose `message` is the same text.
    void exportResult(bool ok, const QString& title, const QString& fileName, const QString& message);
    void writeFailed(const QString& operation, const QString& message);
    /// A type added by `addErrorType` or `addErrorTypeAt` is in force (saved, or in the set-up in progress): its name
    /// and the board position it is on, or 0 when the board was full. Not emitted for a type the Other sheet adds.
    void errorTypeAdded(const QString& name, int position);

private:
    using EditResult = std::expected<bool, QString>; // true: changed; false: nothing to do; error: the message
    using Edit = std::function<EditResult(Configuration&)>;

    struct ImportOutcome;

    /// Where `add_type` puts the new type on the board.
    enum class Placement
    {
        OFF_BOARD,   // the Other sheet: the type is for the entry being logged, and the board does not change
        FIRST_EMPTY, // Settings, Error types
        AT_POSITION  // the board setup's picker
    };
    struct Added
    {
        ErrorTypeId m_Id;
        int m_Position = 0; // 0: not on the board
    };

    [[nodiscard]] const Configuration& view() const
    {
        return m_Configured ? m_Configuration : m_Setup;
    }

    /// Applies `edit` to a copy of the configuration (or of the set-up) and saves it.
    [[nodiscard]] std::expected<void, ConfigEditError> change(const QString& operation, const Edit& edit,
                                                              bool needs_configured);
    [[nodiscard]] QString change_message(const QString& operation, const Edit& edit);
    [[nodiscard]] std::expected<void, ConfigEditError> commit(Configuration candidate, const QString& operation);
    [[nodiscard]] std::expected<Added, ConfigEditError> add_type(const QString& name, bool needs_configured,
                                                                 Placement placement, int position = 0);
    /// `addErrorType` and `addErrorTypeAt`: adds, and tells `errorTypeAdded` when the change is in force.
    [[nodiscard]] QString add_and_announce(const QString& name, Placement placement, int position);

    void finish_load(Result<std::optional<std::string>, StorageError> result);
    void finish_save(Configuration saved, const QString& operation, const Result<void, StorageError>& result);
    void finish_import(ImportOutcome outcome);
    void publish(bool in_force_changed);
    void set_saving(bool saving);

    StorageGateway& m_Gateway;
    IdGenerator& m_Ids;
    const Clock& m_Clock;
    const DeviceZone& m_Zone;
    const TimeZoneProvider& m_Zones;
    TimeFormat m_ShownTimeFormat; // what the last `publish` showed, to tell whether the format changed

    Configuration m_Configuration; // in force; empty until `m_Configured`
    Configuration m_Setup;         // the first-launch set-up in progress
    bool m_Configured = false;
    bool m_Loading = true;
    bool m_LoadStarted = false;
    bool m_Saving = false;
    QString m_LoadProblem;
    std::optional<Configuration> m_PreparedImport;
    std::uint64_t m_ImportToken = 0;
    // The added type `errorTypeAdded` tells about when the save in flight has finished: its name and position.
    std::optional<std::pair<QString, int>> m_PendingAdded;

    ObserverList<ConfigurationObserver> m_Observers;

    ErrorTypeListModel* m_ErrorTypes;
    LocationListModel* m_Locations;
    TemplateListModel* m_Templates;
    BoardLayoutModel* m_BoardLayout;
};

} // namespace ej::viewmodel
