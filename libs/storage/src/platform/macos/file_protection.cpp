// SPDX-License-Identifier: GPL-3.0-or-later
#include "../file_protection.hpp"

namespace ej::platform
{

Result<void, StorageError> apply_file_protection(const std::filesystem::path&)
{
    return {};  // nothing to do on a desktop
}

}  // namespace ej::platform
