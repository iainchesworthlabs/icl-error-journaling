// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/platform_services.hpp"

namespace ej::bridge
{

PlatformServices make_platform_services()
{
    PlatformServices services;
    services.m_Share = make_share();
    services.m_Haptics = make_haptics();
    services.m_PerfMarks = make_perf_marks();
    services.m_Accessibility = make_accessibility_settings();
    return services;
}

} // namespace ej::bridge
