// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"

#include <string>

namespace ej::viewmodel
{

class ConfigurationSource;

/// Told, on the GUI thread, after the configuration in force has changed (a setting saved, an import applied, the
/// first load). The configuration is a plain C++ observer for the same reason as `LogObserver`.
class ConfigurationObserver
{
public:
    ConfigurationObserver() = default;
    ConfigurationObserver(const ConfigurationObserver&) = delete;
    ConfigurationObserver& operator=(const ConfigurationObserver&) = delete;
    virtual ~ConfigurationObserver() = default;

    virtual void on_configuration_changed(const ConfigurationSource& source) = 0;
};

/// The read side of the configuration: what the models and the export context are built from. `SettingsModel`
/// implements it (3.3); a model that reads it never waits for I/O.
class ConfigurationSource
{
public:
    ConfigurationSource() = default;
    ConfigurationSource(const ConfigurationSource&) = delete;
    ConfigurationSource& operator=(const ConfigurationSource&) = delete;
    virtual ~ConfigurationSource() = default;

    /// False until a valid configuration is in force (PRF-09: first launch shows setup).
    [[nodiscard]] virtual bool configured() const = 0;

    /// The configuration in force; an empty `Configuration` while `configured()` is false.
    [[nodiscard]] virtual const Configuration& configuration() const = 0;

    /// Observers are told in the order they were added. An observer that is added is not told about the current
    /// configuration: it reads `configuration()` itself.
    virtual void add_observer(ConfigurationObserver& observer) = 0;
    virtual void remove_observer(ConfigurationObserver& observer) = 0;
};

/// Why `ConfigurationEditor` refused a change.
struct ConfigEditError
{
    enum class Code
    {
        REFUSED,   // the core's rules refuse it (an empty, duplicate or over-long name, a full list)
        NOT_READY, // no configuration is in force yet
        QUEUE_FULL // the store is not keeping up; try again
    };

    Code m_Code = Code::REFUSED;
    std::string m_Message; // plain English, for the screen to show inline
};

/// The one write of the configuration that a model other than the settings screen makes: the Other sheet adds an error
/// type to the master list (BRD-06, PRF-04). Everything else about the configuration is `SettingsModel`'s own.
class ConfigurationEditor
{
public:
    ConfigurationEditor() = default;
    ConfigurationEditor(const ConfigurationEditor&) = delete;
    ConfigurationEditor& operator=(const ConfigurationEditor&) = delete;
    virtual ~ConfigurationEditor() = default;

    /// Validates `name`, makes an id for the new type and queues the save. The type is in `ConfigurationSource` (and
    /// the observers are told) only once the save has committed; the id is returned now, so the caller can name the
    /// type before it appears. The type goes to the end of the master list and is not on the board.
    [[nodiscard]] virtual Result<ErrorTypeId, ConfigEditError> add_error_type(std::string name) = 0;
};

} // namespace ej::viewmodel
