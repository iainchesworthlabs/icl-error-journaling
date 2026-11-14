// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/export_renderer.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/environment.hpp"

#include <string>

namespace ej::viewmodel
{

/// Builds the renderer's `ExportContext` from the configuration in force (docs/plan/stage-3.md, "One source of
/// wording"). A timeline row, the "Last" bar, the edit sheet's preview, Copy and Share all render with a context made
/// here, so the review screen reads exactly like the export (REV-01).
///
/// What the context holds:
///  - the unit prefix, the time format and the totals switch, as the configuration has them now;
///  - lookups of error type, location and note template by id. They resolve retired types and locations too, and
///    answer a renamed type with its current name. An id the configuration does not know yields the empty view (or a
///    null template) that the renderer reports as an error. An error entry that carries its own name snapshot prints
///    that name and does not look it up (Stage 3 decision 7; the renderer's rule), so the lookup is the fallback for
///    entries written before snapshots;
///  - the order of the totals line: types on the board in board order, then the others in master-list order (PRF-10);
///  - the time zone provider;
///  - for interactions (Stage 14, 3.6): the type names and line patterns of the configuration, the name of a person
///    from the environment's `PersonNames`, and the label of an interaction line from its `ExportPreferences`.
///
/// There are two ways to build. `build()` is for the screens (Review, the Last bar, previews): a person's name is the
/// full name, because the screen never leaves the device. `build_for_sharing()` is for text that leaves it (Share,
/// Copy line, Copy all, a past log re-shared): a person's name is written at the level of the preference `name_export`,
/// initials unless the owner changed it. Names are never exported in any other form.
///
/// The context refers into the configuration (the prefix and the names are views of its strings), so it is valid
/// until the configuration next changes. Build it when it is needed and drop it after rendering; do not keep one.
/// Building twice from an unchanged configuration gives contexts that render identical text.
class ExportContextBuilder
{
public:
    /// Neither argument is owned; both must outlive the builder. `zones` is `Environment::m_Zones`.
    ExportContextBuilder(const ConfigurationSource& configuration, const TimeZoneProvider& zones)
        : m_Configuration(configuration), m_Zones(zones)
    {
    }

    /// As above, with the people and the preferences of the environment too.
    ExportContextBuilder(const ConfigurationSource& configuration, const Environment& environment)
        : m_Configuration(configuration), m_Zones(environment.m_Zones), m_People(environment.m_People),
          m_Preferences(environment.m_ExportPreferences)
    {
    }

    /// While the source is not `configured()` the context is that of an empty configuration: no names resolve.
    [[nodiscard]] ExportContext build() const;
    /// The context of text that leaves the device: the name level is the preference.
    [[nodiscard]] ExportContext build_for_sharing() const;

private:
    const ConfigurationSource& m_Configuration;
    const TimeZoneProvider& m_Zones;
    const PersonNames* m_People = nullptr;
    const ExportPreferences* m_Preferences = nullptr;
};

/// The text of the export: header block, timeline and, when the context says so, the totals line. It wraps
/// `render_export`, and Copy, Share, the timeline's Copy all and the golden test call this one function, so there is
/// one place where the text is made. On an error nothing is returned, so no partial text can leak.
[[nodiscard]] Result<std::string, RenderError> export_text(const Log& log, const ExportContext& context);

} // namespace ej::viewmodel
