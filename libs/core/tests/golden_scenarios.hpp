// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "error_journaling/export_renderer.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"

namespace ej::test
{

/// A deterministic id for a test: the same number always gives the same id.
template <typename Id>
[[nodiscard]] Id test_id(std::uint64_t number)
{
    return Id(Uuid::from_random(0, number));
}

/// The scenario behind golden/prd-example.txt: the PRD example, with times in Australia/Melbourne (UTC+11 that day).
[[nodiscard]] Log prd_example_log();

/// The context that goes with `prd_example_log()`: prefix UNIT and the location "North Site".
[[nodiscard]] ExportContext prd_example_context();

}  // namespace ej::test
