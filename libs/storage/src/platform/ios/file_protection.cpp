// SPDX-License-Identifier: GPL-3.0-or-later
#include "../file_protection.hpp"

namespace ej::platform
{

Result<void, StorageError> apply_file_protection(const std::filesystem::path&)
{
    return {};  // the iOS protection class is applied in Stage 5 (section 2.10 of the Stage 2 plan)
}

}  // namespace ej::platform
