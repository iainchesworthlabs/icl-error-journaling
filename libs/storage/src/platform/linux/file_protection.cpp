// SPDX-License-Identifier: GPL-3.0-or-later
#include "../file_protection.hpp"

namespace ej::platform
{

namespace
{

class NoFileProtection final : public FileProtection
{
public:
    Result<void, StorageError> apply(const std::filesystem::path&, ProtectionClass) override
    {
        return {};  // nothing to do here
    }

    Result<ProtectionClass, StorageError> read_back(const std::filesystem::path&) override
    {
        return ProtectionClass::UNSUPPORTED;
    }
};

}  // namespace

FileProtection& system_file_protection()
{
    static NoFileProtection instance;
    return instance;
}

}  // namespace ej::platform
