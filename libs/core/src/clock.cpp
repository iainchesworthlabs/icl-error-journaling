// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/clock.hpp"

namespace ej
{

UtcMillis SystemClock::now() const
{
    return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
}

} // namespace ej
