// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/accessibility_settings.hpp"
#include "error_journaling/background_tasks.hpp"
#include "error_journaling/document_picker.hpp"
#include "error_journaling/haptics.hpp"
#include "error_journaling/perf_marks.hpp"
#include "error_journaling/share.hpp"
#include "error_journaling/window_limits.hpp"

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
    std::unique_ptr<AccessibilitySettings> m_Accessibility;
    std::unique_ptr<DocumentPicker> m_DocumentPicker;
    std::unique_ptr<BackgroundTasks> m_BackgroundTasks;
    std::unique_ptr<WindowLimits> m_WindowLimits;
};

/// The implementations for the platform CMake linked.
[[nodiscard]] PlatformServices make_platform_services();

} // namespace ej::bridge
