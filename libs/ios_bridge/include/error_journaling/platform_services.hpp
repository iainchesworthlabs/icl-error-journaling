// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptics.hpp"
#include "error_journaling/perf_marks.hpp"
#include "error_journaling/share.hpp"

#include <memory>

namespace ej::bridge
{

/// What Qt does not cover on an iPhone, one small interface per capability. Created once at start-up and owned by the
/// QmlBridge. Each package adds its capability here with one member and one line in make_platform_services().
struct PlatformServices
{
    std::unique_ptr<Share> m_Share;
    std::unique_ptr<Haptics> m_Haptics;
    std::unique_ptr<PerfMarks> m_PerfMarks;
};

/// The implementations for the platform CMake linked.
[[nodiscard]] PlatformServices make_platform_services();

} // namespace ej::bridge
