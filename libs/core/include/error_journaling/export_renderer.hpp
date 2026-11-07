// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/result.hpp"

namespace ej
{

struct ExportContext
{
    std::string_view m_UnitPrefix;
    /// The display name of a location; an empty view means the id is unknown.
    std::function<std::string_view(LocationId)> m_LocationName;
};

enum class RenderError
{
    UNKNOWN_ERROR_TYPE,
    UNKNOWN_LOCATION,
    UNKNOWN_TEMPLATE,
    UNKNOWN_ZONE
};

/// The header block from the log's initial values: five lines, no trailing newline.
[[nodiscard]] Result<std::string, RenderError> render_header(const Log& log, const ExportContext& context);

/// The whole log. Skeleton in work package 1.2: the header block only, until 1.5 adds the timeline.
[[nodiscard]] Result<std::string, RenderError> render_export(const Log& log, const ExportContext& context);

}  // namespace ej
