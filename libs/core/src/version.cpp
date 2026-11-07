// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/version.hpp"

namespace error_journaling
{

QString version_text()
{
    return QString::fromUtf8(VERSION_STRING.data(), static_cast<qsizetype>(VERSION_STRING.size()));
}

}  // namespace error_journaling
