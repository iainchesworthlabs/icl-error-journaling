// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/recovery_report.hpp"

#include <QString>

namespace ej::viewmodel
{

/// The sentence the Log screen shows once after a start that had to recover (DS-04), made from what recovery did. It
/// is empty when nothing was recovered (`CLEAN`), and one wording each for `REPLAYED`, `REBUILT`, `PARTIAL_LOSS` and
/// `UNRECOVERABLE`.
///
/// A pure function of the report. The sentence holds counts and nothing else: no entry text, no path, no message of
/// the database, because the report is the only input and the sentence may reach CI output (PRD NFR-10). It is
/// user-visible text made in C++, so it goes through the translation system (NFR-11) as whole sentences with a
/// `%1` placeholder and no concatenation.
[[nodiscard]] QString recovery_notice(const RecoveryReport& report);

} // namespace ej::viewmodel
